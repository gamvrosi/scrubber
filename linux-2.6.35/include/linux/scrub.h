/*
 * Copyright (C) 2012 George Amvrosiadis <gamvrosi@gmail.com>
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

#ifndef _LINUX_SCRUB_H
#define _LINUX_SCRUB_H

#ifdef CONFIG_BLK_DEV_SCRUB

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <scsi/sg.h>
//#include <linux/timer.h>

#define SCRUB_STRAT_NAME_MAX	10
#define SCRUB_STRAT_NUM		3
#define SCRUB_PRIO_NAME_MAX	10
#define SCRUB_PRIO_NUM		2

struct disk_scrubber {
	/* Pointer to the name of the gendisk we're scrubbing 
	 * and the scrubbing task */
	char		*disk_name;
	struct task_struct *task;

	uint64_t	reqbound; /* Request limit per scrubbing round */
	char		*strategy;
	char		*priority;
	uint64_t	segsize; /* Segment size of scrubber */
	uint64_t	regsize; /* Region size of scrubber */

	int		state; /* State of scrubber: {on, off} */
	int		threads; /* Number of threads used by scrubber */
	int		dpo; /* Disable page out */
	int		vrprotect; /* VRP value */
	int		verbose; /* Verbosity of scrubber */

	int		timed; /* Whether we should keep scrubbing stats */
	uint64_t	ttime_ms; /* Total scrubbing time (ms) */
	uint64_t	resptime_us; /* Measured SCSIVerify average response time (us) */
	uint64_t	reqcount; /* Total number of requests executed during last scrub */
	uint64_t	spoint; /* Sector where scrubbing begins */
	uint64_t	scount; /* Number of sectors scrubbed by scrubber */
	struct gendisk	*disk; /* Pointer to scrubber's gendisk */

	/* Queue related stuff */
	struct timespec	idle;
	uint64_t	delayms;

	/* Embedded kobject for the scrubber */
	struct kobject	kobj;
	struct mutex	sysfs_lock;
};

int kscrubd_init(void *data);
int blk_register_scrub(struct gendisk *disk);
void blk_unregister_scrub(struct gendisk *disk);
int scsi_verify(struct gendisk *disk, uint64_t lba, unsigned int count);
int scrubber(struct gendisk *disk);

#endif /* CONFIG_BLK_DEV_SCRUB */

#endif
