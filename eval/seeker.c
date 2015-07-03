/*
 * Copyright (C) 2010-2012 George Amvrosiadis <gamvrosi@gmail.com>
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

/* Seeker v0.9.5b, 2010-09-02 */

#define _GNU_SOURCE /* Needed for O_DIRECT */
#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <getopt.h>
#include <signal.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

/* Chunk size to be read at once -- BYTES here */
#define BLOCKNUM 16*sysconf(_SC_PAGESIZE)

/* Find the syscall codes for ioprio_set and ioprio_get */
#if defined(__i386__)
#define __NR_ioprio_set         289
#define __NR_ioprio_get         290
#elif defined(__ppc__)
#define __NR_ioprio_set         273
#define __NR_ioprio_get         274
#elif defined(__x86_64__)
#define __NR_ioprio_set         251
#define __NR_ioprio_get         252
#elif defined(__ia64__)
#define __NR_ioprio_set         1274
#define __NR_ioprio_get         1275
#else
#error "Unsupported arch"
#endif
/* Syscall codes found */

/* Declare ioprio stuff/functions here */
#define IOPRIO_CLASS_SHIFT      13
#define IOPRIO_PRIO_MASK        ((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask) ((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)  ((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)  (((class) << IOPRIO_CLASS_SHIFT) | data)

static inline int ioprio_set(int which, int who, int ioprio)
{
        return syscall(__NR_ioprio_set, which, who, ioprio);
}

static inline int ioprio_get(int which, int who)
{
        return syscall(__NR_ioprio_get, which, who);
}

enum {
        IOPRIO_CLASS_NONE,
        IOPRIO_CLASS_RT,
        IOPRIO_CLASS_BE,
        IOPRIO_CLASS_IDLE,
};

enum {
        IOPRIO_WHO_PROCESS = 1,
        IOPRIO_WHO_PGRP,
        IOPRIO_WHO_USER,
};

/* ioprio stuff declared */

/* File descriptors */
#define RANDNUM 200000

enum {
	RANDWKLD,
	SEQLWKLD,
	SCRUBDEV,
	TESTCACHE,
};

enum {
	IDLEPRIO,
	DEFPRIO,
	RTPRIO,
};

int dev = 0;
int num = 0;
FILE *inpf = NULL;
FILE *outf = NULL;
char out[256], inp[256];

int seed = 0;
int count = 0;
int runtime = 0;
int workload = RANDWKLD;
int priority = DEFPRIO;
int direct = 0;
uint64_t maxcount = 0;
uint64_t sectorcount = 0;
uint64_t sectorstart = 0;
double thinkprob = 0.0;
long double totaltime = 0.0;
struct timeval start, lap0, lap1;
long double total;

struct timeval reqa, reqb;

uint64_t testblks[] =
	/*{  918441, 1703431, 1723250, 1701050, 1744863,
	  1707070, 1884211, 1032065, 1602603,  393395,
          1245628,  744659,  968025, 1014003, 1467116,
	  1951144,  983418,  764375, 1633611,  353936,
	   813432,  223510,  418001,  342386, 1608997,
	   953224, 1444244,  503346, 1211599, 1002872,
	   403671, 1208823, 1437170,  105421, 1468314,
	   673021, 1001783,  304001,  709867,  695792,
	   588080, 1651857,  852548,  343061,  777037,
	  1575096, 1568679,  271903,  261819, 1378256 };*/
	{  918441, 1703431, 1701050,  393395, 1245628,
	   744659, 1951144,  353936,  105421, 1468314 };
long double testtimes1[50], testtimes2[50];

int verbose = 0;

static struct option long_options[] = {
	{"workload", 1, 0, 'w'},
	{"direct", 0, 0, 'd'},
	{"help", 0, 0, 'h'},
	{"verbose", 0, 0, 'v'},
	{"priority", 1, 0, 'p'},
	{"input", 1, 0, 'i'},
	{"output", 1, 0, 'o'},
	{"count", 1, 0, 'c'},
	{"runtime", 1, 0, 't'},
	{"seed", 1, 0, 's'},
	{"sectors", 1, 0, 'S'},
	{"start", 1 , 0, 'P'},
	{"thinktime", 1, 0, 'Z'},
	{0, 0, 0, 0},
};

