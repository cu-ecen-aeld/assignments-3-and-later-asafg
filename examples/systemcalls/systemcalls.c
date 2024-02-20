#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "systemcalls.h"

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{
    int rt;
    rt = system(cmd);
    if (rt == -1) {
      perror("system");
      return false;
    }
    if (rt == 127) {
      return false; 
    }
    return true;
}

const char * get_file_name(const char * path);

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    int status;
    pid_t pid;
    bool is_success = false;

    pid = fork();
    if (pid == -1) {
      perror("fork failed");
      goto cleanup;
    }
    else if (pid == 0) { /* child process */
      const char * path = command[0];
      const char * file_name = get_file_name(path);
      command[0] = (char *)file_name;
      execv(path, command);
      /* would reach here only if execv fails */
      exit(-1);
    }
    /* parent process */
    if (waitpid(pid, &status, 0) == -1) {
      perror("waitpid");
      goto cleanup;
    }
    else {
      if (WIFEXITED(status)) { /* child process exited (was not iterrupted) */
        if (WEXITSTATUS(status)) {
          /* child process exited with non zero value - i.e. failed */
          goto cleanup;
        }
      }
    }
    /* if we reached here, everything is fine */
    is_success = true;

cleanup:
    va_end(args);
    return is_success;
}

/* helper function to extract file name from an absolute path */
const char * get_file_name(const char * path) {
  int len = strlen(path);
  int i;
  for (i = len - 1;i > 0;i--) {
    if (path[i] == '/') {
      i++;
      break;
    }
  }
  return path + i;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;

    int status;
    pid_t pid;
    bool is_success = false;
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) { 
      perror("open");
      goto cleanup;
    }
    pid = fork();
    if (pid == -1) {
      perror("fork");
      goto cleanup;
    }
    else if (pid == 0) { /* child process */
      /* output redirection */
      if (dup2(fd, STDOUT_FILENO) < 0) {
        perror("dup2");
        goto cleanup;
      }
      close(fd);
      const char * path = command[0];
      const char * file_name = get_file_name(path);
      command[0] = (char *)file_name;
      execv(path, command);
      /* would reach here only if execv fails */
      exit(-1);
    }
    /* parent process */
    if (waitpid(pid, &status, 0) == -1) {
      perror("waitpid");
      goto cleanup;
    }
    else {
      if (WIFEXITED(status)) {
        if (WEXITSTATUS(status)) {
          /* child process exited with non zero value - i.e. failed */
          goto cleanup;
        }
      }
    }
    /* if we reached here, everything is fine */
    is_success = true;

cleanup:
    va_end(args);
    if (fd > -1) {
      close(fd);
    }

    return is_success;
}
