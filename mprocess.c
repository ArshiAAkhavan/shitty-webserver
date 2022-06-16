#include "lq.h"
#include "utils.h"
#include "wq.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t max_process;
static size_t num_forked;
static pid_t *forks;
static wq_t work_queue;
static lq_t *log_queue;

static void wait_for_child() {
  int status;
  wait(&status);
  char text[1000];
  int log_read_fd = WEXITSTATUS(status);
  read(log_read_fd, text, sizeof(text));
  lq_push(log_queue, text);
  close(log_read_fd);
}

static _Noreturn void thread_func(void (*request_handler)(int)) {
  while (1) {
    if (num_forked >= max_process) {
      wait_for_child();
      if (num_forked)
        num_forked--;
      continue;
    }

    int fd = wq_pop(&work_queue);

    int log_fd[2];
    pipe(log_fd);
    pid_t cid = fork();
    if (cid == 0) {
      close(log_fd[0]);
      char *log = log_wrapper(request_handler, fd);
      int nbytes = write(log_fd[1], log, strlen(log));
      printf("wrote %d bytes in fd:%d\n", nbytes, log_fd[1]);
      fflush(stdout);

      free(log);
      exit(log_fd[0]);
    } else if (cid > 0) {
      close(log_fd[1]);
      num_forked++;
    } else {
      // in case of error, we push back request back to queue
      wq_push(&work_queue, fd);
    }
  }
}

void mprocess_init_pool(int num_process, lq_t *lq,
                        void (*request_handler)(int)) {
  forks = malloc(sizeof(pid_t) * max_process);
  max_process = num_process;
  num_forked = 0;
  log_queue = lq;

  pthread_t scheduler;
  pthread_create(&scheduler, NULL, (void *)thread_func, request_handler);
}

void mprocess_submit_task(int client_socket_number) {
  wq_push(&work_queue, client_socket_number);
}
