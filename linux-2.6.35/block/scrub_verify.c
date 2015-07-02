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

#include <linux/kernel.h>
#include <linux/scrub.h>
#include <linux/blkdev.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>

#ifndef SAM_STAT_GOOD
/* The SCSI status codes as found in SAM-4 at www.t10.org */
#define SAM_STAT_CHECK_CONDITION 0x2
#define SAM_STAT_COMMAND_TERMINATED 0x22	/* obsolete in SAM-3 */
#endif

/* The SCSI sense key codes as found in SPC-4 at www.t10.org */
#define SPC_SK_NO_SENSE 0x0
#define SPC_SK_RECOVERED_ERROR 0x1
#define SPC_SK_NOT_READY 0x2
#define SPC_SK_MEDIUM_ERROR 0x3
#define SPC_SK_HARDWARE_ERROR 0x4
#define SPC_SK_ILLEGAL_REQUEST 0x5
#define SPC_SK_UNIT_ATTENTION 0x6
#define SPC_SK_BLANK_CHECK 0x8
#define SPC_SK_COPY_ABORTED 0xa
#define SPC_SK_ABORTED_COMMAND 0xb

/* Utilities can use these process status values for syntax errors and
 * file (device node) problems (e.g. not found or permissions). */

/* The sg_err_category_sense() function returns one of the following.
 * These may be used as process status values (on exit). Notice that
 * some of the lower values correspond to SCSI sense key values. */
#define SG_LIB_CAT_NOT_READY 2	/* interpreted from sense buffer */
#define SG_LIB_CAT_MEDIUM_HARD 3	/* medium or hardware error, blank check */
#define SG_LIB_CAT_MEDIUM_HARD_WITH_INFO 18	/* medium or hardware error sense key plus 'info' field */
#define SG_LIB_CAT_ILLEGAL_REQ 5	/* Illegal request (not invalid opcode) */
#define SG_LIB_CAT_UNIT_ATTENTION 6	/* interpreted from sense buffer */
#define SG_LIB_CAT_INVALID_OP 9		/* (Illegal request,) Invalid opcode */
#define SG_LIB_CAT_ABORTED_COMMAND 11	/* interpreted from sense buffer */
#define SG_LIB_FILE_ERROR 15
#define SG_LIB_CAT_NO_SENSE 20		/* sense data with key of "no sense" */
#define SG_LIB_CAT_RECOVERED 21	/* Successful command after recovered err */
#define SG_LIB_CAT_SENSE 98	/* Something else is in the sense buffer */
#define SG_LIB_CAT_OTHER 99	/* Some other error/warning has occurred */

#define SENSE_BUFF_LEN 32       /* Arbitrary, could be larger */
#define DEF_PT_TIMEOUT 60       /* 60 seconds */
#define DEF_TIMEOUT 60000       /* 60,000 millisecs (60 seconds) */

#define VERIFY10_CMD 0x2f
#define VERIFY10_CMDLEN 10

#define SG_LIB_DRIVER_MASK	0x0f
#define SG_LIB_DRIVER_SENSE	0x08

#define SCSI_PT_DO_BAD_PARAMS 1
#define SCSI_PT_DO_TIMEOUT 2

#define SCSI_PT_RESULT_GOOD 0
#define SCSI_PT_RESULT_STATUS 1 /* other than GOOD and CHECK CONDITION */
#define SCSI_PT_RESULT_SENSE 2
#define SCSI_PT_RESULT_TRANSPORT_ERR 3
#define SCSI_PT_RESULT_OS_ERR 4

/* Needed stuff from all around the source tree */

/* This is a slightly stretched SCSI sense "descriptor" format header.
 * The addition is to allow the 0x70 and 0x71 response codes. The idea
 * is to place the salient data of both "fixed" and "descriptor" sense
 * format into one structure to ease application processing.
 * The original sense buffer should be kept around for those cases
 * in which more information is required (e.g. the LBA of a MEDIUM ERROR). */
