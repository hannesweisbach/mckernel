#ifndef IHKLIB_RUSAGE_H_INCLUDED
#define IHKLIB_RUSAGE_H_INCLUDED

#define IHK_MAX_NUM_PGSIZES 4
#define IHK_MAX_NUM_NUMA_NODES 1024
#define IHK_MAX_NUM_CPUS 1024

#define IHK_OS_PGSIZE_4KB 0
#define IHK_OS_PGSIZE_2MB 1
#define IHK_OS_PGSIZE_1GB 2

struct mckernel_rusage {
	unsigned long memory_stat_rss[IHK_MAX_NUM_PGSIZES];
	unsigned long memory_stat_mapped_file[IHK_MAX_NUM_PGSIZES];
	unsigned long memory_max_usage;
	unsigned long memory_kmem_usage;
	unsigned long memory_kmem_max_usage;
	unsigned long memory_numa_stat[IHK_MAX_NUM_NUMA_NODES];
	unsigned long cpuacct_stat_system;
	unsigned long cpuacct_stat_user;
	unsigned long cpuacct_usage;
	unsigned long cpuacct_usage_percpu[IHK_MAX_NUM_CPUS];
	int num_threads;
	int max_num_threads;
};

#endif /* !defined(IHKLIB_RUSAGE_H_INCLUDED) */
