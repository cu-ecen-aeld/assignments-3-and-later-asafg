#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include "aesdsocket.h"

#ifdef __UCLIBC__
#define reallocarray(ptr, nmemb, size) realloc((ptr), ((nmemb) * (size)))
#endif

/*
 * fucntions used by client socket thread
 */

char * readline_from_socket(int client_sock_fd) {
  int bufsize = BUFLEN;
  char * buf = NULL;
  char * newbuf = NULL;
  if (buf == NULL) {
    buf = malloc(sizeof(char) * BUFLEN);
    if (buf == NULL) {
      perror("readline: malloc");
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
      perror("readline: recv");
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
          perror("readline: reallocarray");
          free(buf);
          return NULL;
        }
        buf = newbuf;
      }
    }
  }
  return buf;
}

bool send_file(FILE * file, int client_sock_fd) {
  char * line = NULL;
  size_t bufsize = 0;
  ssize_t bytes_read = 0;
  ssize_t bytes_sent = 0;
  size_t bytes_left = 0;
  size_t offset = 0;
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
    perror("send_file: send");
    return false;
  }
  return true;
}

void* sock_thread_func(void* thread_param) {
  struct aesd_thread_args * args = (struct aesd_thread_args *)thread_param;
  char * line = NULL;
  FILE * file = NULL;
  int rt = 0;
  line = readline_from_socket(args->sock_fd);
  if (line == NULL) {
    args->last_error = errno;
    goto err_readline_from_socket; //1
  }
  file = fopen(OUTPUT_FILE, "a+");
  if (file == NULL) {
    args->last_error = errno;
    perror("sock_thread_func: fopen a");
    goto err_fopen_a; //2
  }
  if ((args->last_error = pthread_mutex_lock(args->mutex))) {
    errno = args->last_error;
    perror("sock_thread_func: pthread_mutex_lock");
    goto err_mutex_lock; //3
  }
  if (!append_to_file(file, line)) {
    args->last_error = errno;
    goto err_append_to_file; //4
  }
  // closing the file here, and open again for read, 
  // rather than share the open file since rewind(file) 
  // doesn't work good with /dev/aesdsocket,
  // and causes it to skip the first 5 bytes when reading.
  if (fclose(file)) {
    perror("sock_thread_func: fclose");
    if (!args->last_error) { // update last_error only if no prior error
      args->last_error = errno;
    }
  }
  file = fopen(OUTPUT_FILE, "r");
  if (file == NULL) {
    args->last_error = errno;
    perror("sock_thread_func: fopen r");
    goto err_fopen_r; //5
  }
  if (!send_file(file, args->sock_fd)) {
    args->last_error = errno;
  }
err_fopen_r: //5
err_append_to_file: //4 
  if ((rt = pthread_mutex_unlock(args->mutex))) {
    errno = rt;
    perror("sock_thread_func: pthread_mutex_unlock");
    if (!args->last_error) { // update last_error only if no prior error
      args->last_error = rt;
    }
  }
err_mutex_lock: //3
  if (fclose(file)) {
    perror("sock_thread_func: fclose");
    if (!args->last_error) { // update last_error only if no prior error
      args->last_error = errno;
    }
  }
err_fopen_a: //2
  free(line);
err_readline_from_socket: //1
  close(args->sock_fd);
  syslog(LOG_INFO, "Closed connection from %s", args->ip_address);
  args->finished = true;
  return args;
}

