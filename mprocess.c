#include "lq.h"
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
  // printf("child exited with: %d\n", WEXITSTATUS(status));
  // fflush(stdout);

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

      struct timeval t1, t2;
      double elapsedTime;

      gettimeofday(&t1, NULL);
      request_handler(fd);
      close(fd);
      gettimeofday(&t2, NULL);

      elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;    // sec to ms
      elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms

      char text[1000];
      sprintf(text, "%ld: request took: %f", t1.tv_sec, elapsedTime);
      int nbytes = write(log_fd[1], text, strlen(text));
      printf("wrote %d bytes in fd:%d\n", nbytes, log_fd[1]);
      fflush(stdout);

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
