#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/wait.h>

pid_t pid;

void
sig(int s)
{
	static int cnt = 0;

	cnt++;
	if (cnt == 1) {
		fprintf(stderr, "kill SIGURG\n");
		kill(pid, SIGURG);
	}
	else if (cnt == 2) {
		fprintf(stderr, "kill SIGINT\n");
		kill(pid, SIGINT);
	}
	alarm(2);
}

void
child()
{
	struct sigaction act;
	int fds[2];
	char c;
	int rc;

	pipe(fds);
	rc = read(fds[0], &c, 1);
}

int
main(int argc, char **argv)
{
	int st;
	int rc;

	pid = fork();
	if (pid == 0) {
		child();
		exit(1);
	}
	signal(SIGALRM, sig);
	alarm(2);
	while ((rc = waitpid(pid, &st, 0)) == -1 && errno == EINTR);
	if (rc != pid) {
		fprintf(stderr, "CT3005 NG BAD wait rc=%d errno=%d\n", rc, errno);
		exit(1);
	}
	if (!WIFSIGNALED(st)) {
		fprintf(stderr, "CT3005 NG no signaled st=%08x\n", st);
		exit(1);
	}
	if (WTERMSIG(st) != SIGINT) {
		fprintf(stderr, "CT3005 NG BAD signal sig=%d\n", WTERMSIG(st));
		exit(1);
	}
	fprintf(stderr, "CT3005 OK\n");
	exit(0);
}