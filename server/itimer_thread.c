#include <malloc.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include "aesdsocket.h"

#ifndef USE_AESD_CHAR_DEVICE  // no need for this module if using /dev/aesdchar
                              
#define CLOCK_ID CLOCK_MONOTONIC
#define RFC2822_TIME_FORMAT "%a, %d %b %Y %T %z"
#define STRFTIME_BUF_SIZE 200 // used to hold the formatted time string

/*
 * fucntions used by interval timer thread
 */

void timer_thread(union sigval sigval) {
  struct timer_thread_args * args = (struct timer_thread_args *)sigval.sival_ptr; 
  int rc;
  char rfc2822_time[STRFTIME_BUF_SIZE];
  FILE * file = NULL;
  file = fopen(OUTPUT_FILE, "a+");
  if (file == NULL) {
    perror("timer_thread: fopen");
    goto err_fopen;
  }
  if ((rc = pthread_mutex_lock(args->mutex))) {
    errno = rc;
    perror("timer_thread: pthread_mutex_lock");
    goto err_mutex_lock; 
  }
  time_t cur_time = time(NULL);
  struct tm * tm_p = localtime(&cur_time);
  rc = strftime(
      rfc2822_time, sizeof(rfc2822_time),
      "timestamp: " RFC2822_TIME_FORMAT, tm_p)
    ;
  if (rc == 0) {
    perror("timer_thread: strftime");
    goto err_strftime;
  }
  if (rc > STRFTIME_BUF_SIZE - 1) {
      rc = STRFTIME_BUF_SIZE - 1;
    } 
    rfc2822_time[rc-1] = '\n';
    rfc2822_time[rc] = '\0';
    /*
     * we don't check for error here,
     * since append_to_file will perror if it fails.
     * there is no additional work to do if it fails.
     */
    append_to_file(file, rfc2822_time);
    /*
     * cleanup starts here
     */
err_strftime:
  if ((rc = pthread_mutex_unlock(args->mutex))) {
    errno = rc;
    perror("timer_thread: pthread_mutex_unlock");
  }
err_mutex_lock:
  if (fclose(file)) {
    perror("timer_thread: fclose");
  }
err_fopen:
  /* nothing to do here */
  return;
}

bool start_timer(int interval_sec, struct timer_thread_args * timer_args, timer_t * timer_id) {
  //  bool success = false;
  struct sigevent sev;
  memset(&sev, 0, sizeof(struct sigevent));
  /*
   * Setup a call to timer_thread passing in the td structure as the sigev_value
   * argument
   */
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_value.sival_ptr = timer_args;
  sev.sigev_notify_function = timer_thread;
  if (timer_create(CLOCK_ID, &sev, timer_id)) {
    perror("start_timer: timer_create");
    return false;
  } 
  struct itimerspec interval;
  memset(&interval, 0, sizeof(struct itimerspec));
  interval.it_interval.tv_sec = interval_sec;
  interval.it_interval.tv_nsec = 0;
  interval.it_value.tv_sec = interval_sec;
  interval.it_value.tv_nsec = 0;
  if (timer_settime(*timer_id, 0, &interval, NULL)) {
    perror("start_timer: timer_settime");
    return false;
  }
  return true;
}
#endif //!USE_AESD_CHAR_DEVICE
