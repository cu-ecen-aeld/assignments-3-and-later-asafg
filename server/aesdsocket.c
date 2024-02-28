#ifdef __GLIBC__
  #include <gnu/libc-version.h>
  #if !__GNUC_PREREQ (2,26)
    #define reallocarray(ptr, nmemb, size) realloc((ptr), ((nmemb) * (size)))
  #endif
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <malloc.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include "aesdsocket.h"

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 20   // how many pending connections queue will hold
#define BUFLEN  1024
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"

static bool is_running = false;
static int server_sock_fd;

void signal_handler(int signal)
{
  // waitpid() might overwrite errno, so we save and restore it:
  int saved_errno = errno;
  if (signal == SIGCHLD) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
  }
  else { // SIGTERM, SIGINT
    is_running = false;
    syslog(LOG_INFO, "Caught signal, exiting");
    shutdown(server_sock_fd, SHUT_RDWR);
  }
  errno = saved_errno;
}

bool set_signals(void) {
  struct sigaction sa;
  sa.sa_handler = signal_handler; // handles SIGCHLD, SIGINT, SIGTERM 
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    return false;
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    return false;
  }
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    perror("sigaction");
    return false;
  }
  return true;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) { // IPv4
    return &(((struct sockaddr_in*)sa)->sin_addr);
  } //IPv6
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int start_listening(char * ip_address) {
  int server_sock_fd; 
  struct addrinfo hints, *servinfo, *p_addrinfo;
  int yes=1;
  int rv;
  servinfo = NULL;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    if (servinfo != NULL) {
      freeaddrinfo(servinfo);
    }
    return -1;
  }

  // loop through all the results and bind to the first we can
  for(p_addrinfo = servinfo; p_addrinfo != NULL; p_addrinfo = p_addrinfo->ai_next) {
    if ((server_sock_fd = socket(p_addrinfo->ai_family, p_addrinfo->ai_socktype,
            p_addrinfo->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
          sizeof(int)) == -1) {
      perror("setsockopt");
      freeaddrinfo(servinfo);
      return -1;
    }

    if (bind(server_sock_fd, p_addrinfo->ai_addr, p_addrinfo->ai_addrlen) == -1) {
      close(server_sock_fd);
      perror("server: bind");
      continue;
    }
    break;
  }

  if (p_addrinfo == NULL)  {
    fprintf(stderr, "server: failed to bind\n");
    freeaddrinfo(servinfo);
    return -1;
  }
  // get human readable address of the bound socket
  inet_ntop(p_addrinfo->ai_family, 
      get_in_addr((struct sockaddr *)p_addrinfo->ai_addr),
      ip_address, sizeof ip_address);
  freeaddrinfo(servinfo); // all done with this structure

  if (listen(server_sock_fd, BACKLOG) == -1) {
    perror("listen");
  }
  return server_sock_fd;
}

/*
 * Read line from the socket.
 *
 * Return a pointer to the line read or NULL if failed to read the line.
 * The memory allocated should be released by the caller.
 */
char * readline(int client_sock_fd) {
  int bufsize = BUFLEN;
  char * buf = NULL;
  char * newbuf = NULL;
  if (buf == NULL) {
    buf = malloc(sizeof(char) * BUFLEN);
    if (buf == NULL) {
      perror("malloc");
      return NULL;
    }
  }
  int offset = 0;
  char * eol = NULL;
  ssize_t bytes_read = 0;
  buf[bufsize - 1] = '\0';
  while (eol == NULL) {
    bytes_read = recv(client_sock_fd, buf + offset, bufsize - offset - 1, 0);
    if (bytes_read == -1) {
      perror("recv");
      free(buf);
      return NULL;
    }
    else if (bytes_read == 0) {
      return buf;
    }
    buf[offset + bytes_read] = '\0';
    eol = strchr(buf + offset, '\n');
    if (eol == NULL) { // we did not encounter '\n', double the buffer size and read again
      offset += bytes_read;
      if (offset == bufsize - 1) {
        bufsize *= 2;
        newbuf = reallocarray(buf, bufsize, sizeof(char));
        if (newbuf == NULL) {
          perror("reallocarray");
          free(buf);
          return NULL;
        }
        buf = newbuf;
      }
    }
  }
  return buf;
}

/*
 * Append line to file
 */
bool append_to_file(FILE * file, char * line) {
  size_t bytes_written = 0;
  bytes_written = fwrite(line, 1, strlen(line), file);
  if (bytes_written < strlen(line)) {
    perror("fwrite");
    false;
  }
  return true;
}

