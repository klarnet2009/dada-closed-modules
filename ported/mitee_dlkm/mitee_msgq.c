// SPDX-License-Identifier: GPL-2.0-only
/*
 * mitee msgq: Gunyah message-queue transport of the Xiaomi MiTEE driver (OS4 vendor_dlkm/mitee_dlkm.ko).
 *
 * Own source, recovered from the disassembly of the stock module (no public source exists for this file;
 * the surrounding mitee/optee/tee code is the public Xiaomi XRING driver, MiCode dijun-v-oss, GPL-2.0).
 * Wire format, state machine and struct offsets were read from the machine code: see STATUS.md.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/rhashtable.h>
#include <linux/notifier.h>
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_msgq.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <soc/qcom/secure_buffer.h>
#include "optee_private.h"
#include "optee_ffa.h"
#include "rpc_callback.h"
#include "dynamic_mem.h"

#define MITEE_MSGQ_MAGIC 0x5147534d4f414958ULL /* "XIAOMSGQ" */

/* message types on the queue (header word at +0x8) */
#define MSGQ_TYPE_CTRL		0 /* TEE -> REE handshake */
#define MSGQ_TYPE_FASTCALL_REQ	2
#define MSGQ_TYPE_YIELD_REQ	3
#define MSGQ_TYPE_YIELD_RESP	4
#define MSGQ_TYPE_FASTCALL_RESP	5

#define MSGQ_SRC_REE		0x8000
#define MSGQ_HDR_SIZE		0x28
#define MSGQ_MSG_SIZE		0x50 /* header + cmd + 4 args */
#define MSGQ_RECV_SIZE		240

#define MITEE_MSGQ_STATE_UP		2
#define MITEE_SHM_STATIC_SIZE		0x200000

/* fastcall commands handled locally (same numbers as OPTEE_FFA_*) */
#define MITEE_FC_GET_SHM_CONFIG		10
#define MITEE_FC_GET_LOG_BUFFER		11
#define MITEE_FC_SET_MSG_BUFFER		12

struct mitee_msgq_msg {
	u64 magic;
	u32 type;
	u32 len;
	u32 ids; /* src (low 16) | dst (high 16) */
	u32 msg_id;
	u64 rsv[2];
	u64 payload[5]; /* cmd, 4 args */
};

struct mitee_recv_slot {
	u64 v[5];
};

struct mitee_recv_queue {
	u32 head;
	u32 tail;
	u32 size;
	u32 pad;
	struct mitee_recv_slot slot[32];
	spinlock_t lock;
	struct completion c;
};

struct mitee_fastcall_data {
	bool valid;
	u64 resp[5];
	struct mutex fastcall_mut;
	spinlock_t lock;
	struct completion c;
};

struct mitee_msgq_dev {
	struct device *dev;
	struct resource res;
	u8 pad0[0x68 - 0x8 - sizeof(struct resource)];
	u64 pa;
	u64 size;
	void *va;
	u32 shm_handle;
	u32 shm_label;
	bool master;
	u32 peer_name;
	struct notifier_block nb;
	u16 this_vmid;
	u16 peer_vmid;
	void *msgq;
	struct task_struct *recv_task;
	struct mitee_recv_queue *rx;
	struct mitee_fastcall_data *f_data;
	u64 task_buf_pa;
	u64 task_buf_size;
	u64 unk_e0;
	u32 msg_handle;
	u32 msg_label;
	enum tee_state vm_state;
	struct rhashtable shm_ht;
	u8 pad1[0x1a0 - 0xf8 - sizeof(struct rhashtable)];
	struct mutex shm_mutex;
	u8 pad2[0x1d0 - 0x1a0 - sizeof(struct mutex)];
	struct list_head secmem_list;
	struct mutex secmem_mut;
	u8 pad3[0x210 - 0x1e0 - sizeof(struct mutex)];
};

struct mitee_shm_entry {
	struct tee_shm *shm;
	struct sg_table *sgt;
	u64 handle;
	struct rhash_head node;
};

struct mitee_secmem {
	struct sg_table *sgt;
	u64 handle;
	u32 size;
	struct list_head node;
};

static struct mitee_msgq_dev *g_msgq_dev;

static const struct rhashtable_params shm_rhash_params = {
	.key_len = sizeof(u64),
	.key_offset = offsetof(struct mitee_shm_entry, handle),
	.head_offset = offsetof(struct mitee_shm_entry, node),
	.automatic_shrinking = true,
};

static void rh_free_fn(void *ptr, void *arg)
{
	kfree(ptr);
}

static struct gh_sgl_desc *sgt_to_gh_sgl_desc(struct sg_table *sgt, u32 nents)
{
	struct gh_sgl_desc *desc;
	struct scatterlist *sg;
	u32 i;

	if (sgt->orig_nents < nents) {
		pr_err("mitee msgq: generate sgl_desc failed, invalid arg");
		return NULL;
	}

	desc = kmalloc(offsetof(struct gh_sgl_desc, sgl_entries) + nents * sizeof(struct gh_sgl_entry),
		       GFP_KERNEL | __GFP_ZERO);
	if (!desc) {
		pr_err("mitee msgq: generate sgl_desc failed, no memory");
		return NULL;
	}

