#include <features.h>
#ifdef __UCLIBC__
#define reallocarray(ptr, nmemb, size) realloc((ptr), ((nmemb) * (size)))
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
#include <pthread.h>
#include <bsd/sys/queue.h>
#include "aesdsocket.h"

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
void *get_in_addr(struct sockaddr *sa) {
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
    fprintf(stderr, "start_listening: getaddrinfo: %s\n", gai_strerror(rv));
    if (servinfo != NULL) {
      freeaddrinfo(servinfo);
    }
    return -1;
  }

  // loop through all the results and bind to the first we can
  for(p_addrinfo = servinfo; p_addrinfo != NULL; p_addrinfo = p_addrinfo->ai_next) {
    if ((server_sock_fd = socket(p_addrinfo->ai_family, p_addrinfo->ai_socktype,
            p_addrinfo->ai_protocol)) == -1) {
      perror("start_listening: socket");
      continue;
    }

    if (setsockopt(server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
          sizeof(int)) == -1) {
      perror("start_listening: setsockopt");
      freeaddrinfo(servinfo);
      return -1;
    }

    if (bind(server_sock_fd, p_addrinfo->ai_addr, p_addrinfo->ai_addrlen) == -1) {
      close(server_sock_fd);
      perror("start_listening: bind");
      continue;
    }
    break;
  }

  if (p_addrinfo == NULL)  {
    fprintf(stderr, "start_listening: failed to bind\n");
    freeaddrinfo(servinfo);
    return -1;
  }
  // get human readable address of the bound socket
  inet_ntop(p_addrinfo->ai_family, 
      get_in_addr((struct sockaddr *)p_addrinfo->ai_addr),
      ip_address, sizeof ip_address);
  freeaddrinfo(servinfo); // all done with this structure

  if (listen(server_sock_fd, BACKLOG) == -1) {
    perror("start_listening: listen");
  }
  return server_sock_fd;
}

/*
 * Append line to file
 */
bool append_to_file(FILE * file, char * line) {
  size_t bytes_written = 0;
  bytes_written = fwrite(line, 1, strlen(line), file);
  if (bytes_written < strlen(line)) {
    perror("append_to_file: fwrite");
    false;
  }
  return true;
}