/* Print usage information for this program to STREAM (typically
 stdout or stderr), and exit the program with EXIT_CODE. Does not
 return. */

static void usage()
{
	fprintf(stderr, "Usage: "
			"seeker [--count=COUNT] [--help] [--workload=WKLD] [--verbose] [--direct]\n"
			"       [--output=OUT] [--priority=PRIO] [--seed=SEED] DEVICE\n"
			"  where:\n"
			"  -c  --count COUNT     Count of requests to execute (def 0, infinite)\n"
			"  -t  --runtime TIME    Runtime of benchmarks in seconds (def 0, infinite)\n"
			"  -d  --direct          Enable O_DIRECT flag for each request\n"
			"  -h  --help            Display this usage information.\n"
			"  -w  --workload WKLD   Workload type used (def RAND):\n"
			"                        o RAND or 0: Random workload\n"
			"                        o SEQL or 1: Partially sequential random workload\n"
			"                        o SCRUB or 2: Beggining-to-end sequential workload\n"
			"                        o CACHE or 3: Cache-testing workload\n"
			"  -v  --verbose         Increase verbosity\n"
			"  -p  --priority        Set CFQ priority of seeker:\n"
			"                        o IDLE or 0: Idle priority\n"
			"                        o DEF or 1: Default priority\n"
			"                        o RT or 2: Realtime priority\n"
			"  -i  --input INP       Name a file where the exponential numbers will be\n"
			"                        fetched from\n"
			"  -o  --output OUT      Name a file where seeker's output will be dumped\n"
			"  -s  --seed SEED       Set seed for uniform random numbers\n"
			"  -S  --sectors SECTRS  Count of sectors to limit scanning to (def 0, infinite)\n"
			"  -P  --start START     Sector number where the scan should start from (def 0)\n"
			"  -Z  --thinktime PROB  Thinking probability (def. 0)\n"
          );
}

long double timediff (struct timeval start, struct timeval end) {
	long double ta, tb;

	ta = start.tv_sec + start.tv_usec * 0.000001;
	tb = end.tv_sec + end.tv_usec * 0.000001;

	return tb-ta;
}

void done()
{
	struct timeval end;

	gettimeofday(&end, NULL);
	total += timediff(lap0, end);
	if (count) {
		if (verbose)
			printf(".\nResults: %.3Lf requests/second, %.3Lf ms/request (avg), %d requests\n",
				count / total, 1000.0 * totaltime / count, count);
		else
			printf("%.3Lf\t%.3Lf\t%d\n",
				count / total, 1000.0 * totaltime / count, count);
	}

	/* Close open file descriptors */
	if (dev) close(dev);
	if (inpf != NULL) fclose(inpf);
	if (outf != NULL) fclose(outf);

	if (verbose)
		printf("Done. Used up %d random numbers.\n", num);

	exit(EXIT_SUCCESS);
}

void update()
{
	struct timeval end;

	gettimeofday(&end, NULL);
	if (verbose)
		printf("\rTime elapsed: %10.6Lf (used up %d random numbers)",
			timediff(start, end), num);
	if (runtime && timediff(start,end) >= runtime)
		done();
	alarm(1);
	return;
}

void handle(const char *string, int error)
{
	if (error) {
		perror(string);
		exit(EXIT_FAILURE);
	}
}