	desc->n_sgl_entries = nents;
	sg = sgt->sgl;
	for (i = 0; i < nents; i++) {
		desc->sgl_entries[i].ipa_base = page_to_phys(sg_page(sg)) + sg->offset;
		desc->sgl_entries[i].size = sg->length;
		sg = sg_next(sg);
	}

	return desc;
}

static void mitee_shm_rem_mem_handle(struct mitee_msgq_dev *qdev, u64 handle)
{
	struct mitee_shm_entry *entry;

	mutex_lock(&qdev->shm_mutex);
	entry = rhashtable_lookup_fast(&qdev->shm_ht, &handle, shm_rhash_params);
	if (entry)
		rhashtable_remove_fast(&qdev->shm_ht, &entry->node, shm_rhash_params);
	mutex_unlock(&qdev->shm_mutex);
	kfree(entry);
}

static int mitee_msgq_share_mem_contiguous(struct mitee_msgq_dev *qdev, u64 pa, u64 size,
					   u32 label, u32 *handle)
{
	u64 src_back;
	u64 src_fwd;
	struct qcom_scm_vmperm newvms[2];
	struct qcom_scm_vmperm back;
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	int ret;

	back.vmid = qdev->this_vmid;
	back.perm = PERM_READ | PERM_WRITE | PERM_EXEC;
	newvms[0].vmid = qdev->this_vmid;
	newvms[0].perm = PERM_READ | PERM_WRITE;
	newvms[1].vmid = qdev->peer_vmid;
	newvms[1].perm = PERM_READ | PERM_WRITE;
	src_back = BIT(qdev->this_vmid) | BIT(qdev->peer_vmid);
	src_fwd = BIT(qdev->this_vmid);

	ret = qcom_scm_assign_mem(pa, size, &src_fwd, newvms, 2);
	if (ret) {
		pr_err("%s: mitee msgq qcom_scm_assign_mem failed addr=%llx size=%llx err=%d\n",
		       "mitee_msgq_share_mem_contiguous", pa, size, ret);
		return ret;
	}

	acl = kmalloc(struct_size(acl, acl_entries, 2), GFP_KERNEL | __GFP_ZERO);
	if (!acl)
		return -ENOMEM;
	sgl = kmalloc(offsetof(struct gh_sgl_desc, sgl_entries) + sizeof(struct gh_sgl_entry),
		      GFP_KERNEL | __GFP_ZERO);
	if (!sgl) {
		kfree(acl);
		return -ENOMEM;
	}

	acl->n_acl_entries = 2;
	acl->acl_entries[0].vmid = qdev->this_vmid;
	acl->acl_entries[0].perms = 6;
	acl->acl_entries[1].vmid = qdev->peer_vmid;
	acl->acl_entries[1].perms = 6;
	sgl->n_sgl_entries = 1;
	sgl->sgl_entries[0].ipa_base = pa;
	sgl->sgl_entries[0].size = size;

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, label, acl, sgl, NULL, handle);
	if (ret) {
		pr_err("%s: mitee msgq gh_rm_mem_share failed addr=%llx size=%llu err=%d\n",
		       "mitee_msgq_share_mem_contiguous", pa, size, ret);
		qcom_scm_assign_mem(pa, size, &src_back, &back, 1);
	}
	kfree(acl);
	kfree(sgl);
	return ret;
}

static int mitee_msgq_share_static_mem(struct mitee_msgq_dev *qdev)
{
	return mitee_msgq_share_mem_contiguous(qdev, qdev->res.start,
					       resource_size(&qdev->res),
					       qdev->shm_label, &qdev->shm_handle);
}

static void mitee_msgq_unshare_static_mem(struct mitee_msgq_dev *qdev, u16 this_vmid, u16 peer_vmid)
{
	u64 srcvm[1];
	struct qcom_scm_vmperm newvm;
	int ret;

	srcvm[0] = BIT(this_vmid) | BIT(peer_vmid);
	newvm.vmid = this_vmid;
	newvm.perm = PERM_READ | PERM_WRITE | PERM_EXEC;

	ret = ghd_rm_mem_reclaim(qdev->shm_handle, 0);
	if (ret)
		pr_err("%s: mitee msgq Gunyah reclaim failed\n", "mitee_msgq_unshare_mem");

	qcom_scm_assign_mem(qdev->res.start, resource_size(&qdev->res), srcvm, &newvm, 1);
}

static u32 mitee_msgq_rpc_callback(struct optee_msg_param_value *value, void *buf,
				   u32 size_in, u32 *size_out);

static int mitee_msgq_rm_cb(struct notifier_block *nb, unsigned long cmd, void *data)
{
	struct mitee_msgq_dev *qdev = container_of(nb, struct mitee_msgq_dev, nb);
	struct gh_rm_notif_vm_status_payload *payload = data;
	gh_vmid_t this_vmid = 0, peer_vmid = 0;
	int ret;

	if (cmd != GH_RM_NOTIF_VM_STATUS)
		return 0;

	if (payload->vm_status != GH_RM_VM_STATUS_RESET && payload->vm_status != GH_RM_VM_STATUS_READY)
		return 0;

	if (ghd_rm_get_vmid(qdev->peer_name, &peer_vmid))
		return 0;
	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &this_vmid))
		return 0;
	if (peer_vmid != payload->vmid)
		return 0;

	if (payload->vm_status == GH_RM_VM_STATUS_READY) {
		qdev->vm_state = 1;
		qdev->this_vmid = this_vmid;
		qdev->peer_vmid = peer_vmid;
		ret = mitee_msgq_share_static_mem(qdev);
		if (ret) {
			pr_err("%s: mitee msgq failed to share memory\n", "mitee_msgq_rm_cb");
			return 0;
		}
		mitee_rpc_register_callback(REE_CALLBACK_MODULE_TEE_FRAMEWORK, 0, mitee_msgq_rpc_callback);
		mitee_rpc_register_callback(REE_CALLBACK_MODULE_TEE_FRAMEWORK, 1, mitee_msgq_rpc_callback);
	}

	if (payload->vm_status == GH_RM_VM_STATUS_RESET)
		mitee_msgq_unshare_static_mem(qdev, this_vmid, peer_vmid);

	return 0;
}

