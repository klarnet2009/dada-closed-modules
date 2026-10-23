// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015-2021, Linaro Limited
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": %s: " fmt, __func__

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/thread_info.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <tee_drv.h>
#include "optee_bench.h"
#include "optee_private.h"

#define MAX_TPATH_LEN				512
#define MAX_CA_PATH				128
#define UUID_MAX_STR_LEN			255U
#define CLIENT_VAL_LEN				4U

void optee_cq_wait_init(struct optee_call_queue *cq,
			struct optee_call_waiter *w)
{
	/*
	 * We're preparing to make a call to secure world. In case we can't
	 * allocate a thread in secure world we'll end up waiting in
	 * optee_cq_wait_for_completion().
	 *
	 * Normally if there's no contention in secure world the call will
	 * complete and we can cleanup directly with optee_cq_wait_final().
	 */
	mutex_lock(&cq->mutex);

	/*
	 * We add ourselves to the queue, but we don't wait. This
	 * guarantees that we don't lose a completion if secure world
	 * returns busy and another thread just exited and try to complete
	 * someone.
	 */
	init_completion(&w->c);
	list_add_tail(&w->list_node, &cq->waiters);

	mutex_unlock(&cq->mutex);
}

void optee_cq_wait_for_completion(struct optee_call_queue *cq,
				  struct optee_call_waiter *w)
{
	wait_for_completion(&w->c);

	mutex_lock(&cq->mutex);

	/* Move to end of list to get out of the way for other waiters */
	list_del(&w->list_node);
	reinit_completion(&w->c);
	list_add_tail(&w->list_node, &cq->waiters);

	mutex_unlock(&cq->mutex);
}

static void optee_cq_complete_one(struct optee_call_queue *cq)
{
	struct optee_call_waiter *w = NULL;

	list_for_each_entry(w, &cq->waiters, list_node) {
		if (!completion_done(&w->c)) {
			complete(&w->c);
			break;
		}
	}
}

void optee_cq_wait_final(struct optee_call_queue *cq,
			 struct optee_call_waiter *w)
{
	/*
	 * We're done with the call to secure world. The thread in secure
	 * world that was used for this call is now available for some
	 * other task to use.
	 */
	mutex_lock(&cq->mutex);

	/* Get out of the list */
	list_del(&w->list_node);

	/* Wake up one eventual waiting task */
	optee_cq_complete_one(cq);

	/*
	 * If we're completed we've got a completion from another task that
	 * was just done with its call to secure world. Since yet another
	 * thread now is available in secure world wake up another eventual
	 * waiting task.
	 */
	if (completion_done(&w->c))
		optee_cq_complete_one(cq);

	mutex_unlock(&cq->mutex);
}

/* Requires the filpstate mutex to be held */
static struct optee_session *find_session(struct optee_context_data *ctxdata,
					  u32 session_id)
{
	struct optee_session *sess;

	list_for_each_entry(sess, &ctxdata->sess_list, list_node)
		if (sess->session_id == session_id)
			return sess;
	return NULL;
}

#if 0
struct tee_shm *optee_get_msg_arg(struct tee_context *ctx, size_t num_params,
								  struct optee_msg_arg **msg_arg)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	size_t sz = OPTEE_MSG_GET_ARG_SIZE(num_params);
	struct tee_shm *shm;
	struct optee_msg_arg *ma;

	/*
	 * rpc_arg_count is set to the number of allocated parameters in
	 * the RPC argument struct if a second MSG arg struct is expected.
	 * The second arg struct will then be used for RPC.
	 */
	if (optee->rpc_arg_count)
		sz += OPTEE_MSG_GET_ARG_SIZE(optee->rpc_arg_count);

	shm = tee_shm_alloc(ctx, sz, TEE_SHM_MAPPED | TEE_SHM_PRIV);
	if (IS_ERR(shm))
		return shm;

	ma = tee_shm_get_va(shm, 0);
	if (IS_ERR(ma)) {
		tee_shm_free(shm);
		return (void *)ma;
	}

	memset(ma, 0, OPTEE_MSG_GET_ARG_SIZE(num_params));
	ma->num_params = num_params;
	*msg_arg = ma;

	return shm;
}
#endif

