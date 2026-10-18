// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 * Copyright (c) 2016, EPAM Systems
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/crash_dump.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/cpumask.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/kstrtox.h>

#include <tee_drv.h>
#include "optee_private.h"
#include "optee_rpc_cmd.h"
#include "optee_bench.h"
#include "optee_smc.h"
#include "mitee_memlog.h"
#include "mitee_smc_notify.h"
#include "mitee_tui.h"
#include "rpc_callback.h"
#include "dynamic_mem.h"
#include "mitee_task.h"

/*
 * This file implement the core API of mitee.
 * This file is divided into the following sections:
 * 1. Setup and release of common tee context
 * 2. Parse DTS files related to soc platforms
 * 3. Implement fastcalls for initialization
 * 4. Convert params and transmit message
 * 5. Operations of clinet teedev and supplicant teedev
 * 6. Initialization and Uninstallation of Mitee Driver
 */

struct mutex tee_mutex;
static struct optee *optee_svc;

#ifdef ENABLE_CMDLINE_SUPPORT
static bool mitee_disable;
module_param(mitee_disable, bool, 0);
#endif


struct optee *get_optee_drv_state(void)
{
	return optee_svc;
}

/*
 * 1. Setup and release of common tee context
 *
 * The tee context is created every time an open client file node is created,
 * that is, every time TEEC_InitializeContext is called.
 */

int optee_open_common(struct tee_context *ctx, bool cap_memref_null)
{
	struct optee_context_data *ctxdata;
	struct tee_device *teedev = ctx->teedev;
	struct optee *optee = tee_get_drvdata(teedev);

	ctxdata = kzalloc(sizeof(*ctxdata), GFP_KERNEL);
	if (!ctxdata)
		return -ENOMEM;

	if (teedev == optee->supp_teedev) {
		bool busy = true;

		mutex_lock(&optee->supp.mutex);
		if (!optee->supp.ctx) {
			busy = false;
			optee->supp.ctx = ctx;
		}
		mutex_unlock(&optee->supp.mutex);
		if (busy) {
			kfree(ctxdata);
			return -EBUSY;
		}
	}
	mutex_init(&ctxdata->mutex);
	INIT_LIST_HEAD(&ctxdata->sess_list);

	ctx->cap_memref_null = cap_memref_null;
	ctx->data = ctxdata;
	return 0;
}

static void optee_release_helper(struct tee_context *ctx,
				 int (*close_session)(struct tee_context *ctx,
						      u32 session))
{
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_session *sess;
	struct optee_session *sess_tmp;

	if (!ctxdata)
		return;

	list_for_each_entry_safe(sess, sess_tmp, &ctxdata->sess_list,
				  list_node) {
		list_del(&sess->list_node);
		close_session(ctx, sess->session_id);
		kfree(sess);
	}
	kfree(ctxdata);
	ctx->data = NULL;
}

void optee_release(struct tee_context *ctx)
{
	optee_release_helper(ctx, optee_close_session_helper);
}

void optee_release_supp(struct tee_context *ctx)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	optee_release_helper(ctx, optee_close_session_helper);
	if (optee->scan_bus_wq) {
		destroy_workqueue(optee->scan_bus_wq);
		optee->scan_bus_wq = NULL;
	}
	optee_supp_release(&optee->supp);
}

/*
 * 2. Parse DTS files related to soc platforms
 */

/* TODO: add dts */
static int mitee_get_comm_ops(struct device_node *node,
			      const struct mitee_comm_ops **ops)
{
	(void)node;

	*ops = &mitee_msgq_com_ops;

	return 0;
}

/* bind task to specific cores, typically big cores */
static int mitee_get_cpu_allows(struct device_node *node,
				struct cpumask *cpus_allowed)
{
	int rc = 0;
	int cpus_num = 0;
	int i = 0;
	u32 *cpu = NULL;

	cpumask_clear(cpus_allowed);

	cpus_num = of_property_count_u32_elems(node, MITEE_CORE_ID);
	if (cpus_num <= 0) {
		pr_info("no cpu limitation(%d), execute freely\n", cpus_num);
		/* defualt value */
		memcpy(cpus_allowed, cpu_online_mask, sizeof(struct cpumask));
		return 0;
	}