static int mitee_recv_task_fn(void *data)
{
	struct mitee_msgq_dev *qdev = data;
	struct mitee_msgq_msg *msg;
	size_t recv_size = 0;
	int ret = 0;

	pr_info("mitee msgq: recv task start\n");
	if (!qdev)
		__builtin_trap(); /* stock: NULL qdev compiles to a plain trap (brk #1), no bug-table entry */

	msg = kmalloc(MSGQ_RECV_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!msg) {
		dev_err(qdev->dev, "mitee msgq: without enough memory");
		return -1;
	}

	while (!kthread_should_stop()) {
		memset(msg, 0, MSGQ_RECV_SIZE);
		ret = gh_msgq_recv(qdev->msgq, msg, MSGQ_RECV_SIZE, &recv_size, 0);
		if (ret < 0 || recv_size > MSGQ_RECV_SIZE) {
			dev_err(qdev->dev, "mitee msgq: Failed to receive the msg with status %d", ret);
			continue;
		}
		if (msg->magic != MITEE_MSGQ_MAGIC) {
			pr_err("mitee msgq: msgq data magic num is wrong!\n");
			continue;
		}
		if (msg->len > MSGQ_RECV_SIZE) {
			pr_err("mitee msgq: msgq data length is too long!\n");
			continue;
		}

		switch (msg->type) {
		case MSGQ_TYPE_CTRL: {
			u32 flags = msg->ids;

			if ((flags >> 16) != MSGQ_SRC_REE) {
				pr_err("mitee msgq: unknown server id");
				break;
			}
			if (msg->msg_id == 2) {
				if (msg->len)
					pr_err("mitee msgq: incorrect msg length");
				qdev->vm_state = MITEE_MSGQ_STATE_READY;
				break;
			} else if (msg->msg_id == 1) {
				if (msg->len)
					pr_err("mitee msgq: incorrect msg length");
				msg->ids = flags | MSGQ_SRC_REE;
				msg->payload[0] = 5;
				msg->payload[1] = qdev->pa;
				msg->type = MSGQ_TYPE_FASTCALL_REQ;
				msg->len = MSGQ_HDR_SIZE;
				msg->payload[2] = MITEE_SHM_STATIC_SIZE;
				msg->payload[3] = qdev->shm_handle;
				msg->payload[4] = qdev->shm_label;
				gh_msgq_send(qdev->msgq, msg, MSGQ_MSG_SIZE, 0);
				udelay(5000);
				mitee_msgq_share_mem_contiguous(qdev, qdev->task_buf_pa, qdev->task_buf_size,
								qdev->msg_label, &qdev->msg_handle);
				msg->ids |= MSGQ_SRC_REE;
				msg->payload[0] = 12;
				msg->payload[1] = qdev->task_buf_pa;
				msg->payload[2] = qdev->task_buf_size;
				msg->payload[3] = qdev->msg_handle;
				msg->payload[4] = qdev->msg_label;
				msg->type = MSGQ_TYPE_FASTCALL_REQ;
				msg->len = MSGQ_HDR_SIZE;
				gh_msgq_send(qdev->msgq, msg, MSGQ_MSG_SIZE, 0);
				break;
			} else if (msg->msg_id == 0) {
				if (qdev->vm_state >= MITEE_MSGQ_STATE_UP)
					dev_err(qdev->dev, "mitee msgq: msgq state error");
				if (msg->len) {
					pr_err("mitee msgq: incorrect msg length");
					break;
				}
				msg->ids |= MSGQ_SRC_REE;
				gh_msgq_send(qdev->msgq, msg, MSGQ_HDR_SIZE, 0);
				qdev->vm_state = MITEE_MSGQ_STATE_UP;
				break;
			} else {
				pr_info("mitee msgq: unknown msg_id 0x%x\n", msg->msg_id);
				break;
			}
			break;
		}
		case 1:
			if (qdev->vm_state <= 1)
				pr_err("mitee msgq process data state error");
			pr_err("mitee msgq: warining, should not reach here ");
			break;
		case MSGQ_TYPE_YIELD_RESP: {
			struct mitee_recv_queue *q = qdev->rx;
			u32 next;

			if (qdev->vm_state != MITEE_MSGQ_STATE_READY)
				pr_err("mitee msgq process reply state error");
			if (msg->len != MSGQ_HDR_SIZE) {
				pr_err("mitee msgq: incorrect data size for reply");
				break;
			}
			spin_lock(&q->lock);
			next = (q->head + 1) % q->size;
			if (next == q->tail) {
				spin_unlock(&q->lock);
				pr_err("mitee msgq: recv_queue is full\n");
				break;
			}
			q->slot[q->head] = *(struct mitee_recv_slot *)msg->payload;
			q->head = next;
			spin_unlock(&q->lock);
			complete(&q->c);
			break;
		}
		case MSGQ_TYPE_FASTCALL_RESP: {
			struct mitee_fastcall_data *f = qdev->f_data;

			if (qdev->vm_state <= 1)
				pr_err("mitee msgq process fastcall state error");
			if (msg->len != MSGQ_HDR_SIZE) {
				pr_err("mitee msgq: incorrect data size for fastcall reply");
				break;
			}
			spin_lock(&f->lock);
			if (f->valid) {
				spin_unlock(&f->lock);
				pr_err("mitee msgq: recv_queue is full\n");
				break;
			}
			memcpy(f->resp, msg->payload, sizeof(f->resp));
			f->valid = true;
			spin_unlock(&f->lock);
			complete(&f->c);
			break;
		}
		default:
			pr_err("unknown msg type from mitee %x\n", msg->type);
			break;
		}
	}

	kfree(msg);
	return ret;
}

