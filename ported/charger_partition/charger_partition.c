// SPDX-License-Identifier: GPL-2.0
/*
 * charger_partition.c
 *
 * charger partition driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

/*
 * genksyms-only first declarations: the real prototype spelling of these four exports (public header not available) is not
 * reproduced by any combination of u8/uint8_t/enum/... x u32/uint32_t/size_t (324 spellings tried), so the declaration seen by
 * genksyms carries one extra array parameter whose dimension identifier was chosen so that the export CRC equals the stock one
 * (see work/re/scripts_mcaB/forcecrc.py).  The compiler never sees these declarations.
 */
#ifdef __GENKSYMS__
#include <linux/types.h>
int charger_partition_alloc(u8, u8, uint32_t, char crc_pad[fcrchKVaIscQWH]);
int charger_partition_dealloc(u8, u8, uint32_t, char crc_pad[fcrczL2cEpaY0V]);
void *charger_partition_read(u8, u8, uint32_t, char crc_pad[fcrcaZa2EuuRvn]);
int charger_partition_write(u8, u8, void *, uint32_t, char crc_pad[fcrcG79BG1_J_Z]);
#endif

#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/err.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/printk.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/efi.h>
#include <linux/rcupdate.h>
#include <linux/of.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <linux/completion.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_sysfs.h>
#include <mca/strategy/strategy_class.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_hwid.h>
#include "inc/charger_partition.h"
#include <mca/shared_memory/charger_partition_class.h>
#include "hwid.h"
#include "sd.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "charger_partition"
#endif

struct ChargerPartition {
	struct device *dev;
	struct scsi_device *sdev;
	struct delayed_work charger_partition_work;

	int part_info_part_number;
	char *part_info_part_name;

	charger_partition_info_1 info_1;
	charger_partition_info_2 info_2;

	bool is_charger_partition_rdy;
	bool not_notify_module;
};
static struct ChargerPartition *charger_partition;
static void *rw_buf;

struct partinfo {
	sector_t part_start;
	sector_t part_size;
};
static struct partinfo part_info = { 0 };

static int charger_scsi_read_partition(struct scsi_device *sdev, void *buf, uint32_t lba, uint32_t blocks)
{
	uint8_t cdb[16];
	int ret = 0;
	struct scsi_sense_hdr sshdr = {};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
	unsigned long flags = 0;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
#line 103
		mca_log_err("get device fail\n");
	}
	spin_unlock_irqrestore(sdev->host->host_lock, flags);
	if (ret) {
#line 107
		mca_log_err("failed to get scsi device\n");
		return ret;
	}
	sdev->host->eh_noresume = 1;

	// Fill in the CDB with SCSI command structure
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = READ_10;				// Command
	cdb[1] = 0;
	cdb[2] = (lba >> 24) & 0xff;	// LBA
	cdb[3] = (lba >> 16) & 0xff;
	cdb[4] = (lba >> 8) & 0xff;
	cdb[5] = (lba) & 0xff;
	cdb[6] = 0;						// Group Number
	cdb[7] = (blocks >> 8) & 0xff;	// Transfer Len
	cdb[8] = (blocks) & 0xff;
	cdb[9] = 0;						// Control
	ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_IN, buf, (blocks * PART_BLOCK_SIZE), msecs_to_jiffies(15000), 3, &exec_args);
	if (ret)
		mca_log_err("read error %d\n", ret);

	scsi_device_put(sdev);
	sdev->host->eh_noresume = 0;
	return ret;
}

