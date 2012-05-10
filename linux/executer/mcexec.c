#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <asm/unistd.h>
#include "../include/uprotocol.h"
#include <sched.h>

#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#define DEBUG

#ifndef DEBUG
#define __dprint(msg, ...)
#define __dprintf(arg, ...)
#define __eprint(msg, ...)
#define __eprinf(format, ...)
#else
#define __dprint(msg, ...)  printf("%s: " msg, __FUNCTION__)
#define __dprintf(format, ...)  printf("%s: " format, __FUNCTION__, \
                                       __VA_ARGS__)
#define __eprint(msg, ...)  fprintf(stderr, "%s: " msg, __FUNCTION__)
#define __eprintf(format, ...)  fprintf(stderr, "%s: " format, __FUNCTION__, \
                                        __VA_ARGS__)
#endif

typedef unsigned char   cc_t;
typedef unsigned int    speed_t;
typedef unsigned int    tcflag_t;

#ifdef NCCS
#undef NCCS
#endif

#define NCCS 19
struct kernel_termios {
	tcflag_t c_iflag;               /* input mode flags */
        tcflag_t c_oflag;               /* output mode flags */
	tcflag_t c_cflag;               /* control mode flags */
	tcflag_t c_lflag;               /* local mode flags */
	cc_t c_line;                    /* line discipline */
	cc_t c_cc[NCCS];                /* control characters */
};

int main_loop(int fd, int cpu);

struct program_load_desc *load_elf(FILE *fp)
{
	Elf64_Ehdr hdr;
	Elf64_Phdr phdr;
	int i, j, nhdrs = 0;
	struct program_load_desc *desc;

	if (fread(&hdr, sizeof(hdr), 1, fp) < 1) {
		__eprint("Cannot read Ehdr.\n");
		return NULL;
	}
	if (memcmp(hdr.e_ident, ELFMAG, SELFMAG)) {
		__eprint("ELFMAG mismatched.\n");
		return NULL;
	}
	fseek(fp, hdr.e_phoff, SEEK_SET);
	for (i = 0; i < hdr.e_phnum; i++) {
		if (fread(&phdr, sizeof(phdr), 1, fp) < 1) {
			__eprintf("Loading phdr failed (%d)\n", i);
			return NULL;
		}
		if (phdr.p_type == PT_LOAD) {
			nhdrs++;
		}
	}
	
	desc = malloc(sizeof(struct program_load_desc)
	              + sizeof(struct program_image_section) * nhdrs);
	fseek(fp, hdr.e_phoff, SEEK_SET);
	j = 0;
	desc->num_sections = nhdrs;
	for (i = 0; i < hdr.e_phnum; i++) {
		if (fread(&phdr, sizeof(phdr), 1, fp) < 1) {
			__eprintf("Loading phdr failed (%d)\n", i);
			return NULL;
		}
		if (phdr.p_type == PT_LOAD) {
			desc->sections[j].vaddr = phdr.p_vaddr;
			desc->sections[j].filesz = phdr.p_filesz;
			desc->sections[j].offset = phdr.p_offset;
			desc->sections[j].len = phdr.p_memsz;

			__dprintf("%d: (%s) %lx, %lx, %lx, %lx\n",
			          j, (phdr.p_type == PT_LOAD ? "PT_LOAD" : "PT_TLS"), 
					  desc->sections[j].vaddr,
			          desc->sections[j].filesz,
			          desc->sections[j].offset,
			          desc->sections[j].len);
			j++;
		}
	}
	desc->pid = getpid();
	desc->entry = hdr.e_entry;

	return desc;
}

unsigned char *dma_buf;


#define PAGE_SIZE 4096
#define PAGE_MASK ~((unsigned long)PAGE_SIZE - 1)