static int get_ca_path(char *ca_path, size_t *size)
{
	const char *kernel_path = "kernel";
	char *path = NULL;
	char tpath[MAX_TPATH_LEN] = { 0 };

	if (current->active_mm == NULL) {
		return -EFAULT;
	}

	if (current->active_mm->exe_file == NULL) {
		memcpy(ca_path, kernel_path, strlen(kernel_path) + 1);
		*size = strlen(kernel_path);
		goto end;
	}

	path = d_path(&(current->active_mm->exe_file->f_path), tpath, MAX_TPATH_LEN);
	if (IS_ERR(path)) {
		return -EINVAL;
	}
	if (strlen(path) > MAX_CA_PATH) {
		return -EINVAL;
	}

	memcpy(ca_path, path, strlen(path) + 1);
	*size = strlen(path);

end:
	return 0;
}

static int ca_path_to_msg_param(struct tee_context *ctx,
								struct tee_shm **ca_shm,
								struct optee_msg_arg *msg_arg)
{
	u8 *temp_buf = NULL;
	phys_addr_t pa_addr = 0;
	int rc = 0;
	char ca_path[MAX_CA_PATH];
	size_t ca_path_size = sizeof(ca_path);

	if (get_ca_path(ca_path, &ca_path_size) != 0) {
		return -EINVAL;
	}

	*ca_shm = tee_shm_alloc(ctx, ca_path_size, TEE_SHM_MAPPED);
	if (IS_ERR(*ca_shm)) {
		return -EINVAL;
	}

	temp_buf = tee_shm_get_va(*ca_shm, 0);
	rc       = tee_shm_get_pa(*ca_shm, 0, &pa_addr);
	if (IS_ERR(temp_buf) || (rc != 0)) {
		rc = -EINVAL;
		goto out;
	}

	memcpy(temp_buf, ca_path, ca_path_size);

	msg_arg->params[1].u.tmem.shm_ref = (unsigned long)(*ca_shm);
	msg_arg->params[1].u.tmem.size = ca_path_size;
	msg_arg->params[1].u.tmem.buf_ptr = pa_addr;
	return 0;

out:
	tee_shm_free(*ca_shm);
	return rc;
}

static int get_uuid_str(const uint8_t *uuid, const uuid_t client_uuid,
						uint32_t clnt_login, char *name, uint32_t *name_len)
{
	char clnt_buf[CLIENT_VAL_LEN];

	/* name fill with two uuid */
	if (*name_len < (2 * sizeof(uuid_t) + CLIENT_VAL_LEN)) {
		return -EINVAL;
	}

	memcpy(name, uuid, sizeof(uuid_t));
	memcpy(name + sizeof(uuid_t), (const void *)&client_uuid, sizeof(client_uuid));

	clnt_buf[0] = (clnt_login & 0xff000000) >> 24;
	clnt_buf[1] = (clnt_login & 0x00ff0000) >> 16;
	clnt_buf[2] = (clnt_login & 0x0000ff00) >> 8;
	clnt_buf[3] = (clnt_login & 0x000000ff);

	memcpy(name + 2 * sizeof(uuid_t), clnt_buf, sizeof(clnt_buf));
	*name_len = 2 * sizeof(uuid_t) + CLIENT_VAL_LEN;

	return 0;
}

static int uuid_to_msg_param(struct tee_context *ctx,
							 struct tee_shm **shm,
							 const uint8_t *uuid,
							 const uuid_t client_uuid,
							 uint32_t clnt_login,
							 struct optee_msg_arg *msg_arg)
{
	u8 *temp_buf = NULL;
	phys_addr_t pa_addr = 0;
	int rc = 0;
	/* add uuid + client uuid */
	char uuid_buf[UUID_MAX_STR_LEN] = { 0 };
	uint32_t uuid_buf_size = sizeof(uuid_buf);

