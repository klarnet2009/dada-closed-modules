// SPDX-License-Identifier: GPL-2.0
/*
 * ufs_ffu - UFS "Field Firmware Update" at boot (Xiaomi, HyperOS 4.0 stock module "Interface for xiaomi memory").
 * Reconstructed from the disassembly of the OS4 stock ufs_ffu.ko (kernel 6.6.57); no vendor source was available.
 *
 * Flow: a delayed work (3 jiffies after module init) finds UFS LUN0, locates the partition named "ffu" (partition
 * 5..19 of the disk whose partition 1 is called "switch"), reads a 4 KB header page from it and optionally up to
 * 15 firmware records (48-byte entry + CRC-32), merges them with the built-in table need_update_k[82], and when the
 * INQUIRY vendor/product/revision of the UFS device matches an entry it downloads the image with SCSI WRITE BUFFER
 * (mode 0xE, 512 KB chunks) and reboots with the reason "ffus" (or "ffuf" on failure).
 *
 * THIRD-PARTY FIRMWARE: the stock module embeds ~25 MB of UFS microcode (Hynix, Kioxia, Samsung, Micron, YMTC).
 * Those images are NOT part of this source: the table below only references them by symbol name and size, the
 * symbols are provided at build time (ufs_ffu_blobs.S, zero-filled placeholder).  Never publish the images.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/blkdev.h>
#include <linux/xarray.h>
#include <linux/reboot.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_eh.h>
#include <ufs/ufshcd.h>

#include "ufs_ffu_blobs.h"

/* the stock functions return int but have no return statement on several paths */
#pragma clang diagnostic ignored "-Wreturn-type"

/* provided by ufs_qcom / qcom-reboot-reason (stubs with the OS4 export CRCs are used at build time) */
int mi_scsi_execute_cmd(struct scsi_device *sdev, const unsigned char *cmd, blk_opf_t opf, void *buffer,
			unsigned int bufflen, int timeout, int retries, const struct scsi_exec_args *args);
void ufs_ffu_reboot_reason_reboot(const char *reason);

/* private struct scsi_disk of drivers/scsi/sd.h: only the members used here */
struct ffu_scsi_disk {
	struct scsi_device *device;
	struct device disk_dev;
	struct gendisk *disk;
};

struct ffu_data {
	struct delayed_work ffu_delayed_work;		/* +0 */
	struct scsi_device *sdev;			/* +136 */
	int part_info_part_number;			/* +144 */
	char *part_info_part_name;			/* +152 */
	u32 bootreason;					/* +160 */
	u32 batterstatus;				/* +164 */
	u8 off_charge_flag;				/* +168 */
	u8 one_time_entry;				/* +169 */
};							/* 176 bytes */

/* firmware table entry (48 bytes, also the layout of a record of the "ffu" partition) */
struct ffu_fw_info {
	char vendor[9];
	char product[17];
	char rev_from[5];
	char rev_to[5];
	u64 addr;		/* built-in: address of the image; partition record: byte offset in the partition */
	u32 size;
} __packed;

/* UFS device identity taken from the INQUIRY data */
struct ffu_dev_id {
	char vendor[9];
	char product[17];
	char rev[5];
};

/* first 36 bytes of the 4 KB header page of the "ffu" partition; records of 256 bytes start at +0x100 */
struct ffu_part_hdr {
	char flag[32];		/* "ffu" */
	u32 ffu_count;
};

static struct ffu_data *p_ffudata;

static struct {
	u64 start;	/* 4 KB blocks */
	u64 size;
} part_info;

/* three of the stock firmware length variables (xxd -i style); the images themselves are not in this tree */
unsigned int X205_Xiaomi_V7_Viking_UFS4_0_SKhynix_bin_len = 786432;
unsigned int Hynix_V8_UFS_4_T100_BIN_len = 786432;
unsigned int Xiaomi_V7_UFS4_0_T300_Downgrade_BridgeFW_bin_len = 786432;

