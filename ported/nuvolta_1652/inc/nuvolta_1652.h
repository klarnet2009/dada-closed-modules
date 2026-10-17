/* SPDX-License-Identifier: GPL-2.0 */
/*
 * nuvolta_1652.h
 *
 * wireless RX ic driver
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

#ifndef __NUVOLTA_1652_HEADER__
#define __NUVOLTA_1652_HEADER__

#include <linux/wait.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#define HANDLE_INT_THREAD_ACTIVE 1

//registers define
#define REG_RX_REV_CMD 0x0020
#define REG_RX_REV_DATA1 0x0021
#define RX_POWER_OFF_ERR 0x0028
#define TRX_MODE_EN 0x0063
#define RX_RTX_MODE 0x002f
#define RX_DATA_INFO 0x1200
#define RX_POWER_MODE 0x1203
#define RX_FASTCHG_RESULT 0x1209
#define TX_MANU_ID_L 0x1224
#define TX_MANU_ID_H 0x1225
#define RX_SS_VOLTAGE 0x126A

#define TRX_MODE_STATUS					0x03

#define REG_GPIO_RAIL_CTRL				0x1153
#define RX_CMD_SET_1P8V					0x3f

#define REG_MTP_CTRL1					0x0019
#define RX_CMD_EXECUTE_HIGH				0x01
#define RX_CMD_EXECUTE_LOW				0x00

#define REG_MTP_CTRL2					0x001a
#define RX_CMD_ENTER_WRITE_MODE			0x5a
#define RX_CMD_ENTER_READ_MODE			0xa5
#define RX_CMD_EN_READ					0x04
#define RX_CMD_ENTER_TRIM				0x3c

#define REG_DTM_UNLOCK_REG0				0x2017
#define REG_DTM_UNLOCK_REG1				0x2020

#define RX_FW_DATA_DEFAULT_LENGTH		32768

#define RX_FW_HANDLE_STEP				4
#define RX_FW_FORMAT_LENGTH				4096
#define RX_FW_WRITE_CHECK_COUNT			250
#define RX_FW_HANDLE_DELAY_MS			20
#define RX_FW_CHECK_DELAY_US			100
#define RX_FW_EXIT_DELAY_MS				100

#define REG_MTP_CTRL_DATA0				0x001c
#define REG_MTP_CTRL_DATA1				0x001d
#define REG_MTP_CTRL_DATA2				0x001e
#define REG_MTP_CTRL_DATA3				0x001f
#define RX_CMD_ERASE_MASK				0xFF

#define REG_MTP_STATUS					0x001b
#define MTP_STATUS_MASK					0x80
#define MTP_WRITE_RESULT_MASK			0x40
#define MTP_STATUS_SHIFT				7
#define MTP_WRITE_RESULT_SHIFT			6

#define REG_RX_INT_0					0x0060
#define REG_RX_INT_1					0x0061
#define REG_RX_INT_2					0x0062
#define REG_RX_INT_3					0x0063
#define RX_CMD_START_READ				0x88
#define RX_CMD_ENABLE_CRC				0x02
#define RX_CMD_ENABLE_TX				0x01
#define RX_CMD_DISABLE_TX				0x00

#define FW_VERSION_BUF_LENGTH			7
#define CRC_WORK_DELAY_US				100000
#define CRC_CHECK_SUCCESS				0x66
#define CRC_CHECK_ERR_VER				0xFE

/* used registers define */
#define REG_RX_SENT_CMD					0x0000
#define REG_RX_SENT_DATA1				0x0001
#define REG_RX_SENT_DATA2				0x0002
#define REG_RX_SENT_DATA3				0x0003
#define REG_RX_SENT_DATA4				0x0004
#define REG_RX_SENT_DATA5				0x0005
#define REG_RX_SENT_DATA6				0x0006
#define REG_RX_SENT_DATA8				0x0008
#define REG_RX_SENT_DATA9				0x0009
#define REG_RX_SENT_DATAA				0x000A
#define REG_RX_SENT_DATAB				0x000B
#define REG_RX_SENT_DATAC				0x000C
#define REG_RX_SENT_DATAD				0x000D
#define REG_RX_REV_CMD					0x0020
#define REG_RX_REV_DATA1				0x0021
#define REG_RX_REV_DATA2				0x0022
#define REG_RX_REV_DATA3				0x0023
#define REG_RX_REV_DATA4				0x0024
#define REG_RX_REV_DATA5				0x0025
#define REG_RX_REV_DATA8				0x0028
#define REG_RX_REV_DATAF				0x002f

