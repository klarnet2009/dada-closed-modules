/* G2d: definitions recovered from the OS4 BTF of nuvolta_1652.ko / mca_platform_wireless_class.ko (verified values; START_ID=141904 btf_dump.py local --all) */
#ifndef __NUVOLTA_OS4_DEFS_H__
#define __NUVOLTA_OS4_DEFS_H__

enum mca_wireless_project_vendor {
	WLS_CHIP_VENDOR_NONE = 0,
	WLS_CHIP_VENDOR_FUDA1651 = 1,
	WLS_CHIP_VENDOR_FUDA1665 = 2,
	WLS_CHIP_VENDOR_FUDA1661 = 3,
	WLS_CHIP_VENDOR_SC96281 = 4,
	WLS_CHIP_VENDOR_SC96231 = 5,
};

enum mca_wireless_adapter_type {
	ADAPTER_NONE = 0,
	ADAPTER_SDP = 1,
	ADAPTER_CDP = 2,
	ADAPTER_DCP = 3,
	ADAPTER_QC2 = 5,
	ADAPTER_QC3 = 6,
	ADAPTER_PD = 7,
	ADAPTER_AUTH_FAILED = 8,
	ADAPTER_XIAOMI_QC3 = 9,
	ADAPTER_XIAOMI_PD = 10,
	ADAPTER_ZIMI_CAR_POWER = 11,
	ADAPTER_XIAOMI_PD_40W = 12,
	ADAPTER_VOICE_BOX = 13,
	ADAPTER_XIAOMI_PD_50W = 14,
	ADAPTER_XIAOMI_PD_60W = 15,
	ADAPTER_XIAOMI_PD_100W = 16,
	ADAPTER_MAX = 17,
};

enum wls_debug_params {
	DEBUG_SET_FCC = 1,
	DEBUG_SET_ICL = 2,
	DEBUG_SET_ONE_EPP_FOD = 3,
	DEBUG_SET_ALL_EPP_FOD = 4,
	DEBUG_SET_ALL_FOD = 5,
	DEBUG_SET_VOUT = 6,
};

#ifndef ADSP_PROP_ID_DC_INPUT_CURR_LIMIT
#define ADSP_PROP_ID_DC_INPUT_CURR_LIMIT 4099	/* enum mca_adsp_prop_id */
#endif

int mca_parse_dts_u8_array(const struct device_node *np, const char *prop, u8 *data, u16 len);
int mca_parse_dts_u8_count(const struct device_node *np, const char *prop, u32 row, u32 col);

#endif
