#define _GNU_SOURCE /* Needed for O_DIRECT. */
#define _FILE_OFFSET_BITS 64 /* Needed for files > 2GB */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#define SEQLSCRUB 1
#define STAGSCRUB 2
#define BOTHSCRUB 3

const char* program_name; /* The name of the scrubber */
long long int segment_size = 0; /* The segment size in bytes */
long long int region_size = 0; /* The region size in bytes */
int technique = 0; /* The scrubbing technique used */
int verbose = 0; /* Whether to display verbose messages */
int debug = 0; /* Whether to display debug messages */

int read_errs = 0; /* Counts read errors that have occurred. */

/* Print usage information for this program to STREAM (typically
   stdout or stderr), and exit the program with EXIT_CODE. Does not
   return. */

void print_usage (FILE* stream, int exit_code)
{
  fprintf (stream, "Usage: %s [option] dev1 [ dev2 ... ]\n", program_name);
  fprintf (stream, 
	   "  -h  --help            Display this usage information.\n"
	   "  -r  --region size     Region size (in KB) used by stag. technique.\n"
	   "  -s  --segment size    Segment size (in KB) used by the scrubber.\n"
	   "  -t  --technique tech  Scrubbing technique to be used:\n"
	   "                        o SEQL or 0: Sequential scrubbing\n"
           "                        o STAG or 1: Staggered  scrubbing\n"
	   "                        o BOTH or 2: Sequential & Staggered\n"
	   "  -d  --debug           Print debugging-related messages.\n"
	   "  -v  --verbose         Print verbose messages.\n");
  exit (exit_code);
}

/* Open the device in DEVPATH and return the file descriptor */

int opendev (char *devpath)
{
  int fd;

  /* We need O_DIRECT to access the data directly on the disk,
     O_RDONLY since we only do reading, and O_NOATIME to (possibly)
     save us some time by skipping updates on file last access time */
  //fd = open (devpath, O_RDONLY | O_NOATIME | O_DIRECT);
  fd = open (devpath, O_RDONLY | O_NOATIME);
  if (fd == -1){
    perror ("open");
    return -1;
  }

  return fd;
}

/* Close device with descriptor FD. */

int closedev (int fd)
{
  int rval;

  rval = close (fd);
  if (rval == -1){
    perror("close");
    return -1;
  }

  return 0;
}

ssize_t segread(int devfd, void* b, size_t size, off_t pos)
{
  size_t alignment = sysconf (_SC_PAGESIZE);
  off_t posb, poso;
  size_t sizeb;
  void *buf = NULL;
  int err;
  ssize_t r;

  /* Divide pos to base pos, which is an aligned boundary,
     and offset pos, which is the offset from that boundary
     to the actual pos. Then lseek there. */
  posb = pos / alignment * alignment;
  poso = pos % alignment;

  pos = lseek (devfd, posb, SEEK_SET);
  if (pos == -1){
    char errmsg[500];
    sprintf (errmsg, "Failed to lseek to 0x%jx %ju", posb, posb);
    perror (errmsg);
    return -1;
  }

  /* Align buffer size to system page size. */
  sizeb = ((size - 1) / alignment + 1) * alignment;
  err = posix_memalign(&buf, alignment, sizeb);
  if (err){
    printf("Failed to posix_memalign buffer size.\n");
    return -1;
  }

  /* Read data into the buffer. */
  r = read(devfd, buf, sizeb);
  if (r < (ssize_t) 0 || r != (ssize_t) sizeb){
    perror("Failed to read from device");
    free(buf);
    read_errs++;
    return -1;
  }

  /* Copy data into the return array. */
  memcpy(b, buf+poso, size);
  free(buf);

  return r;
}

off_t lceil (off_t whole, off_t part){
  if (verbose)
    printf ("lceil :: Ceiling function for long integers.\n"
	    "  Debug info:\n"
	    "    Division: %lld / %lld\n"
	    "    Integer result: %lld\n"
	    "    Modulo result: %lld\n",
	    whole, part, whole/part, whole%part);
  if (whole % part == (off_t) 0){
    return whole/part;
  }else return ((off_t) 1 + whole/part);
}

