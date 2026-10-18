/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 XiaoMi, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MITEE_MSG_H_
#define _MITEE_MSG_H_

#include "optee_msg.h"

/*shared memory layout*/
/*total 12K, 4K mem desc + 4K tx ringbuf + 4K rx ringbuf*/

/*0x0000-----------------------------*/
/*tx(tee->ree) shm_mem_desc          */
/*padding                            */
/*0x0800-----------------------------*/
/*rx(ree->tee) shm_mem_desc          */
/*padding                            */
/*0x1000-----------------------------*/
/*tx ringbuf, 16 entrys shm_mem_msg  */
/*0x2000-----------------------------*/
/*rx ringbuf, 16 entrys shm_mem_msg  */
/*0x3000-----------------------------*/

struct shm_mem_desc {
	volatile uint64_t head;
	volatile uint64_t tail;
	volatile uint64_t size;
};

/*each msg is 256 bytes*/
struct shm_mem_msg {
	uint32_t magic;
	uint32_t version;
	uint32_t rsv0;
	uint32_t msg_id;
	uint32_t msg_type;

	/*message data*/
	uint32_t cmd;
	uint32_t func;
	uint32_t session;
	uint32_t cancel_id;
	uint32_t pad;
	uint32_t ret;
	uint32_t ret_origin;
	uint32_t num_params;

	/* num_params tells the actual number of element in params */
	struct optee_msg_param params[6];

	/*additional info*/
	int64_t timestamp;
	uint32_t crc;
} __packed;

#define MITEE_SHM_MSG_MAGIC 0x4D49
#define MITEE_SHM_MSG_VER_1 0x1

/*message_type*/
/*- Ree call request:*/
#define MITEE_REE_REQUEST 0x1
/*- call done response:*/
#define MITEE_REE_RESPONSE 0x80000001
/*- RPC request:*/
#define MITEE_RPC_REQUEST 0x80000002
/*- return from rpc response:*/
#define MITEE_RPC_RESUME 0x2

/*cmd*/
/*0x0000-0xFFFF*/
#define MITEE_REQ_REGISTER_SHM 0x8001
#define MITEE_REQ_UNREGISTER_SHM 0x8002
#define MITEE_REQ_OPEN_SESSION 0x8003
#define MITEE_REQ_INVOKE_COMMAND 0x8004
#define MITEE_REQ_CLOSE_SESSION 0x8005
#define MITEE_REQ_CANCEL 0x8006

/*0xFFFF0000-0xFFFFFFFF*/
#define MITEE_RPC_LOAD_TA 0xFFFF0001
#define MITEE_RPC_RPMB 0xFFFF0002
#define MITEE_RPC_FS 0xFFFF0003
#define MITEE_RPC_GET_TIME 0xFFFF0004
#define MITEE_RPC_SUSPEND 0xFFFF0005
#define MITEE_RPC_SHM_ALLOC 0xFFFF0006
#define MITEE_RPC_SHM_FREE 0xFFFF0007
#define MITEE_RPC_INTERRUPT 0xFFFF0008
#define MITEE_RPC_SE 0xFFFF0009

#endif