static enum tee_state mitee_msgq_get_vm_state(void)
{
	return g_msgq_dev->vm_state;
}

static int mitee_msgq_round_trip_call(union common_args *args)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	u64 r0, r1, r2, r3, r4;
	u64 cmd;

	if (!qdev) {
		pr_err("mitee msgq: driver didn't initialized, try later\n");
		return -1;
	}

	cmd = args->in.cmd;
	if (cmd & BIT(31)) {
		struct mitee_recv_queue *q;

		if (qdev->vm_state == TEE_STATE_DOWN || qdev->vm_state == TEE_STATE_VM_READY) {
			pr_err("mitee msgq: msgq didn't set up, try later\n");
			return -1;
		}
		{
			struct mitee_msgq_msg msg;

			memset(&msg, 0, sizeof(msg));
			msg.magic = MITEE_MSGQ_MAGIC;
			msg.type = MSGQ_TYPE_YIELD_REQ;
			msg.len = MSGQ_HDR_SIZE;
			msg.ids = MSGQ_SRC_REE;
			msg.payload[0] = cmd;
			msg.payload[1] = args->in.param[0];
			msg.payload[2] = args->in.param[1];
			msg.payload[3] = args->in.param[2];
			msg.payload[4] = args->in.param[3];
			gh_msgq_send(qdev->msgq, &msg, MSGQ_MSG_SIZE, 0);
		}

		q = qdev->rx;
		spin_lock(&q->lock);
		while (q->head == q->tail) {
			spin_unlock(&q->lock);
			wait_for_completion(&q->c);
			spin_lock(&q->lock);
		}
		r0 = q->slot[q->tail].v[0];
		r1 = q->slot[q->tail].v[1];
		r2 = q->slot[q->tail].v[2];
		r3 = q->slot[q->tail].v[3];
		r4 = q->slot[q->tail].v[4];
		memset(&q->slot[q->tail], 0, sizeof(q->slot[0]));
		q->tail = (q->tail + 1) % q->size;
		spin_unlock(&q->lock);
		goto reply;
	}

	switch (cmd) {
	case MITEE_FC_GET_SHM_CONFIG:
		args->out.resp = qdev->pa;
		args->out.param[0] = MITEE_SHM_STATIC_SIZE;
		args->out.param[1] = 1;
		return 0;
	case MITEE_FC_GET_LOG_BUFFER:
		args->out.resp = qdev->pa + MITEE_SHM_STATIC_SIZE;
		args->out.param[0] = qdev->size - MITEE_SHM_STATIC_SIZE;
		return 0;
	case MITEE_FC_SET_MSG_BUFFER:
		qdev->task_buf_pa = args->in.param[0];
		qdev->task_buf_size = args->in.param[1];
		qdev->msg_label = 0;
		return 0;
	default: {
		struct mitee_fastcall_data *f, *f2;
		u64 p0, p1, p2, p3;

		if (qdev->vm_state == TEE_STATE_DOWN || qdev->vm_state == TEE_STATE_VM_READY) {
			pr_err("mitee msgq: msgq didn't set up, try later\n");
			return -1;
		}
		f = qdev->f_data;
		p0 = args->in.param[0];
		p1 = args->in.param[1];
		p2 = args->in.param[2];
		p3 = args->in.param[3];
		mutex_lock(&f->fastcall_mut);
		{
			struct mitee_msgq_msg msg;

			memset(&msg, 0, sizeof(msg));
			msg.magic = MITEE_MSGQ_MAGIC;
			msg.type = MSGQ_TYPE_FASTCALL_REQ;
			msg.len = MSGQ_HDR_SIZE;
			msg.ids = MSGQ_SRC_REE;
			msg.payload[0] = cmd;
			msg.payload[1] = p0;
			msg.payload[2] = p1;
			msg.payload[3] = p2;
			msg.payload[4] = p3;
			gh_msgq_send(qdev->msgq, &msg, MSGQ_MSG_SIZE, 0);
		}

		f2 = qdev->f_data;
		wait_for_completion(&f2->c);
		spin_lock(&f2->lock);
		if (!f2->valid) {
			spin_unlock(&f2->lock);
			pr_err("mitee msgq: failed to get fastcall reply\n");
			pr_err("mitee msgq: read msg from mitee vm failed with 0x%x", -1);
			mutex_unlock(&f->fastcall_mut);
			return -1;
		}
		r0 = f2->resp[0];
		r1 = f2->resp[1];
		r2 = f2->resp[2];
		r3 = f2->resp[3];
		r4 = f2->resp[4];
		memset(f2->resp, 0, sizeof(f2->resp));
		f2->valid = false;
		spin_unlock(&f2->lock);
		mutex_unlock(&f->fastcall_mut);
		goto reply;
	}
	}

