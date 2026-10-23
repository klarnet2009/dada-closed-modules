/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_smart_charge.h
 *
 * smart charge driver - OS4 (HyperOS 4.0) layout.
 * The XRING header (OS3 era) was rebuilt from the module-local BTF of the OS4 stock mca_smart_charge.ko
 * (START_ID=141904 local mode, work/re/g13a_tools/dump_mca_smart_charge/btf_all.txt): struct sizes/offsets and the enums
 * below are the BTF ones.  Member names printed by the BTF as ?strNNN (strings living in the base BTF) are GUESSES.
 * SAFETY: community-only charging source, never flash it, never put it into a ROM build.
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
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

#ifndef __SMART_CHARGE_H__
#define __SMART_CHARGE_H__

enum SMART_CHG_MODE {
	SMART_CHG_NIGHT, // Warning: smart night not a smart charge feature
	SMART_CHG_NAVIGATION		= 1,
	SMART_CHG_OUTDOOR			= 2,
	SMART_CHG_LOWFAST			= 3,
	SMART_CHG_ENDURANCE_PRO		= 4,
	SMART_CHG_WLS_SUPER			= 5,
	SMART_CHG_SENSE_CHG			= 6,
	SMART_CHG_WLS_QUIET			= 7,
	SMART_CHG_EXTREME_COLD		= 8,
	SMART_CHG_TRAVELWAIT		= 9,
	SMART_CHG_BYPASS			= 10,	/* OS4 (BTF) */
	SMART_CHG_MAX				= 15,
};

/* 8 bytes in the BTF; the bitfield names are not in the module BTF (anonymous structs): taken from the XRING header, bit 10 = bypass is a GUESS */
union SMART_CHG_HEADER {
	unsigned long AsUINT32;
	struct {
		unsigned long enable			: 1; /* BIT0: en_ret*/
		unsigned long func_type			: 15; /* BIT1 - BIT15: func_typ */
		unsigned long func_value		: 16; /* BIT16 - BIT31: func_val */
	};
	struct {
		unsigned long					: 1; /*bit [0:0]*/
		unsigned long navigation		: 1; /*bit [1:1]*/
		unsigned long outdoor			: 1; /*bit [2:2]*/
		unsigned long lowfast			: 1; /*bit [3:3]*/
		unsigned long endurance_pro		: 1; /*bit [4:4]*/
		unsigned long wls_super_chg		: 1; /*bit [5:5]*/
		unsigned long sense_chg			: 1; /*bit [6:6]*/
		unsigned long wls_quiet			: 1; /*bit [7:7]*/
		unsigned long extreme_cold		: 1; /*bit [8:8]*/
		unsigned long travel_wait		: 1; /*bit [9:9]*/
		unsigned long bypass			: 1; /*bit [10:10] GUESS*/
		unsigned long					: 5; /*bit [15:11]*/
		unsigned long soc_limit			: 8; /*bit [23:16]*/
		unsigned long					: 8; /*bit [31:24]*/
	};
};

union SMART_CHG_MIEVENT{
	unsigned long AsUINT32;
	struct {
	unsigned long					: 1; /*bit [0:0]*/
	unsigned long navigation		: 1; /*bit [1:1]*/
	unsigned long outdoor			: 1; /*bit [2:2]*/
	unsigned long lowfast			: 1; /*bit [3:3]*/
	unsigned long endurance_pro		: 1; /*bit [4:4]*/
	unsigned long wls_super_chg		: 1; /*bit [5:5]*/
	unsigned long sense_chg			: 1; /*bit [6:6]*/
	unsigned long wls_quiet			: 1; /*bit [7:7]*/
	unsigned long extreme_cold		: 1; /*bit [8:8]*/
	unsigned long travel_wait		: 1; /*bit [9:9]*/
	};
};

enum smart_chg_posture_stat {
	SMART_CHG_POSTURE_UNKNOW,
	SMART_CHG_POSTURE_DESKTOP		= 1,
	SMART_CHG_POSTURE_HOLDER		= 2,
	SMART_CHG_POSTURE_ONEHAND		= 3,
	SMART_CHG_POSTURE_TWOHAND_H		= 4,
	SMART_CHG_POSTURE_TWOHAND_V		= 5,
	SMART_CHG_POSTURE_ANS_CALL		= 6,
	SMART_CHG_POSTURE_INDEX_MAX,
	SMART_CHG_POSTURE_FAKE_CLEAR		= 100,
	SMART_CHG_POSTURE_FAKE_DESKTOP		= 101,
	SMART_CHG_POSTURE_FAKE_HOLDER		= 102,
	SMART_CHG_POSTURE_FAKE_ONEHAND		= 103,
	SMART_CHG_POSTURE_FAKE_TWOHAND_H	= 104,
	SMART_CHG_POSTURE_FAKE_TWOHAND_V	= 105,
	SMART_CHG_POSTURE_FAKE_ANS_CALL		= 106,
	SMART_CHG_POSTURE_FAKE_INDEX_MAX,
};

