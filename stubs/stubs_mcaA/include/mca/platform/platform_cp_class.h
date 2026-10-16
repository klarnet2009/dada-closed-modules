/* SPDX-License-Identifier: GPL-2.0 */
/* platform_cp_class.h - reconstructed API of mca_platform_cp_class.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */
#ifndef __MCA_PLATFORM_PLATFORM_CP_CLASS_H__
#define __MCA_PLATFORM_PLATFORM_CP_CLASS_H__

#include <linux/types.h>

enum cp_role {
	CP_ROLE_MASTER = 0,
};

int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en);
int platform_class_cp_enable_ovpgate(unsigned int role, bool en);
int platform_class_cp_get_present(unsigned int role, bool *present);
int platform_class_cp_enable_ovpgate_with_check(unsigned int role, int type_temp, bool en);
int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor);
int platform_class_cp_set_revchg(unsigned int role, bool en);

#endif /* __MCA_PLATFORM_PLATFORM_CP_CLASS_H__ */