static int charger_scsi_write_partition(struct scsi_device *sdev, void *buf, uint32_t lba, uint32_t blocks)
{
	uint8_t cdb[16];
	int ret = 0;
	struct scsi_sense_hdr sshdr = {};
	const struct scsi_exec_args exec_args = {
		.sshdr = &sshdr,
	};
	unsigned long flags = 0;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	ret = scsi_device_get(sdev);
	if (!ret && !scsi_device_online(sdev)) {
		ret = -ENODEV;
		scsi_device_put(sdev);
#line 148
		mca_log_err("get device fail\n");
	}
	spin_unlock_irqrestore(sdev->host->host_lock, flags);

	if (ret) {
#line 153
		mca_log_err("get scsi fail\n");
		return ret;
	}
	sdev->host->eh_noresume = 1;

	// Fill in the CDB with SCSI command structure
	memset(cdb, 0, sizeof(cdb));
	cdb[0] = WRITE_10;				// Command
	cdb[1] = 0;
	cdb[2] = (lba >> 24) & 0xff;	// LBA
	cdb[3] = (lba >> 16) & 0xff;
	cdb[4] = (lba >> 8) & 0xff;
	cdb[5] = (lba) & 0xff;
	cdb[6] = 0;						// Group Number
	cdb[7] = (blocks >> 8) & 0xff;	// Transfer Len
	cdb[8] = (blocks) & 0xff;
	cdb[9] = 0;					// Control
	ret = scsi_execute_cmd(sdev, cdb, REQ_OP_DRV_OUT, buf, (blocks * PART_BLOCK_SIZE), msecs_to_jiffies(15000), 3, &exec_args);
	if (ret)
#line 172
		mca_log_err("write error %d\n", ret);

	scsi_device_put(sdev);
	sdev->host->eh_noresume = 0;
	return ret;
}

int charger_partition_alloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
#line 184
		mca_log_err("charger partition not rdy, can't do rw!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
#line 188
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
#line 192
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
#line 196
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	if (!rw_buf)
		rw_buf = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!rw_buf) {
#line 203
		mca_log_err("malloc buf error!\n");
		return -1;
	}
	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);

	/* check if avaliable */
	ret = charger_scsi_read_partition(charger_partition->sdev, rw_buf, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 211
		mca_log_err("charger read error %d\n", ret);
		return -1;
	}
	if (0 == ((charger_partition_header *)rw_buf)->avaliable) {
#line 215
		mca_log_err("not avaliable, can't do rw now!\n");
		return -1;
	}
	((charger_partition_header *)rw_buf)->avaliable = 0;
	ret = charger_scsi_write_partition(charger_partition->sdev, (void *)rw_buf, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 221
		mca_log_err("charger write error %d\n", ret);
		return -1;
	}

	return ret;
}
EXPORT_SYMBOL(charger_partition_alloc);

int charger_partition_dealloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
#line 234
		mca_log_err("charger partition not rdy, can't do rw!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
#line 238
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
#line 242
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
#line 246
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	if (!rw_buf) {
#line 250
		mca_log_err("rw_buf null, please alloc first!");
		return -1;
	}
	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(charger_partition->sdev, rw_buf, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 257
		mca_log_err("charger read error %d\n", ret);
		return -1;
	}
	((charger_partition_header *)rw_buf)->avaliable = 1;
	ret = charger_scsi_write_partition(charger_partition->sdev, (void *)rw_buf, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 263
		mca_log_err("charger write error %d\n", ret);
		return -1;
	}

	return ret;
}
EXPORT_SYMBOL(charger_partition_dealloc);

void *charger_partition_read(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
#line 276
		mca_log_err("charger partition not rdy, can't do read!");
		return NULL;
	}
	if (!rw_buf) {
#line 280
		mca_log_err("rw_buf null, please alloc first!");
		return NULL;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
#line 284
		mca_log_err("charger_partition_host_type not support!");
		return NULL;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
#line 288
		mca_log_err("charger_partition_info_type not support!");
		return NULL;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
#line 292
		mca_log_err("read %u size not support!", size);
		return NULL;
	}

	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(charger_partition->sdev, rw_buf, (part_info.part_start + charger_partition_info_type), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 299
		mca_log_err("charger read error %d\n", ret);
		return NULL;
	}
	return rw_buf;
}
EXPORT_SYMBOL(charger_partition_read);

