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

#include <linux/scrub.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>

static char *strategies[SCRUB_STRAT_NUM] = {"seql", "stag", "fixed"};
static char *priorities[SCRUB_PRIO_NUM]  = {"realtime", "idlechk"};

static struct kobj_type scrubber_ktype;

int kscrubd_init(void *data)
{
	struct gendisk *disk = (struct gendisk *) data;
	struct disk_scrubber *s = NULL;

	if (disk == NULL) {
		printk(KERN_INFO "scrubber (???): Scrubber thread data corrupted!\n");
		return 1;
	}

	s = disk->scrubber;

	if (disk->queue == NULL) {
		printk(KERN_INFO "scrubber (%s): No disk queue defined\n", s->disk_name);
		return 1;
	}

	/* Reminder: At this level, the scrubber scrubs sectors (512B), *not* blocks! */
	if (scrubber(disk))
		printk(KERN_INFO "scrubber (%s): Scrubber invocation failed\n", s->disk_name);

	return 0;
}

static struct disk_scrubber *blk_init_scrub(struct gendisk *disk)
{
	struct disk_scrubber *s;

	disk->scrubber = kmalloc_node(sizeof(struct disk_scrubber),
					GFP_KERNEL | __GFP_ZERO, -1);
	s = disk->scrubber;
	disk->queue->scrubber = s;
	if (!s) return NULL;

	/* Set default parameters */
	s->disk_name = disk->disk_name;
	s->disk = disk;

	s->reqbound = 0;
	s->segsize = 2048;
	s->regsize = 131072;
	s->state = 1;
	s->threads = 1;
	s->dpo = 1;
	s->vrprotect = 0;
	s->verbose = 1;
	s->spoint = 0;
	s->reqcount = 0;
	s->scount = 0;

	s->timed = 0;
	s->ttime_ms = 0;
	s->resptime_us = 0;
	s->delayms = 0;

	/* Allocate memory for strategy names */
	s->strategy = kmalloc_node(SCRUB_STRAT_NAME_MAX*sizeof(char),
					GFP_KERNEL | __GFP_ZERO, -1);
	/* Let sequential strategy be the default*/
	sprintf(s->strategy, "%s", "seql");

	/* Allocate memory for priority names */
	s->priority = kmalloc_node(SCRUB_PRIO_NAME_MAX*sizeof(char),
					GFP_KERNEL | __GFP_ZERO, -1);
	/* Let idlechk priority be the default*/
	sprintf(s->priority, "%s", "idlechk");

	kobject_init(&s->kobj, &scrubber_ktype);

	mutex_init(&s->sysfs_lock);

	/* Proceed with starting the scrubber thread */
	s->task = kthread_run(kscrubd_init, (void *) disk, "Scrubber");

	return s;
}

static void blk_release_scrub(struct kobject *kobj)
{
	struct disk_scrubber *s =
		container_of(kobj, struct disk_scrubber, kobj);

	if (!s)
		return;

	kthread_stop(s->task);

	/* De-allocate memory for strategy, priority names */
	kfree(s->strategy);
	kfree(s->priority);

	mutex_destroy(&s->sysfs_lock);
	kfree(s);
}

/*
 * sysfs parts below
 */
struct scrub_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct disk_scrubber *, char *);
	ssize_t (*store)(struct disk_scrubber *, const char *, size_t);
};

static ssize_t scrub_reqbound_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Scrubbing requests to limit next round to: %llu\n",
			s->reqbound);
}

static ssize_t scrub_reqbound_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	unsigned long reqbound;
	char *p = (char *) page;

	reqbound = simple_strtoul(p, &p, 10);

	if (reqbound < 0)
		printk(KERN_ERR "scrubber (%s): cannot set request bound to a negative number.\n",
			s->disk_name);
	else
		s->reqbound = reqbound;

	return count;
}

static ssize_t scrub_segsize_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Segment size: %llu KB\n", s->segsize);
}

