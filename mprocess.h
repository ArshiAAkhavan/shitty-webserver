#include "lq.h"
#include "wq.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void mprocess_init_pool(int num_process, lq_t *lq, void (*request_handler)(int));

void mprocess_submit_task(int client_socket_number);