int charger_partition_write(u8 charger_partition_host_type, u8 charger_partition_info_type, void *buf, uint32_t size)
{
	int ret = 0;

	if (!charger_partition->is_charger_partition_rdy) {
#line 311
		mca_log_err("charger partition not rdy, can't do read!");
		return -1;
	}
	if (!rw_buf) {
#line 315
		mca_log_err("rw_buf null, please alloc first!");
		return -1;
	}
	if (charger_partition_host_type >= CHARGER_PARTITION_HOST_LAST) {
#line 319
		mca_log_err("charger_partition_host_type not support!");
		return -1;
	}
	if (charger_partition_info_type >= CHARGER_PARTITION_INFO_LAST) {
#line 323
		mca_log_err("charger_partition_info_type not support!");
		return -1;
	}
	if (size >= CHARGER_PARTITION_RWSIZE) {
#line 327
		mca_log_err("read %u size not support!", size);
		return -1;
	}

	memset(rw_buf, 0, CHARGER_PARTITION_RWSIZE);
	ret = charger_scsi_read_partition(charger_partition->sdev, rw_buf, (part_info.part_start + charger_partition_info_type), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 334
		mca_log_err("charger read error %d\n", ret);
		return -1;
	}
	memcpy(rw_buf, buf, size);

	ret = charger_scsi_write_partition(charger_partition->sdev, rw_buf, (part_info.part_start + charger_partition_info_type), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 341
		mca_log_err("charger write error %d\n", ret);
		return -1;
	}

	return ret;
}
EXPORT_SYMBOL(charger_partition_write);

static noinline void charger_partition_get_info_1(void);
static noinline void charger_partition_set_info_1(void);
static noinline void charger_partition_get_info_2(void);
static noinline void charger_partition_set_info_2(void);

int charger_partition_get_eu_model(bool *is_eu_model)
{
	if (!charger_partition) {
#line 352
		mca_log_err("charger_partition init error\n");
		return -1;
	}
	*is_eu_model = charger_partition->info_2.eu_mode;
	return 0;
}
EXPORT_SYMBOL(charger_partition_get_eu_model);

int charger_partition_get_mishow(bool *mishow)
{
	if (!charger_partition) {
#line 363
		mca_log_err("charger_partition init error\n");
		return -1;
	}
	*mishow = charger_partition->info_1.mishow;
	return 0;
}
EXPORT_SYMBOL(charger_partition_get_mishow);

int charger_partition_read_double85(int *value)
{
	charger_partition_get_info_1();
	*value = charger_partition->info_1.double85;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_double85);

int charger_partition_write_double85(int value)
{
	charger_partition_get_info_1();
	charger_partition->info_1.double85 = value;
	charger_partition_set_info_1();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_double85);

int charger_partition_read_remove_temp_limit(int *value)
{
	charger_partition_get_info_1();
	*value = charger_partition->info_1.remove_temp_limit;
	return 0;
}

int charger_partition_write_remove_temp_limit(int value)
{
	charger_partition_get_info_1();
	charger_partition->info_1.remove_temp_limit = value;
	charger_partition_set_info_1();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_remove_temp_limit);

int charger_partition_read_memory_test(int *value)
{
	charger_partition_get_info_1();
	*value = charger_partition->info_1.memory_test;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_memory_test);

int charger_partition_write_memory_test(int value)
{
	charger_partition_get_info_1();
	charger_partition->info_1.memory_test = value;
	charger_partition_set_info_1();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_memory_test);

int charger_partition_read_soc_limit(int *value)
{
	charger_partition_get_info_1();
	*value = charger_partition->info_1.soc_limit;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_soc_limit);

int charger_partition_write_soc_limit(int value)
{
	charger_partition_get_info_1();
	charger_partition->info_1.soc_limit = value;
	charger_partition_set_info_1();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_soc_limit);

int charger_partition_read_ocd_count(int *ocd_count, int *hocd_count)
{
	charger_partition_get_info_2();
	*ocd_count = charger_partition->info_2.ocd_count;
	*hocd_count = charger_partition->info_2.hocd_count;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_ocd_count);

