#ifndef TELEMETRY_QUEUE_H
#define TELEMETRY_QUEUE_H

#include <stddef.h>
#include "telemetry_uapi.h"

struct telem_queue;

int queue_init(struct telem_queue **qp, size_t capacity);
void queue_close(struct telem_queue *q);
void queue_destroy(struct telem_queue *q);
int queue_push(struct telem_queue *q, const struct telemetry_sample *s);
int queue_pop(struct telem_queue *q, struct telemetry_sample *s);

#endif