#define I2C_CMD_CHECK_RETRY_COUNT		100
#define I2C_CMD_CHECK_BUSY				0x55
#define RX_CMD_I2C_CHECK_MASK			0x88

#define RX_AUTH_DATA_LENGTH				40
#define ADAPTER_TYPE_SHIFT				7
#define AUTH_RESULT_SHIFT				8
#define POWER_MODE_SHIFT				3
#define MAX_POWER_SHIFT					5
#define TX_ID_LOW_SHIFT					26
#define TX_ID_HIGH_SHIFT				27
#define UUID0_SHIFT						28
#define UUID1_SHIFT						29
#define UUID2_SHIFT						30
#define UUID3_SHIFT						31
#define TX_MAC_ADDR0_SHIFT				34
#define TX_MAC_ADDR1_SHIFT				33
#define TX_MAC_ADDR2_SHIFT				32
#define TX_MAC_ADDR3_SHIFT				38
#define TX_MAC_ADDR4_SHIFT				37
#define TX_MAC_ADDR5_SHIFT				36

#define RX_CLEAR_INT_LENGTH				0x02
#define RX_CLEAR_INT_DATA				0xFF
#define RX_CLEAR_INT_TRIGGER_RX			0x04

#define RX_CMD_CLEAR_CEP				0x21
#define RX_CMD_ENABLE_REVERSE_FOD		0x23
#define RX_CMD_SET_RX_VOUT				0x31
#define RX_CMD_CLEAR_INT				0x68
#define RX_CMD_TRANSMIT_PACKET			0x69
#define RX_CMD_FOD_SET					0x98
#define RX_CMD_RENEGO_SET				0xA8
#define RX_CMD_ALARM_VOL				0xA9
#define RX_CMD_ENABLE_BLE				0xAA

#define REVERSE_FOD_EN					0x01
#define REVERSE_FOD_DIS					0x00
#define REVERSE_FOD_DEFAULT_GAIN		94
#define REVERSE_FOD_TRIGGER_RX			0x04
#define DISABLE_REVERSE_FOD_TRIGGER_RX	0x02

#define BPP_PLUS_FOD_SET_CMD_LENGTH		0x0D
#define FOD_PARAMS_MAX_LENGTH			5
#define FOD_SET_DELAY_MS				20
#define FOD_SET_TRIGGER_RX				15

#define RECEIVE_DATA_MAX_COUNT			50
#define RECEIVE_DATA_LENGTH_SHIFT		40
#define RECEIVE_DATA_SHIFT				41

#define REG_MTP_STATE_PIN				0x1001
#define RX_CMD_MTP_STATE_EN				0x80

#define REG_SECTOR_SELECT_REG			0x0012
#define SECTOR_SELECT_MASK				0xff

#define I2C_READ_RX_BUF_LENGTH			30
#define GET_RX_TEMP_SHIFT0				14
#define GET_RX_TEMP_SHIFT1				15
#define GET_IOUT_SHIFT0					16
#define GET_IOUT_SHIFT1					17
#define VRECT_SHIFT0					18
#define VRECT_SHIFT1					19
#define GET_VOUT_SHIFT0					20
#define GET_VOUT_SHIFT1					21
#define GET_CEP_SHIFT					10

#define ADAPTER_VOL_MAX_MV				30000
#define ADAPTER_VOL_MIN_MV				4000
#define ADAPTER_VOL_DEFAULT_MV			6000
#define ADAPTER_VOL_PACKET_LENGTH		0x05
#define ADAPTER_VOL_SET_TYPE			0x02
#define ADAPTER_VOL_TRIGGER_RX			0x07
#define RX_CMD_FASTCHG_SET_TX_VOLTAGE	0x0a

