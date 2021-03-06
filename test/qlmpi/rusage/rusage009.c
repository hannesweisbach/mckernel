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
#include "qltest.h"

#define DEBUG

#ifdef DEBUG
#define dprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stderr, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#define eprintf(...)                                            \
    do {                                                        \
	char msg[1024];                                         \
	sprintf(msg, __VA_ARGS__);                              \
	fprintf(stderr, "%s,%s", __FUNCTION__, msg);            \
    } while (0);
#else
#define dprintf(...) do {  } while (0)
#define eprintf(...) do {  } while (0)
#endif

#define CHKANDJUMP(cond, err, ...)                                      \
    do {                                                                \
		if(cond) {                                                      \
			eprintf(__VA_ARGS__);                                       \
			ret = err;                                                  \
			goto fn_fail;                                               \
		}                                                               \
    } while(0)

int sz_mem[] = {
	4 * (1ULL<<10),
	2 * (1ULL<<20),
	1 * (1ULL<<30),
	134217728};

#define SZ_INDEX 0

int main(int argc, char** argv) {
	void* mem;
	int ret = 0;
	pid_t pid;
	int status;
	key_t key = ftok(argv[0], 0);
	int shmid;
// for swap_test
#define TEST_VAL 0x1234
	int swap_rc = 0;
	char buffer[BUF_SIZE];
	
	shmid = shmget(key, sz_mem[SZ_INDEX], IPC_CREAT | 0660);
	CHKANDJUMP(shmid == -1, 255, "shmget failed: %s\n", strerror(errno));
	
	pid = fork();
	CHKANDJUMP(pid == -1, 255, "fork failed\n");
	if(pid == 0) {
		mem = shmat(shmid, NULL, 0);
		CHKANDJUMP(mem == (void*)-1, 255, "shmat failed: %s\n", strerror(errno));
		memset(mem, 0, sz_mem[SZ_INDEX]);
		
// for swap_test
		swap_rc = do_swap("/tmp/rusage009_c.swp", buffer);
		if (swap_rc < 0) {
			printf("[NG] swap in child is failed\n");
		}
		*((unsigned long*)mem) = TEST_VAL;

		ret = shmdt(mem);
		CHKANDJUMP(ret == -1, 255, "shmdt failed\n");

		_exit(123);
	} else {
		mem = shmat(shmid, NULL, 0);
		CHKANDJUMP(mem == (void*)-1, 255, "shmat failed: %s\n", strerror(errno));
		
		ret = waitpid(pid, &status, 0);
		CHKANDJUMP(ret == -1, 255, "waitpid failed\n");

// for swap_test
		// before swap
		unsigned long val = *((unsigned long*)mem);
		if (val == TEST_VAL) {
			printf("[OK] before swap, val:0x%lx\n", val);
		} else {
			printf("[NG] before swap, val is not 0x%lx, val is 0x%lx\n", TEST_VAL, val);
		}

		swap_rc = do_swap("/tmp/rusage009_p.swp", buffer);
		if (swap_rc < 0) {
			printf("[NG] swap in parent is failed\n");
		}

		// after swap
		val = *((unsigned long*)mem);
		if (val == TEST_VAL) {
			printf("[OK] after swap,  val:0x%lx\n", val);
		} else {
			printf("[NG] after swap,  val is not 0x%lx, val is 0x%lx\n", TEST_VAL, val);
		}

#if 0
		struct shmid_ds buf;
		ret = shmctl(shmid, IPC_RMID, &buf);
		CHKANDJUMP(ret == -1, 255, "shmctl failed\n");
#endif

		ret = shmdt(mem);
		CHKANDJUMP(ret == -1, 255, "shmdt failed\n");
	}
	
 fn_exit:
	return ret;
 fn_fail:
	goto fn_exit;
}
