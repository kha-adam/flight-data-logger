#include <stdio.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dev/telemetry", O_RDONLY);
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int rc;

	if (fd < 0) {
		perror("open");
		return 1;
	}
	rc = poll(&pfd, 1, 1000);
	if (rc < 0) {
		perror("poll");
		return 1;
	}
	printf("rc=%d readable=%d\n", rc, (pfd.revents & POLLIN) != 0);
	close(fd);
	return 0;
}