reply:
	args->in.cmd = r0;
	args->in.param[0] = r1;
	args->in.param[1] = r2;
	args->in.param[2] = r3;
	args->in.param[3] = r4;
	return 0;
}

/* wire layout of an optee_msg_param carrying a shm reference (u.rmem) in the stock */
struct mitee_rmem_wire {
	u32 offs_lo;
	u16 offs_hi; /* shm offset bits 32..47 */
	u16 shm_off; /* tee_shm::offset */
	u64 size;
	u64 shm_ref;
};

void mitee_msgq_from_msg_param(struct optee *optee, struct tee_param *p, u32 attr,
				      const struct optee_msg_param *mp)
{
	const struct mitee_rmem_wire *w = (const struct mitee_rmem_wire *)&mp->u;
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct mitee_shm_entry *entry;
	struct tee_shm *shm;
	u64 handle;
	u64 offs;

	p->attr = attr;
	p->u.memref.size = w->size;
	handle = w->shm_ref;
	if (handle == (u64)-1) {
		offs = 0;
		p->u.memref.shm = NULL;
	} else {
		mutex_lock(&qdev->shm_mutex);
		entry = rhashtable_lookup_fast(&qdev->shm_ht, &handle, shm_rhash_params);
		if (entry) {
			shm = entry->shm;
			mutex_unlock(&qdev->shm_mutex);
			p->u.memref.shm = shm;
			if (shm)
				offs = w->offs_lo | ((u64)w->offs_hi << 32);
			else
				offs = 0;
		} else {
			mutex_unlock(&qdev->shm_mutex);
			offs = 0;
			p->u.memref.shm = NULL;
		}
	}
	p->u.memref.shm_offs = offs;
}

int mitee_msgq_to_msg_param(struct optee_msg_param *mp, const struct tee_param *p)
{
	struct mitee_rmem_wire *w = (struct mitee_rmem_wire *)&mp->u;
	struct tee_shm *shm = p->u.memref.shm;
	u64 offs;
	u64 ref;

	mp->attr = p->attr;
	if (shm) {
		offs = p->u.memref.shm_offs;
		w->shm_off = shm->offset;
		w->offs_lo = offs;
		w->offs_hi = offs >> 32;
		if (offs >> 48)
			return -EINVAL;
		ref = shm->sec_world_id;
	} else {
		w->offs_lo = 0;
		w->offs_hi = 0;
		w->shm_off = 0;
		w->size = 0;
		ref = -1;
	}
	w->shm_ref = ref;
	w->size = p->u.memref.size;
	return 0;
}

int mitee_msgq_shm_register(struct tee_context *ctx, struct tee_shm *shm,
				   struct page **pages, size_t num_pages, unsigned long start)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_shm_entry *entry;
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	struct sg_table *sgt;
	u32 srcvm[1];
	int dstvm[2];
	int dstperm[2];
	u32 handle = 0;
	u32 nents;
	int ret;

	if (optee->supp_teedev == ctx->teedev) {
		pr_err("%s mitee msgq: do not support supp !\n", "mitee_msgq_shm_register");
		return -EACCES;
	}

	ret = optee_check_mem_type(start, num_pages);
	if (ret)
		return ret;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL | __GFP_ZERO);
	if (!sgt) {
		pr_err("%s mitee msgq: without enough memory!\n", "mitee_msgq_shm_register");
		return -ENOMEM;
	}

	ret = sg_alloc_table_from_pages_segment(sgt, pages, num_pages, 0, num_pages << PAGE_SHIFT,
						UINT_MAX, GFP_KERNEL);
	if (ret)
		goto free_sgt;

	acl = kmalloc(struct_size(acl, acl_entries, 2), GFP_KERNEL | __GFP_ZERO);
	if (!acl) {
		ret = -ENOMEM;
		goto free_table;
	}

	nents = sgt->orig_nents;
	acl->n_acl_entries = 2;
	acl->acl_entries[0].vmid = qdev->this_vmid;
	acl->acl_entries[0].perms = 6;
	acl->acl_entries[1].vmid = qdev->peer_vmid;
	acl->acl_entries[1].perms = 6;

	srcvm[0] = qdev->this_vmid;
	dstvm[0] = qdev->this_vmid;
	dstvm[1] = qdev->peer_vmid;
	dstperm[0] = 6;
	dstperm[1] = 6;
	ret = hyp_assign_table(sgt, srcvm, 1, dstvm, dstperm, 2);
	if (ret)
		/* the stock ignores the failure after logging it (inlined assign_sgt_perm) */
		pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n", "assign_sgt_perm", ret);

	sgl = sgt_to_gh_sgl_desc(sgt, nents);
	if (!sgl) {
		ret = -ENOMEM;
		kfree(acl);
		goto reclaim;
	}

	ret = ghd_rm_mem_share(GH_RM_MEM_TYPE_NORMAL, 0, 0, acl, sgl, NULL, &handle);
	if (ret) {
		pr_err("mitee msgq shared memory failed with rc (0x%d)\n", ret);
		handle = 0;
	}
	kfree(sgl);
	kfree(acl);
	if (ret)
		goto reclaim;

	ret = -ENOMEM;
	entry = kmalloc(sizeof(*entry), GFP_KERNEL);
	if (entry) {
		entry->shm = shm;
		entry->sgt = sgt;
		entry->handle = handle;

		mutex_lock(&qdev->shm_mutex);
		ret = rhashtable_lookup_insert_fast(&qdev->shm_ht, &entry->node, shm_rhash_params);
		mutex_unlock(&qdev->shm_mutex);
	}
	if (ret) {
		kfree(entry);
		pr_err("%s mitee msgq: add handle failed: %d!\n", "mitee_msgq_shm_register", ret);
		goto reclaim;
	}

	shm->sec_world_id = handle;
	return 0;