static struct ffu_fw_info need_update_k[] = {
#include "ufs_ffu_table.h"
};

static int ufs_read(struct scsi_device *sdev, u8 *buf, u32 lba, u32 len)
{
	unsigned int nblocks = len >> 12;
	struct ufs_hba *hba;
	struct scsi_sense_hdr sshdr;
	struct scsi_exec_args args;
	unsigned int n;
	unsigned int bytes;
	unsigned long flags;
	unsigned char cmd[16] __attribute__((uninitialized));
	int ret = 0;

	unsigned int end = lba + nblocks;
	if (part_info.start + part_info.size < end)
		return -EINVAL;

	while (nblocks) {
		hba = shost_priv(sdev->host);
		memset(&sshdr, 0, sizeof(sshdr));
		memset(&args, 0, sizeof(args));
		args.sshdr = &sshdr;
		n = min_t(unsigned int, nblocks, 128);

		spin_lock_irqsave(hba->host->host_lock, flags);
		ret = scsi_device_get(sdev);
		if (!ret && !scsi_device_online(sdev)) {
			ret = -ENODEV;
			scsi_device_put(sdev);
			pr_err("get device fail\n");
		}
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		if (ret) {
			pr_err("[ufs_ffu] read partition error %d\n", ret);
			return 1;
		}

		hba->host->eh_noresume = 1;
		bytes = n << 12;
		memset(cmd, 0, 16);
		cmd[0] = READ_10;
		cmd[1] = 0;
		cmd[2] = lba >> 24;
		cmd[3] = lba >> 16;
		cmd[4] = lba >> 8;
		cmd[5] = lba;
		cmd[6] = 0;
		cmd[7] = n >> 8;
		cmd[8] = n;
		ret = mi_scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_IN, buf, bytes, 15 * HZ, 3, &args);
		if (ret)
			pr_err("[ufs_ffu] read error %d\n", ret);
		sdev_printk(KERN_ERR, sdev, "sense key:0x%x; asc:0x%x; ascq:0x%x\n", sshdr.sense_key, sshdr.asc, sshdr.ascq);
		scsi_device_put(sdev);
		buf += bytes;
		lba += n;
		nblocks -= n;
		hba->host->eh_noresume = 0;
		if (ret) {
			pr_err("[ufs_ffu] read partition error %d\n", ret);
			return 1;
		}
	}
	return 0;
}

/* writes one zeroed 4 KB block at the start of the "ffu" partition (twin of ufs_read, inlined into the caller) */
static int ufs_write(struct scsi_device *sdev, u8 *buf, u32 lba, u32 len)
{
	unsigned int nblocks = len >> 12;
	struct ufs_hba *hba;
	struct scsi_sense_hdr sshdr;
	struct scsi_exec_args args;
	unsigned int n;
	unsigned int bytes;
	unsigned long flags;
	unsigned char cmd[16] __attribute__((uninitialized));
	int ret = 0;

	unsigned int end = lba + nblocks;
	if (part_info.start + part_info.size < end)
		return -EINVAL;

	while (nblocks) {
		hba = shost_priv(sdev->host);
		memset(&sshdr, 0, sizeof(sshdr));
		memset(&args, 0, sizeof(args));
		args.sshdr = &sshdr;
		n = min_t(unsigned int, nblocks, 128);

		spin_lock_irqsave(hba->host->host_lock, flags);
		ret = scsi_device_get(sdev);
		if (!ret && !scsi_device_online(sdev)) {
			ret = -ENODEV;
			scsi_device_put(sdev);
			pr_err("get device fail\n");
		}
		spin_unlock_irqrestore(hba->host->host_lock, flags);
		if (ret) {
			pr_err("[ufs_ffu] write partition error %d\n", ret);
			return 1;
		}

		hba->host->eh_noresume = 1;
		bytes = n << 12;
		memset(cmd, 0, 16);
		cmd[0] = WRITE_10;
		cmd[1] = 0;
		cmd[2] = lba >> 24;
		cmd[3] = lba >> 16;
		cmd[4] = lba >> 8;
		cmd[5] = lba;
		cmd[6] = 0;
		cmd[7] = n >> 8;
		cmd[8] = n;
		ret = mi_scsi_execute_cmd(sdev, cmd, REQ_OP_DRV_OUT, buf, bytes, 15 * HZ, 3, &args);
		if (ret)
			pr_err("[ufs_ffu] write error %d\n", ret);
		sdev_printk(KERN_ERR, sdev, "sense key:0x%x; asc:0x%x; ascq:0x%x\n", sshdr.sense_key, sshdr.asc, sshdr.ascq);
		scsi_device_put(sdev);
		buf += bytes;
		lba += n;
		nblocks -= n;
		hba->host->eh_noresume = 0;
		if (ret) {
			pr_err("[ufs_ffu] write partition error %d\n", ret);
			return 1;
		}
	}
	return 0;
}