ssize_t segread(int devfd, void* buf, size_t size, off_t pos, int direct, int timed)
{
	size_t alignment = sysconf(_SC_PAGESIZE);
	off_t posb, poso;
	size_t sizeb;
	int err;
	ssize_t r;

	pos += sectorstart * 512;

	if (direct) {
		/* Divide pos to base pos, which is an aligned boundary,
		 * and offset pos, which is the offset from that boundary
		 * to the actual pos. Then lseek there. */
		posb = pos / alignment * alignment;
		poso = pos % alignment;

		pos = lseek (devfd, posb, SEEK_SET);
		if (pos == -1){
			char errmsg[500];
			sprintf (errmsg, "Failed to lseek to 0x%jx %ju", (uintmax_t) posb,
				(uintmax_t) posb);
			perror (errmsg);
			return -1;
		}

		/* Align buffer size to system page size. */
		sizeb = ((size - 1) / alignment + 1) * alignment;

		/* Start recording duration of the read() */
		if (timed) gettimeofday(&reqa, NULL);

		/* Read data into the buffer. */
		r = read(devfd, buf, sizeb);

		if (timed) gettimeofday(&reqb, NULL);

		if (r < (ssize_t) 0 || r != (ssize_t) sizeb){
			perror("Failed to read from device");
			return -1;
		}
	} else {
		posb = lseek (devfd, pos, SEEK_SET);

		if (posb == -1){
			char errmsg[500];
			sprintf (errmsg, "Failed to lseek to 0x%jx %ju", (uintmax_t) pos,
				(uintmax_t) pos);
			perror (errmsg);
			return -1;
		}

		/* Read data directly into the buffer. */
		r = read(devfd, buf, size);
		if (r < (ssize_t) 0 || r != (ssize_t) size){
			perror("Failed to read from device");
			return -1;
		}
	}

	if (verbose)
		printf("Offset = %lu sectors, %lu bytes\n", pos / 512, pos);
	return r;
}

