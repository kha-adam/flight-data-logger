#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "telemetry_uapi.h"

int main(int argc, char **argv)
{
	int fd;
	__u32 rate;
	struct telemetry_stats s;

	if (argc < 2) {
		fprintf(stderr, "usage: telemetryctl set-rate N | stats | reset\n");
		return 1;
	}

	fd = open("/dev/telemetry", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (strcmp(argv[1], "set-rate") == 0) {
		if (argc != 3){
            fprintf(stderr, "usage: telemetryctl set-rate N\n");
            return 1;
        }
        rate = strtoul(argv[2], NULL, 10);
        if (ioctl(fd, TELEM_IOC_SET_RATE, &rate) < 0) {
	        perror("set-rate");
	        return 1;
        }

	} else if (strcmp(argv[1], "stats") == 0) {
		if (ioctl(fd, TELEM_IOC_GET_STATS, &s) < 0) {
	        perror("stats");
	        return 1;
        }
        printf("produced: %llu\n", (unsigned long long)s.produced);
        printf("consumed: %llu\n", (unsigned long long)s.consumed);
        printf("dropped: %llu\n", (unsigned long long)s.dropped);
        printf("high_watermark: %u\n", s.high_watermark);
        printf("rate_hz: %u\n", s.rate_hz);
        printf("buf_samples: %u\n", s.buf_samples);
	} else if (strcmp(argv[1], "reset") == 0) {
		if (ioctl(fd, TELEM_IOC_RESET, NULL) < 0) {
	        perror("reset");
	        return 1;
        }
	} else {
		fprintf(stderr, "unknown command\n");
		return 1;
	}

	close(fd);
	return 0;
}