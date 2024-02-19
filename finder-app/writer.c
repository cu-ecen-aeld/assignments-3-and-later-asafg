#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main(int argc, char* argv[]) {
  const char * str_to_write;
  const char * out_file;
  int write_len;
  int fd_out_file;
  int exit_code;

  exit_code = 0;
  if (argc != 3) {
    fprintf(stderr, "writer: Wrong number of arguments\n");
    fprintf(stderr, "usage: writer <filepath> <string>\n");
    return 1;
  }
  out_file = argv[1];
  str_to_write = argv[2];
  write_len = strlen(str_to_write);
  openlog(NULL, 0, LOG_USER);
  fd_out_file = creat(out_file, S_IRUSR | S_IWUSR | S_IRGRP);
  if (fd_out_file == -1) {
    exit_code = errno;
    char * error_msg = strerror(errno);
    syslog(LOG_ERR, "Failed to open file %s : %s", out_file, error_msg);
    return exit_code;
  }
  ssize_t written;
  char * error_msg;
  syslog(LOG_DEBUG, "Writing %s to %s", str_to_write, out_file);
  written = write(fd_out_file, str_to_write, write_len);
  if (written == -1) {
    exit_code = errno;
    error_msg = strerror(errno);
    syslog(LOG_ERR, "Failed to write to file %s : %s", out_file, error_msg);
    return exit_code;
  }
  if (written < write_len) {
    error_msg = "Unknown error";
    exit_code = 2;
    if (errno != 0) {
      exit_code = errno;
      error_msg = strerror(errno);
    }
    syslog(LOG_ERR, "Failed to write to file %s : %s", out_file, error_msg);
    return exit_code;
  }
  int rt_value;
  rt_value = close(fd_out_file);
  if (rt_value == -1) {
    exit_code = errno;
    error_msg = strerror(errno);
    syslog(LOG_ERR, "Failed to close file %s : %s", out_file, error_msg);
    return exit_code;
  }
  closelog();
  return 0;
}