/* values from the OS4 BTF enum smart_chg_scene_type */
enum smart_chg_scene_type {
	SMART_CHG_SCENE_NORMAL			= 0,
	SMART_CHG_SCENE_HUANJI			= 1,
	SMART_CHG_SCENE_PHONE			= 5,
	SMART_CHG_SCENE_NOLIMIT			= 6,
	SMART_CHG_SCENE_CLASS0			= 7,
	SMART_CHG_SCENE_YOUTUBE			= 8,
	SMART_CHG_SCENE_NAVIGATION		= 10,
	SMART_CHG_SCENE_VIDEO			= 11,
	SMART_CHG_SCENE_VIDEOCHAT		= 14,
	SMART_CHG_SCENE_CAMERA			= 15,
	SMART_CHG_SCENE_TGAME			= 18,
	SMART_CHG_SCENE_MGAME			= 19,
	SMART_CHG_SCENE_YUANSHEN		= 20,
	SMART_CHG_SCENE_XINGTIE			= 25,
	SMART_CHG_SCENE_DANMU			= 28,
	SMART_CHG_SCENE_PER_NORMAL		= 50,
	SMART_CHG_SCENE_PER_CLASS0		= 57,
	SMART_CHG_SCENE_PER_YOUTUBE		= 58,
	SMART_CHG_SCENE_PER_VIDEO		= 61,
	SMART_CHG_SCENE_PER_XINGTIE		= 75,
	SMART_CHG_SCENE_PER_DANMU		= 78,
	SMART_CHG_SCENE_HP_GAME			= 501,
	SMART_CHG_SCENE_CGAME			= 700,
	SMART_CHG_SCENE_CGAME_2			= 702,
	SMART_CHG_SCENE_INDEX_MAX		= 703,
	SMART_CHG_SCENE_REDIR_START		= 1000,
	SMART_CHG_SCENE_REDIR_INDEX_MAX	= 1703,
};

enum smart_chg_sic_mode {
	SMART_CHG_SIC_MODE_BALANCED		= 0,
	SMART_CHG_SIC_MODE_SLIGHTCHG	= 2,
	SMART_CHG_SIC_MODE_MIDDLE		= 4,
	SMART_CHG_SIC_MODE_SUPERCHG		= 8,
	SMART_CHG_SIC_MODE_MODE_MAX_INDEX = 9,
};

enum smart_chg_pwr_boost_type {
	SMART_CHG_OUTDOOR_PWR_BOOST		= 0,
	SMART_CHG_TRAVELWAIT_PWR_BOOST		= 1,
	SMART_CHG_PWR_BOOST_MAX_INDEX,
};

struct SMART_CHG_DATA {
	uint16_t enable;
	uint16_t func_value;
	bool use_fake_value;
	int fake_value;
};

struct SMART_CHG_INFO {
	uint8_t ret_code;
	struct SMART_CHG_DATA smart_chgcfg[SMART_CHG_MAX];
};

struct ICHG_CC_CFG {
	int cc_max;
	int delta_ichg;
};

#define CC_ICHG_MAX_GROUP 2
#define SMART_BYPASS_LMT_ROWS 10
#define SMART_BYPASS_LMT_COLS 5

/* OS4: smart bypass (BTF struct SMART_BYPASS_CFG, 636 bytes) */
struct SMART_BYPASS_CFG {
	int smart_bypass_support;
	int active_status;
	int cur_temp_index;
	int smart_bypass_fcc;
	int smart_bypass_mode;
	int smart_bypass_entry_soc;
	int smart_bypass_high_lmt_size;
	int smart_bypass_med_lmt_size;
	int smart_bypass_low_lmt_size;
	int smart_bypass_high_lmt_cfg[SMART_BYPASS_LMT_ROWS][SMART_BYPASS_LMT_COLS];
	int smart_bypass_med_lmt_cfg[SMART_BYPASS_LMT_ROWS][SMART_BYPASS_LMT_COLS];
	int smart_bypass_low_lmt_cfg[SMART_BYPASS_LMT_ROWS][SMART_BYPASS_LMT_COLS];
};

/* OS4 layout: 1624 bytes (BTF); `mmap_size` and `data_lock` of the XRING template do not exist in OS4 */
struct smart_charge_info {
	struct class *smart_charge_class;
	struct cdev pri_dev;
	struct device *sys_dev;
	struct device *dev;
	dev_t dev_num;				/* name GUESS (BTF ?str393301, u32) */
	struct delayed_work smart_charge_work;
	struct delayed_work smart_sense_chg_work;
	struct delayed_work smart_soc_limit_work;
	struct mca_votable *smartchg_delta_fv_voter;
	struct mca_votable *smartchg_delta_ichg_voter;
	struct mca_votable *smartchg_fcc_voter;	/* OS4 */
	struct mca_votable *soc_limit_voter;	/* GLOBAL OS4.0.0.7 (+600, BTF) */
	struct mca_votable *curr_max_voter;	/* GLOBAL OS4.0.0.7 (+608, BTF; found with mca_find_votable("curr_max")) */
	struct notifier_block panel_nb;
	struct notifier_block thermal_board_nb;	/* OS4 */