static ssize_t scrub_segsize_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t segsize;
	char *p = (char *) page;

	segsize = simple_strtoull(p, &p, 10);

	if (!segsize)
		printk(KERN_ERR "scrubber (%s): cannot set segsize to 0 KB.\n",
			s->disk_name);
	//else if (segsize > s->regsize)
	//	printk(KERN_ERR "scrubber (%s): cannot set segsize to be greater than regsize.\n",
	//		s->disk_name);
	else if (s->segsize > get_capacity(s->disk))
		s->segsize = get_capacity(s->disk);
	else s->segsize = segsize;

	return count;
}

static ssize_t scrub_regsize_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Region size: %llu KB\n", s->regsize);
}

static ssize_t scrub_regsize_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t regsize;
	char *p = (char *) page;

	regsize = simple_strtoull(p, &p, 10);

	if (!regsize)
		printk(KERN_ERR "scrubber (%s): cannot set regsize to 0 KB.\n",
			s->disk_name);
	//else if (regsize < s->segsize)
	//	printk(KERN_ERR "scrubber (%s): cannot set regsize to be smaller than segsize.\n",
	//		s->disk_name);
	else if (s->regsize > get_capacity(s->disk))
		s->regsize = get_capacity(s->disk);
	else s->regsize = regsize;

	return count;
}

static ssize_t scrub_spoint_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Scrubbing starts at sector: %llu\n", s->spoint);
}

static ssize_t scrub_spoint_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t spoint;
	char *p = (char *) page;

	spoint = simple_strtoull(p, &p, 10);

	if (s->spoint > get_capacity(s->disk)) {
		printk(KERN_ERR "scrubber (%s): scrubbing starting point exceeds disk capacity.\n",
			s->disk_name);
		s->spoint = 0;
	} else s->spoint = spoint;

	return count;
}

static ssize_t scrub_scount_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "# of sectors to be scrubbed: %llu\n", s->scount);
}

static ssize_t scrub_scount_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t scount;
	char *p = (char *) page;

	scount = simple_strtoull(p, &p, 10);

	if (s->scount > get_capacity(s->disk)) {
		printk(KERN_ERR "scrubber (%s): requested scrubbing area larger than disk.\n",
			s->disk_name);
		s->scount = get_capacity(s->disk);
	} else s->scount = scount;

	return count;
}

static ssize_t scrub_strategy_show(struct disk_scrubber *s, char *page)
{
	int i, len = 0;

	for (i=0; i<SCRUB_STRAT_NUM; i++) {
		if (!strcmp(s->strategy,strategies[i]))
			len += sprintf(page+len, "[%s] ", strategies[i]);
		else
			len += sprintf(page+len, "%s ", strategies[i]);
	}

	len += sprintf(page+len, "\n");
	return len;
}

static ssize_t scrub_strategy_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	int i, flag = 0;
	size_t len;
	char *p = (char *) page;

	len = strlen(p);
	if (len && p[len-1] == '\n')
		p[len-1] = '\0';

	for (i=0; i<SCRUB_STRAT_NUM; i++)
		if (!strcmp(p, strategies[i])) {
			flag = 1;
			if (strcmp(p, s->strategy)) {
				flag = 2;
				sprintf(s->strategy, "%s", p);
			}
		}

	if (flag == 0)
		printk(KERN_ERR "scrubber (%s): strategy '%s' not found.\n",
			s->disk_name, p);
	else if (flag == 1)
		printk(KERN_ERR "scrubber (%s): strategy already set to '%s'.\n",
			s->disk_name, p);
	return count;
}

static ssize_t scrub_priority_show(struct disk_scrubber *s, char *page)
{
	int i, len = 0;

	for (i=0; i<SCRUB_PRIO_NUM; i++) {
		if (!strcmp(s->priority,priorities[i]))
			len += sprintf(page+len, "[%s] ", priorities[i]);
		else
			len += sprintf(page+len, "%s ", priorities[i]);
	}

	len += sprintf(page+len, "\n");
	return len;
}

