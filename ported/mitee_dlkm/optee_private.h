/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */

#ifndef OPTEE_PRIVATE_H
#define OPTEE_PRIVATE_H

#include <linux/arm-smccc.h>
#include <linux/rhashtable.h>
#include <linux/semaphore.h>
#include <linux/types.h>

#include <tee_drv.h>
#include "optee_msg.h"
#include "optee_ffa.h"
#include "mitee_msg.h"
#include "mitee_task.h"

#define DRIVER_NAME "optee"

#define OPTEE_MAX_ARG_SIZE 1024

/* Some Global Platform error codes used in this driver */
#define TEEC_SUCCESS 0x00000000
#define TEEC_ERROR_BAD_PARAMETERS 0xFFFF0006
#define TEEC_ERROR_NOT_SUPPORTED 0xFFFF000A
#define TEEC_ERROR_COMMUNICATION 0xFFFF000E
#define TEEC_ERROR_OUT_OF_MEMORY 0xFFFF000C
#define TEEC_ERROR_BUSY 0xFFFF000D
#define TEEC_ERROR_SHORT_BUFFER 0xFFFF0010
#define TEEC_ERROR_ITEM_NOT_FOUND      0xFFFF0008

#define TEEC_ORIGIN_COMMS 0x00000002

#define MITEE_FEATURE_FW_NP_ENABLE (0)
#define MITEE_FEATURE_HVC_ENABLE (0)
#define MITEE_FEATURE_CAP_ENABLE (0)
#define OPTEE_SHM_NUM_PRIV_PAGES CONFIG_XRING_OPTEE_SHM_NUM_PRIV_PAGES
#define THREAD_RPC_MAX_NUM_PARAMS (4)
#define DEBUG_CALL (1)
#define DEBUG_RPC (0)
#undef MITEE_SHM_DEBUG

#define MITEE_COMPATIBLE "xiaomi,mitee"
#define MITEE_CORE_ID "binding-core"
#define MITEE_WORKER_COUNT "worker-count"

struct optee_call_waiter {
	struct list_head list_node;
	struct completion c;
};

struct optee_call_queue {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head waiters;
};

struct optee_wait_queue {
	/* Serializes access to this struct */
	struct mutex mu;
	struct list_head db;
};

struct mitee_rpc_callback_queue {
	/* Serializes access to this struct */
	struct mutex mut;
	struct list_head item;
};

/**
 * struct optee_supp - supplicant synchronization struct
 * @ctx			the context of current connected supplicant.
 *			if !NULL the supplicant device is available for use,
 *			else busy
 * @mutex:		held while accessing content of this struct
 * @req_id:		current request id if supplicant is doing synchronous
 *			communication, else -1
 * @reqs:		queued request not yet retrieved by supplicant
 * @idr:		IDR holding all requests currently being processed
 *			by supplicant
 * @reqs_c:		completion used by supplicant when waiting for a
 *			request to be queued.
 */
struct optee_supp {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct tee_context *ctx;

	int req_id;
	struct list_head reqs;
	struct idr idr;
	struct completion reqs_c;
};

struct optee;

/**
 * struct optee_ops - OP-TEE driver internal operations
 * @do_call_with_arg:	enters OP-TEE in secure world
 * @to_msg_param:	converts from struct tee_param to OPTEE_MSG parameters
 * @from_msg_param:	converts from OPTEE_MSG parameters to struct tee_param
 *
 * These OPs are only supposed to be used internally in the OP-TEE driver
 * as a way of abstracting the different methogs of entering OP-TEE in
 * secure world.
 */
struct optee_ops {
	int (*do_call_with_arg)(struct tee_context *ctx,
				struct optee_msg_arg *msg_arg);
	int (*to_msg_param)(struct optee *optee,
			    struct optee_msg_param *msg_params,
			    size_t num_params, const struct tee_param *params);
	int (*from_msg_param)(struct optee *optee, struct tee_param *params,
			      size_t num_params,
			      const struct optee_msg_param *msg_params);
};

struct optee *get_optee_drv_state(void);

union common_args {
	struct {
		u64 cmd;
		u64 param[4];
	} in;

