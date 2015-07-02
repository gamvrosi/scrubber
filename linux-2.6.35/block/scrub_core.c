/*
 * Copyright (C) 2012 George Amvrosiadis <gamvrosi@gmail.com>
 *   (with code by Douglas Gilbert, Copyright (C) 2004-2008)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or any
 * later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/scrub.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/ioprio.h>
#include <linux/iocontext.h>
#include <linux/kthread.h>
#include <linux/err.h>
#include <linux/blkdev.h>

#define SEQLSCRUB 1
#define STAGSCRUB 2
#define FIXEDSCRUB 3

#define RTIMEPRIO 1
#define IDCHKPRIO 2

/* Thread states */
#define TINIT  0
#define TIDLE  1
#define TBUSY  2

struct scrubparams {
	uint64_t reqbound;	/* The scrubbing request limit per round */
	int strategy;		/* The scrubbing technique used */
	int priority;		/* The scrubbing priority used */
	uint64_t segsize;	/* The segment size in Kbytes */
	uint64_t regsize;	/* The region size in Kbytes */
	int threads;		/* Number of scrubbing threads to be used */
	int dpo;		/* Page out state */
	int vrprotect;		/* Contents of VRP vrprotect field */
	int verbose;		/* Whether to display verbose messages */
	int timed;		/* Whether to keep scrubbing statistics */

	uint64_t ttime_ms;	/* Total scrubbing time (ms) */
	int available;		/* Number of available threads */
	int read_errs;		/* Counts read errors that have occurred */
	uint64_t capacity;	/* Number of sectors scrubbed by scrubber */
	uint64_t start;		/* Sector where scrubbing begins */
	struct timespec idlestamp;	/* Timestamp of latest idle time */
	uint64_t delayms;	/* Artificial delay inbetween SCSIVerify requests (ms) */
	uint64_t resptime_us;	/* Avg. response time per SCSIVerify (us) */
	uint64_t reqcount;	/* Total number of requests executed during last scrub */

	/* Mutex variables */
	struct mutex mutexerr;
	struct mutex mutextime;
	struct mutex mutexavail;
	spinlock_t *barrier;
};

struct scrub_thread_data {
	struct task_struct *task;
	struct gendisk *disk;
	struct scrubparams *s;
	uint64_t pos;
	uint64_t count;
	int state;
	int tid;
};

int islater(struct timespec *b, struct timespec *c)
{
	if (b->tv_sec > c->tv_sec) {
		return 1;
	} else if (b->tv_sec == c->tv_sec) {
		if (b->tv_nsec > c->tv_nsec)
			return 1;
		else
			return 0;
	} else {
		return 0;
	}
}