static ssize_t scrub_priority_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	int i, flag = 0;
	size_t len;
	char *p = (char *) page;

	len = strlen(p);
	if (len && p[len-1] == '\n')
		p[len-1] = '\0';

	for (i=0; i<SCRUB_PRIO_NUM; i++)
		if (!strcmp(p, priorities[i])) {
			flag = 1;
			if (strcmp(p, s->priority)) {
				flag = 2;
				sprintf(s->priority, "%s", p);
			}
		}

	if (flag == 0)
		printk(KERN_ERR "scrubber (%s): priority '%s' not found.\n",
			s->disk_name, p);
	else if (flag == 1)
		printk(KERN_ERR "scrubber (%s): priority already set to '%s'.\n",
			s->disk_name, p);
	return count;
}

static ssize_t scrub_state_show(struct disk_scrubber *s, char *page)
{
	int len = 0;

	if (s->state == 0)
		len = sprintf(page, "Scrubber state: [on] off abort\n");
	else if (s->state == 1)
		len = sprintf(page, "Scrubber state: on [off] abort\n");
	else if (s->state == 2)
		len = sprintf(page, "Scrubber state: on off [abort]\n");

	return len;
}

static ssize_t scrub_state_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	size_t len;
	char *p = (char *) page;

	len = strlen(p);
	if (len && p[len-1] == '\n')
		p[len-1] = '\0';

	if (s->state != 0 && s->state != 1 && s->state != 2)
		s->state = 1;

	if (!strcmp(p, "on") && s->state != 0) {
		s->state = 0;
		wake_up_process(s->task);
	} else if (!strcmp(p, "off") && s->state != 1) {
		s->state = 1;
	} else if (!strcmp(p, "abort") && s->state != 2) {
		s->state = 2;
	} else {
		printk(KERN_ERR "scrubber (%s): state '%s' not found, or coincides with"
			" the current one.\n", s->disk_name, p);
	}

	return count;
}

static ssize_t scrub_threads_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Number of scrubbing threads: %d\n", s->threads);
}

static ssize_t scrub_threads_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	int threads;
	char *p = (char *) page;

	threads = (int) simple_strtol(p, &p, 10);

	if (threads <= 0)
		printk(KERN_ERR "scrubber (%s): Check that threads > 0.\n",
			s->disk_name);
	else s->threads = threads;

	return count;
}

static ssize_t scrub_dpo_show(struct disk_scrubber *s, char *page)
{
	int len = 0;

	if (s->dpo)
		len = sprintf(page, "Disable page out: [on] off\n");
	else
		len = sprintf(page, "Disable page out:  on [off]\n");

	return len;
}

static ssize_t scrub_dpo_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	size_t len;
	char *p = (char *) page;

	len = strlen(p);
	if (len && p[len-1] == '\n')
		p[len-1] = '\0';

	if (s->dpo != 0 && s->dpo != 1)
		s->dpo = 0;

	if (!strcmp(p, "on") && !s->dpo)
		s->dpo = 1;
	else if (!strcmp(p, "off") && s->dpo)
		s->dpo = 0;
	else
		printk(KERN_ERR "scrubber (%s): state '%s' not found, or coincides "
			"with the current one.\n", s->disk_name, p);

	return count;
}

static ssize_t scrub_vrprotect_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "VRP set to: %d\n", s->vrprotect);
}

static ssize_t scrub_vrprotect_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	int vrprotect;
	char *p = (char *) page;

	vrprotect = (int) simple_strtol(p, &p, 10);

	if (vrprotect < 0 || vrprotect > 7)
		printk(KERN_ERR "scrubber (%s): Check that 0 <= vrprotect <= 7.\n",
			s->disk_name);
	else
		s->vrprotect = vrprotect;

	return count;
}

static ssize_t scrub_verbose_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "Verbosity of scrubber: %d\n", s->verbose);
}

static ssize_t scrub_verbose_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	int verbose;
	char *p = (char *) page;

	verbose = (int) simple_strtol(p, &p, 10);

	if (verbose < 0 || verbose > 3)
		printk(KERN_ERR "scrubber (%s): Check that 0 <= verbose <= 3.\n",
			s->disk_name);
	else
		s->verbose = verbose;

	return count;
}

