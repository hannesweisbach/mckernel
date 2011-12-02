#include <linux/sched.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/delay.h>
#include <asm/msr.h>
#include "mcctrl.h"

static DECLARE_WAIT_QUEUE_HEAD(wq_prepare);
extern struct mcctrl_channel *channels;
int mcctrl_ikc_set_recv_cpu(int cpu);

static long mcexec_prepare_image(aal_os_t os,
                                 struct program_load_desc * __user udesc)
{
	struct program_load_desc desc, *pdesc;
	struct ikc_scd_packet isp;

	if (copy_from_user(&desc, udesc,
	                    sizeof(struct program_load_desc))) {
		return -EFAULT;
	}
	if (desc.num_sections <= 0 || desc.num_sections > 16) {
		printk("# of sections: %d\n", desc.num_sections);
		return -EINVAL;
	}
	pdesc = kmalloc(sizeof(struct program_load_desc) + 
	                sizeof(struct program_image_section)
	                * desc.num_sections, GFP_KERNEL);
	memcpy(pdesc, &desc, sizeof(struct program_load_desc));
	if (copy_from_user(pdesc->sections, udesc->sections,
	                   sizeof(struct program_image_section)
	                   * desc.num_sections)) {
		kfree(pdesc);
		return -EFAULT;
	}

	pdesc->pid = task_tgid_vnr(current);

	isp.msg = SCD_MSG_PREPARE_PROCESS;
	isp.ref = pdesc->cpu;
	isp.arg = virt_to_phys(pdesc);

	printk("# of sections: %d\n", pdesc->num_sections);
	printk("%p (%lx)\n", pdesc, isp.arg);
	
	pdesc->status = 0;
	mcctrl_ikc_send(pdesc->cpu, &isp);

	wait_event_interruptible(wq_prepare, pdesc->status);

	copy_to_user(udesc, pdesc, sizeof(struct program_load_desc) + 
	             sizeof(struct program_image_section) * desc.num_sections);

	kfree(pdesc);

	return 0;
}

int mcexec_load_image(aal_os_t os, struct program_transfer *__user upt)
{
	struct program_transfer pt;
	unsigned long dma_status = 0;
	aal_dma_channel_t channel;
	struct aal_dma_request request;
	void *p;

	channel = aal_device_get_dma_channel(aal_os_to_dev(os), 0);
	if (!channel) {
		return -EINVAL;
	}

	if (copy_from_user(&pt, upt, sizeof(pt))) {
		return -EFAULT;
	}
	p = (void *)__get_free_page(GFP_KERNEL);

	if (copy_from_user(p, pt.src, PAGE_SIZE)) {
		return -EFAULT;
	}

	memset(&request, 0, sizeof(request));
	request.src_os = NULL;
	request.src_phys = virt_to_phys(p);
	request.dest_os = os;
	request.dest_phys = pt.dest;
	request.size = PAGE_SIZE;
	request.notify = (void *)virt_to_phys(&dma_status);
	request.priv = (void *)1;

	aal_dma_request(channel, &request);

	while (!dma_status) {
		mb();
		udelay(1);
	}

	free_page((unsigned long)p);

	return 0;
}

static long mcexec_start_image(aal_os_t os,
                                 struct program_load_desc * __user udesc)
{
	struct program_load_desc desc;
	struct ikc_scd_packet isp;
	struct mcctrl_channel *c;

	if (copy_from_user(&desc, udesc,
	                   sizeof(struct program_load_desc))) {
		return -EFAULT;
	}

	c = channels + desc.cpu;

	mcctrl_ikc_set_recv_cpu(desc.cpu);
	
	isp.msg = SCD_MSG_SCHEDULE_PROCESS;
	isp.ref = desc.cpu;
	isp.arg = desc.rprocess;

	mcctrl_ikc_send(desc.cpu, &isp);

	return 0;
}

int mcexec_syscall(struct mcctrl_channel *c, unsigned long arg)
{
	c->req = 1;
	wake_up(&c->wq_syscall);

	return 0;
}


int __do_in_kernel_syscall(aal_os_t os, struct mcctrl_channel *c,
                           struct syscall_request *sc);

