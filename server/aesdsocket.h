#ifndef AESDSOCKET_H
#define AESDSOCKET_H

#include <stdio.h>

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
char * readline(int client_sock_fd);

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
 * Creates and binds a socket to the first available local address,
 * then starts listening on this socket.
 * returns the fd for the bound socket 
 *   or -1 if any of the actions invloved in the sequence fails
 */
int start_listening(char * server_ip);
#endif