	cpu = kcalloc(cpus_num, sizeof(*cpu), GFP_KERNEL);
	if (!cpu) {
		pr_err("kcalloc cpu(%d) failed\n", cpus_num);
		return -ENOMEM;
	}

	rc = of_property_read_u32_array(node, MITEE_CORE_ID, cpu, cpus_num);
	if (rc) {
		pr_err("read dts failed(%d): %s\n", rc, MITEE_CORE_ID);
		return rc;
	}

	for (i = 0; i < cpus_num; i++) {
		if (cpu[i] >= nr_cpu_ids)
			continue;

		cpumask_set_cpu(cpu[i], cpus_allowed);
	}

	pr_info("bind core list: %*pbl\n", cpumask_pr_args(cpus_allowed));

	return rc;
}

static int mitee_parse_dts(struct optee *optee)
{
	struct device_node *dev_node = NULL;
	int rc = 0;

	if (!optee)
		return -EINVAL;

	dev_node = of_find_compatible_node(NULL, NULL, MITEE_COMPATIBLE);
	if (!dev_node) {
		pr_err("no such dev: %s\n", MITEE_COMPATIBLE);
		return -ENODEV;
	}

	rc = mitee_get_cpu_allows(dev_node, &optee->cpus_allowed);
	if (rc) {
		pr_err("get cpu allowed failed: %s\n", MITEE_COMPATIBLE);
		return rc;
	}

	rc = mitee_get_comm_ops(dev_node, &optee->comm_ops);
	if (rc) {
		pr_err("get cpu allowed failed: %s\n", MITEE_COMPATIBLE);
		return rc;
	}

	optee->tui_supported = of_property_read_bool(dev_node, "tui-supported");

	return rc;
}

/*
 * 3. Implement fastcalls for initialization
 *
 * Currently, fastcall is mainly used to pass shared memory information and
 * prepare for subsequent yielding call communication.
 *
 */
#if MITEE_FEATURE_CAP_ENABLE
union api_ver_args {
	union common_args common;

	struct {
		u64 cmd;
	} in;

	struct {
		u64 major;
		u64 minor;
	} out;
};

union os_ver_args {
	union common_args common;

	struct {
		u64 cmd;
	} in;

	struct {
		u64 major;
		u64 minor;
		u64 sha1;
	} out;
};

union cap_args {
	union common_args common;

	struct {
		u64 cmd;
	} in;

	struct {
		u64 resp;
		u64 cap;
	} out;
};

static bool mitee_api_is_compatbile(struct optee *optee)
{
	union api_ver_args api_args;
	union os_ver_args os_args;
	int rc;

	/* check api compatible */
	api_args.in.cmd = OPTEE_FFA_GET_API_VERSION;

	rc = optee->comm_ops->call(&api_args.common);
	if (rc) {
		pr_err("Unexpected error %d\n", rc);
		return false;
	}

	if (api_args.out.major != OPTEE_FFA_VERSION_MAJOR ||
	    api_args.out.minor < OPTEE_FFA_VERSION_MINOR) {
		pr_err("Incompatible API version %lu.%lu", api_args.out.major,
		       api_args.out.minor);
		return false;
	}

	/* check os compatible */
	os_args.in.cmd = OPTEE_FFA_GET_OS_VERSION;

	rc = optee->comm_ops->call(&os_args.common);
	if (rc) {
		pr_err("Unexpected error %d\n", rc);
		return false;
	}

	if (os_args.out.sha1)
		pr_info("revision %lu.%lu (%08lx)", os_args.out.major,
			os_args.out.minor, os_args.out.sha1);
	else
		pr_info("revision %lu.%lu", os_args.out.major,
			os_args.out.minor);

	return true;
}

static bool mitee_exchange_caps(struct optee *optee,
				unsigned int *rpc_arg_count)
{
	union cap_args args;
	int rc;

	args.in.cmd = OPTEE_FFA_EXCHANGE_CAPABILITIES;

	rc = optee->comm_ops->call(&args.common);
	if (rc) {
		pr_err("Unexpected error %d", rc);
		return false;
	}
	if (args.out.resp) {
		return false;
	}

	*rpc_arg_count = (u8)args.out.cap;

	return true;
}
#endif

union memremap_args {
	union common_args common;

	struct {
		u64 cmd;
	} in;

	struct {
		u64 paddr;
		u64 size;
		u64 cached;
	} out;
};

