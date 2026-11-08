#include <stdio.h>
#include <string.h>

#include "queue.h"

static int fail(const char *what)
{
	fprintf(stderr, "fail: %s\n", what);
	return 1;
}

int main(void)
{
	struct telem_queue *q;
	struct telemetry_sample in, out;

	if (queue_init(&q, 2) != 0)
		return fail("init");

	memset(&in, 0, sizeof(in));
	in.seq = 1;
	if (queue_push(q, &in) != 0)
		return fail("push 1");
	in.seq = 2;
	if (queue_push(q, &in) != 0)
		return fail("push 2");

	if (queue_pop(q, &out) != 0 || out.seq != 1)
		return fail("pop seq 1");

	in.seq = 3;
	if (queue_push(q, &in) != 0)
		return fail("push 3");

	if (queue_pop(q, &out) != 0 || out.seq != 2)
		return fail("pop seq 2");
	if (queue_pop(q, &out) != 0 || out.seq != 3)
		return fail("pop seq 3");

	queue_close(q);
	if (queue_pop(q, &out) != -1)
		return fail("pop after close");
	in.seq = 4;
	if (queue_push(q, &in) != -1)
		return fail("push after close");

	queue_destroy(q);
	printf("queue_test ok\n");
	return 0;
}