static ssize_t scrub_timed_show(struct disk_scrubber *s, char *page)
{
	int len = 0;

	if (s->timed)
		len = sprintf(page, "Scrubber statistics: [on] off\n");
	else
		len = sprintf(page, "Scrubber statistics:  on [off]\n");

	return len;
}

static ssize_t scrub_timed_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	size_t len;
	char *p = (char *) page;

	len = strlen(p);
	if (len && p[len-1] == '\n')
		p[len-1] = '\0';

	if (s->timed != 0 && s->timed != 1)
		s->timed = 0;

	if (!strcmp(p, "on") && !s->timed)
		s->timed = 1;
	else if (!strcmp(p, "off") && s->timed)
		s->timed = 0;
	else
		printk(KERN_ERR "scrubber (%s): state '%s' not found, or coincides "
			"with the current one.\n", s->disk_name, p);

	return count;
}

static ssize_t scrub_ttime_ms_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "%llu\n", s->ttime_ms);
}

static ssize_t scrub_ttime_ms_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t ttime_ms;
	char *p = (char *) page;

	ttime_ms = simple_strtoull(p, &p, 10);
	s->ttime_ms = ttime_ms;

	return count;
}

static ssize_t scrub_resptime_us_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "%llu\n", s->resptime_us);
}

static ssize_t scrub_reqcount_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "%llu\n", s->reqcount);
}

static ssize_t scrub_reqcount_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t reqcount;
	char *p = (char *) page;

	reqcount = simple_strtoull(p, &p, 10);
	s->reqcount = reqcount;

	return count;
}

static ssize_t scrub_delayms_show(struct disk_scrubber *s, char *page)
{
	return sprintf(page, "%llu\n", s->delayms);
}

static ssize_t scrub_delayms_store(struct disk_scrubber *s, const char *page,
	size_t count)
{
	uint64_t delayms;
	char *p = (char *) page;

	delayms = simple_strtoull(p, &p, 10);
	s->delayms = delayms;

	return count;
}