struct sg_scsi_sense_hdr {
	unsigned char response_code; /* permit: 0x0, 0x70, 0x71, 0x72, 0x73 */
	unsigned char sense_key;
	unsigned char asc;
	unsigned char ascq;
	unsigned char byte4;
	unsigned char byte5;
	unsigned char byte6;
	unsigned char additional_length;
};

struct sg_pt_scsi {
	struct sg_io_hdr io_hdr;
	int in_err;
	int os_err;
};

static int sg_scsi_normalize_sense(const unsigned char * sensep, int sb_len,
	struct sg_scsi_sense_hdr * sshp)
{
	if (sshp)
		memset(sshp, 0, sizeof(struct sg_scsi_sense_hdr));
	if ((NULL == sensep) || (0 == sb_len) || (0x70 != (0x70 & sensep[0])))
		return 0;
	if (sshp) {
		sshp->response_code = (0x7f & sensep[0]);
		if (sshp->response_code >= 0x72) {  /* descriptor format */
			if (sb_len > 1)
				sshp->sense_key = (0xf & sensep[1]);
			if (sb_len > 2)
				sshp->asc = sensep[2];
			if (sb_len > 3)
				sshp->ascq = sensep[3];
			if (sb_len > 7)
				sshp->additional_length = sensep[7];
		} else {                              /* fixed format */
			if (sb_len > 2)
				sshp->sense_key = (0xf & sensep[2]);
			if (sb_len > 7) {
				sb_len = (sb_len < (sensep[7] + 8)) ? sb_len :
					(sensep[7] + 8);
				if (sb_len > 12)
					sshp->asc = sensep[12];
				if (sb_len > 13)
					sshp->ascq = sensep[13];
			}
		}
	}
	return 1;
}

static int get_scsi_pt_result_category(const struct sg_pt_scsi * ptp)
{
	int dr_st = ptp->io_hdr.driver_status & SG_LIB_DRIVER_MASK;
	int scsi_st = ptp->io_hdr.status & 0x7e;

	if (ptp->os_err)
		return SCSI_PT_RESULT_OS_ERR;
	else if (ptp->io_hdr.host_status)
		return SCSI_PT_RESULT_TRANSPORT_ERR;
	else if (dr_st && (SG_LIB_DRIVER_SENSE != dr_st))
		return SCSI_PT_RESULT_TRANSPORT_ERR;
	else if ((SG_LIB_DRIVER_SENSE == dr_st) ||
		(SAM_STAT_CHECK_CONDITION == scsi_st) ||
		(SAM_STAT_COMMAND_TERMINATED == scsi_st))
		return SCSI_PT_RESULT_SENSE;
	else if (scsi_st)
		return SCSI_PT_RESULT_STATUS;
	else
		return SCSI_PT_RESULT_GOOD;
}

static void sg_get_scsi_status_str(int scsi_status, int buff_len, char * buff)
{
	const char * ccp;

	scsi_status &= 0x7e; /* sanitize as much as possible */
	switch (scsi_status) {
		case 0: ccp = "Good"; break;
		case 0x2: ccp = "Check Condition"; break;
		case 0x4: ccp = "Condition Met"; break;
		case 0x8: ccp = "Busy"; break;
		case 0x10: ccp = "Intermediate (obsolete)"; break;
		case 0x14: ccp = "Intermediate-Condition Met (obs)"; break;
		case 0x18: ccp = "Reservation Conflict"; break;
		case 0x22: ccp = "Command Terminated (obsolete)"; break;
		case 0x28: ccp = "Task set Full"; break;
		case 0x30: ccp = "ACA Active"; break;
		case 0x40: ccp = "Task Aborted"; break;
		default: ccp = "Unknown status"; break;
	}

	strncpy(buff, ccp, buff_len);
}