reclaim:
	ghd_rm_mem_reclaim(handle, 0);
	srcvm[0] = qdev->this_vmid;
	dstvm[0] = qdev->this_vmid;
	dstvm[1] = qdev->peer_vmid;
	dstperm[0] = 7;
	hyp_assign_table(sgt, (u32 *)dstvm, 2, (int *)srcvm, dstperm, 1);
free_table:
	sg_free_table(sgt);
free_sgt:
	kfree(sgt);
	return ret;
}

int mitee_msgq_shm_unregister(struct tee_context *ctx, struct tee_shm *shm)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct optee *optee = tee_get_drvdata(ctx->teedev);
	struct mitee_shm_entry *entry;
	struct optee_msg_arg *msg_arg;
	struct sg_table *sgt;
	u32 srcvm[2];
	int dstvm[1];
	int dstperm[1];
	u64 handle = shm->sec_world_id;
	int rc, rc2;

	msg_arg = kzalloc(sizeof(*msg_arg) + sizeof(struct optee_msg_param), GFP_KERNEL);
	if (!msg_arg)
		return -ENOMEM;
	msg_arg->cmd = OPTEE_MSG_CMD_UNREGISTER_SHM;
	msg_arg->num_params = 1;
	msg_arg->params[0].attr = OPTEE_MSG_ATTR_TYPE_RMEM_INPUT;
	msg_arg->params[0].u.rmem.shm_ref = handle;

	mutex_lock(&qdev->shm_mutex);
	entry = rhashtable_lookup_fast(&qdev->shm_ht, &handle, shm_rhash_params);
	sgt = entry ? entry->sgt : NULL;
	mutex_unlock(&qdev->shm_mutex);
	mitee_shm_rem_mem_handle(qdev, handle);
	shm->sec_world_id = 0;

	rc = optee->ops->do_call_with_arg(ctx, msg_arg);
	if (rc)
		pr_err("mitee msgq: Unregister SHM id 0x%llx rc %d\n", handle, rc);

	rc = ghd_rm_mem_reclaim(handle, 0);
	if (rc)
		pr_err("mitee msgq mem_reclaim: failed with 0x%llx %d", handle, rc);

	srcvm[0] = qdev->this_vmid;
	srcvm[1] = qdev->peer_vmid;
	dstvm[0] = qdev->this_vmid;
	dstperm[0] = 7;
	rc2 = hyp_assign_table(sgt, srcvm, 2, dstvm, dstperm, 1);
	if (rc2)
		pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n", "assign_sgt_perm", rc2);
	sg_free_table(sgt);
	kfree(sgt);
	kfree(msg_arg);
	return rc;
}

int mitee_msgq_shm_unregister_supp(struct tee_context *ctx, struct tee_shm *shm)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct mitee_shm_entry *entry;
	struct sg_table *sgt;
	u32 srcvm[2];
	int dstvm[1];
	int dstperm[1];
	u64 handle = shm->sec_world_id;
	int rc, rc2;

	mutex_lock(&qdev->shm_mutex);
	entry = rhashtable_lookup_fast(&qdev->shm_ht, &handle, shm_rhash_params);
	sgt = entry ? entry->sgt : NULL;
	mutex_unlock(&qdev->shm_mutex);
	mitee_shm_rem_mem_handle(qdev, handle);

	rc = ghd_rm_mem_reclaim(handle, 0);
	if (rc)
		pr_err("mitee msgq mem_reclaim: failed with 0x%llx %d", handle, rc);

	srcvm[0] = qdev->this_vmid;
	srcvm[1] = qdev->peer_vmid;
	dstvm[0] = qdev->this_vmid;
	dstperm[0] = 7;
	rc2 = hyp_assign_table(sgt, srcvm, 2, dstvm, dstperm, 1);
	if (rc2)
		pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n", "assign_sgt_perm", rc2);
	sg_free_table(sgt);
	kfree(sgt);
	return rc;
}