static struct scrub_sysfs_entry scrub_reqbound_entry = {
	.attr = {.name = "reqbound", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_reqbound_show,
	.store = scrub_reqbound_store,
};

static struct scrub_sysfs_entry scrub_segsize_entry = {
	.attr = {.name = "segsize", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_segsize_show,
	.store = scrub_segsize_store,
};

static struct scrub_sysfs_entry scrub_regsize_entry = {
	.attr = {.name = "regsize", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_regsize_show,
	.store = scrub_regsize_store,
};

static struct scrub_sysfs_entry scrub_spoint_entry = {
	.attr = {.name = "spoint", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_spoint_show,
	.store = scrub_spoint_store,
};

static struct scrub_sysfs_entry scrub_scount_entry = {
	.attr = {.name = "scount", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_scount_show,
	.store = scrub_scount_store,
};

static struct scrub_sysfs_entry scrub_strategy_entry = {
	.attr = {.name = "strategy", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_strategy_show,
	.store = scrub_strategy_store,
};

static struct scrub_sysfs_entry scrub_priority_entry = {
	.attr = {.name = "priority", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_priority_show,
	.store = scrub_priority_store,
};

static struct scrub_sysfs_entry scrub_state_entry = {
	.attr = {.name = "state", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_state_show,
	.store = scrub_state_store,
};

static struct scrub_sysfs_entry scrub_threads_entry = {
	.attr = {.name = "threads", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_threads_show,
	.store = scrub_threads_store,
};

static struct scrub_sysfs_entry scrub_dpo_entry = {
	.attr = {.name = "dpo", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_dpo_show,
	.store = scrub_dpo_store,
};

static struct scrub_sysfs_entry scrub_vrprotect_entry = {
	.attr = {.name = "vrprotect", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_vrprotect_show,
	.store = scrub_vrprotect_store,
};

static struct scrub_sysfs_entry scrub_verbose_entry = {
	.attr = {.name = "verbose", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_verbose_show,
	.store = scrub_verbose_store,
};

static struct scrub_sysfs_entry scrub_timed_entry = {
	.attr = {.name = "timed", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_timed_show,
	.store = scrub_timed_store,
};

static struct scrub_sysfs_entry scrub_ttime_ms_entry = {
	.attr = {.name = "ttime_ms", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_ttime_ms_show,
	.store = scrub_ttime_ms_store,
};

static struct scrub_sysfs_entry scrub_resptime_us_entry = {
	.attr = {.name = "resptime_us", .mode = S_IRUGO },
	.show = scrub_resptime_us_show,
	.store = NULL,
};

static struct scrub_sysfs_entry scrub_reqcount_entry = {
	.attr = {.name = "reqcount", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_reqcount_show,
	.store = scrub_reqcount_store,
};

static struct scrub_sysfs_entry scrub_delayms_entry = {
	.attr = {.name = "delayms", .mode = S_IRUGO | S_IWUSR },
	.show = scrub_delayms_show,
	.store = scrub_delayms_store,
};

static struct attribute *default_attrs[] = {
	&scrub_reqbound_entry.attr,
	&scrub_segsize_entry.attr,
	&scrub_regsize_entry.attr,
	&scrub_spoint_entry.attr,
	&scrub_scount_entry.attr,
	&scrub_strategy_entry.attr,
	&scrub_priority_entry.attr,
	&scrub_state_entry.attr,
	&scrub_threads_entry.attr,
	&scrub_dpo_entry.attr,
	&scrub_vrprotect_entry.attr,
	&scrub_verbose_entry.attr,
	&scrub_timed_entry.attr,
	&scrub_ttime_ms_entry.attr,
	&scrub_resptime_us_entry.attr,
	&scrub_reqcount_entry.attr,
	&scrub_delayms_entry.attr,
	NULL,
};

#define to_scrub(atr) container_of((atr), struct scrub_sysfs_entry, attr)

static ssize_t scrub_attr_show(struct kobject *kobj, struct attribute *attr,
	char *page)
{
	struct scrub_sysfs_entry *entry = to_scrub(attr);
	struct disk_scrubber *s =
		container_of(kobj, struct disk_scrubber, kobj);
	ssize_t res;

	if (!entry->show)
		return -EIO;
	mutex_lock(&s->sysfs_lock);
	res = entry->show(s, page);
	mutex_unlock(&s->sysfs_lock);
	return res;
}

static ssize_t scrub_attr_store(struct kobject *kobj, struct attribute *attr,
	const char *page, size_t length)
{
	struct scrub_sysfs_entry *entry = to_scrub(attr);
	struct disk_scrubber *s =
		container_of(kobj, struct disk_scrubber, kobj);
	ssize_t res;

	if (!entry->store)
		return -EIO;
	mutex_lock(&s->sysfs_lock);
	res = entry->store(s, page, length);
	mutex_unlock(&s->sysfs_lock);
	return res;
}

static struct sysfs_ops scrub_sysfs_ops = {
	.show	= scrub_attr_show,
	.store	= scrub_attr_store,
};

static struct kobj_type scrubber_ktype = {
	.sysfs_ops	= &scrub_sysfs_ops,
	.default_attrs	= default_attrs,
	.release	= blk_release_scrub,
};

int blk_register_scrub(struct gendisk *disk)
{
	int ret;
	struct disk_scrubber *s = blk_init_scrub(disk);

	/* 
	 * Checks prior to registration.
	 * If anything fails, just return.
	 */
	if (!s)
		return -ENXIO;

	ret = kobject_add(&s->kobj, get_disk(disk), "%s", "scrubber");
	if (ret < 0)
		return ret;

	kobject_uevent(&s->kobj, KOBJ_ADD);

	/* 
	 * Check stuff
	 * If something goes wrong, remember:
	 * kobject_uevent(&s->kobj, KOBJ_REMOVE);
	 * kobject_del(&s->kobj);
	 * return;
	 */

	return 0;
}

void blk_unregister_scrub(struct gendisk *disk)
{
	struct disk_scrubber *s = disk->scrubber;

	if (WARN_ON(!s))
		return;

	//kobject_put(&s->kobj);
	kobject_uevent(&s->kobj, KOBJ_REMOVE);
	kobject_del(&s->kobj);
	put_disk(disk);
}

