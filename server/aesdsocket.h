#ifndef AESDSOCKET_H
/*
 * Creates and binds a socket to the first available local address,
 * then starts listening on this socket.
 * returns the fd for the bound socket 
 *   or -1 if any of the actions invloved in the sequence fails
 */
int start_listening(char * server_ip);
#endif
