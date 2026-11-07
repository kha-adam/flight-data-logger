#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "telemetry_uapi.h"

int main(void)
{
	struct telemetry_sample sample;
	int fd;
	ssize_t n;

	fd = open("/dev/telemetry", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	for (;;) {
		n = read(fd, &sample, sizeof(sample));
		if (n < 0) {
			perror("read");
			return 1;
		}
		printf("seq=%u temp_mc=%d pressure_pa=%u\n",
		       sample.seq, sample.temp_mc, sample.pressure_pa);
	}
}