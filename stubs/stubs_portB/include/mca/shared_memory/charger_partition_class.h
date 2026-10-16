/* SPDX-License-Identifier: GPL-2.0 */
/*
 * charger_partition_class.h - reconstructed (Xiaomi charger partition API).  Partition layout and enumerator values are proven by
 * the stock charger_partition.ko (dada); names follow the XRING template.
 */
#ifndef __CHARGER_PARTITION_CLASS_H__
#define __CHARGER_PARTITION_CLASS_H__

#include <linux/types.h>

#define CHARGER_PARTITION_MAGIC		0x20240725

enum charger_partition_host_type {
	CHARGER_PARTITION_HOST_UNKNOWN = 0,
	CHARGER_PARTITION_HOST_KERNEL = 4,
	CHARGER_PARTITION_HOST_LAST = 8,
};

enum charger_partition_info_type {
	CHARGER_PARTITION_HEADER = 0,
	CHARGER_PARTITION_INFO_1 = 1,
	CHARGER_PARTITION_INFO_2 = 2,
	CHARGER_PARTITION_INFO_LAST = 11,
};

/* partition block 0 */
typedef struct {
	uint32_t magic;
	uint32_t version;
	uint32_t initialized;
	uint32_t avaliable;
} charger_partition_header;

/* partition block 1 (32 bytes) */
typedef struct {
	uint32_t power_off_mode;
	uint32_t zero_speed_mode;
	uint32_t mishow;
	uint32_t double85;
	uint32_t remove_temp_limit;
	int32_t soc_limit;
	uint32_t memory_test;
	uint32_t reserved;
} charger_partition_info_1;

/* partition block 2 (32 bytes) */
typedef struct {
	uint32_t eu_mode;
	uint32_t ocd_count;
	uint32_t hocd_count;
	uint32_t cuv_count;
	uint32_t hcuv_count;
	uint32_t hscd_count;
	uint32_t test;
	uint32_t reserved;
} charger_partition_info_2;

int charger_partition_alloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size);
int charger_partition_dealloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size);
void *charger_partition_read(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size);
int charger_partition_write(u8 charger_partition_host_type, u8 charger_partition_info_type, void *buf, uint32_t size);
int charger_partition_read_double85(int *value);
int charger_partition_write_double85(int value);
int charger_partition_read_remove_temp_limit(int *value);
int charger_partition_write_remove_temp_limit(int value);
int charger_partition_read_memory_test(int *value);
int charger_partition_write_memory_test(int value);
int charger_partition_read_soc_limit(int *value);
int charger_partition_write_soc_limit(int value);
int charger_partition_get_eu_model(bool *is_eu_model);
int charger_partition_get_mishow(bool *mishow);
int charger_partition_read_cuv_count(int *cuv_count, int *hcuv_count);
int charger_partition_read_hscd_count(int *hscd_count);
int charger_partition_read_ocd_count(int *ocd_count, int *hocd_count);
int charger_partition_write_cuv_count(int cuv_count, int hcuv_count);
int charger_partition_write_hcsd_count(int hscd_count);
int charger_partition_write_ocd_count(int ocd_count, int hocd_count);

#endif
