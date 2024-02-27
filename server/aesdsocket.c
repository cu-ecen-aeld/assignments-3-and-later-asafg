#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <syslog.h>
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
#define BUFLEN  10 //1024
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
    printf("Caught signal, exiting\n");
    shutdown(server_sock_fd, SHUT_RDWR);
    // TODO: close socket, exit gracefully
  }

  errno = saved_errno;
}

int set_signals(void) {
  struct sigaction sa;
  sa.sa_handler = signal_handler; // handles SIGCHLD, SIGINT, SIGTERM 
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL) == -1) {
    perror("sigaction");
    return -1;
  }
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("sigaction");
    return -1;
  }
  if (sigaction(SIGTERM, &sa, NULL) == -1) {
    perror("sigaction");
    return -1;
  }
  return 0;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }
  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int start_listening(void) {
  int server_sock_fd; 
  struct addrinfo hints, *servinfo, *p_addrinfo;
  char server_ip[INET6_ADDRSTRLEN];
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
      server_ip, sizeof server_ip);
  freeaddrinfo(servinfo); // all done with this structure

  if (listen(server_sock_fd, BACKLOG) == -1) {
    perror("listen");
  }
  printf("Listening on address %s\n", server_ip);
  return server_sock_fd;
}

/*
 * Reads line from the socket.
 *
 * Returns a pointer to the line read or NULL if failed to read the line.
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
  //memset(buf, 0, bufsize);
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
    printf("after recv: bytes_read: %ld, bufsize: %d, offset: %d, buf + offset: %p, bufsize - 1 - offset: %d);\n",
        bytes_read, bufsize, offset, (buf + offset), (bufsize - 1 - offset));
    buf[offset + bytes_read] = '\0';
    printf("buf: ***%s***", buf);
    eol = strchr(buf + offset, '\n');
    printf("\neol is NULL ? %s\n", (eol == NULL) ? "yes" : "no");
    if (eol == NULL) { // we did not encounter '\n', double the buffer size and read again
      printf("\n");
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
    printf("before printing line\n");
    printf("buf: -**%s**-", buf);
    printf("after printing line\n");
  }
  return buf;
}

bool write_to_file(FILE * file, char * line) {
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
  printf("before send loop\n");
  while ((bytes_read = getline(&line, &bufsize, file)) != -1) {
    printf("bytes_read: %jd line: %s\n", bytes_read, line);
    bytes_left = bytes_read;
    offset = 0;
    while (bytes_left > 0) {
      bytes_sent = send(client_sock_fd, line + offset, bytes_left, 0);
      bytes_left -= bytes_sent;
      offset += bytes_sent; 
    }
  }
  printf("after recv loop: bytes_read: %jd\n", bytes_read);
  free(line);
  if (errno != 0) {
    perror("send");
    return false;
  }
  return true;
}

int main(void)
{
  int client_sock_fd;  // listen on sock_fd, new connection on new_fd
  struct sockaddr_storage client_address; // connector's address information
  socklen_t sin_size;
  char server_ip[INET6_ADDRSTRLEN];
  char * line = NULL;
  FILE * file = NULL;
  int exit_code = EXIT_SUCCESS;

  openlog(NULL, 0, LOG_USER);
  server_sock_fd = start_listening();
  if (server_sock_fd == -1) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }
  if (set_signals() == -1) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

  is_running = true;
  printf("Accepting connections...\n");
  while(is_running) {
    sin_size = sizeof client_address;
    client_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_address, &sin_size);
    if (client_sock_fd == -1) {
      if (is_running) {
        perror("accept");
        continue;
      }
      else {
        // we're not running any more...
        goto cleanup;
      }
    }

    inet_ntop(client_address.ss_family,
        get_in_addr((struct sockaddr *)&client_address),
        server_ip, sizeof server_ip);
    printf("server: got connection from %s\n", server_ip);

    if (!fork()) { // this is the child process
      close(server_sock_fd); // child doesn't need the listener
      server_sock_fd = -1;
      line = readline(client_sock_fd);
      if (line == NULL) {
        exit_code = EXIT_FAILURE;
        goto cleanup;
      }
      printf("line: %s", line);
      file = fopen(OUTPUT_FILE, "a+");
      if (file == NULL) {
        perror("fopen");
        exit_code = EXIT_FAILURE;
        goto cleanup;
      }
      if (!write_to_file(file, line)) {
        exit_code = EXIT_FAILURE;
      }
      if (!send_file(file, client_sock_fd)) {
        exit_code = EXIT_FAILURE;
      }
      goto cleanup;
    }
    close(client_sock_fd);  // parent doesn't need this
  }
cleanup:
  printf("reached cleanup\n");
  if (server_sock_fd != -1) {
    close(server_sock_fd);
  }
  if (client_sock_fd != -1) {
    close(client_sock_fd);
  }
  free(line);
  if (file != NULL) {
    fclose(file);
  }
  if (!is_running) {
    printf("going to remove file...\n");
    if (!remove(OUTPUT_FILE)) {
      perror("remove");
    }
  }
  return exit_code;
}