int seqscrub (int devfd, char* b, off_t capacity)
{
  off_t pos;
  ssize_t bytes_read;

  /* Scrub sequentially in SEGMENT_SIZE chunks */
  pos = (off_t) 0;
  while (pos < capacity){

    /* DELAY ADDITION */
    /* nanosleep(5985,NULL); */

    if (pos + segment_size > capacity){
      if (segment_size > capacity){
	/* Segment size is larger than the capacity of the drive...
	   What to do?! */
	pos = (off_t) 0;
	bytes_read = segread (devfd, (void*) b, capacity, pos);
	if (bytes_read < 0){
	  perror("Sequential scrub failed (segment size > disk capacity)");
	  return -1;
	}
      }else{
	/* This is probably the last segment of the drive, and there aren't
	   enough bytes to fill it. Just backtrack, so that there are exactly
	   segment_size bytes to read. */
	pos = capacity - segment_size;
	bytes_read = segread (devfd, (void*) b, sizeof(char)*segment_size-1, pos);
	if (bytes_read < 0){
	  printf("An error occurred in 0x%jx %ju\n", pos, pos);
	  continue;
	}
      }
    }else{
      /* Read segment_size bytes to buffer. */
      bytes_read = segread (devfd, (void*) b, sizeof(char)*segment_size-1, pos);
      if (bytes_read < 0){
	printf("An error occurred in 0x%jx %ju\n", pos, pos);
	continue;
      }
    }
    /* For debugging purposes, we might want to print the first byte of every
       segment. */
    if (debug)
      printf ("Offset %lld: %02x (read %u)\n", pos, b[0], bytes_read);
    /* Everything went well, advance by the amount of bytes read */
    pos += (off_t) bytes_read;
  }

  return 0;
}

int stgscrub (int devfd, char* b, off_t capacity)
{
  off_t s, r, pos, regnum, segnum;
  ssize_t bytes_read;

  /* Calculate and round up the number of regions in the drive and the number
     of segments in each region. */
  regnum = lceil(capacity, region_size);
  segnum = lceil(region_size, segment_size);
  if (verbose)
    printf("There are %lld regions (%lldKB), with %lld segments (%lldKB) each, "
	   "in the drive of %lldKB capacity.\n", regnum, region_size/(off_t)1024,
	   segnum, segment_size/(off_t)1024, capacity/(off_t)1024);

  /* Scrub staggeredly in REGION_SIZE chunks of SEGMENT_SIZE segments */
  for (s = (off_t) 0; s < segnum; ++s){
    if (verbose)
      printf("Scrubbing segment: %lld/%lld\n", s+(off_t)1, segnum);
    for (r = (off_t) 0; r < regnum; ++r){

      /* DELAY ADDITION */
      /* nanosleep(5985,NULL); */

      /* Scrub the S-th segment of the R-th region. */
      pos = s*segment_size + r*region_size;
      if (pos > capacity) break;
      if (pos + segment_size > capacity){
	if (segment_size > capacity){
	  /* Segment size is larger than the capacity of the drive...
	     What to do?! */
	  pos = (off_t) 0;
	  bytes_read = segread (devfd, (void*) b, capacity, pos);
	  if (bytes_read < 0){
	    perror("Staggered scrub failed (segment size > disk capacity)");
	    return -1;
	  }
	}else{
	  /* This is probably the last segment of the drive, and there aren't
	     enough bytes to fill it. Just backtrack, so that there are exactly
	     segment_size bytes to read. */
	  pos = capacity - segment_size;
	  bytes_read = segread (devfd, (void*) b, sizeof(char)*segment_size-1, pos);
	  if (bytes_read < 0){
	    printf("An error occurred in 0x%jx %ju\n", pos, pos);
	    continue;
	  }
	}
      }else{
	/* Read segment_size bytes to buffer. */
	bytes_read = segread (devfd, (void*) b, sizeof(char)*segment_size-1, pos);
	if (bytes_read < 0){
	  printf("An error occurred in 0x%jx %ju\n", pos, pos);
	  continue;
	}
      }
      /* For debugging purposes, we might want to print the first byte of every
	 segment. */
      if (debug)
	printf ("Offset %lld: %02x (read %u)\n", pos, b[0], bytes_read);
      /* Everything went well, advance to the next segment in the next region. */
    }
  }

  return 0;
}

