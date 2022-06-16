#ifndef __LQ__
#define __LQ__

#include <pthread.h>
#include <semaphore.h>

/* LQ defines a log queue which will be used to store accepted client sockets
 * waiting to be served. */

typedef struct lq_item {
  char text[1000]; // Log
  struct lq_item *next;
  struct lq_item *prev;
} lq_item_t;

typedef struct lq {
  int size;
  lq_item_t *head;
  sem_t sema;
  pthread_mutex_t lock;
} lq_t;

void lq_init(lq_t *lq);
void lq_push(lq_t *lq, char* text);
char* lq_pop(lq_t *lq);

#endif
