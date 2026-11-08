#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

#include "queue.h"

struct reader_cfg {
	const char *device;
	struct telem_queue *queue;
	volatile sig_atomic_t *stop;
};

const char *device = "/dev/telemetry";

static void *reader_thread(void *arg);
static void *writer_thread(void *arg);

static volatile sig_atomic_t stop;
static void on_signal(int signo)
{
	(void)signo;
	stop = 1;
}

int main(int argc, char **argv){
    if (argc == 3 && strcmp(argv[1], "--device") == 0){
	    device = argv[2];
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    struct telem_queue *queue;
    struct reader_cfg cfg;
    if (queue_init(&queue, 64) != 0){
        return 1;
    }
    cfg.device = device;
    cfg.queue = queue;
    cfg.stop = &stop;
    pthread_t reader;
    pthread_t writer;
    if (pthread_create(&reader, NULL, reader_thread, &cfg) != 0){
        return 1;
    }
    if (pthread_create(&writer, NULL, writer_thread, &cfg) != 0){
        return 1;
    }
    pthread_join(reader, NULL);
    pthread_join(writer, NULL);
    queue_destroy(queue);
    return 0;
}

static void *reader_thread(void *arg)
{
	struct reader_cfg *cfg = arg;
	struct telemetry_sample sample;
	int fd;
	ssize_t n;

	fd = open(cfg->device, O_RDONLY);
	if (fd < 0) {
		perror("open");
		queue_close(cfg->queue);
		return NULL;
	}

	for (;;) {
		if (*cfg->stop){
			break;
        }
		n = read(fd, &sample, sizeof(sample));
		if (n < 0) {
			if (errno == EINTR || *cfg->stop)
				break;
			perror("read");
			break;
		}
		if (n != (ssize_t)sizeof(sample)){
			continue;
        }

		if (queue_push(cfg->queue, &sample) != 0){
			break;
        }
	}

	queue_close(cfg->queue);
	close(fd);
	return NULL;
}

static void *writer_thread(void *arg)
{
	struct reader_cfg *cfg = arg;
	struct telemetry_sample sample;
	FILE *out;
	unsigned long long gaps = 0;
	int have_expected = 0;
	__u32 expected = 0;

	out = fopen("samples.csv", "w");
	if (out == NULL) {
		perror("fopen");
		return NULL;
	}
	fprintf(out, "ts_ns,seq,temp_mc,pressure_pa\n");

	for (;;) {
		if (queue_pop(cfg->queue, &sample) != 0)
			break;
		if (have_expected && sample.seq != expected)
			gaps++;
		have_expected = 1;
		expected = sample.seq + 1;
		fprintf(out, "%llu,%u,%d,%u\n",
			(unsigned long long)sample.ts_ns,
			sample.seq, sample.temp_mc, sample.pressure_pa);
	}

	fclose(out);
	printf("gaps=%llu\n", gaps);
	return NULL;
}
