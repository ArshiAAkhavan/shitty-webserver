#include <stdlib.h>
#include "wq.h"
#include "utlist.h"

/* Initializes a work queue WQ. */
void wq_init(wq_t *wq) {

    /* TODO: Make me thread-safe! */
    sem_init(&(wq->sema), 0, 0);
    pthread_mutex_init(&(wq->lock),NULL);

    wq->size = 0;
    wq->head = NULL;
}

/* Remove an item from the WQ. This function should block until there
 * is at least one item on the queue. */
int wq_pop(wq_t *wq) {

    /* TODO: Make me blocking and thread-safe! */
    sem_wait(&(wq->sema));
    pthread_mutex_lock(&(wq->lock));


    wq_item_t *wq_item = wq->head;
    int client_socket_fd = wq->head->client_socket_fd;
    wq->size--;
    DL_DELETE(wq->head, wq->head);

    pthread_mutex_unlock(&(wq->lock));

    free(wq_item);
    return client_socket_fd;
}

/* Add ITEM to WQ. */
void wq_push(wq_t *wq, int client_socket_fd) {

    /* TODO: Make me thread-safe! */
    pthread_mutex_lock(&(wq->lock));

    wq_item_t *wq_item = calloc(1, sizeof(wq_item_t));
    wq_item->client_socket_fd = client_socket_fd;
    DL_APPEND(wq->head, wq_item);
    wq->size++;
    pthread_mutex_unlock(&(wq->lock));

    sem_post(&(wq->sema));

}
