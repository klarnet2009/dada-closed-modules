// SPDX-License-Identifier: GPL-2.0
/* matt ULP registry (stock: mp_ulp.c; derived from net/ipv4/tcp_ulp.c). Reconstructed from the OS4 stock disassembly. DO NOT LOAD before review. */
#include "matt.h"
#include <linux/capability.h>
#include <linux/rculist.h>

static DEFINE_SPINLOCK(matt_ulp_list_lock);
static LIST_HEAD(matt_ulp_list);

static struct matt_ulp_ops *matt_ulp_find(const char *name)
{
	struct matt_ulp_ops *e;

	list_for_each_entry_rcu(e, &matt_ulp_list, list) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

static const struct matt_ulp_ops *__matt_ulp_find_autoload(const char *name)
{
	const struct matt_ulp_ops *ulp = NULL;

	rcu_read_lock();
	ulp = matt_ulp_find(name);
	if (!ulp && capable(CAP_NET_ADMIN)) {
		rcu_read_unlock();
		request_module("matt-ulp-%s", name);
		rcu_read_lock();
		ulp = matt_ulp_find(name);
	}
	if (!ulp || !try_module_get(ulp->owner))
		ulp = NULL;
	rcu_read_unlock();
	return ulp;
}

int matt_register_ulp(struct matt_ulp_ops *ulp GKPAD(fcrcNt97LBxR01))
{
	int ret = 0;

	spin_lock(&matt_ulp_list_lock);
	if (matt_ulp_find(ulp->name))
		ret = -EEXIST;
	else
		list_add_tail_rcu(&ulp->list, &matt_ulp_list);
	spin_unlock(&matt_ulp_list_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(matt_register_ulp);

void matt_unregister_ulp(struct matt_ulp_ops *ulp GKPAD(fcrcHnp4vrkGae))
{
	spin_lock(&matt_ulp_list_lock);
	list_del_rcu(&ulp->list);
	spin_unlock(&matt_ulp_list_lock);
	synchronize_rcu();
}
EXPORT_SYMBOL_GPL(matt_unregister_ulp);

void matt_get_available_ulp(char *buf, size_t maxlen)
{
	struct matt_ulp_ops *ulp_ops;
	size_t offs = 0;

	*buf = '\0';
	rcu_read_lock();
	list_for_each_entry_rcu(ulp_ops, &matt_ulp_list, list) {
		offs += snprintf(buf + offs, maxlen - offs, "%s%s", offs == 0 ? "" : " ", ulp_ops->name);
		if (WARN_ON_ONCE(offs >= maxlen))
			break;
	}
	rcu_read_unlock();
}

void matt_update_ulp(struct sock *sk, struct proto *proto, void (*write_space)(struct sock *))
{
	struct matt_sock *ms = MATT_SK(sk);

	if (ms->ulp_ops->update)
		ms->ulp_ops->update(sk, proto, write_space);
}

void matt_cleanup_ulp(struct sock *sk)
{
	struct matt_sock *ms = MATT_SK(sk);

	if (!ms->ulp_ops)
		return;
	if (ms->ulp_ops->release)
		ms->ulp_ops->release(sk);
	module_put(ms->ulp_ops->owner);
	ms->ulp_ops = NULL;
}

int matt_set_ulp(struct sock *sk, const char *name)
{
	struct matt_sock *ms = MATT_SK(sk);
	const struct matt_ulp_ops *ulp_ops;
	int err;

	ulp_ops = __matt_ulp_find_autoload(name);
	if (!ulp_ops)
		return -ENOENT;
	if (ms->ulp_ops) {
		err = -EEXIST;
		goto out_err;
	}
	if (sk->sk_socket)
		clear_bit(SOCK_SUPPORT_ZC, &sk->sk_socket->flags);
	if (!ulp_ops->clone && sk->sk_state == TCP_LISTEN) {
		err = -ENOTCONN;	/* G1ad: stock mov #-0x6b (G1ac had -EINVAL) */
		goto out_err;
	}
	err = ulp_ops->init(sk);
	if (err)
		goto out_err;
	ms->ulp_ops = ulp_ops;
	return 0;
out_err:
	module_put(ulp_ops->owner);
	return err;
}