	struct {
		u64 resp;
		u64 param[4];
	} out;
};

/* state of the secure VM link as seen by get_vm_state() (enum tag from the kCFI hash 0x7f76d6d2; values 2/3 from the code, names guessed) */
enum tee_state {
	TEE_STATE_DOWN = 0,
	TEE_STATE_VM_READY = 1,
	TEE_STATE_UP = 2,
	TEE_STATE_READY = 3,
};

enum comm_protocal {
	ARM_FFA,
	QCOM_QMI,
};

struct mitee_comm_ops {
	enum comm_protocal id;
	int (*init)(void);
	void (*deinit)(void);
	enum tee_state (*get_vm_state)(void);
	int (*call)(union common_args *args);
	void (*from_msg_param)(struct optee *optee, struct tee_param *p,
				   u32 attr, const struct optee_msg_param *mp);
	int (*to_msg_param)(struct optee_msg_param *mp,
					const struct tee_param *p);
	int (*shm_register)(struct tee_context *ctx, struct tee_shm *shm,
				  struct page **pages, size_t num_pages,
				  unsigned long start);
	int (*shm_unregister)(struct tee_context *ctx,
				    struct tee_shm *shm);
	int (*shm_unregister_supp)(struct tee_context *ctx,
					 struct tee_shm *shm);
};

extern const struct mitee_comm_ops mitee_msgq_com_ops;

/**
 * struct optee - main service struct
 * @supp_teedev:	supplicant device
 * @ops:		internal callbacks for different ways to reach secure
 *			world
 * @teedev:		client device
 * @call_queue:		queue of threads waiting to call @invoke_fn
 * @wait_queue:		queue of threads from secure world waiting for a
 *			secure world sync object
 * @supp:		supplicant synchronization struct for RPC to supplicant
 * @pool:		shared memory pool
 * @rpc_arg_count:	If > 0 number of RPC parameters to make room for
 * @scan_bus_done	flag if device registation was already done.
 * @scan_bus_wq		workqueue to scan optee bus and register optee drivers
 * @scan_bus_work	workq to scan optee bus and register optee drivers
 * @msg_queue		transmit message between ree and tee
 * @task_list		a wait list for CA request
 * @sem				limit the concurrency of CA request
 * @workers_run		counter of started mitee worker threads
 * @workers_wq		waitqueue of the mitee worker
 * @worker		the mitee worker thread
 */
struct optee {
	struct tee_device *supp_teedev;
	struct tee_device *teedev;
	const struct optee_ops *ops;
	const struct mitee_comm_ops *comm_ops;
	void *memremaped_shm;
	struct optee_call_queue call_queue;
	struct optee_wait_queue wait_queue;
	struct mitee_rpc_callback_queue cb_queue;
	struct optee_supp supp;
	struct tee_shm_pool *pool;
	unsigned int rpc_arg_count;
	bool scan_bus_done;
	struct workqueue_struct *scan_bus_wq;
	struct work_struct scan_bus_work;
	struct atomic_notifier_head notifier;
	struct platform_device *mitee_memlog_pdev;
	struct cpumask cpus_allowed;
	struct mitee_msg_queue msg_queue;
	struct mitee_task_list task_list;
	struct semaphore sem;
	/* the single mitee worker thread (stock layout: +0x2d8/+0x2e0/+0x2f8) */
	atomic_t workers_run;
	wait_queue_head_t workers_wq;
	struct task_struct *worker;
	bool tui_supported;
};

struct optee_session {
	struct list_head list_node;
	u32 session_id;
};

struct optee_context_data {
	/* Serializes access to this struct */
	struct mutex mutex;
	struct list_head sess_list;
};

struct optee_rpc_param {
	u32 a0;
	u32 a1;
	u32 a2;
	u32 a3;
	u32 a4;
	u32 a5;
	u32 a6;
	u32 a7;
};

/* Holds context that is preserved during one STD call */
struct optee_call_ctx {
	/* information about pages list used in last allocation */
	void *pages_list;
	size_t num_entries;
};

