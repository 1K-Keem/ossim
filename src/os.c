
#include "cpu.h"
#define OSSIM_PROJECT_SCHED_H
#include "sched.h"
#undef OSSIM_PROJECT_SCHED_H
#include "timer.h"
#include "loader.h"
#include "mm.h"
#include "log.h"
#ifdef MM64
#include "mm64.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int time_slot;
static int num_cpus;
static int done = 0;
static pthread_mutex_t done_lock = PTHREAD_MUTEX_INITIALIZER;
static struct krnl_t os;

#ifdef MM_PAGING
static unsigned long memramsz;
static unsigned long memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
	/* A dispatched argument struct to compact many-fields passing to loader */
	int vmemsz;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	int active_mswp_id;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};

static int is_done(void)
{
	int ret;

	pthread_mutex_lock(&done_lock);
	ret = done;
	pthread_mutex_unlock(&done_lock);
	return ret;
}

static void set_done(void)
{
	pthread_mutex_lock(&done_lock);
	done = 1;
	pthread_mutex_unlock(&done_lock);
}

static void free_pcb_all(struct pcb_t *proc)
{
	if (proc == NULL)
		return;

#ifdef MM_PAGING
	if (proc->krnl != NULL)
	{
		/* 1. Free all physical memory (RAM/Swap) occupied by the process */
		extern int free_pcb_memph(struct pcb_t * caller);
		free_pcb_memph(proc);

		if (proc->krnl->mm != NULL)
		{
			/* 2. Free all virtual memory metadata (VMAs, Regions) and page tables */
			extern void free_mm(struct mm_struct * mm);
			free_mm(proc->krnl->mm);
		}
		
		/* 3. Free the kernel context structure */
		free(proc->krnl);
	}
#endif

	/* 4. Free the code segments */
	if (proc->code != NULL)
	{
		if (proc->code->text != NULL)
			free(proc->code->text);
		free(proc->code);
	}

	/* 5. Free the PCB itself */
	free(proc);
}


static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
		 	* ready queue */
			proc = get_proc();
			if (proc == NULL) {
                           next_slot(timer_id);
                           continue; /* First load failed. skip dummy load */
                        }
		}else if (proc->pc == proc->code->size) {
			/* The porcess has finish it job */
			os_log(LOG_DEBUG, "sched", "cpu=%d finish pid=%u", id, proc->pid);
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			finish_proc(proc);
			free_pcb_all(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			os_log(LOG_DEBUG, "sched", "cpu=%d requeue pid=%u", id, proc->pid);
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}
		
		/* Recheck process status after loading new process */
		if (proc == NULL && is_done()) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			os_log(LOG_DEBUG, "sched", "cpu=%d dispatch pid=%u", id, proc->pid);
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		run(proc);
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void * ld_routine(void * args) {
#ifdef MM_PAGING
	struct memphy_struct* mram = ((struct mmpaging_ld_args *)args)->mram;
	struct memphy_struct** mswp = ((struct mmpaging_ld_args *)args)->mswp;
	struct memphy_struct* active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
	struct timer_id_t * timer_id = ((struct mmpaging_ld_args *)args)->timer_id;
#else
	struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif
	int i = 0;
  /* TODO init kernel page table directory */
#ifdef MM64
	memset(&os, 0, sizeof(os));
	init_kernel_page_table(&os);
#else
	os.krnl_pgd = malloc(PAGING_MAX_PGN * sizeof(uint32_t));
#endif
	i=0;
	printf("ld_routine\n");
	while (i < num_processes) {
		struct pcb_t * proc = load(ld_processes.path[i]);
		struct krnl_t * krnl = malloc(sizeof(struct krnl_t));
		*krnl = os;
		proc->krnl = krnl;

#ifdef MLQ_SCHED
		proc->prio = ld_processes.prio[i];
#endif
		while (current_time() < ld_processes.start_time[i]) {
			next_slot(timer_id);
		}
#ifdef MM_PAGING
		krnl->mm = malloc(sizeof(struct mm_struct));
		init_mm(krnl->mm, proc);
		krnl->mram = mram;
		krnl->mswp = mswp;
		krnl->active_mswp = active_mswp;
#endif
		printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
			ld_processes.path[i], proc->pid, ld_processes.prio[i]);
		add_proc(proc);
		free(ld_processes.path[i]);
		i++;
		next_slot(timer_id);
	}
	free(ld_processes.path);
	free(ld_processes.start_time);
	set_done();
	detach_event(timer_id);
	pthread_exit(NULL);
}

