#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include <malloc.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include "aesdsocket.h"
#include "../aesd-char-driver/aesd_ioctl.h"

#ifdef __UCLIBC__
#define reallocarray(ptr, nmemb, size) realloc((ptr), ((nmemb) * (size)))
#endif

#define EMBED_CTRL_PREF "AESDCHAR_IOCSEEKTO:"
#define MIN_EMBED_PARAM_CHARS 3

/*
 * fucntions used by client socket thread
 */

char *readline_from_socket(int client_sock_fd, size_t *line_size) {
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
  while (eol == NULL) {
    bytes_read = recv(client_sock_fd, buf + offset, bufsize - offset, 0);
    if (bytes_read == -1) {
      perror("readline: recv");
      free(buf);
      return NULL;
    }
    else if (bytes_read == 0) {
      *line_size = offset;
      return buf;
    }
    eol = memchr(buf + offset, '\n', bytes_read);
    if (eol == NULL) { // we did not encounter '\n', double the buffer size and read again
      offset += bytes_read;
      if (offset == bufsize) {
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
  *line_size = (eol - buf) + 1;
  printf("readline_from_socket: *line_size: %ld\n", *line_size);
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

#ifdef USE_AESD_CHAR_DEVICE
/**
 * parse the line read from the socket.
 * return true if the line is embedded control string, false otherwise.
 * When embedded control string is found, *seek_to would be populated
 * with the relevant offsets.
 * Expected format for embedded control string is:
 * "AESDCHAR_IOCSEEKTO:<X>,<Y>"
 * where <X> is between 0 and AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED
 * and <Y> is between 0 and the size of the command in the index specified in <X>
 */
static bool parse_ctrl_line(char *line, size_t *line_size, struct aesd_seekto *seek_to) {
  char *write_cmd_str, *write_cmd_offset_str, *comma_ptr;
  int write_cmd, write_cmd_offset;
  size_t prefix_size = strlen(EMBED_CTRL_PREF);
  write_cmd_str = write_cmd_offset_str = comma_ptr = NULL;
  write_cmd = write_cmd_offset = -1;

  if (*line_size < prefix_size + MIN_EMBED_PARAM_CHARS
      || line[*line_size - 1] != '\n') {
    return false;
  }
  if (!memmem(line, prefix_size, EMBED_CTRL_PREF, prefix_size)) {
    return false;
  }
  comma_ptr = memchr(line + prefix_size, ',', *line_size - prefix_size);
  if (!comma_ptr) {
    return false;
  }
  *comma_ptr = '\0'; //terminate the first param
  line[*line_size - 1] = '\0'; //terminate 2nd param
  write_cmd_str = line + prefix_size;
  write_cmd_offset_str = comma_ptr + 1;
  write_cmd = atoi(write_cmd_str);
  write_cmd_offset = atoi(write_cmd_offset_str);
  seek_to->write_cmd = write_cmd;
  seek_to->write_cmd_offset = write_cmd_offset;
  return true;
}
#endif // USE_AESD_CHAR_DEVICE

void* sock_thread_func(void* thread_param) {
  struct aesd_thread_args * args = (struct aesd_thread_args *)thread_param;
  char * line = NULL;
  size_t line_size = 0;
  FILE * file = NULL;
  bool is_ctrl_cmd = false;
  int rt = 0;
  int fd = -1;
  struct aesd_seekto seek_to;
  memset(&seek_to, 0, sizeof(struct aesd_seekto));
  line = readline_from_socket(args->sock_fd, &line_size);
  if (line == NULL) {
    args->last_error = errno;
    goto err_readline_from_socket; //1
  }

#ifdef USE_AESD_CHAR_DEVICE
  is_ctrl_cmd = parse_ctrl_line(line, &line_size, &seek_to);
#endif
  if (!is_ctrl_cmd) { // normal line, open the file in append mode
    file = fopen(OUTPUT_FILE, "a");
    if (file == NULL) {
      args->last_error = errno;
      perror("sock_thread_func: fopen a");
      goto err_fopen_a; //2
    }
  } // if (!is_ctrl_cmd)
  // acquire mutex any way, we're either writing to the file,
  // or changing its position before read.
  if ((args->last_error = pthread_mutex_lock(args->mutex))) {
    errno = args->last_error;
    perror("sock_thread_func: pthread_mutex_lock");
    goto err_mutex_lock; //3
  }
  if (!is_ctrl_cmd) {
    if (!append_to_file(file, line, line_size)) {
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
  } // if (!is_ctrl_cmd)
  file = fopen(OUTPUT_FILE, "r");
  if (file == NULL) {
    args->last_error = errno;
    perror("sock_thread_func: fopen r");
    goto err_fopen_r; //5
  }
  if (is_ctrl_cmd) { //we have embedded control, issue ioctl
    fd = fileno(file);
    if ((rt = ioctl(fd, AESDCHAR_IOCSEEKTO, &seek_to))) {
      errno = rt;
      args->last_error = errno;
      perror("sock_thread_func: ioctl");
      goto err_ioctl; // 5.5
    }
  }
  if (!send_file(file, args->sock_fd)) {
    args->last_error = errno;
  }
err_ioctl:   //5.5
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

