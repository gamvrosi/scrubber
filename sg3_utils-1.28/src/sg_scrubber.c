/*
 * Copyright (c) 2009-2010 George Amvrosiadis.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
//#define _GNU_SOURCE /* Needed for O_DIRECT. */
//#define _FILE_OFFSET_BITS 64 /* Needed for files > 2GB */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_cmds_basic.h"
#include "sg_cmds_extra.h"

#define SEQLSCRUB 1
#define STAGSCRUB 2
#define BOTHSCRUB 3

/* A utility program for the Linux OS SCSI subsystem.
 *
 * This program issues the SCSI VERIFY command to the given SCSI block device.
 */

static char * version_str = "0.90 20091126";
off_t segment_size = 2*1024*1024; /* The segment size in bytes */
off_t region_size = 128*1024*1024; /* The region size in bytes */
int technique = BOTHSCRUB;	/* The scrubbing technique used */
int verbose = 0;		/* Whether to display verbose messages */
int debug = 0;			/* Whether to display debug messages */
uint64_t lba = 0;		/* LBA to start scrubbing from */
int dpo = 0;			/* Page out state */
int vrprotect = 0;		/* Contents of VRP vrprotect field */
int64_t count = 0;		/* Count of blocks to verify */

int read_errs = 0; /* Counts read errors that have occurred. */

#define ME "sg_scrubber: "

static struct option long_options[] = {
	{"count", 1, 0, 'c'},
	{"dpo", 0, 0, 'd'},
	{"help", 0, 0, 'h'},
	{"lba", 1, 0, 'l'},
	{"region", 1, 0, 'r'},
	{"segment", 1, 0, 's'},
	{"technique", 1, 0, 't'},
	{"debug", 0, 0, 'D'},
	{"verbose", 0, 0, 'v'},
	{"version", 0, 0, 'V'},
	{"vrprotect", 1, 0, 'P'},
	{0, 0, 0, 0},
};

/*
 * Print usage information for this program to STREAM (typically
 * stdout or stderr), and exit the program with EXIT_CODE. Does not
 * return.
 */
static void usage(void)
{
	fprintf(stderr, "Usage: "
		"sg_scrubber [--count=COUNT] [--dpo] [--help] [--lba=LBA] [--region=SIZE]\n"
		"            [--segment=SIZE] [--technique=TECH] [--debug] [--verbose]\n"
		"            [--version] [--vrprotect=VRP] DEVICE\n"
		"  where:\n"
		"  -c  --count COUNT     Count of blocks to verify (def 0, which scrubs\n"
		"                        the whole device)\n"
		"  -d  --dpo             Disable page out (cache retention priority)\n"
		"  -l  --lba LBA         Logical block address to start verify (def 0)\n"
		"  -h  --help            Display this usage information.\n"
		"  -r  --region SIZE     Region size (in KB) used by stag. technique (def 128MB)\n"
		"  -s  --segment SIZE    Segment size (in KB) used by the scrubber (def 1MB)\n"
		"  -t  --technique TECH  Scrubbing technique to be used (def BOTH):\n"
		"                        o SEQL or 0: Sequential scrubbing\n"
		"                        o STAG or 1: Staggered  scrubbing\n"
		"                        o BOTH or 2: Sequential & Staggered\n"
		"  -D  --debug           Print debugging-related messages.\n"
		"  -v  --verbose         Increase verbosity\n"
		"  -V  --version         Print version string and exit\n"
		"  -P  --vrprotect VRP   Set vrprotect field to VRP (def 0)\n"
		"Scrubs the SCSI device using the SCSI VERIFY(10) command and one of the\n"
		"       available scrubbing techniques.\n"
	);
}