int kthread_segread(void *thread_data)
{
	int res;
	uint64_t pos, count, resptime;
	unsigned int num;
	//float sumtime = 0.0;
	struct timeval va, vb;

	/* Extract preloaded/stable thread data from struct */
	struct scrub_thread_data *data = (struct scrub_thread_data *) thread_data;
	int tid = data->tid;

	if (data->s->verbose)
		printk(KERN_INFO "scrubber (%s): Just created thread no.%d\n",
			data->disk->disk_name, tid);

	data->state = TIDLE;
	mutex_lock(&data->s->mutexavail);
	++data->s->available;
	mutex_unlock(&data->s->mutexavail);
	wake_up_state(data->disk->scrubber->task, TASK_REPORT);

	/* Start event loop */
	while(1) {

		set_current_state(TASK_INTERRUPTIBLE);
		while (!spin_trylock(&data->s->barrier[tid]) &&
			!kthread_should_stop()) {
			//printk(KERN_INFO "scrubber (%s): About to fall asleep (%d).\n",
			//	data->disk->disk_name, current->pid);
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
		set_current_state(TASK_RUNNING);

		if (kthread_should_stop()) {
			if (data->s->verbose > 2)
				printk(KERN_INFO "scrubber (%s): THREAD_%d is done.\n",
					data->disk->disk_name, tid);
			break;
		}

		/* Stall until barrier is raised */
		if (data->s->verbose > 1)
			printk(KERN_INFO "scrubber (%s): Thread No.%d passed barrier.\n",
				data->disk->disk_name, tid);

		/* Perform state-related actions */
		switch (data->state) {
			case TBUSY:
				if (data->s->verbose > 2)
					printk(KERN_INFO "scrubber (%s): THREAD_%d: On TBUSY\n",
						data->disk->disk_name, tid);

				/* Extract data loaded in struct */
				pos = data->pos;
				count = data->count;
				/* if (count == 65536)
					count = 65535; */

				for (;; count -= 65535, pos += 65535) {
					if (data->s->verbose > 1)
						printk(KERN_INFO "scrubber (%s): About to scrub %llu "
						"sectors, starting from %llu.\n", data->disk->disk_name,
						count, pos);

					num = (count > 65535) ? 65535 : (unsigned int) count;

					if (data->s->timed) do_gettimeofday(&va);

					res = scsi_verify(data->disk, pos, num);

					if (data->s->timed){
						do_gettimeofday(&vb);
						if (data->s->verbose > 3)
							printk(KERN_INFO "scrubber (%s): SCSIVerify duration. A = (%lu.)%lu us, B = (%lu.)"
								"%lu us. (Thread:%d/Offset:%llu)\n", data->disk->disk_name, va.tv_sec,
								va.tv_usec, vb.tv_sec, vb.tv_usec, tid, pos);

						if (data->s->verbose > 2) {
							if (vb.tv_usec - va.tv_usec < 0) {
								printk(KERN_INFO "scrubber (%s): SCSIVerify duration = (%ld.)%ld us "
									"(Thread:%d/Offset:%llu).\n", data->disk->disk_name,
									(unsigned long) (vb.tv_sec - va.tv_sec - 1),
									(unsigned long) (vb.tv_usec - va.tv_usec + 1000000), tid, pos);
							} else {
								printk(KERN_INFO "scrubber (%s): SCSIVerify duration = (%ld.)%ld us "
									"(Thread:%d/Offset:%llu).\n", data->disk->disk_name,
									(unsigned long) (vb.tv_sec - va.tv_sec),
									(unsigned long) (vb.tv_usec - va.tv_usec), tid, pos);
							}
						}
						resptime = (vb.tv_sec - va.tv_sec) * 1000000 + (vb.tv_usec - va.tv_usec);
						mutex_lock(&data->s->mutextime);
						data->s->resptime_us += resptime;
						mutex_unlock(&data->s->mutextime);
					}

					if (res) {
						mutex_lock(&data->s->mutexerr);
						++data->s->read_errs;
						mutex_unlock(&data->s->mutexerr);
					}

					//mutex_lock(&data->s->mutextime);
					mutex_lock(&data->disk->scrubber->sysfs_lock);
					++data->s->reqcount;
					++data->disk->scrubber->reqcount;
					//mutex_unlock(&data->s->mutextime);
					mutex_unlock(&data->disk->scrubber->sysfs_lock);

					if (count <= 65535) break;
				}
				/* Record time passed */
				//if (rtime[tid] == 0.0)
				//   rtime[tid] = sumtime;
				// else{
				//   rtime[tid] += sumtime;
				//   rtime[tid] /= 2.0;
				// }

				data->state = TIDLE;

				mutex_lock(&data->s->mutexavail);
				++data->s->available;
				mutex_unlock(&data->s->mutexavail);
				//printk(KERN_INFO "scrubber (%s): About to wake up (%d)\n",
				//	data->disk->disk_name, data->disk->scrubber->task->pid);
				wake_up_state(data->disk->scrubber->task, TASK_REPORT);
				break;
			case TIDLE:
				if (data->s->verbose > 2)
					printk(KERN_INFO "scrubber (%s): THREAD_%d: On TIDLE\n",
						data->disk->disk_name, tid);
				break;
			default:
				/* How did we get here? */
				if (data->s->verbose > 2)
					printk(KERN_INFO "scrubber (%s): THREAD_%d: On DEF\n",
						data->disk->disk_name, tid);
				break;
		}
	}

	//printk(KERN_INFO "scrubber (%s): Thread exiting with return. That shouldn't happen!\n",
	//	data->disk->disk_name);

	return 0;
}

int segread(struct gendisk *disk, struct scrubparams *s,
	struct scrub_thread_data *tdata, uint64_t pos, uint64_t count)
{
	int tcounter = 0;
	struct timeval temp;

	/* Check the number of available threads */
	set_current_state(TASK_INTERRUPTIBLE);
	if (s->priority == RTIMEPRIO || s->priority == IDCHKPRIO) {
		while (!s->available){
			schedule();
			set_current_state(TASK_INTERRUPTIBLE);
		}
	} /* else if (s->priority == IDCHKPRIO) {
		if (!s->available ||
		    (disk->scrubber->idle.tv_sec != s->idlestamp.tv_sec ||
		    disk->scrubber->idle.tv_nsec != s->idlestamp.tv_nsec)) {
			while (!s->available ||
			      (!disk->scrubber->idle.tv_sec &&
			       !disk->scrubber->idle.tv_nsec)) {
				schedule();
				set_current_state(TASK_INTERRUPTIBLE);
				if (s->idlems) {
					schedule_timeout(msecs_to_jiffies
						(s->idlems));
					set_current_state(TASK_INTERRUPTIBLE);
				}
				if (!elv_queue_empty(disk->queue))
					continue;
			}
		}*/
		/*while (!s->available || !elv_queue_empty(disk->queue)) {
			schedule_timeout(msecs_to_jiffies(s->idlems));
			set_current_state(TASK_INTERRUPTIBLE);
		}
	}*/
	set_current_state(TASK_RUNNING);

	mutex_lock(&s->mutexavail);
	--s->available;
	mutex_unlock(&s->mutexavail);

	if (s->delayms) {
		if (s->verbose > 2) {
			do_gettimeofday(&temp);
			printk(KERN_INFO "scrubber (%s): Before schedule_timeout, current_time is %ld.%ldusec\n",
				disk->disk_name, temp.tv_sec, temp.tv_usec);
		}
		schedule_timeout_uninterruptible(msecs_to_jiffies(s->delayms));
		if (s->verbose > 2) {
			do_gettimeofday(&temp);
			printk(KERN_INFO "scrubber (%s): After schedule_timeout, current_time is %ld.%ldusec\n",
				disk->disk_name, temp.tv_sec, temp.tv_usec);
		}
	}

	while (tdata[tcounter].state != TIDLE)
		tcounter = (tcounter + 1) % s->threads;
	tdata[tcounter].state = TBUSY;

	/* Prep thread data */
	tdata[tcounter].pos = pos;
	tdata[tcounter].count = count;

	/* Now that we're ready, raise the barrier */
	if (s->verbose > 2)
		printk(KERN_INFO "scrubber (%s): Ready to raise barrier for No.%d\n",
			   disk->disk_name, tcounter);
	spin_unlock(&s->barrier[tcounter]);
	//printk(KERN_INFO "scrubber (%s): About to wake up (%d)\n",
	//	disk->disk_name, tdata[tcounter].task->pid);

	set_current_state(TASK_INTERRUPTIBLE);
	while (tdata[tcounter].task->state == TASK_RUNNING){
		schedule_timeout(msecs_to_jiffies(2));
		set_current_state(TASK_INTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);

	wake_up_state(tdata[tcounter].task, TASK_REPORT);

	return 0;
}

static uint64_t lceil (uint64_t whole, uint64_t part, struct gendisk *disk,
	struct scrubparams *s)
{
	if (s->verbose > 2)
		printk(KERN_INFO "scrubber (%s): lceil :: Debug info:\n"
			   "scrubber (%s):          Division: %llu / %llu\n"
			   "scrubber (%s):          Integer result: %llu\n"
			   "scrubber (%s):          Modulo result: %llu\n",
			   disk->disk_name, disk->disk_name, whole, part,
			   disk->disk_name, whole/part, disk->disk_name,
			   whole%part);

	if (whole % part == (uint64_t) 0)
		return (whole/part);
	else
		return ((uint64_t) 1 + (whole/part));
}

int seqscrub (struct gendisk *disk, struct scrubparams *s,
	struct scrub_thread_data *tdata)
{
	int i;
	uint64_t pos, num, reqcount = 0;
	//struct timespec rqtp;
	//struct timespec ta, tb;

	/* Scrub sequentially in SEGMENT_SIZE chunks */
	pos = (uint64_t) s->start;
	if (s->verbose)
		printk(KERN_INFO "scrubber (%s): Starting from %llu to %llu.\n",
			   disk->disk_name, pos, s->capacity);

	/* Initialize thread & global timers
	ta.tv_sec = tb.tv_sec = 0;
	ta.tv_nsec = tb.tv_nsec = 0; */

	//for (i = 0; i < tnum; i++)
	//   rtime[i] = 0.0;
	//if (s->timed) ta = current_kernel_time();

	while (pos < s->capacity) {

		//if (LAG > 0.00) {
		/* Introduce a delay equal to the disk's rotational latency */
			//rqtp.tv_sec = 0;
			//rqtp.tv_nsec = LAG * 5985000;
			//nanosleep(&rqtp,NULL);
		//}

		if (pos + s->segsize > s->capacity)
		/* Either the segsize is larger than the device (read device in one go),
		   or the remaining sectors are less than segsize (read just those). */
			num = (s->segsize > s->capacity) ? s->capacity : (s->capacity - pos);
		else
		/* Verify segsize sectors */
			num = s->segsize;
		if (segread (disk, s, tdata, pos, num))
			return -1;
		if (disk->scrubber->state == 2 || (s->reqbound && ++reqcount > s->reqbound))
			/* Exceeded maximum number of requests for this round. Bail. */
			return 0;

		if (s->verbose > 2) {
			printk (KERN_INFO "scrubber (%s): Offset %llu (reading %llu)\n",
					disk->disk_name, pos, num);
			if (s->verbose > 2) {
				printk (KERN_INFO "scrubber (%s): Thread state:\n", disk->disk_name);
				for (i=0; i<s->threads; i++)
					printk(KERN_INFO "scrubber (%s):               %d\n",
						   disk->disk_name, tdata[i].state);
			}
		}

		/* Everything went well, advance by the amount of bytes read */
		pos += num;
	}

	/* Scrubbing finished -- stop recording *
	if (s->timed) {
		tb = current_kernel_time();
		if (tb.tv_nsec - ta.tv_nsec < 0) {
			s->ttime_ms = (unsigned long) (tb.tv_sec - ta.tv_sec - 1) * 1000
				+ (unsigned int) (tb.tv_nsec - ta.tv_nsec + 1000000000)
				* 0.000001;
		} else {
			s->ttime_ms = (unsigned long) (tb.tv_sec - ta.tv_sec) * 1000
				+ (unsigned int) (tb.tv_nsec - ta.tv_nsec) * 0.000001;
		}
	}*/

	/* Calculate and print timing results */
	// for (i = 0; i < tnum; i++)
	//   thtime += rtime[j];

	return 0;
}

int stgscrub (struct gendisk *disk, struct scrubparams *s,
	struct scrub_thread_data *tdata)
{
	uint64_t pos, num, sn, rn, regnum, segnum, reqcount = 0;
	//struct timespec rqtp;
	//struct timespec ta, tb;

	/* Calculate and round up the number of regions in the drive and the number
	   of segments in each region */
	regnum = lceil(s->capacity - s->start, s->regsize, disk, s);
	segnum = lceil(s->regsize, s->segsize, disk, s);
	if (s->verbose) {
		printk(KERN_INFO "scrubber (%s): Starting from %llu to %llu.\n",
			   disk->disk_name, s->start, s->capacity);
		printk(KERN_INFO "scrubber(%s): There are %llu regions (%llu sectors), "
			   "with %llu segments (%llu sectors) each, in the portion of %llu "
			   "sectors to be scrubbed.\n", disk->disk_name, regnum, s->regsize,
			   segnum, s->segsize, s->capacity - s->start);
	}

	/* Initialize thread & global timers *
	ta.tv_sec = tb.tv_sec = 0;
	ta.tv_nsec = tb.tv_nsec = 0;*/

	//if (s->timed) ta = current_kernel_time();

	/* Scrub staggeredly in REGION_SIZE chunks of SEGMENT_SIZE segments */
	for (sn = (uint64_t) 0; sn < segnum; sn++) {
		if (s->verbose > 1)
			printk(KERN_INFO "scrubber(%s): Scrubbing segment: %llu/%llu\n",
				   disk->disk_name, sn+(uint64_t)1, segnum);
		for (rn = (uint64_t) 0; rn < regnum; rn++){

			//if (LAG > 0.00) {
			/* Introduce a delay equal to the disk's rotational latency */
			//	rqtp.tv_sec = 0;
			//	rqtp.tv_nsec = LAG * 5985000;
			//	nanosleep(&rqtp,NULL);
			//}

			/* Scrub the S-th segment of the R-th region */
			pos = s->start + rn * s->regsize + sn * s->segsize;
			if (pos < s->capacity) {
				if (pos + s->segsize > s->capacity)
				/* Either the segsize is larger than the device (read device in
				   one go), or the remaining sectors are less than segsize (read
				   just those). */
					num = (s->segsize > s->capacity) ? s->capacity :
						(s->capacity - pos);
				else
				/* Verify segsize sectors */
					num = s->segsize;
				if (segread (disk, s, tdata, pos, num))
					return -1;
				if (disk->scrubber->state == 2 || (s->reqbound && ++reqcount > s->reqbound))
					/* Exceeded maximum number of requests for this round. Bail. */
					return 0;
			}
		}
	}

	/* Scrubbing finished -- stop recording *
	if (s->timed) {
		tb = current_kernel_time();
		if (tb.tv_nsec - ta.tv_nsec < 0) {
			s->ttime_ms = (unsigned long) (tb.tv_sec - ta.tv_sec - 1) * 1000
				+ (unsigned int) (tb.tv_nsec - ta.tv_nsec + 1000000000)
				* 0.000001;
		} else {
			s->ttime_ms = (unsigned long) (tb.tv_sec - ta.tv_sec) * 1000
				+ (unsigned int) (tb.tv_nsec - ta.tv_nsec) * 0.000001;
		}
	}*/

	return 0;
}

/* Fixed scrubbing. Used to test-drive hard disk SCSI Verify response times */
int fixedscrub (struct gendisk *disk, struct scrubparams *s,
	struct scrub_thread_data *tdata)
{
	uint64_t pos, num, i;

	pos = 0;
	if (s->verbose) {
		printk(KERN_INFO "scrubber (%s): Performing a fixed scrub from 0 ~ 20GB\n",
			   disk->disk_name);
	}

	if (get_capacity(disk) < 40000001) {
		printk(KERN_INFO "scrubber (%s): Error:: Device smaller than 20GB! Aborting...\n",
			disk->disk_name);
		return -1;
	}

	s->timed = 0;
	if (segread (disk, s, tdata, 0, 1)) return -1;
	s->timed = 1;

	for (i = 0; i < 50; i++) {
		if (i % 2 == 0)
			pos = 0 + (i/2) * 400000;
		else if (i % 2 == 1)
			pos = 40000000 - ((i-1)/2) * 400000;

		num = s->segsize;
		if (segread (disk, s, tdata, pos, num))
			return -1;
	}

	return 0;
}

static int scrubdev (struct gendisk *disk, struct scrubparams *s,
	struct scrub_thread_data *tdata)
{
	int ret = 0;

	/* Print the capacity of the drive.
	   We're talking sectors down here (512b each). */
	if (s->verbose)
		printk(KERN_INFO "scrubber (%s): Capacity = %llu (of %ld) sectors, "
			   "Sector size = 512 bytes (fixed).\n", disk->disk_name,
			   s->capacity, get_capacity(disk));
	if (!s->capacity || s->start + s->capacity > get_capacity(disk))
		s->capacity = get_capacity(disk);
	else s->capacity = s->start + s->capacity;

	s->regsize = s->regsize * 2;
	s->segsize = s->segsize * 2;
	if (s->verbose > 1) {
		printk(KERN_INFO "scrubber (%s): Device  size in sectors = "
			   "%ld.\n", disk->disk_name, get_capacity(disk));
		printk(KERN_INFO "scrubber (%s): Device size seen by scrubber = "
			   "%llu.\n", disk->disk_name, s->capacity);
		printk(KERN_INFO "scrubber (%s): Region  size in sectors = "
			   "%llu.\n", disk->disk_name, s->regsize);
		printk(KERN_INFO "scrubber (%s): Segment size in sectors = "
			   "%llu.\n", disk->disk_name, s->segsize);
	}

	if (s->strategy == SEQLSCRUB) {
		if ((ret = seqscrub (disk, s, tdata)) < 0 && s->verbose)
			printk(KERN_INFO "scrubber (%s): Sequential scrub failed."
				   " %d errors detected.\n", disk->disk_name, s->read_errs);
		else if (s->verbose)
			printk(KERN_INFO "scrubber (%s): Sequential scrub succeeded. "
				   "Completed %llu requests. %d errors detected.\n",
				   disk->disk_name, s->reqcount, s->read_errs);
	} else if (s->strategy == STAGSCRUB) {
		if ((ret = stgscrub (disk, s, tdata)) < 0 && s->verbose)
			printk(KERN_INFO "scrubber (%s): Staggered scrub failed."
				   " %d errors detected.\n", disk->disk_name, s->read_errs);
		else if (s->verbose)
			printk(KERN_INFO "scrubber (%s): Staggered scrub succeeded. "
				   "Completed %llu requests. %d errors detected.\n",
				   disk->disk_name, s->reqcount, s->read_errs);
	} else if (s->strategy == FIXEDSCRUB) {
		if ((ret = fixedscrub (disk, s, tdata)) < 0 && s->verbose)
			printk(KERN_INFO "scrubber (%s): Fixed scrub failed.\n", disk->disk_name);
		else if (s->verbose)
			printk(KERN_INFO "scrubber (%s): Fixed scrub succeeded.\n", disk->disk_name);
	}

	return ret;
}

int scrubber (struct gendisk *disk)
{
	int i, res, err, ret = 0;
	struct scrubparams *s;
	struct scrub_thread_data *tdata;
	struct task_struct *ttask;
	struct timeval ta, tb;

	/* Allocate memory for the local scrubbing parameters */
	s = (struct scrubparams *) kmalloc_node(sizeof(struct scrubparams),
		GFP_KERNEL | __GFP_ZERO, -1);

	/* Start an endless loop of polling for signal to scrub. Terminate
	 * if appropriate signal has been received. */
	while(1) {
		if (disk->scrubber->state == 0) {

			/* Copy parameters locally */
			mutex_lock(&disk->scrubber->sysfs_lock);

			s->reqbound = disk->scrubber->reqbound;

			if (!strcmp(disk->scrubber->strategy, "seql"))
				s->strategy = SEQLSCRUB;
			else if (!strcmp(disk->scrubber->strategy, "stag"))
				s->strategy = STAGSCRUB;
			else if (!strcmp(disk->scrubber->strategy, "fixed"))
				s->strategy = FIXEDSCRUB;

			if (!strcmp(disk->scrubber->priority, "realtime"))
				s->priority = RTIMEPRIO;
			else if (!strcmp(disk->scrubber->priority, "idlechk"))
				s->priority = IDCHKPRIO;

			s->segsize = disk->scrubber->segsize;
			s->regsize = disk->scrubber->regsize;
			s->threads = disk->scrubber->threads;
			s->dpo = disk->scrubber->dpo;
			s->vrprotect = disk->scrubber->vrprotect;
			s->verbose = disk->scrubber->verbose;
			s->timed = disk->scrubber->timed;
			s->capacity = disk->scrubber->scount;
			s->start = disk->scrubber->spoint;
			s->delayms = disk->scrubber->delayms;

			mutex_unlock(&disk->scrubber->sysfs_lock);

			disk->scrubber->ttime_ms = 0;
			s->ttime_ms = 0;
			disk->scrubber->resptime_us = 0;
			s->resptime_us = 0;
			//disk->scrubber->reqcount = 0;
			s->reqcount = 0;
			s->available = 0;
			s->read_errs = 0;
			s->idlestamp = current_kernel_time();

			/* Start scrubbing */
			if (s->verbose > 1){
				if (s->strategy == SEQLSCRUB)
					printk(KERN_INFO "scrubber (%s): Scrubbing strategy used:"
						   "Sequential scrubbing.\n", disk->disk_name);
				else if (s->strategy == STAGSCRUB)
					printk(KERN_INFO "scrubber (%s): Scrubbing strategy used:"
						   "Staggered scrubbing.\n", disk->disk_name);
				else if (s->strategy == FIXEDSCRUB)
					printk(KERN_INFO "scrubber (%s): Scrubbing strategy used:"
						   "Fixed scrubbing.\n", disk->disk_name);

				if (s->priority == RTIMEPRIO)
					printk(KERN_INFO "scrubber (%s): Scrubbing priority used:"
						   "Real Time.\n", disk->disk_name);
				else if (s->priority == IDCHKPRIO)
					printk(KERN_INFO "scrubber (%s): Scrubbing priority used:"
						   "Idle Check.\n", disk->disk_name);

				printk(KERN_INFO "scrubber (%s): Using Segment Size = %lluKB\n",
					   disk->disk_name, s->segsize);
				printk(KERN_INFO "scrubber (%s): Using Region  Size = %lluKB\n",
					   disk->disk_name, s->regsize);
			}

			/* Mutex initialization */
			mutex_init(&s->mutexerr);
			mutex_init(&s->mutextime);
			mutex_init(&s->mutexavail);
			s->barrier = (spinlock_t*)kmalloc_node(sizeof(spinlock_t)*s->threads,
							   GFP_KERNEL | __GFP_ZERO, -1);
			for (i = 0; i < s->threads; i++)
				spin_lock_init(&s->barrier[i]);

			/* Thread initialization */
			tdata = (struct scrub_thread_data*) kmalloc_node(sizeof(struct
					scrub_thread_data)*s->threads,GFP_KERNEL | __GFP_ZERO,-1);

			/* Thread data initialization */
			for (i = 0; i < s->threads; i++) {
				tdata[i].disk = disk;
				tdata[i].s = s;
				tdata[i].state = TINIT;
				tdata[i].tid = i;
			}

			if (s->verbose)
				printk(KERN_INFO "scrubber (%s): Creating threads...\n", disk->disk_name);

			/* Thread creation */
			for (i = 0; i < s->threads; i++){
				tdata[i].task = kthread_run(kthread_segread, (void*) &tdata[i], "Scrubber_thread");
				if (IS_ERR((void*) tdata[i].task) == -ENOMEM)
					printk(KERN_INFO "scrubber (%s): Failed to create kernel "
						"thread %d/%d.\n", disk->disk_name, i, s->threads);
			}


			/* Set priorities for SCSIVerify requests, for all threads */
			if (s->priority == IDCHKPRIO) {
				for (i=0; i <= s->threads; i++) {

					if (i == s->threads)
						ttask = current;
					else ttask = tdata[i].task;
					err = 0;

					if (s->verbose)
						printk(KERN_INFO "scrubber (%s): Setting CFQ priorities for %d\n",
							disk->disk_name, ttask->pid);

					err = set_task_ioprio(ttask, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE,0));

					if (err)
						printk(KERN_INFO "scrubber (%s): Failed to set CFQ priorities for %d\n",
							disk->disk_name, ttask->pid);
				}
			}

			/* Move the head to the first block and read it */
			if (s->capacity && s->capacity < s->segsize)
				printk(KERN_INFO "scrubber (%s): Warm-up error - segsize > capacity\n", disk->disk_name);
			else if (segread(disk, s, tdata, s->start + s->segsize, s->segsize))
				printk(KERN_INFO "scrubber (%s): Failed to read first segment during warm-up\n",
					disk->disk_name);

			/* Initialize and start the timer */
			ta.tv_sec = tb.tv_sec = 0;
			ta.tv_usec = tb.tv_usec = 0;

			if (s->timed) do_gettimeofday(&ta);

			/* Scrub device. */
			if ((ret = scrubdev(disk, s, tdata)) < 0)
				printk(KERN_INFO "scrubber (%s): scrub failed (%d)\n", disk->disk_name, ret);

			/* Now wait for the threads to return */
			if (s->verbose)
				printk(KERN_INFO "scrubber (%s): Waiting for threads to return...\n",
					disk->disk_name);
			for (i = 0; i < s->threads; i++){
				res = -1;
				while (res < 0) {
					res = kthread_stop(tdata[i].task);
					wake_up_state(tdata[i].task, TASK_REPORT);
					if (res) {
						printk(KERN_INFO "scrubber (%s): Return code from kthread_stop()"
							   " was %d\n", disk->disk_name, res);
						set_current_state(TASK_INTERRUPTIBLE);
						schedule();
						set_current_state(TASK_RUNNING);
					}
				}
			}
			if (s->verbose)
				printk(KERN_INFO "scrubber (%s): Done waiting for threads to return.\n",
					disk->disk_name);

			/* Scrubbing finished -- stop recording */
			if (s->timed) {
				do_gettimeofday(&tb);
				s->ttime_ms = ((tb.tv_sec - ta.tv_sec) * 1000000 + tb.tv_usec - ta.tv_usec) / 1000;
			}

			/* Mutex destruction */
			kfree(s->barrier);
			mutex_destroy(&s->mutexerr);
			mutex_destroy(&s->mutextime);
			mutex_destroy(&s->mutexavail);

			/* Thread destruction */
			kfree(tdata);

			/* Update sysfs entries */
			mutex_lock(&disk->scrubber->sysfs_lock);
		
			if (s->ttime_ms)
				disk->scrubber->ttime_ms = s->ttime_ms;
			if (s->reqcount)
				disk->scrubber->resptime_us = (uint64_t) s->resptime_us / s->reqcount;
			disk->scrubber->reqcount = s->reqcount;

			mutex_unlock(&disk->scrubber->sysfs_lock);

		} else {
			set_current_state(TASK_INTERRUPTIBLE);
			if (!kthread_should_stop()) {
				/* Schedule the task out of the running queue */
				schedule();
			} else {
				printk(KERN_INFO "scrubber (%s): Main scrubber thread decided to "
					"terminate.\n", disk->disk_name);
				break;
			}
		}
	}

	return 0;
}