static int sg_err_category_sense(const unsigned char * sense_buffer, int sb_len)
{
	struct sg_scsi_sense_hdr ssh;

	if ((sense_buffer && (sb_len > 2)) &&
		(sg_scsi_normalize_sense(sense_buffer, sb_len, &ssh))) {
		switch (ssh.sense_key) {
			case SPC_SK_NO_SENSE:
				return SG_LIB_CAT_NO_SENSE;
			case SPC_SK_RECOVERED_ERROR:
				return SG_LIB_CAT_RECOVERED;
			case SPC_SK_NOT_READY:
				return SG_LIB_CAT_NOT_READY;
			case SPC_SK_MEDIUM_ERROR:
			case SPC_SK_HARDWARE_ERROR:
			case SPC_SK_BLANK_CHECK:
				return SG_LIB_CAT_MEDIUM_HARD;
			case SPC_SK_UNIT_ATTENTION:
				return SG_LIB_CAT_UNIT_ATTENTION;
				/* used to return SG_LIB_CAT_MEDIA_CHANGED when ssh.asc==0x28 */
			case SPC_SK_ILLEGAL_REQUEST:
				if ((0x20 == ssh.asc) && (0x0 == ssh.ascq))
					return SG_LIB_CAT_INVALID_OP;
				else
					return SG_LIB_CAT_ILLEGAL_REQ;
				break;
			case SPC_SK_ABORTED_COMMAND:
				return SG_LIB_CAT_ABORTED_COMMAND;
		}
	}

	return SG_LIB_CAT_SENSE;
}

static const unsigned char * sg_scsi_sense_desc_find(const unsigned char * sensep,
	int sense_len, int desc_type)
{
	int add_sen_len, add_len, desc_len, k;
	const unsigned char * descp;

	if ((sense_len < 8) || (0 == (add_sen_len = sensep[7])))
		return NULL;
	if ((sensep[0] < 0x72) || (sensep[0] > 0x73))
		return NULL;
	add_sen_len = (add_sen_len < (sense_len - 8)) ?
		add_sen_len : (sense_len - 8);
	descp = &sensep[8];

	for (desc_len = 0, k = 0; k < add_sen_len; k += desc_len) {
		descp += desc_len;
		add_len = (k < (add_sen_len - 1)) ? descp[1]: -1;
		desc_len = add_len + 2;
		if (descp[0] == desc_type)
			return descp;
		if (add_len < 0) /* short descriptor ?? */
			break;
	}

	return NULL;
}

static struct sg_pt_scsi * construct_scsi_pt_obj(void)
{
	struct sg_pt_scsi * ptp;

	ptp = (struct sg_pt_scsi *) kmalloc_node(sizeof(struct sg_pt_scsi),
		GFP_KERNEL | __GFP_ZERO, -1);
	if (ptp) {
		ptp->io_hdr.interface_id = 'S';
		ptp->io_hdr.dxfer_direction = SG_DXFER_NONE;
		ptp->io_hdr.iovec_count = 0;
		ptp->io_hdr.dxfer_len = 0;
	}

	return ptp;
}

static void set_scsi_pt_cdb(struct sg_pt_scsi * ptp, const unsigned char * cdb,
	int cdb_len)
{
	if (ptp->io_hdr.cmdp)
		++ptp->in_err;
	ptp->io_hdr.cmdp = (unsigned char *)cdb;
	ptp->io_hdr.cmd_len = cdb_len;
}

static void set_scsi_pt_sense(struct sg_pt_scsi * ptp, unsigned char * sense,
	int max_sense_len)
{
	if (ptp->io_hdr.sbp)
		++ptp->in_err;
	memset(sense, 0, max_sense_len);
	ptp->io_hdr.sbp = sense;
	ptp->io_hdr.mx_sb_len = max_sense_len;
}

/* Executes SCSI command (or at least forwards it to lower layers).
 * Clears os_err field prior to active call (whose result may set it
 * again). */
