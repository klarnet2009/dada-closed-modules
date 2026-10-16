#ifndef __LPD_DETECT_H__
#define __LPD_DETECT_H__

struct mca_lpd_dev {
	struct device *dev;
	int lpd_charging;
};

enum lpd_attr_list {
	LPD_PROP_EN,
	LPD_PROP_STATUS,
	LPD_PROP_SBU1,
	LPD_PROP_SBU2,
	LPD_PROP_CC1,
	LPD_PROP_CC2,
	LPD_PROP_DP,
	LPD_PROP_DM,
	LPD_PROP_CHARGING,
	LPD_PROP_CONTROL,
	LPD_PROP_UART_CONTROL,
};
extern int mca_lpd_get_reg(enum lpd_attr_list LPD, int *int_status_reg);

#endif /*__HW_LPD_DETECH_H__*/