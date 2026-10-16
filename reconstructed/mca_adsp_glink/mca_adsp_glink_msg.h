/* SPDX-License-Identifier: GPL-2.0 */
/* wire format of the MCA <-> ADSP battery manager glink messages (reconstructed from the stock binary) */
#ifndef __MCA_ADSP_GLINK_MSG_H__
#define __MCA_ADSP_GLINK_MSG_H__

#include <linux/types.h>
#include <linux/soc/qcom/qti_pmic_glink.h>

#define MCA_ADSP_GLINK_OWNER		32778	/* PMIC_GLINK_OWNER_BATTMGR */
#define MCA_ADSP_GLINK_QBG_OWNER	32777
#define MCA_ADSP_GLINK_MAX_DATA		256
#define MCA_ADSP_GLINK_WAIT_MS		1000
#define PMIC_GLINK_REQ_RESP		1
#define PMIC_GLINK_NOTIFY		2

enum mca_adsp_glink_opcode {
	MCA_ADSP_OPCODE_READ = 1,
	MCA_ADSP_OPCODE_WRITE = 2,
};

/* request sent to the ADSP, 0x114 bytes */
struct mca_adsp_req_msg {
	struct pmic_glink_hdr hdr;
	u32 prop_id;
	u32 seq_num;
	u8 data[MCA_ADSP_GLINK_MAX_DATA];
};

/* response from the ADSP, 0x118 bytes */
struct mca_adsp_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 prop_id;
	u32 ret_code;
	u32 seq_num;
	u8 data[MCA_ADSP_GLINK_MAX_DATA];
};

/* unsolicited notification from the ADSP */
struct mca_adsp_notify_msg {
	struct pmic_glink_hdr hdr;
	u32 notify_id;
	u8 data[MCA_ADSP_GLINK_MAX_DATA];
};

#endif /* __MCA_ADSP_GLINK_MSG_H__ */