/* an entry that came from the partition (index >= 82) failed to flash: wipe the header block */
static int check_ffu_parition_fw_error(struct scsi_device *sdev, int idx)
{
	u8 *buf = kzalloc(4096, GFP_KERNEL);
	int ret;

	if (idx >= 82) {
		ret = ufs_write(sdev, buf, part_info.start, 4096);
		if (ret)
			pr_err("[ufs_ffu] write partion info error %d\n", ret);
	}
	kfree(buf);
}

/* SCSI WRITE BUFFER, mode 0xE (download microcode with offsets, save and defer activate) */
static int ufs_scsi_write_buf(struct scsi_device *sdev, u8 *buf, u32 offset, u32 len)
{
	struct ufs_hba *hba = shost_priv(sdev->host);
	struct scsi_sense_hdr sshdr = {};
	struct scsi_exec_args args = { .sshdr = &sshdr, .req_flags = BLK_MQ_REQ_PM };
	unsigned char cdb[10];
	unsigned long flags;
	int ret;

	spin_lock_irqsave(hba->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
		pr_err("get device fail\n");
	}
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	if (ret)
		return ret;

	hba->host->eh_noresume = 1;
	cdb[0] = WRITE_BUFFER;
	cdb[1] = 0x0e;
	cdb[2] = 0;
	cdb[3] = offset >> 16;
	cdb[4] = offset >> 8;
	cdb[5] = offset;
	cdb[6] = len >> 16;
	cdb[7] = len >> 8;
	cdb[8] = len;
	cdb[9] = 0;
	ret = mi_scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_OUT, buf, len, 15 * HZ, 0, &args);
	if (ret)
		sdev_printk(KERN_ERR, sdev, "WRITE BUFFER failed for firmware upgrade %d\n", ret);
	sdev_printk(KERN_ERR, sdev, "sense key:0x%x; asc:0x%x; ascq:0x%x\n", sshdr.sense_key, sshdr.asc, sshdr.ascq);
	scsi_device_put(sdev);
	hba->host->eh_noresume = 0;
	return ret;
}

static noinline int ufs_fw_update_write(struct scsi_device *sdev, int size, const u8 *fw)
{
	int offset, remain, ret = 0;
	u8 *buf;

	buf = kzalloc(size, GFP_KERNEL);
	if (!buf) {
		pr_err("ffu kzalloc fail.\n");
		return -1;
	}
	memcpy(buf, fw, size);
	pr_info("fw addr: %p, %p. total fw size: %d\n", buf, fw, size);

	for (offset = 0; offset + 0x80000 <= size; offset += 0x80000) {
		pr_info("ffu offset:%d, size%d\n", offset, 0x80000);
		ret = ufs_scsi_write_buf(sdev, buf + offset, offset, 0x80000);
		if (ret)
			goto out;
	}
	remain = size % 0x80000;
	if (remain) {
		pr_info("ffu offset:%d, size%d\n", offset, remain);
		ret = ufs_scsi_write_buf(sdev, buf + offset, offset, remain);
	}
out:
	kfree(buf);
	return ret;
}