void transfer_image(FILE *fp, int fd, struct program_load_desc *desc)
{
	struct program_transfer pt;
	unsigned long s, e, flen, rpa;
	int i, l, lr;

	for (i = 0; i < desc->num_sections; i++) {
		s = (desc->sections[i].vaddr) & PAGE_MASK;
		e = (desc->sections[i].vaddr + desc->sections[i].len
		     + PAGE_SIZE - 1) & PAGE_MASK;
		rpa = desc->sections[i].remote_pa;

		fseek(fp, desc->sections[i].offset, SEEK_SET);
		flen = desc->sections[i].filesz;

		__dprintf("seeked to %lx | size %lx\n",
		          desc->sections[i].offset, flen);

		while (s < e) {
			pt.dest = rpa;
			pt.src = dma_buf;
			pt.sz = PAGE_SIZE;
			
			memset(dma_buf, 0, PAGE_SIZE);
			if (s < desc->sections[i].vaddr) {
				l = desc->sections[i].vaddr 
					& (PAGE_SIZE - 1);
				lr = PAGE_SIZE - l;
				if (lr > flen) {
					lr = flen;
				}
				fread(dma_buf + l, 1, lr, fp); 
				flen -= lr;
			} else if (flen > 0) {
				if (flen > PAGE_SIZE) {
					lr = PAGE_SIZE;
				} else {
					lr = flen;
				}
				fread(dma_buf, 1, lr, fp);
				flen -= lr;
			} 
			s += PAGE_SIZE;
			rpa += PAGE_SIZE;

			if (ioctl(fd, MCEXEC_UP_LOAD_IMAGE,
			          (unsigned long)&pt)) {
				perror("dma");
				break;
			}
		}

	}
}

void print_desc(struct program_load_desc *desc)
{
	int i;

	__dprintf("Desc (%p)\n", desc);
	__dprintf("Status = %d, CPU = %d, pid = %d, entry = %lx, rp = %lx\n",
	          desc->status, desc->cpu, desc->pid, desc->entry,
	          desc->rprocess);
	for (i = 0; i < desc->num_sections; i++) {
		__dprintf("%lx, %lx, %lx\n", desc->sections[i].vaddr,
		          desc->sections[i].len, desc->sections[i].remote_pa);
	}
}

#define PIN_SHIFT  16
#define PIN_SIZE  (1 << PIN_SHIFT)
#define PIN_MASK  ~(unsigned long)(PIN_SIZE - 1)

unsigned long dma_buf_pa;

int main(int argc, char **argv)
{
	int fd, fdm;
	FILE *fp;
	struct program_load_desc *desc;
	long r;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s (program) [args...]\n",
		        argv[0]);
		return 1;
	}
	
	fp = fopen(argv[1], "rb");
	if (!fp) {
		fprintf(stderr, "Error: Failed to open %s\n", argv[1]);
		return 1;
	}
	desc = load_elf(fp);
	if (!desc) {
		fclose(fp);
		fprintf(stderr, "Error: Failed to parse ELF!\n");
		return 1;
	}

	__dprintf("# of sections: %d\n", desc->num_sections);

	fd = open("/dev/mcos0", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Error: Failed to open /dev/mcctrl.\n");
		return 1;
	}
	fdm = open("/dev/fmem", O_RDWR);
	if (fdm < 0) {
		fprintf(stderr, "Error: Failed to open /dev/fmem.\n");
		return 1;
	}

	if ((r = ioctl(fd, MCEXEC_UP_PREPARE_DMA, 
	               (unsigned long)&dma_buf_pa)) < 0) {
		perror("prepare_dma");
		close(fd);
		return 1;
	}

	dma_buf = mmap(NULL, PIN_SIZE, PROT_READ | PROT_WRITE,
	               MAP_SHARED, fdm, dma_buf_pa);
	__dprintf("DMA Buffer: %lx, %p\n", dma_buf_pa, dma_buf);

	if (ioctl(fd, MCEXEC_UP_PREPARE_IMAGE, (unsigned long)desc) != 0) {
		perror("prepare");
		close(fd);
		return 1;
	}

	print_desc(desc);
	transfer_image(fp, fd, desc);
	fflush(stdout);
	fflush(stderr);

	if (ioctl(fd, MCEXEC_UP_START_IMAGE, (unsigned long)desc) != 0) {
		perror("exec");
		close(fd);
		return 1;
	}

	return main_loop(fd, desc->cpu);
}

void do_syscall_return(int fd, int cpu,
                       int ret, int n, unsigned long src, unsigned long dest,
                       unsigned long sz)
{
	struct syscall_ret_desc desc;

	desc.cpu = cpu;
	desc.ret = ret;
	desc.src = src;
	desc.dest = dest;
	desc.size = sz;
	
	if (ioctl(fd, MCEXEC_UP_RET_SYSCALL, (unsigned long)&desc) != 0) {
		perror("ret");
	}
}

void do_syscall_load(int fd, int cpu, unsigned long dest, unsigned long src,
                     unsigned long sz)
{
	struct syscall_load_desc desc;

	desc.cpu = cpu;
	desc.src = src;
	desc.dest = dest;
	desc.size = sz;

	if (ioctl(fd, MCEXEC_UP_LOAD_SYSCALL, (unsigned long)&desc) != 0){
		perror("load");
	}
}