#define TRANS_DATA_LENGTH_1BYTE			0x18
#define TRANS_DATA_LENGTH_2BYTE			0x28
#define TRANS_DATA_LENGTH_3BYTE			0x38
#define TRANS_DATA_LENGTH_5BYTE			0x58

#define VOUT_SET_MAX_MV					19500
#define VOUT_SET_MIN_MV					4000
#define VOUT_SET_DEFAULT_MV				6000
#define VOUT_SET_PACKET_LENGTH			0x02
#define VOUT_SET_TRIGGER_RX				0x04

#define REG_MCU_CTRL_REG				0x1000
#define RX_CMD_DIS_MCU_1665				0xc0
#define RX_CMD_DIS_MCU_1651				0x90
#define RX_CMD_DIS_MCU_EN_TRIM			0xe0

#define REG_RX_SLEEP_CTRL_REG			0x0090
#define RX_CMD_DISABLE_SLEEP			0x41


#define FUDA1665_DTM_REG0_DATA0			0x2d
#define FUDA1665_DTM_REG0_DATA1			0xd2
#define FUDA1665_DTM_REG0_DATA2			0x22
#define FUDA1665_DTM_REG0_DATA3			0xdd
#define FUDA1651_DTM_REG0_DATA0			0x69
#define FUDA1651_DTM_REG0_DATA1			0x96
#define FUDA1651_DTM_REG0_DATA2			0x66
#define FUDA1651_DTM_REG0_DATA3			0x99
#define FUDA1665_DTM_REG1_DATA0			0x4b
#define FUDA1665_DTM_REG1_DATA1			0xb4
#define FUDA1665_DTM_REG1_DATA2			0x44
#define FUDA1665_DTM_REG1_DATA3			0xbb
#define FUDA1651_DTM_REG1_DATA0			0x78
#define FUDA1651_DTM_REG1_DATA1			0x87
#define FUDA1651_DTM_REG1_DATA2			0x1e
#define FUDA1651_DTM_REG1_DATA3			0xe1

#define REG_CONFIRM_DATA				0x008b

#define REG_MTP_CTRL0					0x0017
#define RX_CMD_WRITE_ENABLE				0x01

#define RENEGO_LENGTH					0x01
#define RENEGO_TRIGGER_RX				0x03

#define RX_CHECK_SUCCESS (1 << 0)
#define TX_CHECK_SUCCESS (1 << 1)
#define BOOT_CHECK_SUCCESS (1 << 2)


#define FACTORY_TEST_CMD_ADAPTER_TYPE	0x0b
#define FACTORY_TEST_CMD_RX_IOUT			0x12
#define FACTORY_TEST_CMD_RX_VOUT			0x13
#define FACTORY_TEST_CMD_RX_CHIP_ID		0x23
#define FACTORY_TEST_CMD_RX_FW_ID		0x24
#define FACTORY_TEST_CMD_REVERSE_REQ		0x30

#define TRX_ISENSE_HIGH	0x11
#define TRX_ISENSE_LOW	0x10
#define TRX_VRECT_HIGH	0x13
#define TRX_VRECT_LOW	0x12

#define TRANS_DATA_LENGTH_1BYTE	0x18
#define TRANS_DATA_LENGTH_2BYTE	0x28
#define TRANS_DATA_LENGTH_3BYTE	0x38
#define TRANS_DATA_LENGTH_5BYTE	0x58

#ifndef ABS
#define ABS(x) ((x) > 0 ? (x) : -(x))
#endif
#define ABS_CEP_VALUE 1

#define RX_OFFSET_THRESHOLD 5000

//Wireless Power Interrupt Request
#define DECL_INTERRUPT_MAP(regval, redir_irq) {\
	.irq_regval = regval,\
	.irq_flag = redir_irq, \
}