int segread(int devfd, off_t pos, off_t count)
{
	int res, ret = 0;
	int bytechk = 0;
	unsigned int info = 0;
	off_t num;

	for (; count > 0; count -= 65535, lba += 65535) {
		num = (count > 65535) ? 65535 : count;
		res = sg_ll_verify10(devfd, vrprotect, dpo, bytechk,
			(unsigned int)pos, num, NULL, 0, &info, 1, verbose);

		if (0 != res) {
			ret = res;

			switch (res) {
			case SG_LIB_CAT_NOT_READY:
				fprintf(stderr, "Verify(10) failed, device not ready\n");
				break;
			case SG_LIB_CAT_UNIT_ATTENTION:
				fprintf(stderr, "Verify(10), unit attention\n");
				break;
			case SG_LIB_CAT_ABORTED_COMMAND:
				fprintf(stderr, "Verify(10), aborted command\n");
				break;
			case SG_LIB_CAT_INVALID_OP:
				fprintf(stderr, "Verify(10) command not supported\n");
				break;
			case SG_LIB_CAT_ILLEGAL_REQ:
				fprintf(stderr, "bad field in Verify(10) cdb, near "
					"lba=0x%" PRIx64 "\n", lba);
				break;
			case SG_LIB_CAT_MEDIUM_HARD:
				fprintf(stderr, "medium or hardware error near "
					"lba=0x%" PRIx64 "\n", lba);
				break;
			case SG_LIB_CAT_MEDIUM_HARD_WITH_INFO:
				fprintf(stderr, "medium or hardware error, reported "
					"lba=0x%u\n", info);
				break;
			default:
				fprintf(stderr, "Verify(10) failed near lba=%" PRIu64
					" [0x%" PRIx64 "]\n", lba, lba);
				break;
			}

			++read_errs;
		}
	}

	return ret;
}

off_t lceil (off_t whole, off_t part)
{
	if (verbose)
		printf("lceil :: Ceiling function for long integers.\n"
			"  Debug info:\n"
			"    Division: %lld / %lld\n"
			"    Integer result: %lld\n"
			"    Modulo result: %lld\n",
			whole, part, whole/part, whole%part);

	if (whole % part == (off_t) 0)
		return whole/part;
	else
		return ((off_t) 1 + whole/part);
}

int seqscrub (int devfd, off_t capacity)
{
	int ret = 0;
	off_t pos, num;

	/* Scrub sequentially in SEGMENT_SIZE chunks */
	pos = (off_t) lba;

	if (!count)
		count = capacity;

	if (verbose)
		printf("Starting from %lld to %lld.\n", pos, count);

	while (pos < capacity) {
		if (pos + segment_size > capacity) {
			/* Either the segment_size is larger than the device
			 * (read device in one go), or the remaining blocks are
			 * less than segment_size (read just those). */
			num = (segment_size > capacity) ? capacity :
				(capacity-segment_size);
		} else {
			/* Verify segment_size blocks */
			num = segment_size;
		}

		ret = segread (devfd, pos, num);
		if (ret == SG_LIB_CAT_INVALID_OP) {
			fprintf(stderr, "TERMINAL ERROR: SCSIVerify command not supported!\n");
			return -1;
		}

		if (debug)
			printf ("Offset: %lld (reading %u)\n", pos, num);

		/* Everything went well, advance by the amount of bytes read */
		pos += num;
	}

	return 0;
}

int stgscrub (int devfd, off_t capacity)
{
	int ret = 0;
	off_t pos, num, s, r, regnum, segnum;

	/* Calculate and round up the number of regions in the drive and the
	   number of segments in each region */
	regnum = lceil(capacity, region_size);
	segnum = lceil(region_size, segment_size);
	if (verbose)
		printf("There are %lld regions (%lld blocks), with %lld "
			"segments (%lld blocks) each, in the drive of %lld "
			"capacity in blocks.\n", regnum, region_size/(off_t)1024,
			segnum, segment_size/(off_t)1024, capacity/(off_t)1024);

	/* Scrub staggeredly in REGION_SIZE chunks of SEGMENT_SIZE segments */
	for (s = (off_t) 0; s < segnum; ++s) {
		if (verbose)
			printf("Scrubbing segment: %lld/%lld\n", s+(off_t)1, segnum);

		for (r = (off_t) 0; r < regnum; ++r) {
			/* Scrub the S-th segment of the R-th region */
			pos = s*segment_size + r*region_size;
			if (pos > capacity)
				break;

			if (pos + segment_size > capacity) {
				/*
				 * Either the segment_size is larger than the
				 * device (read device in one go), or the
				 * remaining blocks are less than segment_size
				 * (read just those).
				 */
				num = (segment_size > capacity) ? capacity :
					(capacity-segment_size);
			} else {
				/* Verify segment_size blocks */
				num = segment_size;
			}

			ret = segread (devfd, pos, num);
			if (ret == SG_LIB_CAT_INVALID_OP) {
				fprintf(stderr, "TERMINAL ERROR: SCSIVerify command not supported!\n");
				return -1;
			}
		}
	}

	return 0;
}

