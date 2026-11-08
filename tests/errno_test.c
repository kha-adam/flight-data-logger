#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "telemetry_uapi.h"

static int fail(const char *what)
{
	fprintf(stderr, "fail: %s (%s)\n", what, strerror(errno));
	return 1;
}

static void on_usr1(int signo)
{
	(void)signo;
}

int main(void)
{
	struct telemetry_sample sample;
	struct sigaction sa;
	__u32 rate;
	int fd;
	int other;
	int saw_data = 0;
	int saw_again = 0;
	int i;
	pid_t child;
	ssize_t n;

	fd = open("/dev/telemetry", O_RDONLY);
	if (fd < 0)
		return fail("open");
	other = open("/dev/telemetry", O_RDONLY);
	if (other != -1 || errno != EBUSY)
		return fail("second open should be EBUSY");

	rate = 0;
	if (ioctl(fd, TELEM_IOC_SET_RATE, &rate) != -1 || errno != EINVAL)
		return fail("rate 0 should be EINVAL");
	rate = 1000000000U;
	if (ioctl(fd, TELEM_IOC_SET_RATE, &rate) != -1 || errno != EINVAL)
		return fail("huge rate should be EINVAL");
	if (ioctl(fd, _IO('T', 9), NULL) != -1 || errno != ENOTTY)
		return fail("unknown ioctl should be ENOTTY");
	close(fd);

	fd = open("/dev/telemetry", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return fail("nonblock open");
	for (i = 0; i < 100000 && !(saw_data && saw_again); i++) {
		n = read(fd, &sample, sizeof(sample));
		if (n == (ssize_t)sizeof(sample))
			saw_data = 1;
		else if (n < 0 && errno == EAGAIN)
			saw_again = 1;
		else
			return fail("nonblock read");
	}
	if (!saw_data || !saw_again)
		return fail("expected both data and EAGAIN");
	close(fd);

	rate = 1;
	fd = open("/dev/telemetry", O_RDONLY);
	if (fd < 0)
		return fail("open for slow rate");
	if (ioctl(fd, TELEM_IOC_SET_RATE, &rate) != 0)
		return fail("set rate 1");
	close(fd);

	child = fork();
	if (child < 0)
		return fail("fork");
	if (child == 0) {
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_usr1;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGUSR1, &sa, NULL);
		fd = open("/dev/telemetry", O_RDONLY);
		if (fd < 0)
			return fail("child open");
		n = read(fd, &sample, sizeof(sample));
		if (n < 0 && errno == EINTR)
			return 0;
		fprintf(stderr, "fail: blocked read was not interrupted\n");
		return 1;
	}
	usleep(200000);
	kill(child, SIGUSR1);
	waitpid(child, &i, 0);
	if (!WIFEXITED(i) || WEXITSTATUS(i) != 0)
		return fail("child did not exit 0");

	rate = 100;
	fd = open("/dev/telemetry", O_RDONLY);
	if (fd < 0)
		return fail("reopen to restore rate");
	if (ioctl(fd, TELEM_IOC_SET_RATE, &rate) != 0)
		return fail("restore rate 100");
	close(fd);

	printf("errno_test ok\n");
	return 0;
}