	*shm = tee_shm_alloc(ctx, uuid_buf_size, TEE_SHM_MAPPED);
	if (IS_ERR(*shm)) {
		return -EINVAL;
	}

	temp_buf = tee_shm_get_va(*shm, 0);
	rc	   = tee_shm_get_pa(*shm, 0, &pa_addr);
	if (IS_ERR(temp_buf) || (rc != 0)) {
		rc = -EINVAL;
		goto out;
	}

	rc = get_uuid_str(uuid, client_uuid, clnt_login, uuid_buf, &uuid_buf_size);
	if (rc < 0) {
		rc = -EINVAL;
		goto out;
	}

	memcpy(temp_buf, uuid_buf, uuid_buf_size);
	msg_arg->params[0].u.tmem.shm_ref = (unsigned long)(*shm);
	msg_arg->params[0].u.tmem.size = uuid_buf_size;
	msg_arg->params[0].u.tmem.buf_ptr = pa_addr;
	return 0;

out:
	tee_shm_free(*shm);
	return rc;
}

static int metadata_to_msg_param(struct tee_context *ctx,
	const struct tee_ioctl_open_session_arg *arg,
	struct optee_msg_arg *msg_arg, struct tee_shm **ca_shm,
	struct tee_shm **uuid_shm)
{
	int rc;
	uuid_t client_uuid;

	msg_arg->cmd = OPTEE_MSG_CMD_OPEN_SESSION;
	msg_arg->cancel_id = arg->cancel_id;
	msg_arg->num_params = arg->num_params + 2;

	/*
	 * Initialize and add the meta parameters needed when opening a
	 * session.
	 */
	if (DEBUG_CALL)
		pr_info("UUID: 0x%x%x%x%x-0x%x%x-0x%x%x-0x%x%x%x%x%x%x%x%x\n",
			arg->uuid[0], arg->uuid[1], arg->uuid[2],
			arg->uuid[3], arg->uuid[4], arg->uuid[5], arg->uuid[6],
			arg->uuid[7], arg->uuid[8], arg->uuid[9], arg->uuid[10],
			arg->uuid[11], arg->uuid[12], arg->uuid[13],
			arg->uuid[14], arg->uuid[15]);

	msg_arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_TMEM_INPUT |
							  OPTEE_MSG_ATTR_META;
	msg_arg->params[1].attr = OPTEE_MSG_ATTR_TYPE_TMEM_INPUT |
							  OPTEE_MSG_ATTR_META;

	rc = tee_session_calc_client_uuid(&client_uuid, arg->clnt_login,
					  arg->clnt_uuid);
	if (rc) {
		return rc;
	}

	rc = uuid_to_msg_param(ctx, uuid_shm, arg->uuid, client_uuid,
						   arg->clnt_login, msg_arg);
	if (rc) {
		return rc;
	}

	return ca_path_to_msg_param(ctx, ca_shm, msg_arg);
}

#define MITEE_CA_PATH_MATCH(path, pfx) ({				\
	int __l = strlen(path);					\
	(__l == (int)sizeof(pfx) - 1 ||					\
	 (__l > (int)sizeof(pfx) - 1 && (path)[sizeof(pfx) - 1] == '/')) &&	\
		!strncmp(path, pfx, sizeof(pfx) - 1);			\
})

static int auth_ca_name(char *path)
{
	/* only a few vendor directories may talk to the TEE */
	if (MITEE_CA_PATH_MATCH(path, "/vendor/bin") ||
	    MITEE_CA_PATH_MATCH(path, "/odm/bin") ||
	    MITEE_CA_PATH_MATCH(path, "/apex/com.google.android.widevine/bin/hw"))
		return 0;

	pr_err("fail to auth ca path: %s\n", path);
	return -EINVAL;
}