void optee_wait_queue_init(struct optee_wait_queue *wq);
void optee_wait_queue_exit(struct optee_wait_queue *wq);
void mitee_rpc_callback_queue_init(struct mitee_rpc_callback_queue *queue);
void mitee_rpc_callback_queue_deinit(struct mitee_rpc_callback_queue *queue);
uint32_t mitee_rpc_callback(struct optee_msg_param_value *value, void *buf,
				uint32_t size_in, uint32_t *size_out);

u32 optee_supp_thrd_req(struct tee_context *ctx, u32 func, size_t num_params,
			struct tee_param *param);

int optee_supp_read(struct tee_context *ctx, void __user *buf, size_t len);
int optee_supp_write(struct tee_context *ctx, void __user *buf, size_t len);
void optee_supp_init(struct optee_supp *supp);
void optee_supp_uninit(struct optee_supp *supp);
void optee_supp_release(struct optee_supp *supp);

int optee_supp_recv(struct tee_context *ctx, u32 *func, u32 *num_params,
		    struct tee_param *param);
int optee_supp_send(struct tee_context *ctx, u32 ret, u32 num_params,
		    struct tee_param *param);

int optee_open_session(struct tee_context *ctx,
		       struct tee_ioctl_open_session_arg *arg,
		       struct tee_param *param);
int optee_close_session_helper(struct tee_context *ctx, u32 session);
int optee_close_session(struct tee_context *ctx, u32 session);
int optee_invoke_func(struct tee_context *ctx, struct tee_ioctl_invoke_arg *arg,
		      struct tee_param *param);
int optee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session);

#define PTA_CMD_GET_DEVICES 0x0
#define PTA_CMD_GET_DEVICES_SUPP 0x1
int optee_enumerate_devices(u32 func);
void optee_unregister_devices(void);

int optee_pool_op_alloc_helper(
	struct tee_shm_pool_mgr *poolm, struct tee_shm *shm, size_t size,
	int (*shm_register)(struct tee_context *ctx, struct tee_shm *shm,
			    struct page **pages, size_t num_pages,
			    unsigned long start));

static inline void optee_from_msg_param_value(struct tee_param *p, u32 attr,
					      const struct optee_msg_param *mp)
{
	p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT + attr -
		  OPTEE_MSG_ATTR_TYPE_VALUE_INPUT;
	p->u.value.a = mp->u.value.a;
	p->u.value.b = mp->u.value.b;
	p->u.value.c = mp->u.value.c;
}

static inline void optee_to_msg_param_value(struct optee_msg_param *mp,
					    const struct tee_param *p)
{
	mp->attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT + p->attr -
		   TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
	mp->u.value.a = p->u.value.a;
	mp->u.value.b = p->u.value.b;
	mp->u.value.c = p->u.value.c;
}

void optee_cq_wait_init(struct optee_call_queue *cq,
			struct optee_call_waiter *w);
void optee_cq_wait_for_completion(struct optee_call_queue *cq,
				  struct optee_call_waiter *w);
void optee_cq_wait_final(struct optee_call_queue *cq,
			 struct optee_call_waiter *w);
int optee_check_mem_type(unsigned long start, size_t num_pages);

struct tee_shm *optee_get_msg_arg(struct tee_context *ctx, size_t num_params,
				  struct optee_msg_arg **msg_arg);

struct tee_shm *optee_rpc_cmd_alloc_suppl(struct tee_context *ctx, size_t sz);
void optee_rpc_cmd_free_suppl(struct tee_context *ctx, struct tee_shm *shm);
void optee_rpc_cmd(struct tee_context *ctx, struct optee *optee,
		   struct optee_msg_arg *arg);

/*
 * Small helpers
 */

static inline void *reg_pair_to_ptr(u32 reg0, u32 reg1)
{
	return (void *)(unsigned long)(((u64)reg0 << 32) | reg1);
}

static inline void reg_pair_from_64(u32 *reg0, u32 *reg1, u64 val)
{
	*reg0 = val >> 32;
	*reg1 = val;
}

/* mitee_comm_ops->get_vm_state(): secure VM is up and the message queue is registered */
#define MITEE_MSGQ_STATE_READY 3

enum {
	MITEE_CALL_PREPARE,
	MITEE_CALL_RETURNED,
};

#endif /*OPTEE_PRIVATE_H*/
