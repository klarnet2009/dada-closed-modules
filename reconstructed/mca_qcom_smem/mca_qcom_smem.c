// SPDX-License-Identifier: GPL-2.0
/* Reconstructed from the stock mca_qcom_smem.ko (Xiaomi 15 "dada") by reverse engineering; not original source. */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/soc/qcom/smem.h>
#include "mca_log.h"

#define SMEM_ITEM_ID	81
#define SMEM_ITEM_SIZE	40

/* 40-byte shared state entry published by the bootloader (UEFI); offsets taken from the disassembly */
struct smem_batt_state {
	u8 reserved0[0x20];
	u32 zero_speed_start_mode;
	u8 verify_bits;	/* bit5: battery verified, bit4: verified chip ok */
	u8 cell_sn;
	u8 reserved1[0x12];
};


























int get_smem_battery_info(int *mode)
{
	struct smem_batt_state *s;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, SMEM_ITEM_SIZE);
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	s = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, NULL);
	if (IS_ERR(s)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}

	*mode = s->zero_speed_start_mode;

	mca_log_err("zero_speed_start_mode: %d\n", *mode);
	return 0;
}





int get_smem_battery_verify_result(bool *verified, bool *chip_ok)
{
	struct smem_batt_state *s;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, SMEM_ITEM_SIZE);
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	s = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, NULL);
	if (IS_ERR(s)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}
	u8 bits = s->verify_bits;
	*verified = (bits >> 5) & 1;
	*chip_ok = (bits >> 4) & 1;



	mca_log_err("uefi_batt_verfitied: %d, uefi_batt_verfitied_chip_ok: %d\n", *verified, *chip_ok);
	return 0;
}




int get_smem_battery_cell_sn(int *sn)
{
	struct smem_batt_state *s;
	int ret;

	ret = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, SMEM_ITEM_SIZE);
	if (ret < 0 && ret != -EEXIST) {
		mca_log_err("unable to allocate shared state entry\n");
		return 0;
	}

	s = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_ITEM_ID, NULL);
	if (IS_ERR(s)) {
		mca_log_err("Unable to acquire shared state entry\n");
		return 0;
	}

	*sn = s->cell_sn;


	mca_log_err("uefi_battery_cell_sn: %d\n", *sn);
	return 0;
}

EXPORT_SYMBOL(get_smem_battery_cell_sn);
EXPORT_SYMBOL(get_smem_battery_info);
EXPORT_SYMBOL(get_smem_battery_verify_result);

MODULE_DESCRIPTION("mca get smem info");
MODULE_AUTHOR("liuliang10@xiaomi.com");
MODULE_LICENSE("GPL v2");