int mitee_msgq_secmem_lend(u64 *handle_out, struct sg_table *sgt, u32 size)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct mitee_secmem *sm;
	struct gh_acl_desc *acl;
	struct gh_sgl_desc *sgl;
	u32 srcvm[1];
	int dstvm[1];
	int dstperm[1];
	u32 handle = 0;
	u32 nents;
	int ret, rc2;

	acl = kmalloc(struct_size(acl, acl_entries, 1), GFP_KERNEL | __GFP_ZERO);
	if (!acl)
		return -ENOMEM;
	nents = sgt->orig_nents;
	acl->n_acl_entries = 1;
	acl->acl_entries[0].vmid = qdev->peer_vmid;
	acl->acl_entries[0].perms = 7;
	srcvm[0] = qdev->this_vmid;
	dstvm[0] = qdev->peer_vmid;
	dstperm[0] = 7;
	ret = hyp_assign_table(sgt, srcvm, 1, dstvm, dstperm, 1);
	if (ret) {
		pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n", "assign_sgt_perm_lend", ret);
		kfree(acl);
		goto revert;
	}

	sgl = sgt_to_gh_sgl_desc(sgt, nents);
	if (!sgl) {
		ret = -ENOMEM;
		kfree(acl);
		goto revert;
	}
	ret = ghd_rm_mem_lend(GH_RM_MEM_TYPE_NORMAL, 0, 0, acl, sgl, NULL, &handle);
	if (ret) {
		pr_err("mitee msgq lend memory failed with rc (%d)\n", ret);
		handle = 0;
	}
	kfree(sgl);
	kfree(acl);
	if (!ret) {
		sm = kmalloc(sizeof(*sm), GFP_KERNEL);
		if (sm) {
			sm->sgt = sgt;
			sm->handle = handle;
			sm->size = size;
			mutex_lock(&qdev->secmem_mut);
			list_add_tail(&sm->node, &qdev->secmem_list);
			mutex_unlock(&qdev->secmem_mut);
			*handle_out = handle;
			return 0;
		}
		if (handle)
			ghd_rm_mem_reclaim(handle, 0);
	}
revert:
	srcvm[0] = qdev->peer_vmid;
	dstvm[0] = qdev->this_vmid;
	dstperm[0] = 7;
	rc2 = hyp_assign_table(sgt, srcvm, 1, dstvm, dstperm, 1);
	if (rc2)
		pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n", "assign_sgt_perm_lend", rc2);
	mitee_free_memory_sgt(size, sgt);
	return ret;
}

static u32 mitee_msgq_rpc_callback(struct optee_msg_param_value *value, void *buf,
				   u32 size_in, u32 *size_out)
{
	struct mitee_msgq_dev *qdev = g_msgq_dev;
	struct sg_table *sgt = NULL;
	u64 handle = 0;
	u32 size;
	int ret = 0;

	switch (value->a) {
	case OPTEE_REE_CALLBACK_ALLOCATE_SECMEM:
		if (value->b != 0)
			return 0xffff0006;
		size = value->c;
		ret = mitee_alloc_memory_sgt(ALIGN(size, PAGE_SIZE), &sgt);
		if (ret)
			return 0xffff0006;
		ret = mitee_msgq_secmem_lend(&handle, sgt, size);
		if (ret)
			return 0xffff0006;
		value->a = handle;
		value->b = size;
		return 0;
	case OPTEE_REE_CALLBACK_FREE_SECMEM: {
		struct mitee_secmem *sm;

		handle = value->c;
		if (value->b != 0)
			return 0xffff0006;
		mutex_lock(&qdev->secmem_mut);
		/* as in the stock: no found-check, `sm` is the list-head container when nothing matched */
		list_for_each_entry(sm, &qdev->secmem_list, node) {
			if (sm->handle == handle) {
				list_del(&sm->node);
				break;
			}
		}
		mutex_unlock(&qdev->secmem_mut);
		sgt = sm->sgt;
		size = sm->size;
		ret = ghd_rm_mem_reclaim(handle, 0);
		if (ret)
			pr_err("mitee msgq mem_reclaim: failed with 0x%llx %d", handle, ret);
		{
			u32 srcvm[1] = { qdev->peer_vmid };
			int dstvm[1] = { qdev->this_vmid };
			int dstperm[1] = { 7 };

			ret = hyp_assign_table(sgt, srcvm, 1, dstvm, dstperm, 1);
			if (ret)
				pr_err("mitee msgq %s: hyp_assign_table mem failed with err=%d\n",
				       "assign_sgt_perm_lend", ret);
		}
		kfree(sm);
		mitee_free_memory_sgt(size, sgt);
		return 0;
	}
	default:
		pr_err("mitee msgq: unknown cmd\n");
		return 0xffff0006;
	}
}

static void fastcall_data_init(struct mitee_fastcall_data *f_data)
{
	f_data->valid = false;
	memset(f_data->resp, 0, sizeof(f_data->resp));
	mutex_init(&f_data->fastcall_mut);
	spin_lock_init(&f_data->lock);
	init_completion(&f_data->c);
}