/* CRC-32 (poly 0xEDB88320, init 0, no final inversion) over the firmware image */
static __always_inline u32 ffu_crc32(const u8 *data, u32 len)
{
	u32 crc = 0;
	u32 i;
	int j;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++)
			crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
	}
	return crc;
}

static __always_inline struct ffu_fw_info *ffu_load_table(struct scsi_device *sdev, struct ffu_dev_id *dev, int *np)
{
	struct ffu_part_hdr hdr = {};
	struct ffu_fw_info info = {};
	struct ffu_fw_info *table = need_update_k;
	int n = ARRAY_SIZE(need_update_k);
	u8 *page;
	unsigned int i;
	int ffu_count, ret;

	page = kmalloc(4096, GFP_KERNEL);
	memset(page, 0, 4096);
	ret = ufs_read(sdev, page, part_info.start, 4096);
	if (ret) {
		pr_err("[ufs_ffu] get partion info error %d\n", ret);
		goto builtin;
	}
	memcpy(&hdr, page, sizeof(hdr));
	if (strcmp(hdr.flag, "ffu")) {
		pr_err("[ufs_ffu] ufs_flag is not FFU %s\n", hdr.flag);
		goto builtin;
	}
	ffu_count = hdr.ffu_count;
	pr_notice("[ufs_ffu] ffu_count is %d\n", ffu_count);
	if (ffu_count) {
		table = kmalloc((ffu_count + 82) * sizeof(*table), GFP_KERNEL);
		memset(table, 0, (ffu_count + 82) * sizeof(*table));
		memcpy(table, need_update_k, sizeof(need_update_k));
		for (i = 0; i < ffu_count; i++) {
			u8 *rec = page + 0x100 + i * 0x100;
			u8 *fwbuf;
			u64 addr;
			int size;
			u32 crc;
			u64 blk;

			memcpy(&info, rec, sizeof(info));
			pr_notice("[ufs_ffu] get info from partition crc 0x%x addr 0x%llx size 0x%x\n",
				  *(u32 *)(rec + 0x30), info.addr, info.size);
			addr = info.addr;
			if (addr == 0 || addr % 4096) {
				pr_notice("[ufs_ffu] ffu bin is not 4K align 0x%llx\n", addr);
				continue;
			}
			if (strncmp(dev->vendor, info.vendor, strlen(info.vendor))) {
				pr_notice("[ufs_ffu] check fw vendor_id not matching %s\n", info.vendor);
				continue;
			}
			if (strncmp(dev->product, info.product, strlen(info.product))) {
				pr_notice("[ufs_ffu] check fw product_id not matching %s\n", info.product);
				continue;
			}
			if (!strncmp(dev->rev, info.rev_to, strlen(info.rev_to))) {
				pr_notice("[ufs_ffu] check fw product_rev_to not matching %s\n", info.rev_to);
				continue;
			}
			blk = addr >> 12;
			size = (info.size + 0xfff) & 0xfffff000;
			fwbuf = kmalloc(size, GFP_KERNEL);
			memset(fwbuf, 0, size);
			ret = ufs_read(sdev, fwbuf, part_info.start + blk, size);
			if (ret) {
				pr_err("[ufs_ffu] get fw bin info error %d\n", ret);
				continue;
			}
			crc = *(u32 *)(rec + 0x30);
			if (ffu_crc32(fwbuf, info.size) != crc) {
				pr_notice("[ufs_ffu] crc error 0x%x diff 0x%x\n", crc, ffu_crc32(fwbuf, info.size));
				kfree(fwbuf);
				continue;
			}
			info.addr = (u64)fwbuf;
			memcpy(&table[n], &info, sizeof(info));
			n++;
		}
		pr_info("[ufs_ffu] partion start %llu size %llu\n", part_info.start, part_info.size);
	}
builtin:
	kfree(page);
	*np = n;
	return table;
}

