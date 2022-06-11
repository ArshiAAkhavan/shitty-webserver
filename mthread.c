#include "wq.h"
#include <unistd.h>
#include <pthread.h>

static wq_t work_queue;

static _Noreturn void thread_func(void (*request_handler)(int)) {
  while (1) {
    int fd = wq_pop(&work_queue);
    request_handler(fd);
    close(fd);
  }
}

void mthread_init_pool(int num_threads, void (*request_handler)(int)) {
  pthread_t threads[num_threads];
  for (int i = 0; i < num_threads; i++) {
    pthread_create(threads + i, NULL, (void *)thread_func, request_handler);
  }
}

void mthread_submit_task(int client_socket_number) {
  wq_push(&work_queue, client_socket_number);
}