void daemonize(void) {
  pid_t pid;
  pid = fork(); 
  if (pid < 0) {
    perror("daemonize: fork");
    exit(EXIT_FAILURE);
  }
  if (pid > 0) { // parent
    exit(EXIT_SUCCESS);
  }
  if (setsid() == -1) { // new session creation failed
    perror("daemonize: setsid");
    exit(EXIT_FAILURE);    
  }
  // fork again
  pid = fork(); 
  if (pid < 0) {
    perror("daemonize: fork");
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
    perror("daemonize: dup2");
    exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDOUT_FILENO) < 0) {
    perror("daemonize: dup2");
    exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDERR_FILENO) < 0) {
    perror("daemonize: dup2");
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

struct aesd_thread_args * init_thread(pthread_mutex_t * mutex, int sock_fd, char * ip_address) {
  struct aesd_thread_args * thread_args = malloc(sizeof(struct aesd_thread_args));
  if (thread_args == NULL) {
    perror("init_thread: malloc");
    return NULL;
  }
  memset(thread_args, 0, sizeof(struct aesd_thread_args));
  thread_args->mutex = mutex;
  thread_args->sock_fd = sock_fd;
  strncpy(thread_args->ip_address, ip_address, INET6_ADDRSTRLEN - 1);
  return thread_args;
}

void remove_joinable_threads(struct thread_args_head * list_head) {
  int rc;
  struct aesd_thread_args *next, *next_temp;
  void *join_rt = NULL;
  SLIST_FOREACH_SAFE(next, list_head, elements, next_temp){
    if (next->finished) {
      if ((rc = pthread_join(next->thread_id, join_rt))) {
        fprintf(stderr, "main: join(%zu): %s\n", next->thread_id, strerror(rc));
      }
      else {
        if (join_rt == PTHREAD_CANCELED) {
          fprintf(stderr, "main: join(%zu): thread was canceled.\n", next->thread_id);
        }
      }
      SLIST_REMOVE(list_head, next, aesd_thread_args, elements);
      free(next);
    }
  }
}

void remove_all_remaining_threads(struct thread_args_head * list_head) {
  while (!SLIST_EMPTY(list_head)) {
    remove_joinable_threads(list_head);
  }
}


int main(int argc, char **argv) {
  int client_sock_fd; 
  struct sockaddr_storage client_address; // connector's address information
  socklen_t sin_size;
  char ip_address[INET6_ADDRSTRLEN];
  int exit_code = EXIT_FAILURE;
  bool should_daemonize = false;
  int rc; // return code from functions
  pthread_mutex_t mutex;
  parse_args(argc, argv, &should_daemonize);

  openlog("aesdsocket", 0, LOG_USER);
  server_sock_fd = start_listening(ip_address);
  if (server_sock_fd == -1) {
    goto err_start_listening;
  }
  if (!set_signals()) {
    goto err_set_signals;
  }
  if (should_daemonize) {
    daemonize();
  } 
  else { // print only if not being run as daemon 
    printf("Listening on address %s\n", ip_address);
  }
  if ((rc = pthread_mutex_init(&mutex, NULL))) {
    errno = rc;
    perror("main: pthread_mutex_init");
    goto err_mutex_init;
  }
  /*
   * initialize head of linked list of aesd_thread_args
   */
  struct thread_args_head list_head;
  SLIST_INIT(&list_head); 
  /*
   * Set and start timer
   */
  struct timer_thread_args timer_args = { &mutex };
  timer_t timer_id;
  if (!start_timer(TIMER_INTERVAL_SECS, &timer_args, &timer_id)) {
    fprintf(stderr, "main: failed to start timer\n");
    goto err_start_timer;
  }
  // now we can start the main server loop
  is_running = true;
  while(is_running) { // accept loop
    sin_size = sizeof client_address;
    client_sock_fd = accept(server_sock_fd, (struct sockaddr *)&client_address, &sin_size);
    if (client_sock_fd == -1) {
      if (is_running) {
        perror("main: accept");
        continue;
      }
      else { // we're not running any more... 
        break;
      }
    }

    inet_ntop(client_address.ss_family,
        get_in_addr((struct sockaddr *)&client_address),
        ip_address, sizeof ip_address);
    syslog(LOG_INFO, "Accepted connection from %s", ip_address);

    struct aesd_thread_args * thread_args = init_thread(&mutex, client_sock_fd, ip_address);
    if (!thread_args) {
      goto err_init_thread;
    }
    rc = pthread_create(&(thread_args->thread_id), NULL, sock_thread_func, thread_args);
    if (rc != 0) {
      errno = rc;
      perror("main: pthread_create");
      free(thread_args);
      goto err_pthread_create;
    }
    client_sock_fd = -1;
    SLIST_INSERT_HEAD(&list_head, thread_args, elements);
    remove_joinable_threads(&list_head);  
  }
  exit_code = EXIT_SUCCESS;
  // cleanup starts here, 
  // labels are in reverse oreder of their respective gotos
err_pthread_create: //6
err_init_thread: //5
  if (client_sock_fd != -1) {
    close(client_sock_fd);
  }
  if (timer_delete(timer_id)) {
    perror("main: timer_delete");
  }
  remove_all_remaining_threads(&list_head);  
  if (remove(OUTPUT_FILE) == -1) {
    perror("main: remove");
  }
err_start_timer: //4
  if ((rc = pthread_mutex_destroy(&mutex))) {
    errno = rc;
    perror("main: pthread_mutex_destroy");
  }
err_mutex_init: //3
err_set_signals: //2
  close(server_sock_fd);
err_start_listening: //1
  closelog();
  return exit_code;
}