static int do_scsi_pt(struct sg_pt_scsi * ptp, struct gendisk *disk,
	int time_secs, int verbose)
{
	ptp->os_err = 0;

	if (ptp->in_err) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): Replicated or unused set_scsi_pt"
				"... functions\n", disk->disk_name);
		return SCSI_PT_DO_BAD_PARAMS;
	}

	if (NULL == ptp->io_hdr.cmdp) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): No SCSI command (cdb) given\n",
				disk->disk_name);
		return SCSI_PT_DO_BAD_PARAMS;
	}

	/* io_hdr.timeout is in milliseconds */
	ptp->io_hdr.timeout = ((time_secs > 0) ? (time_secs * 1000) : DEF_TIMEOUT);
	if (ptp->io_hdr.sbp && (ptp->io_hdr.mx_sb_len > 0))
		memset(ptp->io_hdr.sbp, 0, ptp->io_hdr.mx_sb_len);
	if ((ptp->os_err = scsi_kern_ioctl(disk->queue, disk, 0, SG_IO, &ptp->io_hdr)) < 0) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): ioctl(SG_IO) failed with os_err "
				"(errno) = %d\n", disk->disk_name, ptp->os_err);
		return -ptp->os_err;
	}

	return 0;
}

/* Returns -2 for sense data (may not be fatal), -1 for failed or the
 number of bytes fetched. If -2 returned then sense category
 output via 'o_sense_cat' pointer (if not NULL). Outputs to stderr if problems;
 depending on 'verbose' */
static int sg_cmds_process_resp(struct gendisk *disk, struct sg_pt_scsi * ptp,
	const char * leadin, int res, const unsigned char * sense_b,
	int verbose, int * o_sense_cat)
{
	int cat, duration, slen, scat;
	char b[512];

	if (NULL == leadin)
		leadin = "";
	if (res < 0) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): %s: pass through os error: %d\n",
				disk->disk_name, leadin, -res);
		return -1;
	} else if (SCSI_PT_DO_BAD_PARAMS == res) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): %s: bad pass through setup\n",
				disk->disk_name, leadin);
		return -1;
	} else if (SCSI_PT_DO_TIMEOUT == res) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): %s: pass through timeout\n",
				disk->disk_name, leadin);
		return -1;
	}
	if ((verbose > 1) && ((duration = ptp->io_hdr.duration) >= 0))
		printk(KERN_INFO "SCSIVerify (%s):      duration=%d ms\n", disk->disk_name,
			duration);
	switch ((cat = get_scsi_pt_result_category(ptp))) {
		case SCSI_PT_RESULT_GOOD:
			return 0;
		case SCSI_PT_RESULT_STATUS: /* other than GOOD and CHECK CONDITION */
			if (verbose > 2) {
				sg_get_scsi_status_str(ptp->io_hdr.status, sizeof(b), b);
				printk(KERN_INFO "SCSIVerify (%s): %s: scsi status: %s\n",
					disk->disk_name, leadin, b);
			}
			return -1;
		case SCSI_PT_RESULT_SENSE:
			slen = ptp->io_hdr.sb_len_wr;
			scat = sg_err_category_sense(sense_b, slen);
			if (o_sense_cat)
				*o_sense_cat = scat;
			return -2;
		case SCSI_PT_RESULT_TRANSPORT_ERR:
			if (verbose > 2)
				printk(KERN_INFO "SCSIVerify (%s): %s: transport error\n",
					disk->disk_name, leadin);
			return -1;
		case SCSI_PT_RESULT_OS_ERR:
			if (verbose > 2) 
				printk(KERN_INFO "SCSIVerify (%s): %s: os error\n", disk->disk_name,
					leadin);
			return -1;
		default:
			if (verbose > 2)
				printk(KERN_INFO "SCSIVerify (%s): %s: unknown pass through result "
					"category (%d)\n", disk->disk_name, leadin, cat);
			return -1;
	}
}