enum wls_rx_irq {
	RX_IRQ_LDO_ON					= 0x0001,
	RX_IRQ_FAST_CHARGE				= 0x0002,
	RX_IRQ_AUTHEN_FINISH			= 0x0004,
	RX_IRQ_RENEGO_DONE				= 0x0008,
	RX_IRQ_ALARM_SUCCESS			= 0x0010,
	RX_IRQ_ALARM_FAIL				= 0x0020,
	RX_IRQ_OOB_GOOD					= 0x0040,
	RX_IRQ_RPP						= 0x0080,
	RX_IRQ_TRANSPARENT_SUCCESS		= 0x0100,
	RX_IRQ_TRANSPARENT_FAIL			= 0x0200,
	RX_IRQ_FACTORY_TEST				= 0x0400,
	RX_IRQ_OCP_OTP_ALARM			= 0x1000,
	RX_IRQ_FIRST_AUTHEN				= 0x2000,
	RX_IRQ_POWER_OFF				= 0x4000,
	RX_IRQ_POWER_ON					= 0x8000,
};

enum wls_rtx_irq {
	RTX_IRQ_PING					= 0x0001,
	RTX_IRQ_GET_RX					= 0x0002,
	RTX_IRQ_CEP_TIMEOUT				= 0x0004,
	RTX_IRQ_EPT						= 0x0008,
	RTX_IRQ_PROTECTION				= 0x0010,
	RTX_IRQ_GET_TX					= 0x0020,
	RTX_IRQ_REVERSE_TEST_READY		= 0x0040,
	RTX_IRQ_REVERSE_TEST_DONE		= 0x0080,
	RTX_IRQ_FOD						= 0x0100,
};

enum rx_int_flag {
	RX_INT_UNKNOWN = 0,
	RX_INT_LDO_ON = 0x0001,
	RX_INT_FAST_CHARGE = 0x0002,
	RX_INT_AUTHEN_FINISH = 0x0004,
	RX_INT_RENEGO_DONE = 0x0008,
	RX_INT_ALARM_SUCCESS = 0x0010,
	RX_INT_ALARM_FAIL = 0x0020,
	RX_INT_OOB_GOOD = 0x0040,
	RX_INT_RPP = 0x0080,
	RX_INT_TRANSPARENT_SUCCESS = 0x0100,
	RX_INT_TRANSPARENT_FAIL = 0x0200,
	RX_INT_FACTORY_TEST = 0x0400,
	RX_INT_OCP_OTP_ALARM = 0x0800,
	RX_INT_FIRST_AUTHEN = 0x1000,
	RX_INT_POWER_OFF = 0x2000,
	RX_INT_POWER_ON = 0x4000,
};

enum rtx_int_flag {
	RTX_INT_UNKNOWN,
	RTX_INT_PING,
	RTX_INT_GET_RX,
	RTX_INT_CEP_TIMEOUT,
	RTX_INT_EPT,
	RTX_INT_PROTECTION,
	RTX_INT_GET_TX,
	RTX_INT_REVERSE_TEST_READY,
	RTX_INT_REVERSE_TEST_DONE,
	RTX_INT_FOD,
};

typedef struct int_map_t {
	uint32_t irq_regval;
	int irq_flag;
} int_map_t;

/* fod_para */
#define FOD_PARA_MAX_GROUP 21
enum fod_para_ele {
	FOD_PARA_TYPE = 0,
	FOD_PARA_LENGTH,
	FOD_PARA_UUID,
	FOD_PARA_PARAMS,
	FOD_PARA_PARAMS_MAG,
	FOD_PARA_MAX,
};

#define DEFAULT_FOD_PARAM_LEN 20
enum PARAMS_T {
	PARAMS_T_GAIN,
	PARAMS_T_OFFSET,
	PARAMS_T_MAX,
};

enum FOD_PARAM_ID {
	FOD_PARAM_20V,
	FOD_PARAM_27V,
	FOD_PARAM_BPP_PLUS,
	FOD_PARAM_MAX,
};

enum wls_work_mode {
	RX_MODE,
	RTX_MODE,
};