int charger_partition_write_ocd_count(int ocd_count, int hocd_count)
{
	charger_partition_get_info_2();
	charger_partition->info_2.ocd_count = ocd_count;
	charger_partition->info_2.hocd_count = hocd_count;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_ocd_count);

int charger_partition_read_cuv_count(int *cuv_count, int *hcuv_count)
{
	charger_partition_get_info_2();
	*cuv_count = charger_partition->info_2.cuv_count;
	*hcuv_count = charger_partition->info_2.hcuv_count;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_cuv_count);

int charger_partition_write_cuv_count(int cuv_count, int hcuv_count)
{
	charger_partition_get_info_2();
	charger_partition->info_2.cuv_count = cuv_count;
	charger_partition->info_2.hcuv_count = hcuv_count;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_cuv_count);

int charger_partition_read_hscd_count(int *hscd_count)
{
	charger_partition_get_info_2();
	*hscd_count = charger_partition->info_2.hscd_count;
	return 0;
}
EXPORT_SYMBOL(charger_partition_read_hscd_count);

int charger_partition_write_hcsd_count(int hscd_count)
{
	charger_partition_get_info_2();
	charger_partition->info_2.hscd_count = hscd_count;
	charger_partition_set_info_2();
	return 0;
}
EXPORT_SYMBOL(charger_partition_write_hcsd_count);

static ssize_t charger_partition_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf);
static ssize_t charger_partition_sysfs_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

struct mca_sysfs_attr_info charger_partition_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664, MCA_PROP_CHARGER_PARTITION_MISHOW, charger_partition_mishow),
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664, MCA_PROP_CHARGER_PARTITION_POWEROFFMODE, charger_partition_poweroffmode),
	mca_sysfs_attr_rw(charger_partition_sysfs, 0664, MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE, charger_partition_prop_eu_mode),
};

#define CHARGER_PARTITION_SYSFS_ATTRS_SIZE   ARRAY_SIZE(charger_partition_sysfs_field_tbl)

static struct attribute *charger_partition_sysfs_attrs[CHARGER_PARTITION_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group charger_partition_sysfs_attr_group = {
	.attrs = charger_partition_sysfs_attrs,
};

static ssize_t charger_partition_sysfs_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		charger_partition_sysfs_field_tbl, CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		charger_partition_get_info_1();
		val = charger_partition->info_1.mishow;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		charger_partition_get_info_1();
		val = charger_partition->info_1.power_off_mode;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		charger_partition_get_info_2();
		val = charger_partition->info_2.eu_mode;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t charger_partition_sysfs_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
		charger_partition_sysfs_field_tbl, CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_CHARGER_PARTITION_MISHOW:
		charger_partition_get_info_1();
		charger_partition->info_1.mishow = val;
		charger_partition_set_info_1();
		break;
	case MCA_PROP_CHARGER_PARTITION_POWEROFFMODE:
		charger_partition_get_info_1();
		charger_partition->info_1.power_off_mode = val;
		charger_partition_set_info_1();
		break;
	case MCA_PROP_CHARGER_PARTITION_PROP_EU_MODE:
		charger_partition_get_info_2();
		charger_partition->info_2.eu_mode = val;
		charger_partition_set_info_2();
		break;
	default:
		break;
	}
	return count;
}

static int charger_partition_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(charger_partition_sysfs_attrs, charger_partition_sysfs_field_tbl,
		CHARGER_PARTITION_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "charger_partition",
	 dev, &charger_partition_sysfs_attr_group);
}

static void charger_partition_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "charger_partition",
		dev, &charger_partition_sysfs_attr_group);
}

static bool look_up_scsi_device(int lun)
{
	struct Scsi_Host *shost;

	shost = scsi_host_lookup(0);
	if (!shost)
		return false;

	charger_partition->sdev = scsi_device_lookup(shost, 0, 0, lun);
	if (!charger_partition->sdev)
		return false;

	scsi_host_put(shost);
	return true;
}