int optee_open_session(struct tee_context *ctx,
		       struct tee_ioctl_open_session_arg *arg,
		       struct tee_param *param)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_msg_arg *msg_arg;
	struct optee_session *sess = NULL;
	uuid_t client_uuid;
	char buf[512];
	char *path;
	size_t sz;
	u32 num_params = arg->num_params;
	int rc;

	memset(buf, 0, sizeof(buf));
	/* stock reads task_struct.active_mm (+0x5a8), not mm (+0x5a0): kernel threads have mm == NULL */
	path = d_path(&current->active_mm->exe_file->f_path, buf, sizeof(buf));

	if (auth_ca_name(path)) {
		pr_err("auth ca name error\n");
		return -EINVAL;
	}

	/* +2 for the meta parameters added below */
	sz = OPTEE_MSG_GET_ARG_SIZE(num_params + 2);
	msg_arg = kzalloc(sz, GFP_KERNEL);
	if (!msg_arg)
		return -ENOMEM;
	msg_arg->cmd = OPTEE_MSG_CMD_OPEN_SESSION;
	msg_arg->cancel_id = arg->cancel_id;
	msg_arg->num_params = arg->num_params + 2;

	/* Initialize and add the meta parameters needed when opening a session. */
	msg_arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT | OPTEE_MSG_ATTR_META;
	msg_arg->params[1].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT | OPTEE_MSG_ATTR_META;
	memcpy(&msg_arg->params[0].u.value, arg->uuid, sizeof(arg->uuid));
	msg_arg->params[1].u.value.c = arg->clnt_login;

	rc = tee_session_calc_client_uuid(&client_uuid, arg->clnt_login, arg->clnt_uuid);
	if (rc)
		goto out;
	memcpy(&msg_arg->params[1].u.value, &client_uuid, sizeof(client_uuid));

	rc = optee->ops->to_msg_param(optee, msg_arg->params + 2, arg->num_params, param);
	if (rc)
		goto out;

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess) {
		rc = -ENOMEM;
		goto out;
	}

	if (optee->ops->do_call_with_arg(ctx, msg_arg)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}

	if (msg_arg->ret == TEEC_SUCCESS) {
		/* A new session has been created, add it to the list. */
		sess->session_id = msg_arg->session;
		mutex_lock(&ctxdata->mutex);
		list_add(&sess->list_node, &ctxdata->sess_list);
		mutex_unlock(&ctxdata->mutex);
	} else {
		pr_warn("%s failed, msg_arg->ret: %#x\n", __func__, msg_arg->ret);
		kfree(sess);
	}

	if (optee->ops->from_msg_param(optee, param, arg->num_params, msg_arg->params + 2)) {
		arg->ret = TEEC_ERROR_COMMUNICATION;
		arg->ret_origin = TEEC_ORIGIN_COMMS;
		/* Close session again to avoid leakage */
		optee_close_session(ctx, msg_arg->session);
	} else {
		arg->session = msg_arg->session;
		arg->ret = msg_arg->ret;
		arg->ret_origin = msg_arg->ret_origin;
	}
out:
	kfree(msg_arg);
	return rc;
}

int optee_close_session_helper(struct tee_context *ctx, u32 session)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_msg_arg *msg_arg = NULL;
	size_t sz = OPTEE_MSG_GET_ARG_SIZE(0);

	msg_arg = kzalloc(sz, GFP_KERNEL);
	if (!msg_arg) {
		return -ENOMEM;
	}

	msg_arg->num_params = 0;
	msg_arg->cmd = OPTEE_MSG_CMD_CLOSE_SESSION;
	msg_arg->session = session;
	optee->ops->do_call_with_arg(ctx, msg_arg);

	kfree(msg_arg);

	return 0;
}

int optee_close_session(struct tee_context *ctx, u32 session)
{
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_session *sess;

	/* Check that the session is valid and remove it from the list */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, session);
	if (sess)
		list_del(&sess->list_node);
	mutex_unlock(&ctxdata->mutex);
	if (!sess) {
		return -EINVAL;
	}
	kfree(sess);

	return optee_close_session_helper(ctx, session);
}

