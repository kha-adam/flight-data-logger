#include "queue.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct telem_queue {
	struct telemetry_sample *slots;
	size_t capacity;
	size_t head;
	size_t tail;
	size_t count;
	int closed;
	pthread_mutex_t mu;
	pthread_cond_t not_empty;
	pthread_cond_t not_full;
};

int queue_init(struct telem_queue **qp, size_t capacity){
    struct telem_queue *q;
    if (qp == NULL || capacity < 1){
        return -1;
    }
    q = calloc(1, sizeof(**qp));
    if (q == NULL){
        return -1;
    }
    q->slots = calloc(capacity, sizeof(*q->slots));
    if(q->slots == NULL){
        free(q);
        return -1;
    }
    q->capacity = capacity;
    if(pthread_mutex_init(&q->mu, NULL) != 0){
        free(q->slots);
        free(q);
        return -1;
    }
    if (pthread_cond_init(&q->not_empty, NULL) != 0){
        pthread_mutex_destroy(&q->mu);
        free(q->slots);
        free(q);
        return -1; 
    }
    if (pthread_cond_init(&q->not_full, NULL) != 0){
        pthread_mutex_destroy(&q->mu);
        pthread_cond_destroy(&q->not_empty);
        free(q->slots);
        free(q);
        return -1; 
    }
    *qp = q;
    return 0;
}
void queue_destroy(struct telem_queue *q){
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);
    pthread_mutex_destroy(&q->mu);
    free(q->slots);
    free(q);
}
void queue_close(struct telem_queue *q)
{
    pthread_mutex_lock(&q->mu);
    q->closed = 1;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->mu);
}

int queue_push(struct telem_queue *q, const struct telemetry_sample *s)
{
	pthread_mutex_lock(&q->mu);
    while(q->count == q->capacity && !q->closed){
        pthread_cond_wait(&q->not_full, &q->mu);
    }
    if (q->closed){
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
    q->slots[q->tail] = *s;
    q->tail+=1;
    if (q->tail == q->capacity){
        q->tail = 0;
    }
    q->count+=1;
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mu);
    return 0;
}

int queue_pop(struct telem_queue *q, struct telemetry_sample *s)
{
	pthread_mutex_lock(&q->mu);
    while(q->count == 0 && !q->closed){
        pthread_cond_wait(&q->not_empty, &q->mu);
    }
    if (q->count == 0) {
        pthread_mutex_unlock(&q->mu);
        return -1;
    }
    *s = q->slots[q->head];
    q->head++;
    if (q->head==q->capacity){
        q->head = 0;
    }
    q->count --;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mu);
    return 0;
}