static int sg_get_sense_info_fld(const unsigned char * sensep, int sb_len,
	uint64_t * info_outp)
{
	int j;
	const unsigned char * ucp;
	uint64_t ull;

	if (info_outp)
		*info_outp = 0;
	if (sb_len < 7)
		return 0;
	switch (sensep[0] & 0x7f) {
		case 0x70:
		case 0x71:
			if (info_outp)
				*info_outp = ((unsigned int)sensep[3] << 24) + (sensep[4] << 16) +
				(sensep[5] << 8) + sensep[6];
			return (sensep[0] & 0x80) ? 1 : 0;
		case 0x72:
		case 0x73:
			ucp = sg_scsi_sense_desc_find(sensep, sb_len, 0 /* info desc */);
			if (ucp && (0xa == ucp[1])) {
				ull = 0;
				for (j = 0; j < 8; ++j) {
					if (j > 0)
						ull <<= 8;
					ull |= ucp[4 + j];
				}
				if (info_outp)
					*info_outp = ull;
				return !!(ucp[2] & 0x80);   /* since spc3r23 should be set */
			} else
				return 0;
		default:
			return 0;
	}
}

static void destruct_scsi_pt_obj(struct sg_pt_scsi * ptp)
{
	if (ptp)
		kfree(ptp);
}

/* Invokes a SCSI VERIFY (10) command (SBC and MMC).
 * Note that 'veri_len' is in blocks. Returns of 0 -> success,
 * SG_LIB_CAT_INVALID_OP -> Verify(10) not supported,
 * SG_LIB_CAT_ILLEGAL_REQ -> bad field in cdb, SG_LIB_CAT_UNIT_ATTENTION,
 * SG_LIB_CAT_MEDIUM_HARD -> medium or hardware error, no valid info,
 * SG_LIB_CAT_MEDIUM_HARD_WITH_INFO -> as previous, with valid info,
 * SG_LIB_CAT_NOT_READY -> device not ready, SG_LIB_CAT_ABORTED_COMMAND,
 * -1 -> other failure */
static int sg_ll_verify10(struct gendisk *disk, int vrprotect, int dpo,
	int bytechk, uint64_t lba, int veri_len, unsigned int * infop,
	int verbose)
{
	int k, res, ret, sense_cat;
	unsigned char vCmdBlk[VERIFY10_CMDLEN] =
		{VERIFY10_CMD, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char sense_b[SENSE_BUFF_LEN];
	struct sg_pt_scsi * ptp;

	vCmdBlk[1] = ((vrprotect & 0x7) << 5) | ((dpo & 0x1) << 4) |
		((bytechk & 0x1) << 1) ;
	vCmdBlk[2] = (unsigned char)((lba >> 24) & 0xff);
	vCmdBlk[3] = (unsigned char)((lba >> 16) & 0xff);
	vCmdBlk[4] = (unsigned char)((lba >> 8) & 0xff);
	vCmdBlk[5] = (unsigned char)(lba & 0xff);
	vCmdBlk[7] = (unsigned char)((veri_len >> 8) & 0xff);
	vCmdBlk[8] = (unsigned char)(veri_len & 0xff);

	if (verbose > 3) {
		printk(KERN_INFO "SCSIVerify (%s):    Verify(10) cdb: \n", disk->disk_name);
		for (k = 0; k < VERIFY10_CMDLEN; ++k)
			printk(KERN_INFO "SCSIVerify (%s):         %02x \n", disk->disk_name,
				vCmdBlk[k]);
	}

	ptp = construct_scsi_pt_obj();
	if (NULL == ptp) {
		if (verbose > 1)
			printk(KERN_INFO "SCSIVerify (%s): verify (10): out of memory\n",
				disk->disk_name);
		return -1;
	}

	set_scsi_pt_cdb(ptp, vCmdBlk, sizeof(vCmdBlk));
	set_scsi_pt_sense(ptp, sense_b, sizeof(sense_b));
	res = do_scsi_pt(ptp, disk, DEF_PT_TIMEOUT, verbose);
	ret = sg_cmds_process_resp(disk, ptp, "verify (10)", res, sense_b,
		verbose, &sense_cat);

	if (-1 == ret) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): verify (10): return code -1\n",
				disk->disk_name);
	} else if (-2 == ret) {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): verify (10): return code -2\n",
				disk->disk_name);

		switch (sense_cat) {
			case SG_LIB_CAT_NOT_READY:
			case SG_LIB_CAT_INVALID_OP:
			case SG_LIB_CAT_ILLEGAL_REQ:
			case SG_LIB_CAT_UNIT_ATTENTION:
			case SG_LIB_CAT_ABORTED_COMMAND:
				ret = sense_cat;
				break;
			case SG_LIB_CAT_RECOVERED:
			case SG_LIB_CAT_NO_SENSE:
				ret = 0;
				break;
			case SG_LIB_CAT_MEDIUM_HARD:
			{
				int valid, slen;
				uint64_t ull = 0;

				slen = ptp->io_hdr.sb_len_wr;
				valid = sg_get_sense_info_fld(sense_b, slen, &ull);
				if (valid) {
					if (infop)
						*infop = (unsigned int)ull;
					ret = SG_LIB_CAT_MEDIUM_HARD_WITH_INFO;
				} else
					ret = SG_LIB_CAT_MEDIUM_HARD;
			}
				break;
			default:
				ret = -1;
				break;
		}
	} else {
		if (verbose > 2)
			printk(KERN_INFO "SCSIVerify (%s): verify (10): return code  0\n",
				disk->disk_name);
		ret = 0;
	}

	destruct_scsi_pt_obj(ptp);
	return ret;
}

