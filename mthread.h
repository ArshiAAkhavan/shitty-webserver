#include "wq.h"
#include <unistd.h>

void mthread_init_pool(int num_threads, void (*request_handler)(int));

void mthread_submit_task(int client_socket_number);
