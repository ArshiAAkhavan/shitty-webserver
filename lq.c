#include "lq.h"
#include "utlist.h"
#include <stdlib.h>
#include <string.h>

/* Initializes a work queue lq. */
void lq_init(lq_t *lq) {

  /* TODO: Make me thread-safe! */
  sem_init(&(lq->sema), 0, 0);
  pthread_mutex_init(&(lq->lock), NULL);

  lq->size = 0;
  lq->head = NULL;
}

/* Remove an item from the lq. This function should block until there
 * is at least one item on the queue. */
char *lq_pop(lq_t *lq) {

  /* TODO: Make me blocking and thread-safe! */
  // sem_wait(&(lq->sema));
  // pthread_mutex_lock(&(lq->lock));
  if (! lq->size){
    return NULL;
  }

  lq_item_t *lq_item = lq->head;
  char *text = malloc(sizeof(char) * 1000);
  memcpy(text, lq->head->text, sizeof(lq_item->text));
  lq->size--;
  DL_DELETE(lq->head, lq->head);

  // pthread_mutex_unlock(&(lq->lock));

  free(lq_item);
  return text;
}

/* Add ITEM to lq. */
void lq_push(lq_t *lq, char *text) {

  /* TODO: Make me thread-safe! */
  pthread_mutex_lock(&(lq->lock));

  lq_item_t *lq_item = calloc(1, sizeof(lq_item_t));
  memcpy(lq_item->text, text, sizeof(lq_item->text));
  DL_APPEND(lq->head, lq_item);
  lq->size++;
  pthread_mutex_unlock(&(lq->lock));

  sem_post(&(lq->sema));
}