static struct tee_shm_pool *optee_shm_memremap(struct optee *optee,
					       void **memremaped_shm)
{
	union memremap_args args;
	unsigned long vaddr;
	phys_addr_t paddr;
	size_t size;
	phys_addr_t begin;
	phys_addr_t end;
	void *va;
	struct tee_shm_pool_mgr *priv_mgr;
	struct tee_shm_pool_mgr *dmabuf_mgr;
	int rc = 0;
	void *pool = NULL;
	const int sz = OPTEE_SHM_NUM_PRIV_PAGES * PAGE_SIZE;

	args.in.cmd = OPTEE_FFA_GET_SHM_CONFIG;

	rc = optee->comm_ops->call(&args.common);
	if (rc) {
		pr_err("Unexpected error %d", rc);
		return NULL;
	}

	if (args.out.cached != OPTEE_SMC_SHM_CACHED) {
		pr_err("only normal cached shared memory supported\n");
		return ERR_PTR(-EINVAL);
	}

	pr_info("get shm pool: %llx size: %llx\n", args.out.paddr,
		args.out.size);

	begin = roundup(args.out.paddr, PAGE_SIZE);
	end = rounddown(args.out.paddr + args.out.size, PAGE_SIZE);
	paddr = begin;
	size = end - begin;

	if (size < 2 * OPTEE_SHM_NUM_PRIV_PAGES * PAGE_SIZE) {
		pr_err("too small shared memory area\n");
		return ERR_PTR(-EINVAL);
	}

	va = memremap(paddr, size, MEMREMAP_WB);
	if (!va) {
		pr_err("shared memory ioremap failed\n");
		return ERR_PTR(-EINVAL);
	}
	vaddr = (unsigned long)va;

	pool = tee_shm_pool_mgr_alloc_res_mem(vaddr, paddr, sz,
					      3 /* 8 bytes aligned */);
	if (IS_ERR(pool)) {
		goto err_memunmap;
	}
	priv_mgr = pool;

	vaddr += sz;
	paddr += sz;
	size -= sz;

	pool = tee_shm_pool_mgr_alloc_res_mem(vaddr, paddr, size, PAGE_SHIFT);
	if (IS_ERR(pool)) {
		goto err_free_priv_mgr;
	}
	dmabuf_mgr = pool;

	pool = tee_shm_pool_alloc(priv_mgr, dmabuf_mgr);
	if (IS_ERR(pool)) {
		goto err_free_dmabuf_mgr;
	}

	*memremaped_shm = va;

	return pool;

err_free_dmabuf_mgr:
	tee_shm_pool_mgr_destroy(dmabuf_mgr);
err_free_priv_mgr:
	tee_shm_pool_mgr_destroy(priv_mgr);
err_memunmap:
	memunmap(va);
	return pool;
}

/*
 * 4. Convert params and transmit message
 *
 * Implement API for parameter conversion and message transmission between the
 * TEE layer interface and the generic protocol layer
 */
/**
 * optee_do_call_with_arg() - Do a FF-A call to enter OP-TEE in secure world
 * @ctx:	calling context
 * @shm:	shared memory holding the message to pass to secure world
 *
 * Does a FF-A call to OP-TEE in secure world and handles eventual resulting
 * Remote Procedure Calls (RPC) from OP-TEE.
 *
 * Returns return code from FF-A, 0 is OK
 */
static int optee_do_call_with_arg(struct tee_context *ctx,
				  struct optee_msg_arg *msg_arg)
{
	struct mitee_task *task = NULL;
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	if (optee->comm_ops->get_vm_state() != MITEE_MSGQ_STATE_READY) {
		pr_err("%s: tee didn't bootup done, try later\n", __func__);
		return -EAGAIN;
	}

	down(&optee->sem);

	task = mitee_task_alloc(ctx, MITEE_REE_REQUEST, msg_arg);
	if (IS_ERR(task)) {
		pr_err("%s:mitee task alloc failed\n", __func__);
		up(&optee->sem);
		return -ENOMEM;
	}

	wake_up(&optee->workers_wq);
	wait_for_completion(&task->c);

	mitee_task_free(ctx, task->id);
	up(&optee->sem);

	return 0;
}