int main(int argc, char **argv)
{
	char *buffer;
	int i, c, retval, groupcount, groupsize;
	uint32_t sectorsize;
	uint64_t numblocks;
	off64_t offset;
	struct timespec rqtp;
	float rands[RANDNUM], times[RANDNUM];
	char *devname = NULL;

	/* Allocate buffer aligned in memory */
	if (posix_memalign((void **) &buffer, sysconf(_SC_PAGESIZE), sizeof(char)*2*BLOCKNUM)) {
		printf("Failed to posix_memalign buffer size.\n");
		exit(EXIT_FAILURE);
	}

	out[0] = inp[0] = '\0';

	while (1) {
		int option_index = 0;
		
		c = getopt_long(argc, argv, "w:dhvp:i:o:c:t:s:S:P:Z:", long_options,
						&option_index);
		if (c == -1)
			break;
		
		switch (c) {
			case 'w': /* -w or --workload */
				if (!strcmp(optarg,"RAND") || !strcmp(optarg,"0"))
					workload = RANDWKLD;
				else if (!strcmp(optarg,"SEQL") || !strcmp(optarg,"1"))
					workload = SEQLWKLD;
				else if (!strcmp(optarg,"SCRUB") || !strcmp(optarg,"2"))
					workload = SCRUBDEV;
				else if (!strcmp(optarg,"CACHE") || !strcmp(optarg,"3"))
					workload = TESTCACHE;
				break;
			case 'c': /* -c or --count */
				maxcount = atoi(optarg);
				if (maxcount < 0) {
					fprintf(stderr, "bad argument to '--count'\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 't': /* -t or --runtime */
				runtime = atoi(optarg);
				if (runtime < 0) {
					fprintf(stderr, "bad argument to '--runtime'\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'd': /* -d or --direct */
				direct = 1;
				break;
			case 'h': /* -h or --help */
			case '?':
				usage();
				exit(EXIT_SUCCESS);
			case 'v': /* -v or --verbose */
				++verbose;
				break;
			case 'p': /* -p or --priority */
				if (!strcmp(optarg,"IDLE") || !strcmp(optarg,"0"))
					priority = IDLEPRIO;
				else if (!strcmp(optarg,"DEF") || !strcmp(optarg,"1"))
					priority = DEFPRIO;
				else if (!strcmp(optarg,"RT") || !strcmp(optarg,"2"))
					priority = RTPRIO;
				break;
			case 'i': /* -i or --input */
				if (optarg == NULL) {
					fprintf(stderr, "bad argument to '--input'\n");
					exit(EXIT_FAILURE);
				}
				strcpy(inp, optarg);
				break;
			case 'o': /* -o or --output */
				if (optarg == NULL) {
					fprintf(stderr, "bad argument to '--output'\n");
					exit(EXIT_FAILURE);
				}
				strcpy(out, optarg);
				break;
			case 's': /* -s or --seed */
				seed = atoi(optarg);
				if (seed < 0) {
					fprintf(stderr, "bad argument to '--seed'\n");
					exit(EXIT_FAILURE);
				}
				break;
			case 'S': /* -S or --sectors */
				sectorcount = (uint64_t) atoi(optarg);
				break;
			case 'P': /* -P or --start */
				sectorstart = (uint64_t) atoi(optarg);
				break;
			case 'Z': /* -Z or --thinktime */
				thinkprob = atof(optarg);
				if (thinkprob < 0.0) {
					fprintf(stderr, "bad argument to '--thinktime'\n");
					exit(EXIT_FAILURE);
				}
				break;
			default:
				fprintf(stderr, "unrecognised option code 0x%x ??\n", c);
				usage();
				exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		if (NULL == devname) {
			devname = argv[optind];
			++optind;
		}
		if (optind < argc) {
			for (; optind < argc; ++optind)
				fprintf(stderr, "Unexpected extra argument: %s\n", argv[optind]);
			usage();
			exit(EXIT_FAILURE);
		}
	}

	if (devname == NULL) {
		fprintf(stderr, "no device specified\n");
		usage();
		exit(EXIT_FAILURE);
	}

	/* Set the seed to a specific value */
	srand(seed);

	/* Set the I/O scheduling class and priority for the scrubber */
	if (priority == IDLEPRIO) {
		retval = ioprio_set(IOPRIO_WHO_PROCESS, (int) getpid(),
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE,0));
		handle("ioprio_set", retval < 0);
	} else if (priority == RTPRIO) {
		retval = ioprio_set(IOPRIO_WHO_PROCESS, (int) getpid(),
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT,0));
		handle("ioprio_set", retval < 0);
	}

	setvbuf(stdout, NULL, _IONBF, 0);

	if (inp[0] != '\0') {
		inpf = fopen(inp, "r");
		if (inpf == NULL) {
			fprintf(stderr, "failed to open input file '%s'\n", inp);
			exit(EXIT_FAILURE);
		}
	}

	if (out[0] != '\0') {
		outf = fopen(out, "w");
		if (outf == NULL) {
			fprintf(stderr, "failed to open output file '%s'\n", out);
			exit(EXIT_FAILURE);
		}
	}

	if (inpf != NULL) {
		for (num=0; num<RANDNUM; num++)
			fscanf(inpf, "%f", &times[num]);
	} /* else {
		for (num=0; num<RANDNUM; num++)
			times[num] = rand() / (float) RAND_MAX;
	} */

	if (direct)
		dev = open(devname, O_RDONLY|O_DIRECT);
	else
		dev = open(devname, O_RDONLY);
	handle("open", dev < 0);

	/* Retrieve device's size in *sectors* */
	retval = ioctl(dev, BLKGETSIZE, &numblocks);
	handle("ioctl (devsize)", retval == -1);
	if (sectorcount && (sectorstart + sectorcount) < numblocks)
		numblocks = sectorcount;

	/* Retrieve sector size in *bytes* -- most probably 512b */
	retval = ioctl(dev, BLKSSZGET, &sectorsize);
	handle("ioctl (sector)", retval == -1);

	/* Convert number of blocks to number of *requests* */
	numblocks = numblocks / (BLOCKNUM / sectorsize);
	//fprintf(stderr, "Number of blocks = %lu (sector = %u)\n", numblocks, sectorsize);
	handle("numblocks <= 0", numblocks <= 0);

	if (verbose)
		printf("Benchmarking %s [%luMB], press Ctrl-C to terminate.\n",
		       devname, numblocks * BLOCKNUM / (1024*1024));

	/* Start timing program execution */
	gettimeofday(&start, NULL);
	//signal(SIGALRM, &update);
	signal(SIGINT, &done);
	//alarm(1);

	num = 0;
	offset = 0;
	total = 0.0;

	/* Group count and size for CLUST workload */
	groupcount = 0;
	//groupsize = (int) (50 * (rand() / (float) RAND_MAX));
	groupsize = (int) 128;

	/* lap0 and lap1 make sure think time is included when recording */
	lap0.tv_sec = start.tv_sec;
	lap0.tv_usec = start.tv_usec;

	if (workload == TESTCACHE) {
		/* Test whether the cache is on */
		struct timeval ra, rb;
		int k;
		long double total1 = 0.0, total2 = 0.0;

		for (k = 0; k < 16384; k++)
			retval = segread(dev, buffer, sysconf(_SC_PAGESIZE),
					k * sysconf(_SC_PAGESIZE), 1, 0);

		for (k = 0; k < 50; k++) {
			retval = segread(dev, buffer, sysconf(_SC_PAGESIZE),
					testblks[k], 1, 1);
			testtimes1[k] = timediff(ra, rb);
			total1 += testtimes1[k];
		}

		for (k = 0; k < 50; k++) {
			retval = segread(dev, buffer, sysconf(_SC_PAGESIZE),
					testblks[k] + sysconf(_SC_PAGESIZE), 1, 1);
			testtimes2[k] = timediff(ra, rb);
			total2 += testtimes2[k];
		}

		for (k = 0; k < 50; k++) {
			fprintf(stdout, "Sector: %lu -- Self=%Lf, Next=%Lf\n",
				testblks[k], testtimes1[k], testtimes2[k]);
		}

		total1 /= 50.0;
		total2 /= 50.0;
		fprintf(stdout, "\nAverage:: Self=%Lf, Next=%Lf\n", total1, total2);
	} else {
		for (;;) {
			long delay;

			/* Only enter this code block when you're about to think */
			if (thinkprob && (rand() / (float) RAND_MAX) <= thinkprob) {
				//gettimeofday(&lap1, NULL);
				rqtp.tv_sec = 0;
				/* Introduce a delay analogous to the disk's rotational latency */
				delay = times[num++] * 10000000;
				if (num >= 199990) num = 0;
				if (delay >= 1000000000)
					rqtp.tv_nsec = 999999999;
				else
					rqtp.tv_nsec = delay;
				//if (VERBOSE)
				//	printf("Delaying work for %ld.\n", rqtp.tv_nsec);
				//total += timediff(lap0, lap1);
				nanosleep(&rqtp,NULL);
				//gettimeofday(&lap0, NULL);
			}

			/* If the workload is RAND, then choose the next sector in a uniform random manner */
			if (workload == RANDWKLD)
				offset = (off64_t) (numblocks * (rand() / (float) RAND_MAX));
			/* If the workload is CLUST, choose next consequent sector or pick a random one if
			   the group has been read */
			else if (workload == SEQLWKLD) {
				if (groupcount == groupsize) {
					offset = (off64_t) (numblocks * (rand() / (float) RAND_MAX));
					//groupsize = (int) (50 * (rand() / (float) RAND_MAX));
					groupcount = 0;
				} else groupcount++;
			}

			/* Convert offset from request no. to *bytes* and issue the read() */
			retval = segread(dev, buffer, BLOCKNUM, offset * BLOCKNUM, direct, 1);

			/* Keep the output in bytes as well, for readability */
			if (outf != NULL)
				/* fprintf(outf, "%lu\t%Lf\n", offset * BLOCKNUM, timediff(reqa, reqb)); */
				fprintf(outf, "%Lf\n", timediff(reqa, reqb));
			totaltime += timediff(reqa, reqb);

			handle("segread", retval < 0);
			if ((maxcount && count >= maxcount) || (num >= RANDNUM-3))
				done();
			count++;

			if (workload == SEQLWKLD || workload == SCRUBDEV)
				offset = (off64_t) (offset + 1) % numblocks;
		}
	}
	/* notreached */
}

