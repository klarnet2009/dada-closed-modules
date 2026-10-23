/* SPDX-License-Identifier: GPL-2.0 */
/* mca_hwid.h - hardware id info (provider: mca_hwid.ko), struct from the stock BTF (size 40) */
#ifndef __MCA_HWID_H__
#define __MCA_HWID_H__

#include <linux/types.h>

struct mca_hwid {
	uint32_t platform_version;
	uint32_t country_version;
	uint32_t major_version;
	uint32_t minor_version;
	uint32_t build_version;
	uint32_t build_adc;
	uint32_t product_adc;
	uint32_t hwid_value;
	const char *product_name;
};

const struct mca_hwid *mca_get_hwid_info(void);
bool mca_is_warsaw_p2_before(const struct mca_hwid *hwid);

#endif /* __MCA_HWID_H__ */
