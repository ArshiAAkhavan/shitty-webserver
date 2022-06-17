#include "lq.h"
#include "utils.h"
#include "wq.h"
#include <pthread.h>
#include <stdlib.h>

static wq_t work_queue;
static lq_t *log_queue;

static _Noreturn void thread_func(void (*request_handler)(int)) {
  while (1) {
    int fd = wq_pop(&work_queue);
    char *log = log_wrapper(request_handler, fd);
    lq_push(log_queue, log);
    free(log);
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
