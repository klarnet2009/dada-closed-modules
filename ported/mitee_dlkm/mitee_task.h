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

#ifndef _MITEE_TASK_H_
#define _MITEE_TASK_H_

#include <asm/page.h>
#include <linux/types.h>
#include <linux/list.h>
#include <tee_drv.h>
#include "optee_private.h"
#include "mitee_msg.h"

struct mitee_msg_ringbuf {
	struct mutex lock;
	struct shm_mem_desc *desc;
	struct shm_mem_msg *buf;
};

/**
 * struct mitee_msg_queue - shared memory for communication
 * @pa:	continuous physical address allocated
 * @queue_size:	size of shared memory
 * @ringbuf_size:	size of singel ringbuf
 * @tx_buf:	for transmitting information from REE to TEE
 * @rx_buf:	for transmitting information from TEE to REE
 */
struct mitee_msg_queue {
	uint64_t pa;
	uint32_t queue_size;
	uint32_t ringbuf_size;
	struct mitee_msg_ringbuf tx_buf;
	struct mitee_msg_ringbuf rx_buf;
};

#define MITEE_MSG_QUEUE_DESC_SIZE (PAGE_SIZE)
#define MITEE_MSG_TX_DESC_OFFSET (0x800)
#define MITEE_MSG_RINGBUF_SIZE (PAGE_SIZE)
#define MITEE_MSG_QUEUE_SIZE                                                   \
	(MITEE_MSG_QUEUE_DESC_SIZE + MITEE_MSG_RINGBUF_SIZE * 2)

#define REE_REQUEST_MIN_ID (0x1)
#define REE_REQUEST_MAX_ID (0x7FFFFFFF)

#define MITEE_MSG_MAX_NUM_PARAMS (6)

#define MITEE_MSG_CRC (0)

enum mitee_task_status {
	MITEE_TASK_INIT = 1,
	MITEE_TASK_ABORT,
	MITEE_TASK_SENT,
	MITEE_TASK_DONE
};

/**
 * struct mitee_task - mitee task struct
 * @data			context data
 * @link			tasklist node
 * @c:				completion used until task finish
 * @legacy_msg:		legacy mitee message format
 * @id			unique id allocated by task list idr
 * @type:	message type
 * @status:	task status
 */
struct mitee_task {
	void *data;
	struct list_head link;
	struct completion c;
	struct optee_msg_arg *legacy_msg;
	int32_t id;
	uint32_t type;
	enum mitee_task_status status;
};

struct mitee_task_list {
	/* used for both idr and list */
	struct mutex lock;
	struct idr idr;
	struct list_head list;
};

int32_t mitee_msg_queue_init(struct mitee_msg_queue *queue);
int32_t mitee_msg_queue_deinit(struct mitee_msg_queue *queue);
int32_t mitee_msg_queue_register(void);

void mitee_task_list_init(struct mitee_task_list *task_list);
void mitee_task_list_deinit(struct mitee_task_list *task_list);

struct mitee_task *mitee_task_alloc(struct tee_context *ctx, uint32_t type,
				    struct optee_msg_arg *msg_arg);
void mitee_task_free(struct tee_context *ctx, int32_t id);

int32_t mitee_worker(void *arg);
#endif /*MITEE_TASK_H*/