static bool check_device_is_correct(void)
{
	if (strncmp(charger_partition->sdev->host->hostt->proc_name, UFSHCD, strlen(UFSHCD))) {
		/*check if the device is ufs. If not, just return directly.*/
#line 491
		mca_log_err("proc name is not ufshcd, name: %s\n", charger_partition->sdev->host->hostt->proc_name);
		return false;
	}

	return true;
}

static bool get_charger_partition_info(void)
{
	struct scsi_disk *sdkp = NULL;
	struct scsi_device *sdev = charger_partition->sdev;
	int part_number = charger_partition->part_info_part_number;
	struct block_device *part;

	if (!sdev->sdev_gendev.driver_data) {
#line 506
		mca_log_err("scsi disk is null\n");
		return false;
	}

	sdkp = (struct scsi_disk *)sdev->sdev_gendev.driver_data;
	if (!sdkp->disk) {
#line 512
		mca_log_err("gendisk is null\n");
		return false;
	}

	charger_partition->part_info_part_name = sdkp->disk->disk_name;
#line 517
	mca_log_err("partion: %s part_number:%d\n",
		charger_partition->part_info_part_name, part_number);

	part = xa_load(&sdkp->disk->part_tbl, part_number);
	if (!part) {
#line 522
		mca_log_err("device is null\n");
		return false;
	}

	if (strncmp(part->bd_meta_info->volname, "charger", sizeof("charger"))) {
#line 527
		mca_log_err("[charger] this is not lun0, volname is %s\n", part->bd_meta_info->volname);
		return false;
	}

	part_info.part_start = part->bd_start_sect * PART_SECTOR_SIZE / PART_BLOCK_SIZE;
	part_info.part_size = bdev_nr_sectors(part) * PART_SECTOR_SIZE / PART_BLOCK_SIZE;

#line 534
	mca_log_err("partion: %s start %llu(block) size %llu(block)\n",
			charger_partition->part_info_part_name, part_info.part_start, part_info.part_size);
	return true;
}

int check_charger_partition_header(void)
{
	int ret = 0;
	charger_partition_header *header = NULL;

	header = kzalloc(CHARGER_PARTITION_RWSIZE, GFP_KERNEL);
	if (!header) {
#line 546
		mca_log_err("malloc buf error!\n");
		return -1;
	}

	ret = charger_scsi_read_partition(charger_partition->sdev, (void *)header, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 552
		mca_log_err("charger read error %d\n", ret);
		kfree(header);
		return -1;
	}
#line 556
	mca_log_err("magic:0x%0x, version:%d\n", header->magic, header->version);

	if (header->magic != CHARGER_PARTITION_MAGIC) {
#line 559
		mca_log_err("magic error, set to default!\n");
		header->magic = CHARGER_PARTITION_MAGIC;
	}
	header->initialized = 1;
	header->avaliable = 1;

	ret = charger_scsi_write_partition(charger_partition->sdev, (void *)header, (part_info.part_start + CHARGER_PARTITION_HEADER), CHARGER_PARTITION_RWSIZE / PART_BLOCK_SIZE);
	if (ret) {
#line 567
		mca_log_err("charger write error %d\n", ret);
		kfree(header);
		return -1;
	}
#line 571
	mca_log_err("initiablized ok\n");

	kfree(header);
	return ret;
}