int optee_invoke_func(struct tee_context *ctx, struct tee_ioctl_invoke_arg *arg,
		      struct tee_param *param)
{
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	struct optee_msg_arg *msg_arg = NULL;
	struct optee_session *sess = NULL;
	size_t sz = OPTEE_MSG_GET_ARG_SIZE(arg->num_params);
	int rc;


	/* Check that the session is valid */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, arg->session);
	mutex_unlock(&ctxdata->mutex);
	if (!sess) {
		return -EINVAL;
	}

	msg_arg = kzalloc(sz, GFP_KERNEL);
	if (!msg_arg) {
		return -ENOMEM;
	}
	msg_arg->num_params = arg->num_params;
	msg_arg->cmd = OPTEE_MSG_CMD_INVOKE_COMMAND;
	msg_arg->func = arg->func;
	msg_arg->session = arg->session;
	msg_arg->cancel_id = arg->cancel_id;
	rc = optee->ops->to_msg_param(optee, msg_arg->params, arg->num_params,
				      param);
	if (rc) {
		goto out;
	}

	if (optee->ops->do_call_with_arg(ctx, msg_arg)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}
	if (optee->ops->from_msg_param(optee, param, arg->num_params,
				       msg_arg->params)) {
		msg_arg->ret = TEEC_ERROR_COMMUNICATION;
		msg_arg->ret_origin = TEEC_ORIGIN_COMMS;
	}

	arg->ret = msg_arg->ret;
	arg->ret_origin = msg_arg->ret_origin;
out:
	kfree(msg_arg);
	return rc;
}

int optee_cancel_req(struct tee_context *ctx, u32 cancel_id, u32 session)
{
	pr_err("%s cancel is not support!\n", __func__);
	return -EACCES;

#if 0
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct optee_context_data *ctxdata = ctx->data;
	struct tee_shm *shm;
	struct optee_msg_arg *msg_arg;
	struct optee_session *sess;

	/* Check that the session is valid */
	mutex_lock(&ctxdata->mutex);
	sess = find_session(ctxdata, session);
	mutex_unlock(&ctxdata->mutex);
	if (!sess)
		return -EINVAL;

	shm = optee_get_msg_arg(ctx, 0, &msg_arg);
	if (IS_ERR(shm))
		return PTR_ERR(shm);

	msg_arg->cmd = OPTEE_MSG_CMD_CANCEL;
	msg_arg->session = session;
	msg_arg->cancel_id = cancel_id;
	optee->ops->do_call_with_arg(ctx, shm);

	tee_shm_free(shm);
	return 0;
#endif
}

static bool is_normal_memory(pgprot_t p)
{
#if defined(CONFIG_ARM)
	return (((pgprot_val(p) & L_PTE_MT_MASK) == L_PTE_MT_WRITEALLOC) ||
		((pgprot_val(p) & L_PTE_MT_MASK) == L_PTE_MT_WRITEBACK));
#elif defined(CONFIG_ARM64)
	return (pgprot_val(p) & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL);
#else
#error "Unuspported architecture"
#endif
}

static int __check_mem_type(struct mm_struct *mm, unsigned long start,
			    unsigned long end)
{
	struct vm_area_struct *vma;

	VMA_ITERATOR(vmi, mm, start);

	for_each_vma_range(vmi, vma, end) {
		if (!is_normal_memory(vma->vm_page_prot)) {
			return -EINVAL;
		}
	}

	return 0;
}

int optee_check_mem_type(unsigned long start, size_t num_pages)
{
	struct mm_struct *mm = current->mm;
	int rc;

	/*
	 * Allow kernel address to register with OP-TEE as kernel
	 * pages are configured as normal memory only.
	 */
	if (virt_addr_valid((const void *)(uintptr_t)start))
		return 0;

	mmap_read_lock(mm);
	rc = __check_mem_type(mm, start, start + num_pages * PAGE_SIZE);
	mmap_read_unlock(mm);

	return rc;
}