int to_msg_param_tmp_mem(struct optee_msg_param *mp, const struct tee_param *p)
{
	int rc;
	phys_addr_t pa;

	mp->attr = OPTEE_MSG_ATTR_TYPE_TMEM_INPUT + p->attr -
		   TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;

	mp->u.tmem.shm_ref = (unsigned long)p->u.memref.shm;
	mp->u.tmem.size = p->u.memref.size;

	if (!p->u.memref.shm) {
		mp->u.tmem.buf_ptr = 0;
		return 0;
	}

	rc = tee_shm_get_pa(p->u.memref.shm, p->u.memref.shm_offs, &pa);
	if (rc) {
		return rc;
	}

	mp->u.tmem.buf_ptr = pa;
	mp->attr |= OPTEE_MSG_ATTR_CACHE_PREDEFINED
		    << OPTEE_MSG_ATTR_CACHE_SHIFT;

	return 0;
}

/**
 * optee_to_msg_param() - convert from struct tee_params to OPTEE_MSG
 *			      parameters
 * @optee:	main service struct
 * @msg_params:	OPTEE_MSG parameters
 * @num_params:	number of elements in the parameter arrays
 * @params:	subsystem itnernal parameter representation
 * Returns 0 on success or <0 on failure
 */
static int optee_to_msg_param(struct optee *optee,
			      struct optee_msg_param *msg_params,
			      size_t num_params, const struct tee_param *params)
{
	size_t n;
	int rc = 0;

	for (n = 0; n < num_params; n++) {
		const struct tee_param *p = params + n;
		struct optee_msg_param *mp = msg_params + n;

		switch (p->attr) {
		case TEE_IOCTL_PARAM_ATTR_TYPE_NONE:
			mp->attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
			memset(&mp->u, 0, sizeof(mp->u));
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
			optee_to_msg_param_value(mp, p);
			break;
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
		case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
			if (tee_shm_is_registered(p->u.memref.shm)) {
				if (optee->comm_ops->id == ARM_FFA)
					rc = optee->comm_ops->to_msg_param(mp,
									   p);
			} else {
				rc = to_msg_param_tmp_mem(mp, p);
			}

			if (rc) {
				pr_err("%s attr: %#llx failed: %d!\n", __func__,
				       p->attr, rc);
				return rc;
			}
			break;
		default:
			pr_err("%s unsupport attr: %#llx!\n", __func__,
			       p->attr);
			return -EINVAL;
		}
	}

	return 0;
}

int from_msg_param_tmp_mem(struct tee_param *p, u32 attr,
			   const struct optee_msg_param *mp)
{
	struct tee_shm *shm;
	phys_addr_t pa;
	int rc;

	p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT + attr -
		  OPTEE_MSG_ATTR_TYPE_TMEM_INPUT;
	p->u.memref.size = mp->u.tmem.size;
	shm = (struct tee_shm *)(unsigned long)mp->u.tmem.shm_ref;
	if (!shm) {
		p->u.memref.shm_offs = 0;
		p->u.memref.shm = NULL;
		return 0;
	}

	rc = tee_shm_get_pa(shm, 0, &pa);
	if (rc) {
		return rc;
	}

	p->u.memref.shm_offs = mp->u.tmem.buf_ptr - pa;
	p->u.memref.shm = shm;

	/* Check that the memref is covered by the shm object */
	if (p->u.memref.size) {
		size_t o = p->u.memref.shm_offs + p->u.memref.size - 1;

		rc = tee_shm_get_pa(shm, o, NULL);
		if (rc) {
			return rc;
		}
	}

	return 0;
}

/**
 * optee_from_msg_param() - convert from OPTEE_MSG parameters to
 *				struct tee_param
 * @optee:	main service struct
 * @params:	subsystem internal parameter representation
 * @num_params:	number of elements in the parameter arrays
 * @msg_params:	OPTEE_MSG parameters
 *
 * Returns 0 on success or <0 on failure
 */