#define SET_ERR(ret) if (ret == -1) ret = -errno

int main_loop(int fd, int cpu)
{
	struct syscall_wait_desc w;
	int ret;
	
	w.cpu = cpu;

	while (ioctl(fd, MCEXEC_UP_WAIT_SYSCALL, (unsigned long)&w) == 0) {

		__dprintf("got syscall: %d\n", w.sr.number);

		switch (w.sr.number) {
		case __NR_open:
			dma_buf[256] = 0;
			
			do_syscall_load(fd, cpu, dma_buf, w.sr.args[0], 256);
			/*
			while (!dma_buf[256]) {
				asm volatile ("" : : : "memory");
			}
			*/

			ret = open(dma_buf, w.sr.args[1], w.sr.args[2]);
			SET_ERR(ret);
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;

		case __NR_close:
			ret = close(w.sr.args[0]);
			SET_ERR(ret);
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;

		case __NR_read:
			ret = read(w.sr.args[0], dma_buf, w.sr.args[2]);
			SET_ERR(ret);
			do_syscall_return(fd, cpu, ret, 1, dma_buf,
			                  w.sr.args[1], w.sr.args[2]);
			break;

		case __NR_write:
			dma_buf[w.sr.args[2]] = 0;
			SET_ERR(ret);
			do_syscall_load(fd, cpu, dma_buf,
			                w.sr.args[1], w.sr.args[2]);

			/*
			while (!dma_buf[w.sr.args[2]]) {
				asm volatile ("" : : : "memory");
			}
			*/

			ret = write(w.sr.args[0], dma_buf, w.sr.args[2]);
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;

		case __NR_lseek:
			ret = lseek64(w.sr.args[0], w.sr.args[1], w.sr.args[2]);
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;

		case __NR_pread64:
			ret = pread(w.sr.args[0], dma_buf, w.sr.args[2],
			            w.sr.args[3]);
			do_syscall_return(fd, cpu, ret, 1, dma_buf,
			                  w.sr.args[1], w.sr.args[2]);
			break;

		case __NR_pwrite64:
			dma_buf[w.sr.args[2]] = 0;
			do_syscall_load(fd, cpu, dma_buf,
			                w.sr.args[1], w.sr.args[2]);

			/*
			while (!dma_buf[w.sr.args[2]]) {
				asm volatile ("" : : : "memory");
			}
			*/

			ret = pwrite(w.sr.args[0], dma_buf, w.sr.args[2],
			             w.sr.args[3]);
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;


		case __NR_fstat:
			ret = fstat(w.sr.args[0], (void *)dma_buf);
			if (ret == -1) {
				ret = -errno;
			}
			do_syscall_return(fd, cpu, ret, 1, dma_buf,
			                  w.sr.args[1], sizeof(struct stat));
			break;

		case __NR_ioctl:
			if (w.sr.args[1] == TCGETS) {
				ret = ioctl(w.sr.args[0], w.sr.args[1],
				            (unsigned long)dma_buf);
				if (ret == -1) {
					ret = -errno;
				}
				do_syscall_return(fd, cpu, ret, 1, dma_buf,
				                  w.sr.args[2],
				                  sizeof(struct kernel_termios)
					);
			}
			break;

		case __NR_getgid:
		case __NR_getuid:
		case __NR_geteuid:
		case __NR_getegid:
		case __NR_getppid:
		case __NR_getpgrp:
			ret = syscall(w.sr.number);
			if (ret == -1) {
				ret = -errno;
			}
			do_syscall_return(fd, cpu, ret, 0, 0, 0, 0);
			break;

		case __NR_clone:

			__dprint("MIC clone(), new thread's cpu_id: %d\n", w.sr.args[0]);


			do_syscall_return(fd, cpu, 0, 0, 0, 0, 0);
			break;
			

		case __NR_exit:
		case __NR_exit_group:
			do_syscall_return(fd, cpu, 0, 0, 0, 0, 0);
			return w.sr.args[0];

		case __NR_uname:
			ret = uname((void *)dma_buf);
			if (ret == -1) {
				ret = -errno;
			}
			do_syscall_return(fd,
			                  cpu, ret, 1, dma_buf, w.sr.args[0],
			                  sizeof(struct utsname));
			break;
		default:
			printf("Unhandled system calls: %ld\n", w.sr.number);
			break;

		}
	}
	printf("timed out.\n");
	return 1;
}