	int delta_fv;
	int delta_ichg;
	int soc_limit;
	int online;				/* name GUESS (BTF ?str11194) */
	int cycle_count;
	int wls_online;
	int cell_type;
	int smart_sic_mode;
	int screen_state;
	int scene;
	int fake_scene;
	int board_temp;
	int enable_fv_dec_by_cc;

	int smart_night_val;
	int smart_batt_val;
	int smart_delta_ichg;

	bool support_cc_ichg;
	bool support_sensechg_v2;
	bool support_sensechg_v3;
	bool soc_limit_enable;
	char reserve[4];			/* BTF: 4 bytes at +716 */
	struct ICHG_CC_CFG ichg_cc_table[CC_ICHG_MAX_GROUP];
	struct SMART_BYPASS_CFG smart_bypass_cfg;
	/* Smart Charging Engine */
	union SMART_CHG_HEADER smart_chg_control;
	struct SMART_CHG_INFO smart_chg_data;
	char *mmap_addr;
	int map_flag;
	int pulse_mode;
	int support_csd_2p0;			/* GLOBAL 0.7 name (CN 0.6: support_csd); DT key "support_csd_2p0" */
	int plugin_rsoc;
	int night_enable_rsoc;
	union SMART_CHG_MIEVENT ignore_upload;
	s64 bypass_debounce_time;	/* BTF T262 (8 bytes); signed: the stock compares ktime with b.le */
	bool in_debounce;
	int mishow;
	/* GLOBAL OS4.0.0.7 (stock BTF, G14e): names are the BTF's, the meaning of batt_safe_* is inferred from the stock code */
	bool support_soft_cid;			/* +1640 */
	int audio_notify_status;		/* +1644 */
	int batt_safe_lvl;			/* +1648: 0/1, written through the sysfs attribute batt_safe_lvl */
	int batt_safe_vlim;			/* +1652: voltage limit read from the charger partition (units unknown) */
	bool batt_safe_active;			/* +1656 */
	bool batt_emerg_soc_limit;		/* +1657: never written in the GLOBAL 0.7 stock (nor here): the branch reading it in handle_algorithm_conflict is dead (G17b, review D4) */
	bool support_batt_safe_lvl;		/* +1658: DT, may be overridden (log "override support_batt_safe_lvl") */
	int real_type;				/* +1660: stored from an event value */
};

_Static_assert(sizeof(struct smart_charge_info) == 1664, "smart_charge_info size");  /* GLOBAL 0.7 (CN 0.6: 1624) */
_Static_assert(offsetof(struct smart_charge_info, smart_bypass_cfg) == 752, "bypass cfg");
_Static_assert(offsetof(struct smart_charge_info, smart_chg_control) == 1392, "control");
_Static_assert(offsetof(struct smart_charge_info, mmap_addr) == 1584, "mmap_addr");
_Static_assert(offsetof(struct smart_charge_info, mishow) == 1636, "mishow");
_Static_assert(offsetof(struct smart_charge_info, real_type) == 1660, "real_type");
_Static_assert(offsetof(struct smart_charge_info, batt_safe_lvl) == 1648, "batt_safe_lvl");

enum smartchg_attr_list {
	MCA_PROP_SMARTCHG,
	MCA_PROP_SMARTCHG_FV,
	MCA_PROP_SMARTCHG_ICHG,
	MCA_PROP_SMARTBATT,
	MCA_PROP_SMARTNIGHT,
	MCA_PROP_POSTURE,
	MCA_PROP_SCENE,
	MCA_PROP_BOARD_TEMP,
	MCA_PROP_SMART_SIC_MODE,
	MCA_PROP_AUDIO_NOTIFY,	/* GLOBAL 0.7 enum value 9: no sysfs table entry in the stock */
	MCA_PROP_BATT_SAFE_LVL,
	MCA_PROP_BATT_SAFE_VLIM,
};

enum smartchg_cell_type {
	MCA_CELL_TYPE_1S = 1,
	MCA_CELL_TYPE_2S,
	MCA_CELL_TYPE_MAX,
};

enum smartchg_cc_ichg_cfg_mode_ele {
	SMARTCHG_MODE_CYCLECOUNT,
	SMARTCHG_MODE_DELTA_ICHG,
	SMARTCHG_MODE_PARA_MAX,
};

#endif /* __SMART_CHARGE_H__ */