int scsi_verify(struct gendisk *disk, uint64_t lba, unsigned int count)
{
	struct disk_scrubber *s = disk->scrubber;
	int res = 0;
	int bytechk = 0;
	unsigned int info = 0;

	res = sg_ll_verify10(disk, s->vrprotect, s->dpo, bytechk,
				lba, count, &info, s->verbose);

	if (0 != res) {
		switch (res) {
			case SG_LIB_CAT_NOT_READY:
				printk(KERN_INFO "SCSIVerify (%s): Verify(10) failed, device not "
					"ready\n", disk->disk_name);
				break;
			case SG_LIB_CAT_UNIT_ATTENTION:
				printk(KERN_INFO "SCSIVerify (%s): Verify(10), unit attention\n",
					disk->disk_name);
				break;
			case SG_LIB_CAT_ABORTED_COMMAND:
				printk(KERN_INFO "SCSIVerify (%s): Verify(10), aborted command\n",
					disk->disk_name);
				break;
			case SG_LIB_CAT_INVALID_OP:
				printk(KERN_INFO "SCSIVerify (%s): Verify(10) command not supported"
					"\n", disk->disk_name);
				break;
			case SG_LIB_CAT_ILLEGAL_REQ:
				printk(KERN_INFO "SCSIVerify (%s): bad field in Verify(10) cdb, "
						"near lba=0x%llu\n", disk->disk_name, lba);
				break;
			case SG_LIB_CAT_MEDIUM_HARD:
				printk(KERN_INFO "SCSIVerify (%s): medium or hardware error near "
						"lba=0x%llu\n", disk->disk_name, lba);
				break;
			case SG_LIB_CAT_MEDIUM_HARD_WITH_INFO:
				printk(KERN_INFO "SCSIVerify (%s): medium or hardware error, reported"
						" lba=0x%u\n", disk->disk_name, info);
				break;
			default:
				printk(KERN_INFO "SCSIVerify (%s): Verify(10) failed near lba=%llu "
						"[0x%llu]\n", disk->disk_name, lba, lba);
				break;
            	}
        }
	return (res >= 0) ? res : SG_LIB_CAT_OTHER;
}