int scrubdev (int devfd, const char *dev_name)
{
	int ret = 0;
	off_t capacity;
	struct stat buf;

	/* Calculate the capacity of the drive. */
	if ((ret = stat(dev_name, &buf))) {
		fprintf(stderr, ME "stat failed(%d): %s\n", ret, dev_name);
		return ret;
	}

	capacity = lseek (devfd, 0, SEEK_END);
	if (capacity == (off_t) -1)
		perror ("Failed to lseek to SEEK_END");

	printf("Size = %lld bytes, Block size = %ld\n", capacity,
		buf.st_blksize);

	/* Now, convert everything to block size */
	capacity = lceil(capacity, buf.st_blksize);
	region_size = lceil(region_size, buf.st_blksize);
	segment_size = lceil(segment_size, buf.st_blksize);
	if (verbose) {
		printf("Device  size in blocks = %lld.\n", capacity);
		printf("Region  size in blocks = %lld.\n", region_size);
		printf("Segment size in blocks = %lld.\n", segment_size);
	}

	if (technique == SEQLSCRUB || technique == BOTHSCRUB) {
		read_errs = 0;
		if (seqscrub (devfd, capacity) < 0)
			printf("Sequential scrub failed. %d errors detected.\n",
				read_errs);
		else
			printf("Sequential scrub succeeded. %d errors detected.\n",
				read_errs);
	}

	if (technique == STAGSCRUB || technique == BOTHSCRUB) {
		read_errs = 0;
		if (stgscrub (devfd, capacity) < 0)
			printf("Staggered scrub failed. %d errors detected.\n",
				read_errs);
		else
			printf("Staggered scrub succeeded. %d errors detected.\n",
				read_errs);
	}

	return ret;
}

