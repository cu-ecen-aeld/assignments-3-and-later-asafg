#ifndef AESDSOCKET_H
#define AESDSOCKET_H

#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <bsd/sys/queue.h>

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 20   // how many pending connections queue will hold
#define BUFLEN  1024
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"
#define TIMER_INTERVAL_SECS 10
/*
 * Used for POSIX interval timer that writes time stmap every 10 seconds.
 * The mutex is used to synchronize read and writes from/to OUTPUT_FILE.
 * The mutes is shared between the timer thread and the socket threads.
 */
struct timer_thread_args {
  pthread_mutex_t * mutex;
};

/*
 * Used for the threads that deal with client sockets.
 * The mutex is used to synchronize read and writes from/to OUTPUT_FILE.
 * The mutes is shared between the timer thread and the socket threads.
 */

SLIST_HEAD(thread_args_head, aesd_thread_args);

struct aesd_thread_args {
  pthread_t thread_id;
  pthread_mutex_t * mutex;
  int sock_fd;
  char ip_address[INET6_ADDRSTRLEN];
  int last_error;
  bool finished;
  SLIST_ENTRY(aesd_thread_args) elements;
};

/*
 * Starts a POSIX interval timer running every interval_secs.
 * On success returns true 
 * and the id of the timer created is stored in @param timer_id.
 */
bool start_timer(int interval_sec, struct timer_thread_args * timer_args, timer_t * timer_id);

/*
 * Allocates aesd_thread_args, and initializes it.
 */
struct aesd_thread_args * init_thread(pthread_mutex_t * mutex, int sock_fd, char * ip_address);

/*
 * Handle SIGCHLD, SIGINT and SIGTERM
 */
void signal_handler(int signal);

/*
 * Register signals for this server.
 * Currently SIGCHLD, SIGINT and SIGTERM are handled.
 * Return true on success or false on failure. 
 */
bool set_signals(void);

/*
 * Extracts struct in_addr or struct in6_addr
 * from @parameter sa according to the AF (address family) being used.
 */
void *get_in_addr(struct sockaddr *sa);

/*
 * Create a socket, binds the socket and starts listening on this socket.
 * Return socket fd or -1 on error. 
 * Set human readable IP address into @parameter ip_address
 */
int start_listening(char * ip_address);

/*
 * Read line from the socket associated with @parameter client_sock_fd.
 * Return a pointer to the line that was read.
 * The pointer should be freed by the caller.
 * If failed to read the line, NULL would be returned.
 */
char * readline_from_socket(int client_sock_fd);

/*
 * Append the line in @parameter line to the file specified by @parameter file.
 * Return true on success or false on failure.
 */
bool append_to_file(FILE * file, char * line);

/*
 * Send the content of file specified in @parameter file
 * on the socket specified by @parameter client_sock_fd.
 * Return true on success or false on failure.
 */
bool send_file(FILE * file, int client_sock_fd);

/*
 * Make this server a UNIX daemon.
 * i.e. run the sequence need in order to make this program run as a daemon.
 */
void daemonize(void);

void print_help(char * progname);

/*
 * Parse command line args.
 * Currently there is only one argument (-d) besides -h (help)
 * If -d is specified @parameter should_daemonize is set to true.
 */
void parse_args(int argc, char **argv, bool * should_daemonize);

/*
 * Client socket thread function.
 * This function is run by each socket thread.
 * It reads a line from a client socket,
 * then appends it to OUTPUT_FILE,
 * then writes the whole contents of OUTPUT_FILE back to the client socket.
 */
void* sock_thread_func(void* thread_param);

#endif