int scrubdev (int devfd)
{
  off_t capacity;
  char *b;

  /* Allocate the buffer array. */
  b = malloc(sizeof(char)*segment_size);
  if (b == NULL){
    perror("Failed to malloc");
    return -1;
  }

  /* Calculate the capacity of the drive. */
  capacity = lseek (devfd, 0, SEEK_END);
  if (capacity == (off_t) -1)
    perror ("Failed to lseek to SEEK_END");

  printf ("Size = %lld bytes.\n", capacity);

  if (technique == SEQLSCRUB || technique == BOTHSCRUB){
    read_errs = 0;
    if (seqscrub (devfd, b, capacity) < 0)
      printf("Sequential scrub failed. %d errors detected.\n", read_errs);
    else
      printf("Sequential scrub succeeded. %d errors detected.\n", read_errs);
  }
  if (technique == STAGSCRUB || technique == BOTHSCRUB){
    read_errs = 0;
    if (stgscrub (devfd, b, capacity) < 0)
      printf("Staggered scrub failed. %d errors detected.\n", read_errs);
    else
      printf("Staggered scrub succeeded. %d errors detected.\n", read_errs);
  }

  free(b);
  return 0;
}


int main (int argc, char* argv[])
{
  int next_option, devfd;
  char dev[256];

  /* A string listing valid short options letters. */
  const char* const short_options = "hr:s:t:dv";
  /* An array describing valid long options. */
  const struct option long_options[] = {
    { "help",      0, NULL, 'h' },
    { "region",    1, NULL, 'r' },
    { "segment",   1, NULL, 's' },
    { "technique", 1, NULL, 't' },
    { "debug",     0, NULL, 'd' },
    { "verbose",   0, NULL, 'v' },
    { NULL,        0, NULL,  0  } /* Required at end of array. */
  };

  /* Remember the name of the program, to incorporate in messages.
     The name is stored in argv[0]. */
  program_name = argv[0];

  do {
    next_option = getopt_long (argc, argv, short_options,
			       long_options, NULL);
    switch (next_option)
      {
      case 'h': /* -h or --help */
	print_usage (stdout, 0);
      case 'r': /* -r or --region */
	region_size = atol(optarg);
	if (region_size <= 0 ||
	    region_size == LONG_MAX){
	  printf("ERROR: Invalid region size.\n");
	  region_size = 0;
	}
	region_size *= 1024;
	break;
      case 's': /* -s or --segment */
	segment_size = atol(optarg);
	if (segment_size <= 0 ||
	    segment_size == LONG_MAX){
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
      case 'd': /* -d or --debug */
	debug = 1;
	break;
      case 'v': /* -v or --verbose */
	verbose = 1;
	break;
      case '?': /* The user specified an invalid option. */
	print_usage (stderr, 1);
      case -1: /* Done with options. */
	break;
      default: /* Something else: unexpected. */
	abort ();
      }
  }
  while (next_option != -1);

  /* Done with options. OPTIND points to first device path argument.
     Check the sanity/integrity of all arguments, print all of them
     if the verbose option was specified. */

  if (!technique){
    if (verbose)
      printf ("Scrubbing technique not defined. Using default (Both) instead.\n");
    technique = BOTHSCRUB;
  }

  if (!segment_size){
    if (verbose)
      printf ("Segment size not defined. Using default (2MB) instead.\n");
    segment_size = 2*1024*1024;
  }else
    if (verbose)
      printf("Using Segment Size = %lld\n", segment_size);

  if (!region_size){
    if (verbose)
      printf ("Region size not defined. Using default (100MB) instead.\n");
    region_size = 100*1024*1024;
  }else
    if (verbose)
      printf("Using Region Size = %lld\n", region_size);

  /* Iterate through the devices and start scrubbing.
     The actual scrubber code starts here. */
  int i;
  for (i = optind; i < argc; ++i){
    if (verbose)
      printf ("Examining Dev%d: %s\n", i-optind, argv[i]);

    /* Open device. */
    sprintf(dev, "%s", argv[i]);
    devfd = opendev (dev);
    if (devfd == -1){
      printf("Failed to open Dev%d: %s .Aborting...\n", i-optind, dev);
      continue;
    }

    /* Scrub device. */
    if (scrubdev(devfd) == -1)
      printf("Failed to initiate scrubbing process for Dev%d: %s .\n",
	     i-optind, dev);

    /* Close device. */
    if (closedev (devfd) == -1)
      printf("Failed to close Dev%d: %s .\n", i-optind, dev);
  }

  return 0;
}