static int start_ffu(struct ffu_data *ffudata)
{
	struct ffu_dev_id dev = {};
	struct ffu_fw_info *table;
	int n;
	unsigned char *inq;
	unsigned int idx;
	int ret;

	inq = ((struct ufs_hba *)shost_priv(ffudata->sdev->host))->ufs_device_wlun->inquiry;
	memcpy(dev.vendor, inq + 8, 8);
	memcpy(dev.product, inq + 16, 16);
	memcpy(dev.rev, inq + 32, 4);
	pr_notice("[ufs_ffu] check ffu_arry, vendor:%s, product:%s, fw_rev:%s\n", dev.vendor, dev.product, dev.rev);
	table = ffu_load_table(ffudata->sdev, &dev, &n);

	for (idx = 0; idx < n; idx++) {
		struct ffu_fw_info *e = &table[idx];

		if (strncmp(dev.vendor, e->vendor, strlen(e->vendor)))
			continue;
		if (strncmp(dev.product, e->product, strlen(e->product)))
			continue;
		pr_info("match vendor:%s, product id:%s hw_sectors %d\n", dev.vendor, dev.product,
			ffudata->sdev->request_queue->limits.max_hw_sectors);
		if (!strncmp(dev.rev, e->rev_from, strlen(e->rev_from))) {
			pr_info("match product revision from:%s\n", dev.rev);
			pr_info("start write buffer\n");
			pr_info("fw_size = %d, address = %lld\n", e->size, e->addr);
			ret = ufs_fw_update_write(ffudata->sdev, e->size, (const u8 *)e->addr);
			if (!ret) {
				pr_notice("write buffer success\n");
				pr_info("clear fail flag, check revision after reboot\n");
				msleep(20);
				ufs_ffu_reboot_reason_reboot("ffus");
				emergency_restart();
			} else {
				pr_err("ufs ffu failed, don't clear fail flag, reboot directly\n");
				check_ffu_parition_fw_error(ffudata->sdev, idx);
				msleep(20);
				ufs_ffu_reboot_reason_reboot("ffuf");
				emergency_restart();
				pr_err("ufs ffu failed, do not reboot to avoid endless reboot\n");
			}
			break;
		}
		if (!strncmp(dev.rev, e->rev_to, strlen(e->rev_to)))
			pr_notice("ufs_ffu firmware is already the newest now\n");
	}
	if (table != need_update_k)
		kfree(table);
}

