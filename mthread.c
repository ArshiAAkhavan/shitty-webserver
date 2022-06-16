#include "lq.h"
#include "wq.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static wq_t work_queue;
static lq_t *log_queue;

static _Noreturn void thread_func(void (*request_handler)(int)) {
  while (1) {
    struct timeval t1, t2;
    double elapsedTime;

    int fd = wq_pop(&work_queue);

    gettimeofday(&t1, NULL);
    request_handler(fd);
    close(fd);
    gettimeofday(&t2, NULL);

    elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;    // sec to ms
    elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0; // us to ms

    char text[1000];
    sprintf(text, "%ld: request took: %f\n", t1.tv_sec, elapsedTime);
    lq_push(log_queue, text);
  }
}

void mthread_init_pool(int num_threads, lq_t *lq,
                       void (*request_handler)(int)) {
  log_queue = lq;
  pthread_t threads[num_threads];
  for (int i = 0; i < num_threads; i++) {
    pthread_create(threads + i, NULL, (void *)thread_func, request_handler);
  }
}

void mthread_submit_task(int client_socket_number) {
  wq_push(&work_queue, client_socket_number);
}