static int optee_from_msg_param(struct optee *optee, struct tee_param *params,
				size_t num_params,
				const struct optee_msg_param *msg_params)
{
	size_t n;
	int rc = 0;

	for (n = 0; n < num_params; n++) {
		struct tee_param *p = params + n;
		const struct optee_msg_param *mp = msg_params + n;
		u32 attr = mp->attr & OPTEE_MSG_ATTR_TYPE_MASK;

		switch (attr) {
		case OPTEE_MSG_ATTR_TYPE_NONE:
			p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
			memset(&p->u, 0, sizeof(p->u));
			break;
		case OPTEE_MSG_ATTR_TYPE_VALUE_INPUT:
		case OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_VALUE_INOUT:
			optee_from_msg_param_value(p, attr, mp);
			break;
		case OPTEE_MSG_ATTR_TYPE_TMEM_INPUT:
		case OPTEE_MSG_ATTR_TYPE_TMEM_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_TMEM_INOUT:
			rc = from_msg_param_tmp_mem(p, attr, mp);
			if (rc) {
				pr_err("%s: tmp mem err: %d!\n", __func__, rc);
				return rc;
			}
			break;
		case OPTEE_MSG_ATTR_TYPE_FMEM_INPUT:
		case OPTEE_MSG_ATTR_TYPE_FMEM_OUTPUT:
		case OPTEE_MSG_ATTR_TYPE_FMEM_INOUT:
			optee->comm_ops->from_msg_param(optee, p, attr, mp);
			break;
		/*rpc msg do not support RMEM*/
		default:
			pr_err("%s: unsupported mem type: %d!\n", __func__,
			       attr);
			return -EINVAL;
		}
	}

	return 0;
}

static const struct optee_ops optee_param_ops = {
	.do_call_with_arg = optee_do_call_with_arg,
	.to_msg_param = optee_to_msg_param,
	.from_msg_param = optee_from_msg_param,
};

/*
 * 5. Operations of clinet teedev and supplicant teedev
 */
static void optee_get_version(struct tee_device *teedev,
			      struct tee_ioctl_version_data *vers)
{
	struct tee_ioctl_version_data v = {
		.impl_id = TEE_IMPL_ID_OPTEE,
		.impl_caps = TEE_OPTEE_CAP_TZ,
		.gen_caps = TEE_GEN_CAP_GP,
	};

	struct optee *optee = tee_get_drvdata(teedev);

	if (teedev != optee->supp_teedev)
		v.gen_caps |= TEE_GEN_CAP_REG_MEM;

	*vers = v;
}

static int optee_open(struct tee_context *ctx)
{
	return optee_open_common(ctx, true);
}

static int optee_shm_register(struct tee_context *ctx, struct tee_shm *shm,
			      struct page **pages, size_t num_pages,
			      unsigned long start)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	return optee->comm_ops->shm_register(ctx, shm, pages, num_pages, start);
}

static int optee_shm_unregister(struct tee_context *ctx, struct tee_shm *shm)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	return optee->comm_ops->shm_unregister(ctx, shm);
}

static int optee_shm_unregister_supp(struct tee_context *ctx,
				     struct tee_shm *shm)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);

	return optee->comm_ops->shm_unregister_supp(ctx, shm);
}

static const struct tee_driver_ops optee_clnt_ops = {
	.get_version = optee_get_version,
	.open = optee_open,
	.release = optee_release,
	.open_session = optee_open_session,
	.close_session = optee_close_session,
	.invoke_func = optee_invoke_func,
	.cancel_req = optee_cancel_req,
	.shm_register = optee_shm_register,
	.shm_unregister = optee_shm_unregister,
};

static const struct tee_desc optee_clnt_desc = {
	.name = DRIVER_NAME "-ffa-clnt",
	.ops = &optee_clnt_ops,
	.owner = THIS_MODULE,
};

static const struct tee_driver_ops optee_supp_ops = {
	.get_version = optee_get_version,
	.open = optee_open,
	.release = optee_release_supp,
	.supp_recv = optee_supp_recv,
	.supp_send = optee_supp_send,
	.shm_register = optee_shm_register, /* same as for clnt ops */
	.shm_unregister = optee_shm_unregister_supp,
};

static const struct tee_desc optee_supp_desc = {
	.name = DRIVER_NAME "-ffa-supp",
	.ops = &optee_supp_ops,
	.owner = THIS_MODULE,
	.flags = TEE_DESC_PRIVILEGED,
};

/*
 * 6. Initialization and Uninstallation of Mitee Driver
 */
void optee_remove_common(struct optee *optee)
{
#if MITEE_FEATURE_FW_NP_ENABLE
	/* Unregister OP-TEE specific client devices on TEE bus */
	optee_unregister_devices();
#endif
	/*
	 * The two devices have to be unregistered before we can free the
	 * other resources.
	 */
	tee_device_unregister(optee->supp_teedev);
	tee_device_unregister(optee->teedev);

	tee_shm_pool_free(optee->pool);
	optee_wait_queue_exit(&optee->wait_queue);
	optee_supp_uninit(&optee->supp);
	mutex_destroy(&optee->call_queue.mutex);
}