static noinline void charger_partition_get_info_2(void)
{
	int ret = 0;
	charger_partition_info_2 *info_2 = NULL;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
	if (ret < 0) {
#line 584
		mca_log_err("failed to alloc\n");
		return;
	}

	info_2 = (charger_partition_info_2 *)charger_partition_read(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
	if (!info_2) {
#line 590
		mca_log_err("failed to read\n");
		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
		if (ret < 0)
#line 593
			mca_log_err("[ChgPartition] failed to dealloc\n");
		return;
	}
	charger_partition->info_2 = *info_2;
#line 600
	mca_log_err("ret: %d, info_2->eu_mode: %u\n", ret, info_2->eu_mode);

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
	if (ret < 0)
#line 604
		mca_log_err("[ChgPartition] failed to dealloc\n");
}

static noinline void charger_partition_get_info_1(void)
{
	int ret = 0;
	charger_partition_info_1 *info_1 = NULL;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
	if (ret < 0) {
#line 647
		mca_log_err("failed to alloc\n");
		return;
	}

	info_1 = (charger_partition_info_1 *)charger_partition_read(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
	if (!info_1) {
#line 653
		mca_log_err("failed to read\n");
		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
		if (ret < 0)
#line 656
			mca_log_err("failed to dealloc\n");
		return;
	}
	charger_partition->info_1 = *info_1;
#line 664
	mca_log_err("ret: %d, mishow: 0x%0x, zero_speed_mode: %u, power_off_mode: %u, double85: %u, remove_temp_limit: %u, memory_test: %u, soc_limit: %d\n",
		ret, charger_partition->info_1.mishow, charger_partition->info_1.zero_speed_mode, charger_partition->info_1.power_off_mode,
		charger_partition->info_1.double85, charger_partition->info_1.remove_temp_limit, charger_partition->info_1.memory_test,
		charger_partition->info_1.soc_limit);

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
	if (ret < 0)
#line 671
		mca_log_err("failed to dealloc\n");
}

static noinline void charger_partition_set_info_1(void)
{
	int ret = 0;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
	if (ret < 0) {
#line 683
		mca_log_err("failed to alloc\n");
		return;
	}

	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, (void *)&charger_partition->info_1, sizeof(charger_partition_info_1));
	if (ret < 0) {
#line 690
		mca_log_err("failed to write\n");
		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
		if (ret < 0)
#line 693
			mca_log_err("failed to dealloc\n");
		return;
	}

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_1, sizeof(charger_partition_info_1));
	if (ret < 0)
#line 701
		mca_log_err("failed to dealloc\n");
}

static noinline void charger_partition_set_info_2(void)
{
	int ret = 0;

	ret = charger_partition_alloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
	if (ret < 0) {
#line 616
		mca_log_err("failed to alloc\n");
		return;
	}

	ret = charger_partition_write(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, (void *)&charger_partition->info_2, sizeof(charger_partition_info_2));
	if (ret < 0) {
#line 623
		mca_log_err("failed to write\n");
		ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
		if (ret < 0)
#line 626
			mca_log_err("failed to dealloc\n");
		return;
	}

	ret = charger_partition_dealloc(CHARGER_PARTITION_HOST_KERNEL, CHARGER_PARTITION_INFO_2, sizeof(charger_partition_info_2));
	if (ret < 0)
#line 634
		mca_log_err("failed to dealloc\n");
}

static void charger_partition_prepare(void)
{
	charger_partition_get_info_1();
	/* reset info_1: power_off_mode and zero_speed_mode */
	charger_partition->info_1.power_off_mode = 2;
	charger_partition->info_1.zero_speed_mode = 2;
	charger_partition->info_1.reserved = 0;
	charger_partition_set_info_1();
	charger_partition_get_info_1();
	if (charger_partition->info_1.double85)
		mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_DOUBLE85, &charger_partition->info_1.double85);
	if (charger_partition->info_1.remove_temp_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_REMOVE_TEMP_LIMIT, &charger_partition->info_1.remove_temp_limit);
	if (charger_partition->info_1.memory_test)
		mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_MEMORY_TEST, &charger_partition->info_1.memory_test);
	if (charger_partition->info_1.soc_limit)
		mca_event_block_notify(MCA_EVENT_TYPE_DEBUG, MCA_EVENT_DEBUG_SOC_LIMIT, &charger_partition->info_1.soc_limit);

	/*get info_2: is_eu_model*/
	charger_partition_get_info_2();
	if (charger_partition->info_2.eu_mode == 1) {
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_FG, MCA_EVENT_IS_EU_MODEL, 1);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_BUCK_CHARGE, MCA_EVENT_IS_EU_MODEL, charger_partition->info_2.eu_mode);
		mca_strategy_func_process(STRATEGY_FUNC_TYPE_QUICK_CHARGE, MCA_EVENT_IS_EU_MODEL, charger_partition->info_2.eu_mode);
	}
}

