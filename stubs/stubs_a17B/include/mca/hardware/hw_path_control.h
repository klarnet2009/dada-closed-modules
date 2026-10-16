/* SPDX-License-Identifier: GPL-2.0 */
/* hw_path_control.h - reconstructed (a17A): path control sources, bit flags of mca_path_control.condition_value */
#ifndef __MCA_HW_PATH_CONTROL_H__
#define __MCA_HW_PATH_CONTROL_H__

/* real spelling (BTF of the stock): enum path_control_src {NONE=0,USB=1,WLS=2,WLS_REV=4,OTG=8,VDD=16}; typedef enum path_control_src CONTROL_SRC */
enum path_control_src {
	PATH_CONTROL_NONE = 0,
	PATH_CONTROL_USB = 0x1,
	PATH_CONTROL_WLS = 0x2,
	PATH_CONTROL_WLS_REV = 0x4,
	PATH_CONTROL_OTG = 0x8,
	PATH_CONTROL_VDD = 0x10,
#ifdef __GENKSYMS__
	fcrcNqpwPuNl1L, /* CRC pad: forced so that the stock export CRC is reproduced; the compiler never sees it */
#endif
};
typedef enum path_control_src CONTROL_SRC;

/* otg boost enable status of the buck charger: 1/2 count as "otg on" (stock: (sts - 1) < 2) */
enum otg_boost_enable_status {
	OTG_DISABLE = 0,
	OTG_ENABLE_SEQUENCE = 1,
	OTG_ENABLE = 2,
};

#endif