int mcexec_wait_syscall(aal_os_t os, struct syscall_wait_desc *__user req)
{
	struct syscall_wait_desc swd;
	struct mcctrl_channel *c;
	unsigned long s, w;
	
	if (copy_from_user(&swd, req, sizeof(swd.cpu))) {
		return -EFAULT;
	}

	c = channels + swd.cpu;

#ifdef DO_USER_MODE
	wait_event_interruptible(c->wq_syscall, c->req);
	c->req = 0;
#else
	while (1) {
		rdtscll(s);
		while (!(*c->param.doorbell_va)) {
			mb();
			cpu_relax();
			rdtscll(w);
			if (w > s + 1024 * 1024 * 1024 * 1) {
				return -EINTR;
			}
		}
		*c->param.doorbell_va = 0;
		printk("(%p) %ld SC %ld: %lx\n", 
		       c->param.request_va,
		       c->param.request_va->valid,
		       c->param.request_va->number,
		       c->param.request_va->args[0]);
		if (__do_in_kernel_syscall(os, c, c->param.request_va)) {
#endif
			if (copy_to_user(&req->sr, c->param.request_va,
			                 sizeof(struct syscall_request))) {
				return -EFAULT;
			}
#ifndef DO_USER_MODE
			break;
		}
	}
#endif
	return 0;
}

long mcexec_pin_region(aal_os_t os, unsigned long *__user uaddress)
{
	int pin_shift = 16;
	unsigned long a;

	a = __get_free_pages(GFP_KERNEL, pin_shift - PAGE_SHIFT);
	if (!a) {
		return -ENOMEM;
	}

	a = virt_to_phys((void *)a);

	if (copy_to_user(uaddress, &a, sizeof(unsigned long))) {
		return -EFAULT;
	}
	return 0;
}

long mcexec_load_syscall(aal_os_t os, struct syscall_load_desc *__user arg)
{
	struct syscall_load_desc desc;
	aal_dma_channel_t channel;
	struct aal_dma_request request;

	channel = aal_device_get_dma_channel(aal_os_to_dev(os), 0);
	if (!channel) {
		return -EINVAL;
	}

	if (copy_from_user(&desc, arg, sizeof(struct syscall_load_desc))) {
		return -EFAULT;
	}

	memset(&request, 0, sizeof(request));
	request.src_os = os;
	request.src_phys = desc.src;
	request.dest_os = NULL;
	request.dest_phys = desc.dest;
	request.size = desc.size;
	request.notify = (void *)(desc.dest + desc.size);
	request.priv = (void *)1;

	aal_dma_request(channel, &request);

	return 0;
}

long mcexec_ret_syscall(aal_os_t os, struct syscall_ret_desc *__user arg)
{
	struct syscall_ret_desc ret;
	aal_dma_channel_t channel;
	struct aal_dma_request request;
	struct mcctrl_channel *mc;

	channel = aal_device_get_dma_channel(aal_os_to_dev(os), 0);
	if (!channel) {
		return -EINVAL;
	}

	if (copy_from_user(&ret, arg, sizeof(struct syscall_ret_desc))) {
		return -EFAULT;
	}
	mc = channels + ret.cpu;
	if (!mc) {
		return -EINVAL;
	}

	mc->param.response_va->ret = ret.ret;

	if (ret.size > 0) {
		memset(&request, 0, sizeof(request));
		request.src_os = NULL;
		request.src_phys = ret.src;
		request.dest_os = os;
		request.dest_phys = ret.dest;
		request.size = ret.size;
		request.notify = (void *)mc->param.response_pa;
		request.priv = (void *)1;
		
		aal_dma_request(channel, &request);
	} else {
		mc->param.response_va->status = 1;
	}

	return 0;
}

long __mcctrl_control(aal_os_t os, unsigned int req, unsigned long arg)
{
	switch (req) {
	case MCEXEC_UP_PREPARE_IMAGE:
		return mcexec_prepare_image(os,
		                            (struct program_load_desc *)arg);
	case MCEXEC_UP_LOAD_IMAGE:
		return mcexec_load_image(os, (struct program_transfer *)arg);

	case MCEXEC_UP_START_IMAGE:
		return mcexec_start_image(os, (struct program_load_desc *)arg);

	case MCEXEC_UP_WAIT_SYSCALL:
		return mcexec_wait_syscall(os, (struct syscall_wait_desc *)arg);

	case MCEXEC_UP_RET_SYSCALL:
		return mcexec_ret_syscall(os, (struct syscall_ret_desc *)arg);

	case MCEXEC_UP_LOAD_SYSCALL:
		return mcexec_load_syscall(os, (struct syscall_load_desc *)arg);

	case MCEXEC_UP_PREPARE_DMA:
		return mcexec_pin_region(os, (unsigned long *)arg);

	}
	return -EINVAL;
}

void mcexec_prepare_ack(unsigned long arg)
{
	struct program_load_desc *desc = phys_to_virt(arg);

	desc->status = 1;
	
	wake_up_all(&wq_prepare);
}