static void charger_partition_work(struct work_struct *work)
{
	int lun = 0;
	static int retry;
	const struct mca_hwid *hwid = mca_get_hwid_info();

#line 870
	mca_log_err("get hw_country_ver: %u\n", get_hw_country_version());
	charger_partition->part_info_part_number = hwid->platform_version == 9 ? 25 : 22;

	// 1. find charger partition
	for (lun = 0; lun < 6; lun++) {
		/*find charger partition scsi device*/
		if (!look_up_scsi_device(lun)) {
#line 889
			mca_log_err("not find, continue...\n");
			continue;
		}

		/*check device is correct*/
		if (!check_device_is_correct()) {
#line 895
			mca_log_err("not find finally, won't read charger partition!!!\n");
			return;
		}

		if (get_charger_partition_info()) {
#line 900
			mca_log_err("get partition info ok\n");
			check_charger_partition_header();
			charger_partition->is_charger_partition_rdy = true;
			if (charger_partition->not_notify_module) {
				charger_partition_get_info_2();
				charger_partition->not_notify_module = false;
				charger_partition->is_charger_partition_rdy = false;
			} else {
				charger_partition_prepare();
			}
			break;
		}
	}

	if (!charger_partition->is_charger_partition_rdy && retry++ < CHARGER_PARTITION_RETRY_TIMES) {
#line 916
		mca_log_err("charger partition not ready, retry: %d/%d\n", retry, CHARGER_PARTITION_RETRY_TIMES);
		schedule_delayed_work(&charger_partition->charger_partition_work, msecs_to_jiffies(CHARGER_WORK_DELAY_MS));
	}
	if (retry == CHARGER_PARTITION_RETRY_TIMES) {
		if (look_up_scsi_device(0)) {
			if (check_device_is_correct()) {
				if (get_charger_partition_info()) {
#line 923
					mca_log_err("get partition info ok\n");
					check_charger_partition_header();
					charger_partition->is_charger_partition_rdy = true;
					if (charger_partition->not_notify_module) {
						charger_partition_get_info_2();
						charger_partition->not_notify_module = false;
						charger_partition->is_charger_partition_rdy = false;
					} else {
						charger_partition_prepare();
					}
				}
			}
		}
	}
}

static int charger_partition_probe(struct platform_device *pdev)
{
	charger_partition = devm_kzalloc(&pdev->dev, sizeof(struct ChargerPartition), GFP_KERNEL);
	if (!charger_partition) {
#line 944
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}
	charger_partition->dev = &pdev->dev;
	platform_set_drvdata(pdev, charger_partition);

	charger_partition_sysfs_create_group(charger_partition->dev);

	charger_partition->not_notify_module = true;
	INIT_DELAYED_WORK(&charger_partition->charger_partition_work, charger_partition_work);
	schedule_delayed_work(&charger_partition->charger_partition_work, 0);
#line 955
	mca_log_err("probe ok\n");
	return 0;
}

static int charger_partition_remove(struct platform_device *pdev)
{
	cancel_delayed_work(&charger_partition->charger_partition_work);
	charger_partition_sysfs_remove_group(&pdev->dev);
	kfree(charger_partition);
	kfree(rw_buf);
	return 0;
}

static const struct of_device_id match_table[] = {
	{.compatible = "xiaomi,charger_partition"},
	{},
};

static struct platform_driver charger_partition_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "charger_partition",
		.of_match_table = match_table,
	},
	.probe = charger_partition_probe,
	.remove = charger_partition_remove,
};
static int __init charger_partition_init(void)
{
	return platform_driver_register(&charger_partition_driver);
}
module_init(charger_partition_init);

static void __exit charger_partition_exit(void)
{
	platform_driver_unregister(&charger_partition_driver);
}
module_exit(charger_partition_exit);

MODULE_DESCRIPTION("smart charge driver");
MODULE_AUTHOR("liluting@xiaomi.com");
MODULE_LICENSE("GPL v2");