static int mitee_msgq_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mitee_msgq_dev *qdev;
	struct mitee_recv_queue *rx;
	struct mitee_fastcall_data *f;
	struct task_struct *task;
	struct device_node *sbuf;
	int ret;

	qdev = devm_kzalloc(&pdev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return -ENOMEM;
	qdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, qdev);

	rx = kzalloc(sizeof(*rx), GFP_KERNEL);
	qdev->rx = rx;
	if (!rx)
		return -ENOMEM;
	rx->head = 0;
	rx->tail = 0;
	rx->size = 32;
	memset((u8 *)rx + 0x10, 0, 0x504);
	init_completion(&rx->c);

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	qdev->f_data = f;
	if (!f)
		return -ENOMEM;
	fastcall_data_init(f);

	ret = of_property_read_u32(np, "gunyah-shm-label", &qdev->shm_label);
	if (ret < 0) {
		dev_err(qdev->dev, "failed to read label info %d\n", ret);
		return ret;
	}

	qdev->master = of_property_read_bool(np, "xiaomi,master");
	if (!qdev->master)
		return -EINVAL;

	sbuf = of_parse_phandle(qdev->dev->of_node, "shared-buffer", 0);
	if (!sbuf) {
		qdev->va = dma_alloc_attrs(qdev->dev, 0x400000, (dma_addr_t *)&qdev->res.start,
					   GFP_KERNEL, DMA_ATTR_FORCE_CONTIGUOUS);
		if (!qdev->va)
			return -ENOMEM;
		qdev->pa = qdev->res.start;
		qdev->res.end = qdev->res.start + 0x400000 - 1;
		qdev->size = 0x400000;
	} else {
		if (of_address_to_resource(sbuf, 0, &qdev->res)) {
			dev_err(qdev->dev, "of_address_to_resource failed!\n");
			return -EINVAL;
		}
		qdev->pa = qdev->res.start;
		qdev->size = resource_size(&qdev->res);
	}

	if (qdev->master) {
		if (of_property_read_u32(np, "peer-name", &qdev->peer_name) < 0)
			qdev->peer_name = 0;
		qdev->nb.notifier_call = mitee_msgq_rm_cb;
		qdev->nb.priority = INT_MAX;
		gh_rm_register_notifier(&qdev->nb);
	}

	qdev->msgq = gh_msgq_register(GH_MSGQ_LABEL_TEST_OEMVM);
	task = kthread_create(mitee_recv_task_fn, qdev, "mitee_msgq_recv_task");
	if (!IS_ERR(task))
		wake_up_process(task);
	qdev->recv_task = task;
	g_msgq_dev = qdev;

	mutex_init(&qdev->shm_mutex);
	ret = rhashtable_init(&qdev->shm_ht, &shm_rhash_params);
	if (ret)
		pr_err("mitee msgq: rhashtable_init error %d\n", ret);

	INIT_LIST_HEAD(&qdev->secmem_list);
	mutex_init(&qdev->secmem_mut);
	return 0;
}

static int mitee_msgq_remove(struct platform_device *pdev)
{
	struct mitee_msgq_dev *qdev = platform_get_drvdata(pdev);
	struct mitee_recv_queue *rx = qdev->rx;
	struct mitee_fastcall_data *f = qdev->f_data;
	gh_vmid_t peer_vmid = 0, this_vmid = 0;
	u64 srcvm[1];
	struct qcom_scm_vmperm newvm;

	rhashtable_free_and_destroy(&qdev->shm_ht, rh_free_fn, NULL);
	if (ghd_rm_get_vmid(qdev->peer_name, &peer_vmid))
		goto out;
	if (ghd_rm_get_vmid(GH_PRIMARY_VM, &this_vmid))
		goto out;

	srcvm[0] = BIT(this_vmid) | BIT(peer_vmid);
	newvm.vmid = this_vmid;
	newvm.perm = PERM_READ | PERM_WRITE | PERM_EXEC;
	if (ghd_rm_mem_reclaim(qdev->shm_handle, 0))
		pr_err("%s: mitee msgq Gunyah reclaim failed\n", "mitee_msgq_unshare_mem");
	qcom_scm_assign_mem(qdev->res.start, resource_size(&qdev->res), srcvm, &newvm, 1);

	if (!of_parse_phandle(qdev->dev->of_node, "shared-buffer", 0)) {
		complete(&f->c);
		complete(&rx->c);
		kfree(rx);
		kfree(f);
	}
	return 0;
out:
	return 0;
}

static const struct of_device_id mitee_msgq_match[] = {
	{ .compatible = "xiaomi,mitee-hlos-ipc" },
	{},
};

static struct platform_driver mitee_msgq_driver = {
	.probe = mitee_msgq_probe,
	.remove = mitee_msgq_remove,
	.driver = {
		.name = "mitee_ipc",
		.of_match_table = mitee_msgq_match,
	},
};

int mitee_msgq_driver_init(void)
{
	return platform_driver_register(&mitee_msgq_driver);
}

void mitee_msgq_driver_deinit(void)
{
	platform_driver_unregister(&mitee_msgq_driver);
}

const struct mitee_comm_ops mitee_msgq_com_ops = {
	.id = QCOM_QMI,
	.init = mitee_msgq_driver_init,
	.deinit = mitee_msgq_driver_deinit,
	.get_vm_state = mitee_msgq_get_vm_state,
	.call = mitee_msgq_round_trip_call,
	.from_msg_param = mitee_msgq_from_msg_param,
	.to_msg_param = mitee_msgq_to_msg_param,
	.shm_register = mitee_msgq_shm_register,
	.shm_unregister = mitee_msgq_shm_unregister,
	.shm_unregister_supp = mitee_msgq_shm_unregister_supp,
};