int main(int argc, char * argv[])
{
	int sg_fd, res, c;
	int64_t ll;
	int64_t orig_count;
	uint64_t orig_lba;
	const char * device_name = NULL;
	int ret = 0;

	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "b:c:dDhl:r:s:t:P:vV", long_options,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'c': /* -c or --count */
			count = sg_get_llnum(optarg);
			if (count < 0) {
				fprintf(stderr, "bad argument to '--count'\n");
				return SG_LIB_SYNTAX_ERROR;
			}
			break;
		case 'd': /* -d or --dpo */
			dpo = 1;
			break;
		case 'h': /* -h or --help */
		case '?':
			usage();
			return 0;
		case 'l': /* -l or --lba */
			ll = sg_get_llnum(optarg);
			if (-1 == ll) {
				fprintf(stderr, "bad argument to '--lba'\n");
				return SG_LIB_SYNTAX_ERROR;
			}
			lba = (uint64_t)ll;
			break;
		case 'P': /* -P or --vrprotect */
			vrprotect = sg_get_num(optarg);
			if (-1 == vrprotect) {
				fprintf(stderr, "bad argument to '--vrprotect'\n");
				return SG_LIB_SYNTAX_ERROR;
			}
			if ((vrprotect < 0) || (vrprotect > 7)) {
				fprintf(stderr, "'--vrprotect' requires a value from 0 to "
					"7 (inclusive)\n");
				return SG_LIB_SYNTAX_ERROR;
			}
			break;
		case 'r': /* -r or --region */
			region_size = atol(optarg);
			if (region_size <= 0 || region_size == LONG_MAX) {
				printf("ERROR: Invalid region size.\n");
				region_size = 0;
			}
			region_size *= 1024;
			break;
		case 's': /* -s or --segment */
			segment_size = atol(optarg);
			if (segment_size <= 0 || segment_size == LONG_MAX) {
				printf("ERROR: Invalid segment size.\n");
				segment_size = 0;
			}
			segment_size *= 1024;
			break;
		case 't': /* -t or --technique */
			if (!strcmp(optarg,"SEQL") || !strcmp(optarg,"0"))
				technique = SEQLSCRUB;
			else if (!strcmp(optarg,"STAG") || !strcmp(optarg,"1"))
				technique = STAGSCRUB;
			else if (!strcmp(optarg,"BOTH") || !strcmp(optarg,"2"))
				technique = BOTHSCRUB;
			break;
		case 'D': /* -d or --debug */
			debug = 1;
			break;
		case 'v': /* -v or --verbose */
			++verbose;
			break;
		case 'V': /* -V or --version */
			fprintf(stderr, ME "version: %s\n", version_str);
			return 0;
		default:
			fprintf(stderr, "unrecognised option code 0x%x ??\n", c);
			usage();
			return SG_LIB_SYNTAX_ERROR;
		}
	}

	/* Done with options. OPTIND points to first device path argument.
	 * Check the sanity/integrity of all arguments, print all of them
	 * if the verbose option was specified. */
	if (verbose) {
		printf("Scrubbing technique used: ");
		switch (technique) {
		case SEQLSCRUB:
			printf("Sequential scrubbing.\n");
			break;
		case STAGSCRUB:
			printf("Staggered scrubbing.\n");
			break;
		case BOTHSCRUB:
			printf("Both (Staggered & Sequential scrubbing).\n");
			break;
		default:
			printf("Incorrectly defined, using default (Both).\n");
			technique = BOTHSCRUB;
			break;
		}

		printf("Using Segment Size = %lld\n", segment_size);
		printf("Using Region  Size = %lld\n", region_size);
	}

	if (optind < argc) {
		if (NULL == device_name) {
			device_name = argv[optind];
			++optind;
		}

		if (optind < argc) {
			for (; optind < argc; ++optind)
				fprintf(stderr, "Unexpected extra argument: %s\n",
					argv[optind]);
			usage();
			return SG_LIB_SYNTAX_ERROR;
		}
	}

	if (lba > 0xffffffffLLU) {
		fprintf(stderr, "'lba' cannot exceed 32 bits\n");
		usage();
		return SG_LIB_SYNTAX_ERROR;
	}

	orig_count = count;
	orig_lba = lba;

	if (NULL == device_name) {
		fprintf(stderr, "missing device name!\n");
		usage();
		return SG_LIB_SYNTAX_ERROR;
	}

	/* Open DEVICE, start scrubbing, and then close it.
	 * The actual scrubber code starts here. */

	/* Open device. */
	sg_fd = sg_cmds_open_device(device_name, 0 /* rw */, verbose);
	if (sg_fd < 0) {
		fprintf(stderr, ME "open error: %s: %s\n", device_name,
		safe_strerror(-sg_fd));
		return SG_LIB_FILE_ERROR;
	}

	/* Scrub device. */
	if ((ret = scrubdev(sg_fd,device_name)) == -1)
		fprintf(stderr, ME "scrub failed: %s\n", device_name);

	/* Print verbose info */
	if (verbose && (0 == ret) && (orig_count > 1))
		fprintf(stderr, "Verified %" PRId64 " [0x%" PRIx64 "] blocks "
				"from lba %" PRIu64 " [0x%" PRIx64 "]\n"
				"    without error\n", orig_count,
				(uint64_t)orig_count, orig_lba, orig_lba);

	/* Close device. */
	res = sg_cmds_close_device(sg_fd);
	if (res < 0) {
		fprintf(stderr, "close error: %s\n", safe_strerror(-res));
		if (0 == ret)
			return SG_LIB_FILE_ERROR;
	}

	return (ret >= 0) ? ret : SG_LIB_CAT_OTHER;
}