enum wls_chg_stage {
	NORMAL_MODE = 1,
	TAPER_MODE,
	FULL_MODE,
	RECHG_MODE,
};

enum fw_version_index {
	FW_DATA_VERSION_V0 = 0,
	FW_DATA_VERSION_V1,
	FW_DATA_NUMS_MAX,
};

enum auth_status {
	AUTH_STATUS_FAILED,
	AUTH_STATUS_SHAR1_OK,
	AUTH_STATUS_USB_TYPE_OK,
	AUTH_STATUS_UUID_OK = 4,
	AUTH_STATUS_TX_MAC_OK = 6,
};

enum wireless_number {
	NU1652_MASTER,
	NU1652_SLAVE,
};

enum wireless_model {
	NU1661,
	NU1652,
};

enum tx_action {
	TX_ACTION_REPLY_ACK = 0x00,
	TX_ACTION_REPLY_PACKAGE = 0x01,
	TX_ACTION_NO_REPLY = 0x02,
};

enum rx_offset {
	RX_OFFSET_GOOD,
	RX_OFFSET_BAD,
};

struct wls_fw_parameters {
	u8 fw_rx_id;
	u8 fw_tx_id;
	u8 fw_boot_id;
	u8 hw_id_h;
	u8 hw_id_l;
};

struct params_t {
	u8	gain;
	u8	offset;
};

struct fod_params_t {
	u8 params_nums;
	u8 type;
	u8 length;
	int uuid;
	struct params_t params[DEFAULT_FOD_PARAM_LEN];
	struct params_t params_mag[DEFAULT_FOD_PARAM_LEN];
};

struct int_flag_lis_node {
	struct list_head lnode;
	int trx_mode;
	u16 int_flag;
};

struct nuvolta_1652_chg_proc_data {
	u8 epp;
	u8 fc_flag;
	int tx_speed;
	u16 adapter_type;
	u8 uuid[4];
	u8 epp_tx_id_h;
	u8 epp_tx_id_l;
	int int_flag;
	int int_trx_mode;
	bool is_car_tx;
	bool power_good_flag;
	bool qc_enable;
	bool parallel_charge;
	int vout_setted;
	int ss_voltage;
};

struct nuvolta_1652_chg {
	struct i2c_client *client;
	struct device *dev;
	struct device *sysfs_dev;
	struct regmap *regmap;
	struct mca_platform_class *class;
	//irq and gpio
	int irq_gpio;
	int irq;
	int power_good_gpio;
	int power_good_irq;
	int enable_gpio;
	int hall_int_gpio;
	int hall_int_irq;
	//pinctrl
	struct pinctrl *nu1652_pinctrl;
	struct pinctrl_state *pinctrl_stat;
	struct pinctrl_state *pinctrl_stat_hall;
	//fw_bin
	unsigned char fw_bin[32768];
	int fw_bin_length;

	int project_vendor;
	int support_hall;
	int rx_role;
	int fw_version_index;

	//fw & fod
	int fw_data_size;
	int fod_params_size;
	unsigned char *fw_data_ptr;
	struct fod_params_t fod_params[FOD_PARA_MAX_GROUP];
	struct fod_params_t fod_params_default;
	struct fod_params_t fod_params_bpp_plus;

	struct wls_fw_parameters *wls_fw_data;
	//lock
	struct mutex i2c_lock;
	struct mutex wireless_chg_int_lock;
	struct mutex data_transfer_lock;
	//notifier
	struct notifier_block shutdown_notifier;
	//delay_work
	struct delayed_work init_detect_work;
	struct delayed_work pg_detect_work;
	struct delayed_work interrupt_work;
	struct delayed_work hall_interrupt_work;

	struct nuvolta_1652_chg_proc_data proc_data;

	bool magnetic_case_flag;
	bool hall_gpio_status;
	int fake_rx_offset;
	//debug_fod
	u8 wls_debug_one_fod_index;
	int wls_debug_set_fod_type;
	struct params_t wls_debug_one_fod_param;
	struct fod_params_t *wls_debug_all_fod_params;
};

#endif /* __NUVOLTA_1652_HEADER__ */