static void ffu_ffu_delayed_work(struct work_struct *work)
{
	struct ffu_data *ffudata = container_of(work, struct ffu_data, ffu_delayed_work.work);
	u32 bootreason, batterystatus;
	struct device_node *np;
	struct Scsi_Host *host;
	struct ffu_scsi_disk *sdkp;
	struct block_device *part;
	const char *proc_name;
	const char *msg;
	int lun, i;

	p_ffudata = ffudata;
	ffudata->off_charge_flag = 0;
	ffudata->one_time_entry = 0;
	ffudata->part_info_part_number = 15;

	bootreason = 0;
	batterystatus = 0;
	np = of_find_node_by_path("/memory");
	if (!np) {
		pr_err("[ufs_ffu] memory node in dts is null\n");
		return;
	}
	of_property_read_u32(np, "ufs_ffu_batterystatus", &batterystatus);
	of_property_read_u32(np, "ufs_ffu_bootreason", &bootreason);
	ffudata->bootreason = bootreason;
	ffudata->batterstatus = batterystatus;

	if (ffudata->batterstatus == 1)
		ffudata->off_charge_flag = 1;
	if (ffudata->off_charge_flag)
		pr_notice("ufs_ffu enter off charger\n");
	if (ffudata->off_charge_flag) {
		pr_err("[ufs_ffu] off_charge_flag is set\n");
		return;
	}

	for (lun = 0; lun < 6; lun++) {
		pr_notice("ufs_ffu lun = %d\n", lun);
		host = scsi_host_lookup(0);
		if (!host)
			continue;
		ffudata->sdev = scsi_device_lookup(host, 0, 0, lun);
		if (!ffudata->sdev)
			continue;
		scsi_host_put(host);

		if (ffudata->one_time_entry) {
			msg = KERN_NOTICE "[ufs_ffu] one time entry is true\n";
			goto out_msg;
		}
		if (ffudata->bootreason == 0x41) {
			msg = KERN_ERR "bootreason was set ffu in last boot. skip FFU.\n";
			goto out_msg;
		}
		proc_name = ffudata->sdev->host->hostt->proc_name;
		if (!proc_name) {
			msg = KERN_NOTICE "[ufs_ffu] proc name is null\n";
			goto out_msg;
		}
		if (strncmp(proc_name, "ufshcd", 6)) {
			msg = KERN_NOTICE "[ufs_ffu] proc name is not ufs\n";
			goto out_msg;
		}
		pr_err("[ufs_ffu] goto get partattion info \n");
		sdkp = dev_get_drvdata(&ffudata->sdev->sdev_gendev);
		if (!sdkp) {
			pr_err("[ufs_ffu] scsi disk is null\n");
			continue;
		}
		if (!sdkp->disk) {
			pr_err("[ufs_ffu] gendisk is null\n");
			continue;
		}
		ffudata->part_info_part_name = sdkp->disk->disk_name;
		part = xa_load(&sdkp->disk->part_tbl, 1);
		if (!part) {
			pr_err("[ufs_ffu] device is null\n");
			continue;
		}
		pr_notice("[ufs_ffu] check partation info \n");
		for (i = 0; i < 10 && !part->bd_meta_info; i++)
			msleep(200);
		if (!part->bd_meta_info) {
			pr_notice("[ufs_ffu] the partition has not been mounted yet\n");
			continue;
		}
		if (strncmp(part->bd_meta_info->volname, "switch", 7)) {
			pr_notice("[ufs_ffu] this is not lun0\n");
			continue;
		}
		for (i = 5; i < 20; i++) {
			part = xa_load(&sdkp->disk->part_tbl, i);
			if (!part || !part->bd_meta_info) {
				pr_err("[ufs_ffu] the partition %d has not been mounted yet\n", i);
				continue;
			}
			if (!strncmp(part->bd_meta_info->volname, "ffu", 4))
				break;
		}
		if (i == 20) {
			pr_err("[ufs_ffu] find ffu partition failed\n");
			continue;
		}
		pr_notice("[ufs_ffu] partion name: %s\n", part->bd_meta_info->volname);
		part_info.start = (part->bd_start_sect * 512) / 4096;
		part_info.size = (part->bd_nr_sectors * 512) / 4096;
		pr_warn("[ufs_ffu] partion: %s start %llu(block) size %llu(block)\n", ffudata->part_info_part_name,
			part_info.start, part_info.size);
		pr_err("[ufs_ffu] get partattion info comeplate\n");
		start_ffu(ffudata);
		break;
	}
	ffudata->one_time_entry = 1;
	if (ffudata->sdev)
		scsi_device_put(ffudata->sdev);
	return;
out_msg:
	_printk(msg);
}

static int __init ufs_ffu_init(void)
{
	struct ffu_data *ffudata = kzalloc(sizeof(*ffudata), GFP_KERNEL);

	pr_notice("ufs_ffu module init---\n ");
	INIT_DELAYED_WORK(&ffudata->ffu_delayed_work, ffu_ffu_delayed_work);
	queue_delayed_work(system_wq, &ffudata->ffu_delayed_work, 3);
	return 0;
}

static void __exit ufs_ffu_exit(void)
{
	cancel_delayed_work(&p_ffudata->ffu_delayed_work);
	kfree(p_ffudata);
	pr_notice("%s:exit!", __func__);
}

module_init(ufs_ffu_init);
module_exit(ufs_ffu_exit);

MODULE_DESCRIPTION("Interface for xiaomi memory");
MODULE_AUTHOR("lilin <lilin16@xiaomi.com>");
MODULE_LICENSE("GPL");
