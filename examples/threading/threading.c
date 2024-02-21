#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

// Optional: use these functions to add debug or error prints to your application
//#define DEBUG_LOG(msg,...)
#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_args structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_args* thread_func_args = (struct thread_data *) thread_param;
    int rc;
    struct thread_data * args;
    args = (struct thread_data *)thread_param;
    rc = usleep(args->wait_to_obtain_ms);
    if (rc == -1) {
      perror("usleep");
      return args;
    }
    rc = pthread_mutex_lock(args->mutex);
    if (rc != 0) {
      errno = rc;
      perror("pthread_mutex_lock");
      return args;
    }
    rc = usleep(args->wait_to_release_ms);
    if (rc == -1) {
      perror("usleep");
      return args;
    }
    rc = pthread_mutex_unlock(args->mutex);
    if (rc != 0) {
      errno = rc;
      perror("pthread_mutex_unlock");
      return args;
    }
    args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_args, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */
    int rc;
    struct thread_data * thread_args = malloc(sizeof(struct thread_data));
    if (thread_args == NULL) {
      perror("malloc");
      return false;
    }
    thread_args->thread_id = -1;
    thread_args->mutex = mutex;
    thread_args->wait_to_obtain_ms = wait_to_obtain_ms;
    thread_args->wait_to_release_ms = wait_to_release_ms;
    thread_args->thread_complete_success = false;
    rc = pthread_create(&(thread_args->thread_id), NULL, threadfunc, thread_args);
    if (rc != 0) {
      errno = rc;
      perror("pthread_create");
      free(thread_args);
      return false;
    }
    *thread = thread_args->thread_id;
    DEBUG_LOG("Created thread(id: %ju)", *thread);
    return true;
}

