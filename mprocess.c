#include "wq.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t max_process;
static size_t num_forked;
static pid_t *forks;
static wq_t work_queue;

static _Noreturn void thread_func(void (*request_handler)(int)) {
  while (1) {
    if (num_forked >= max_process) {
      wait(NULL);
      if (num_forked)
          num_forked--;
      continue;
    }

    int fd = wq_pop(&work_queue);
    pid_t cid = fork();
    if (cid == 0) {
      request_handler(fd);
      close(fd);
      exit(0);
    } else if (cid > 0) {
      num_forked++;
    } else {
      // in case of error, we push back request back to queue
      wq_push(&work_queue, fd);
    }
  }
}

void mprocess_init_pool(int num_process, void (*request_handler)(int)) {
  forks = malloc(sizeof(pid_t) * max_process);
  max_process = num_process;
  num_forked = 0;

  pthread_t scheduler;
  pthread_create(&scheduler, NULL, (void *)thread_func, request_handler);
}

void mprocess_submit_task(int client_socket_number) {
  wq_push(&work_queue, client_socket_number);
}
