#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xpmem.h>

#define TEST_NAME "CT_007"
#define CHKANDJUMP(cond, ...)							\
	do {												\
		if (cond) {										\
			fprintf(stderr, " [NG] ");					\
			fprintf(stderr, __VA_ARGS__);				\
			fprintf(stderr, " failed\n");				\
			goto fn_fail;								\
		}												\
	} while(0);

#define OKNG(cond, ...)									\
	do {												\
		if (cond) {										\
			CHKANDJUMP(cond, __VA_ARGS__);				\
		} else {										\
			fprintf(stdout, " [OK] ");					\
			fprintf(stdout, __VA_ARGS__);				\
			fprintf(stdout, "\n");						\
		}												\
	} while(0);

#define TEST_VAL 0x1129
#define BAD_ADDRESS (void*)-1

int main(int argc, char** argv) {
	void *mem, *attach;
	int rc = 0;
	int status;
	pid_t pid;
	xpmem_segid_t segid;
	xpmem_apid_t apid;
	struct xpmem_addr addr;
	const unsigned int page_size = sysconf(_SC_PAGESIZE);

	printf("*** %s start *******************************\n", TEST_NAME);

	mem = mmap(0, page_size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
	CHKANDJUMP(mem == NULL, "mmap");
	memset(mem, 0, page_size);

	rc = xpmem_init();
	CHKANDJUMP(rc != 0, "xpmem_init");

	segid = xpmem_make(BAD_ADDRESS, page_size, XPMEM_PERMIT_MODE, (void*)0666);
	OKNG(segid != -1, "xpmem_make failed (invalid address)");

	segid = xpmem_make(mem, page_size, XPMEM_PERMIT_MODE, (void*)0666);
	CHKANDJUMP(segid == -1, "xpmem_make");

	segid = xpmem_make(mem, page_size, XPMEM_PERMIT_MODE, (void*)0666);
	OKNG(segid == -1, "xpmem_make succeed(do twice to same address)");

	fflush(0);
	pid = fork();
	CHKANDJUMP(pid == -1, "fork failed\n");

	if (pid == 0) {
		/* Child process */
		apid = xpmem_get(segid, XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
		CHKANDJUMP(apid == -1, "xpmem_get in child");

		addr.apid = apid;
		addr.offset = 0;
		attach = xpmem_attach(addr, page_size, NULL);
		CHKANDJUMP(attach == (void*)-1, "xpmem_attach in child");

		*((unsigned long*)attach) = TEST_VAL;

		rc = xpmem_detach(attach);
		CHKANDJUMP(rc == -1, "xpmem_detach in child");

		fflush(0);
		_exit(0);
	} else {
		/* Parent process */
		rc = waitpid(pid, &status, 0);
		CHKANDJUMP(rc == -1, "waitpid failed\n");

		CHKANDJUMP(*((unsigned long*)mem) != TEST_VAL, "validate TEST_VAL");

		rc = xpmem_remove(segid);
		CHKANDJUMP(rc == -1, "xpmem_remove");
	}

	printf("*** %s PASSED\n\n", TEST_NAME);
	return 0;

fn_fail:
	printf("*** %s FAILED\n\n", TEST_NAME);

	return -1;
}