static int mitee_core_init(void)
{
	unsigned int rpc_arg_count = 0;
	struct tee_device *teedev = NULL;
	void *memremaped_shm = NULL;
	u32 sec_caps = 0;
	struct optee *optee = NULL;
	int rc = 0;
	char worker_name[20];

	pr_info("initializing driver\n");

#ifdef ENABLE_CMDLINE_SUPPORT
	if (mitee_disable) {
		return 0;
	}

	pr_info("mitee_disable is false\n");
#endif

	/*
	 * The kernel may have crashed at the same time that all available
	 * secure world threads were suspended and we cannot reschedule the
	 * suspended threads without access to the crashed kernel's wait_queue.
	 * Therefore, we cannot reliably initialize the OP-TEE driver in the
	 * kdump kernel.
	 */
	if (is_kdump_kernel())
		return -ENODEV;

	rc = mitee_tee_init();
	if (rc)
		return -EINVAL;

	optee = kzalloc(sizeof(*optee), GFP_KERNEL);
	if (!optee) {
		rc = -ENOMEM;
		goto err_tee_exit;
	}

	rc = mitee_parse_dts(optee);
	if (rc) {
		pr_err("dts parsed failed\n");
		goto err_free_optee;
	}

	rc = optee->comm_ops->init();
	if (rc) {
		pr_err("comm protocol init failed\n");
		goto err_free_optee;
	}

	mitee_smc_notify_init(optee->comm_ops);

#if MITEE_FEATURE_CAP_ENABLE
	if (!mitee_api_is_compatbile(optee)) {
		goto err_comm_deinit;
	}

	if (!mitee_exchange_caps(optee, &rpc_arg_count)) {
		goto err_comm_deinit;
	}
#else
	/* fixed capabilities */
	sec_caps |= OPTEE_SMC_SEC_CAP_HAVE_RESERVED_SHM;
	sec_caps |= OPTEE_SMC_SEC_CAP_DYNAMIC_SHM;
	rpc_arg_count = THREAD_RPC_MAX_NUM_PARAMS;
#endif

	optee->pool = ERR_PTR(-EINVAL);

	/* reserved share memory pool for data less than 128KB */
	if (IS_ERR(optee->pool) &&
	    (sec_caps & OPTEE_SMC_SEC_CAP_HAVE_RESERVED_SHM)) {
		optee->pool = optee_shm_memremap(optee, &memremaped_shm);
	}

	if (IS_ERR(optee->pool)) {
		rc = PTR_ERR(optee->pool);
		optee->pool = NULL;
		goto err_comm_deinit;
	}

	optee->ops = &optee_param_ops;
	optee->rpc_arg_count = rpc_arg_count;

	teedev = tee_device_alloc(&optee_clnt_desc, NULL, optee->pool, optee);
	if (IS_ERR(teedev)) {
		rc = PTR_ERR(teedev);
		goto err_free_pool;
	}
	optee->teedev = teedev;

	teedev = tee_device_alloc(&optee_supp_desc, NULL, optee->pool, optee);
	if (IS_ERR(teedev)) {
		rc = PTR_ERR(teedev);
		goto err_unreg_teedev;
	}
	optee->supp_teedev = teedev;

	rc = tee_device_register(optee->teedev);
	if (rc)
		goto err_unreg_supp_teedev;

	rc = tee_device_register(optee->supp_teedev);
	if (rc)
		goto err_unreg_supp_teedev;

	mutex_init(&optee->call_queue.mutex);
	INIT_LIST_HEAD(&optee->call_queue.waiters);
	optee_wait_queue_init(&optee->wait_queue);
	mitee_rpc_callback_queue_init(&optee->cb_queue);
	optee_supp_init(&optee->supp);
	optee->memremaped_shm = memremaped_shm;

#if MITEE_FEATURE_FW_NP_ENABLE
	rc = optee_enumerate_devices(PTA_CMD_GET_DEVICES);
	if (rc)
		goto err_supp_free;
#endif
	optee_svc = optee;
	/* mitee log device function START */
	ATOMIC_INIT_NOTIFIER_HEAD(&optee->notifier);
	optee->mitee_memlog_pdev = platform_device_alloc("mitee_memlog", 0);
	if (IS_ERR_OR_NULL(optee->mitee_memlog_pdev)) {
		rc = PTR_ERR(teedev);
		goto err_unregister_devices;
	}

	(void)platform_device_add(optee->mitee_memlog_pdev);

	rc = mitee_memlog_probe(optee->mitee_memlog_pdev);
	if (rc) {
		pr_err("failed to initial mitee_memlog driver (%d)\n", rc);
		goto err_memlog_device_del;
	}
	/* mitee log device function END */

	/* mitee task function START */
	sema_init(&optee->sem, 1);

	rc = mitee_msg_queue_init(&optee->msg_queue);
	if (rc) {
		pr_err("failed to init mitee msg queue (%d)\n", rc);
		goto err_memlog_remove;
	}

	rc = mitee_msg_queue_register();
	if (rc) {
		pr_err("failed to register mitee msg queue (%d)\n", rc);
		goto err_msg_queue_free;
	}

	mitee_task_list_init(&optee->task_list);

	/* mitee worker: one kernel thread (not bound to a cpu) */
	init_waitqueue_head(&optee->workers_wq);
	snprintf(worker_name, 15, "mitee_worker/%d", 0);
	optee->worker = kthread_create(mitee_worker, (void *)optee, "%s", worker_name);
	if (IS_ERR(optee->worker)) {
		rc = PTR_ERR(optee->worker);
		pr_err("%s: tee_worker thread creation failed", __func__);
		goto err_task_list_deinit;
	}
	set_user_nice(optee->worker, MIN_NICE);
	wake_up_process(optee->worker);
	/* mitee task function END */

	optee_bm_enable();

	if (!optee->tui_supported) {
		pr_warn("tui is not supported on this platform, init\n");
	} else {
		rc = mitee_tui_init();
		if (rc)
			pr_err("failed to initialize tui, ret=%d\n", rc);
	}

	pr_info("initialized driver\n");
	return 0;

err_task_list_deinit:
	mitee_task_list_deinit(&optee->task_list);
err_msg_queue_free:
	mitee_msg_queue_deinit(&optee->msg_queue);
err_memlog_remove:
	(void)mitee_memlog_remove(optee->mitee_memlog_pdev);
err_memlog_device_del:
	platform_device_del(optee->mitee_memlog_pdev);
err_unregister_devices:
#if MITEE_FEATURE_FW_NP_ENABLE
	optee_unregister_devices();
err_supp_free:
#endif
	optee_supp_uninit(&optee->supp);
	mutex_destroy(&tee_mutex);
	mutex_destroy(&optee->call_queue.mutex);
err_unreg_supp_teedev:
	/*
	 * tee_device_unregister() is safe to call even if the
	 * devices hasn't been registered with
	 * tee_device_register() yet.
	 */
	tee_device_unregister(optee->supp_teedev);
err_unreg_teedev:
	tee_device_unregister(optee->teedev);
err_free_pool:
	if (optee->pool)
		tee_shm_pool_free(optee->pool);
	if (memremaped_shm)
		memunmap(memremaped_shm);
err_comm_deinit:
	optee->comm_ops->deinit();
err_free_optee:
	kfree(optee);
err_tee_exit:
	mitee_tee_exit();
	return rc;
}
module_init(mitee_core_init);

static void optee_core_exit(void)
{
	struct optee *optee = get_optee_drv_state();
	u32 cnt = 0;
	int ret;

	if (!optee->tui_supported) {
		pr_warn("tui is not supported on this platform, deinit\n");
	} else {
		ret = mitee_tui_exit();
		if (ret)
			pr_err("failed to deinit tui, ret=%d\n", ret);
	}

	optee_remove_common(optee);
	mutex_destroy(&tee_mutex);

	optee->comm_ops->deinit();
	mitee_rpc_callback_queue_deinit(&optee->cb_queue);

	mitee_msg_queue_deinit(&optee->msg_queue);
	mitee_task_list_deinit(&optee->task_list);
	kthread_stop(optee->worker);
	kfree(optee);
	optee_bm_disable();

}
module_exit(optee_core_exit);

MODULE_AUTHOR("Linaro");
MODULE_DESCRIPTION("OP-TEE driver");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:optee");