bool send_file(FILE * file, int client_sock_fd) {
  char * line = NULL;
  size_t bufsize = 0;
  ssize_t bytes_read = 0;
  ssize_t bytes_sent = 0;
  size_t bytes_left = 0;
  size_t offset = 0;
  rewind(file);
  while ((bytes_read = getline(&line, &bufsize, file)) != -1) {
    bytes_left = bytes_read;
    offset = 0;
    while (bytes_left > 0) {
      bytes_sent = send(client_sock_fd, line + offset, bytes_left, 0);
      bytes_left -= bytes_sent;
      offset += bytes_sent; 
    }
  }
  free(line);
  if (errno != 0) {
    perror("send");
    return false;
  }
  return true;
}

void daemonize(void) {
  pid_t pid;
  pid = fork(); 
  if (pid < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  if (pid > 0) { // parent
    exit(EXIT_SUCCESS);
  }
  if (setsid() == -1) { // new session creation failed
    perror("setsid");
    exit(EXIT_FAILURE);    
  }
  // fork again
  pid = fork(); 
  if (pid < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  }
  if (pid > 0) { // (2nd) parent
    exit(EXIT_SUCCESS);
  }

  /* Set new file permissions */
  umask(022);

  /* Change the working directory to the root directory */
  chdir("/");

  int fd = open("/dev/null", O_RDWR, S_IRUSR | S_IWUSR);
  if (dup2(fd, STDIN_FILENO) < 0) {
     perror("dup2");
     exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDOUT_FILENO) < 0) {
     perror("dup2");
     exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDERR_FILENO) < 0) {
     perror("dup2");
     exit(EXIT_FAILURE);
  }
}

void print_help(char * progname) {
  printf("Usage: %s [OPTION]\n", progname);
  printf("Start AESD socket server.\n");
  printf("options:\n");
  printf("        -d  run as a daemon\n");
  printf("        -h  print this help message\n");
}

void parse_args(int argc, char **argv, bool * should_daemonize) {
  if (argc > 2) {
    print_help(argv[0]);
    printf("\nerror: too many arguments.\n");
    exit(EXIT_FAILURE);
  }
  if (argc == 2) {
    if (strcmp("-d", argv[1]) == 0) {
      *should_daemonize = true;
    }
    else if (strcmp("-h", argv[1]) == 0) {
      print_help(argv[0]);
      exit(EXIT_SUCCESS);
    }
  } 
}

int main(int argc, char **argv) {
  pid_t pid;
  int client_sock_fd;  // listen on sock_fd, new connection on new_fd
  struct sockaddr_storage client_address; // connector's address information
  socklen_t sin_size;
  char ip_address[INET6_ADDRSTRLEN];
  char * line = NULL;
  FILE * file = NULL;
  int exit_code = EXIT_SUCCESS;
  bool should_daemonize = false;
  parse_args(argc, argv, &should_daemonize);

  openlog("aesdsocket", 0, LOG_USER);
  server_sock_fd = start_listening(ip_address);
  if (server_sock_fd == -1) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }
  if (!set_signals()) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }
  if (should_daemonize) {
    daemonize();
  } 
  else { // print only if not being run as daemon 
    printf("Listening on address %s\n", ip_address);
  }
  is_running = true;
  while(is_running) { // accept loop
    sin_size = sizeof client_address;
    client_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_address, &sin_size);
    if (client_sock_fd == -1) {
      if (is_running) {
        perror("accept");
        continue;
      }
      else {
        // we're not running any more...
        break;
      }
    }

    inet_ntop(client_address.ss_family,
        get_in_addr((struct sockaddr *)&client_address),
        ip_address, sizeof ip_address);
    syslog(LOG_INFO, "Accepted connection from %s", ip_address);

    if ((pid = fork()) == 0) { // this is the child process
      close(server_sock_fd); // child doesn't need the listener
      server_sock_fd = -1;
      line = readline(client_sock_fd);
      if (line == NULL) {
        exit_code = EXIT_FAILURE;
        break;
      }
      file = fopen(OUTPUT_FILE, "a+");
      if (file == NULL) {
        perror("fopen");
        exit_code = EXIT_FAILURE;
        break;
      }
      if (!append_to_file(file, line)) {
        exit_code = EXIT_FAILURE;
      }
      if (!send_file(file, client_sock_fd)) {
        exit_code = EXIT_FAILURE;
      }
      break; // we're in child process, we don't loop with accept()s
    }
    close(client_sock_fd);  // parent doesn't need this
    client_sock_fd = -1;
  }
cleanup:
  if (server_sock_fd != -1) {
    close(server_sock_fd);
  }
  if (client_sock_fd != -1) {
    close(client_sock_fd);
    syslog(LOG_INFO, "Closed connection from %s", ip_address);
  }
  free(line);
  if (file != NULL) {
    fclose(file);
  }
  if (!is_running && pid > 0) { // do this only once, in parent process
    if (remove(OUTPUT_FILE) == -1) {
      perror("remove");
    }
    closelog();
  }
  return exit_code;
}