static void read_config(const char * path) {
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find configure file at %s\n", path);
		exit(1);
	}
	fscanf(file, "%d %d %d\n", &time_slot, &num_cpus, &num_processes);
	ld_processes.path = (char**)malloc(sizeof(char*) * num_processes);
	ld_processes.start_time = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#ifdef MM_PAGING
	int sit;
	char first_proc_line[256];
	int has_first_proc_line = 0;
	memramsz = 0x100000000UL;
	memswpsz[0] = 0x1000000UL;
	for(sit = 1; sit < PAGING_MAX_MMSWP; sit++)
		memswpsz[sit] = 0;
#ifndef MM_FIXED_MEMSZ
	if (fgets(first_proc_line, sizeof(first_proc_line), file) != NULL) {
		unsigned long memcfg[PAGING_MAX_MMSWP + 1];
		char extra;
		int nmem = sscanf(first_proc_line, "%lu %lu %lu %lu %lu %c",
		                  &memcfg[0], &memcfg[1], &memcfg[2],
		                  &memcfg[3], &memcfg[4], &extra);
		if (nmem >= PAGING_MAX_MMSWP + 1) {
			memramsz = memcfg[0];
			for(sit = 0; sit < PAGING_MAX_MMSWP; sit++)
				memswpsz[sit] = memcfg[sit + 1];
		} else {
			has_first_proc_line = 1;
		}
	}
#endif
#endif

#ifdef MLQ_SCHED
	ld_processes.prio = (unsigned long*)
		malloc(sizeof(unsigned long) * num_processes);
#endif
	int i;
	for (i = 0; i < num_processes; i++) {
		ld_processes.path[i] = (char*)malloc(sizeof(char) * 100);
		ld_processes.path[i][0] = '\0';
		strcat(ld_processes.path[i], "input/proc/");
		char proc[100];
#ifdef MLQ_SCHED
		if (
#ifdef MM_PAGING
		    has_first_proc_line &&
#else
		    0 &&
#endif
		    i == 0) {
			sscanf(first_proc_line, "%lu %s %lu", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
		} else {
			fscanf(file, "%lu %s %lu\n", &ld_processes.start_time[i], proc, &ld_processes.prio[i]);
		}
#else
		if (
#ifdef MM_PAGING
		    has_first_proc_line &&
#else
		    0 &&
#endif
		    i == 0) {
			sscanf(first_proc_line, "%lu %s", &ld_processes.start_time[i], proc);
		} else {
			fscanf(file, "%lu %s\n", &ld_processes.start_time[i], proc);
		}
#endif
		strcat(ld_processes.path[i], proc);
	}
}

int main(int argc, char * argv[]) {
	/* Read config */
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}
	char path[100];
	path[0] = '\0';
	strcat(path, "input/");
	strcat(path, argv[1]);
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	struct cpu_args * args =
		(struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;
	
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
	/* Init all MEMPHY include 1 MEMRAM and n of MEMSWP */
	int rdmflag = 1; /* By default memphy is RANDOM ACCESS MEMORY */

	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];

	/* Create MEM RAM */
	init_memphy(&mram, memramsz, rdmflag);

        /* Create all MEM SWAP */ 
	int sit;
	for(sit = 0; sit < PAGING_MAX_MMSWP; sit++)
	       init_memphy(&mswp[sit], memswpsz[sit], rdmflag);

	/* In Paging mode, it needs passing the system mem to each PCB through loader*/
	struct mmpaging_ld_args *mm_ld_args = malloc(sizeof(struct mmpaging_ld_args));

	mm_ld_args->timer_id = ld_event;
	mm_ld_args->mram = (struct memphy_struct *) &mram;
	mm_ld_args->mswp = (struct memphy_struct**) &mswp;
	mm_ld_args->active_mswp = (struct memphy_struct *) &mswp[0];
        mm_ld_args->active_mswp_id = 0;


#endif

	/* Init scheduler */
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif
	for (i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL,
			cpu_routine, (void*)&args[i]);
	}

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	/* Stop timer */
	stop_timer();

	return 0;

}
