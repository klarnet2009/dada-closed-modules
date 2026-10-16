// SPDX-License-Identifier: GPL-2.0
/*
 * mca_voter.c
 *
 * mca voteable driver
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
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>
#line 21

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_voter"
#endif

#define VOTER_DEBUG

#define NUM_MAX_CLIENTS		32
#define DEBUG_FORCE_CLIENT	"DEBUG_FORCE_CLIENT"
#define CLIENT_MAX_LEN 	50

static DEFINE_SPINLOCK(votable_list_slock);
static LIST_HEAD(votable_list);

static struct dentry *debug_root;

struct client_vote {
	bool	enabled;
	int	value;
};

struct mca_votable {
	const char		*name;
	char		override_client[CLIENT_MAX_LEN];
	struct list_head	list;
	struct client_vote	votes[NUM_MAX_CLIENTS];
	int			num_clients;
	int			type;
	int			effective_client_id;
	int			effective_result;
	int			override_result;
	struct mutex		vote_lock;
	void			*data;
	int			(*callback)(struct mca_votable *votable,
						void *data,
						int effective_result,
						const char *effective_client);
	char			*client_strs[NUM_MAX_CLIENTS];
	bool			voted_on;
	struct dentry		*root;
	struct dentry		*status_ent;
	u32			force_val;
	bool			force_active;
	struct dentry		*force_active_ent;
	int default_value;
};

/**
 * mca_vote_and()
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets 0 or 1 based on the voting
 * @eff_id:	Always returns the client_id argument
 *
 * Note that for SET_ANY voter, the value is always same as enabled. There is
 * no idea of a voter abstaining from the election. Hence there is never a
 * situation when the effective_id will be invalid, during election.
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void mca_vote_and(struct mca_votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = 0;
	*eff_id = -EINVAL;

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled) {
			if (!votable->votes[i].value) {
				*eff_id = client_id;
				*eff_res = votable->votes[i].value;
				break;
			}
			*eff_id = client_id;
			*eff_res = votable->votes[i].value;
		}
	}

	if (*eff_id == -EINVAL)
		*eff_res = votable->default_value;
}

/**
 * mca_vote_or()
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets 0 or 1 based on the voting
 * @eff_id:	Always returns the client_id argument
 *
 * Note that for SET_ANY voter, the value is always same as enabled. There is
 * no idea of a voter abstaining from the election. Hence there is never a
 * situation when the effective_id will be invalid, during election.
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void mca_vote_or(struct mca_votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = 0;
	*eff_id = -EINVAL;

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled) {
			if (votable->votes[i].value) {
				*eff_id = client_id;
				*eff_res = votable->votes[i].value;
				break;
			}
			*eff_id = client_id;
			*eff_res = votable->votes[i].value;
		}
	}

	if (*eff_id == -EINVAL)
		*eff_res = votable->default_value;
}

/**
 * MCA_VOTE_MIN() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the min. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to INT_MAX
 * @eff_id:	sets this to the client id that has the min value amongst all
 *		the enabled clients. If there is no enabled client, sets this
 *		to -EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void mca_vote_min(struct mca_votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = INT_MAX;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled
			&& *eff_res > votable->votes[i].value) {
			*eff_res = votable->votes[i].value;
			*eff_id = i;
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = votable->default_value;
}

/**
 * MCA_VOTE_MAX() -
 * @votable:	votable object
 * @client_id:	client number of the latest voter
 * @eff_res:	sets this to the max. of all the values amongst enabled voters.
 *		If there is no enabled client, this is set to -EINVAL
 * @eff_id:	sets this to the client id that has the max value amongst all
 *		the enabled clients. If there is no enabled client, sets this to
 *		-EINVAL
 *
 * Context:
 *	Must be called with the votable->lock held
 */
static void mca_vote_max(struct mca_votable *votable, int client_id,
				int *eff_res, int *eff_id)
{
	int i;

	*eff_res = INT_MIN;
	*eff_id = -EINVAL;
	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		if (votable->votes[i].enabled &&
				*eff_res < votable->votes[i].value) {
			*eff_res = votable->votes[i].value;
			*eff_id = i;
		}
	}
	if (*eff_id == -EINVAL)
		*eff_res = votable->default_value;
}

static int mca_get_client_id(struct mca_votable *votable, const char *client_str)
{
	int i;

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]
		 && (strcmp(votable->client_strs[i], client_str) == 0))
			return i;
	}

	/* new client */
	for (i = 0; i < votable->num_clients; i++) {
		if (!votable->client_strs[i]) {
			votable->client_strs[i]
				= kstrdup(client_str, GFP_KERNEL);
			if (!votable->client_strs[i])
				return -ENOMEM;
			return i;
		}
	}
	return -EINVAL;
}

static char *mca_get_client_str(struct mca_votable *votable, int client_id)
{
	if (!votable || (client_id == -EINVAL))
		return NULL;

	return votable->client_strs[client_id];
}

void mca_lock_votable(struct mca_votable *votable)
{
	mutex_lock(&votable->vote_lock);
}
EXPORT_SYMBOL(mca_lock_votable);

void mca_unlock_votable(struct mca_votable *votable)
{
	mutex_unlock(&votable->vote_lock);
}
EXPORT_SYMBOL(mca_unlock_votable);

/**
 * is_override_vote_enabled() -
 * is_override_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether override
		vote is enabled.
 * @votable:	the votable object
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool mca_is_override_vote_enabled_locked(struct mca_votable *votable)
{
	if (!votable)
		return false;

	return votable->override_result != -EINVAL;
}
EXPORT_SYMBOL(mca_is_override_vote_enabled_locked);

bool mca_is_override_vote_enabled(struct mca_votable *votable)
{
	bool enable;

	if (!votable)
		return false;

	mca_lock_votable(votable);
	enable = mca_is_override_vote_enabled_locked(votable);
	mca_unlock_votable(votable);

	return enable;
}
EXPORT_SYMBOL(mca_is_override_vote_enabled);

/**
 * is_client_vote_enabled() -
 * is_client_vote_enabled_locked() -
 *		The unlocked and locked variants of getting whether a client's
		vote is enabled.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	True if the client's vote is enabled; false otherwise.
 */
bool mca_is_client_vote_enabled_locked(struct mca_votable *votable,
							const char *client_str)
{

	int client_id;

	if (!votable || !client_str)
		return false;

	client_id = mca_get_client_id(votable, client_str);
	if (client_id < 0)
		return false;

	return votable->votes[client_id].enabled;
}
EXPORT_SYMBOL(mca_is_client_vote_enabled_locked);

bool mca_is_client_vote_enabled(struct mca_votable *votable, const char *client_str)
{
	bool enabled;

	if (!votable || !client_str)
		return false;

	mca_lock_votable(votable);
	enabled = mca_is_client_vote_enabled_locked(votable, client_str);
	mca_unlock_votable(votable);
	return enabled;
}
EXPORT_SYMBOL(mca_is_client_vote_enabled);

/**
 * get_client_vote() -
 * get_client_vote_locked() -
 *		The unlocked and locked variants of getting a client's voted
 *		value.
 * @votable:	the votable object
 * @client_str: client of interest
 *
 * Returns:
 *	The value the client voted for. -EINVAL is returned if the client
 *	is not enabled or the client is not found.
 */
int mca_get_client_vote_locked(struct mca_votable *votable, const char *client_str)
{
	int client_id;

	if (!votable || !client_str)
		return -EINVAL;

	client_id = mca_get_client_id(votable, client_str);
	if (client_id < 0)
		return -EINVAL;

	if ((votable->type != MCA_VOTE_OR)
		&& !votable->votes[client_id].enabled)
		return -EINVAL;

	return votable->votes[client_id].value;
}
EXPORT_SYMBOL(mca_get_client_vote_locked);

int mca_get_client_vote(struct mca_votable *votable, const char *client_str)
{
	int value;

	if (!votable || !client_str)
		return -EINVAL;

	mca_lock_votable(votable);
	value = mca_get_client_vote_locked(votable, client_str);
	mca_unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL(mca_get_client_vote);

/**
 * get_effective_result() -
 * get_effective_result_locked() -
 *		The unlocked and locked variants of getting the effective value
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective result.
 *	For MIN and MAX votable, returns -EINVAL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns 0 when no clients have casted their votes
 *	because for SET_ANY there is no concept of abstaining from election. The
 *	votes for all the clients of SET_ANY votable is defaulted to false.
 */
int mca_get_effective_result_locked(struct mca_votable *votable)
{
	if (!votable)
		return -EINVAL;

	if (votable->force_active)
		return votable->force_val;

	if (votable->override_result != -EINVAL)
		return votable->override_result;

	if (votable->effective_result == -EINVAL)
		return votable->default_value;

	return votable->effective_result;
}
EXPORT_SYMBOL(mca_get_effective_result_locked);

int mca_get_effective_result(struct mca_votable *votable)
{
	int value;

	if (!votable)
		return -EINVAL;

	mca_lock_votable(votable);
	value = mca_get_effective_result_locked(votable);
	mca_unlock_votable(votable);
	return value;
}
EXPORT_SYMBOL(mca_get_effective_result);

/**
 * get_effective_client() -
 * get_effective_client_locked() -
 *		The unlocked and locked variants of getting the effective client
 *		amongst all the enabled voters.
 *
 * @votable:	the votable object
 *
 * Returns:
 *	The effective client.
 *	For MIN and MAX votable, returns NULL when the votable
 *	object has been created but no clients have casted their votes or
 *	the last enabled client disables its vote.
 *	For SET_ANY votable it returns NULL too when no clients have casted
 *	their votes. But for SET_ANY since there is no concept of abstaining
 *	from election, the only client that casts a vote or the client that
 *	caused the result to change is returned.
 */
const char *mca_get_effective_client_locked(struct mca_votable *votable)
{
	if (!votable)
		return NULL;

	if (votable->force_active)
		return DEBUG_FORCE_CLIENT;

	if (votable->override_result != -EINVAL)
		return votable->override_client;

	return mca_get_client_str(votable, votable->effective_client_id);
}
EXPORT_SYMBOL(mca_get_effective_client_locked);

const char *mca_get_effective_client(struct mca_votable *votable)
{
	const char *client_str;

	if (!votable)
		return NULL;

	mca_lock_votable(votable);
	client_str = mca_get_effective_client_locked(votable);
	mca_unlock_votable(votable);
	return client_str;
}
EXPORT_SYMBOL(mca_get_effective_client);

/**
 * vote() -
 *
 * @votable:	the votable object
 * @client_str: the voting client
 * @enabled:	This provides a means for the client to exclude himself from
 *		election. This clients val (the next argument) will be
 *		considered only when he has enabled his participation.
 *		Note that this takes a differnt meaning for SET_ANY type, as
 *		there is no concept of abstaining from participation.
 *		Enabled is treated as the boolean value the client is voting.
 * @val:	The vote value. This is ignored for SET_ANY votable types.
 *		For MIN, MAX votable types this value is used as the
 *		clients vote value when the enabled is true, this value is
 *		ignored if enabled is false.
 *
 * The callback is called only when there is a change in the election results or
 * if it is the first time someone is voting.
 *
 * Returns:
 *	The return from the callback when present and needs to be called
 *	or zero.
 */
int mca_vote(struct mca_votable *votable, const char *client_str, bool enabled, int val)
{
	int effective_id = -EINVAL;
	int effective_result;
	int client_id;
	int rc = 0;
	bool similar_vote = false;

	if (!votable || !client_str)
		return -EINVAL;

	mca_lock_votable(votable);

	client_id = mca_get_client_id(votable, client_str);
	if (client_id < 0) {
		rc = client_id;
		mca_log_err("can not find client id %s\n", client_str);
		goto out;
	}

	if ((votable->votes[client_id].enabled == enabled) &&
		(votable->votes[client_id].value == val)) {
		mca_log_debug("%s: %s,%d same vote %s of val=%d\n",
				votable->name,
				client_str, client_id,
				enabled ? "on" : "off",
				val);
		similar_vote = true;
	}

	votable->votes[client_id].enabled = enabled;
	votable->votes[client_id].value = val;

	if (similar_vote && votable->voted_on) {
		mca_log_debug("%s: %s,%d Ignoring similar vote %s of val=%d\n",
			votable->name,
			client_str, client_id, enabled ? "on" : "off", val);
		goto out;
	}

	mca_log_info("%s: %s,%d voting %s of val=%d\n",
		votable->name,
		client_str, client_id, enabled ? "on" : "off", val);
	switch (votable->type) {
	case MCA_VOTE_MIN:
		mca_vote_min(votable, client_id, &effective_result, &effective_id);
		break;
	case MCA_VOTE_MAX:
		mca_vote_max(votable, client_id, &effective_result, &effective_id);
		break;
	case MCA_VOTE_OR:
		mca_vote_or(votable, client_id,
				&effective_result, &effective_id);
		break;
	case MCA_VOTE_AND:
		mca_vote_and(votable, client_id,
				&effective_result, &effective_id);
		break;
	default:
		return -EINVAL;
	}

	/*
	 * Note that the callback is called with a NULL string and -EINVAL
	 * result when there are no enabled votes
	 */
	if (!votable->voted_on
			|| (effective_result != votable->effective_result)
			|| (effective_id != votable->effective_client_id)) {
		votable->effective_client_id = effective_id;
		votable->effective_result = effective_result;
		mca_log_info("%s: effective vote is now %d voted by %s,%d\n",
			votable->name, effective_result,
			mca_get_client_str(votable, effective_id),
			effective_id);
		if (votable->callback && !votable->force_active
				&& (votable->override_result == -EINVAL)) {
			rc = votable->callback(votable, votable->data,
					effective_result,
					mca_get_client_str(votable, effective_id));
			if (rc)
				mca_log_err("callback failed with rc=%d\n", rc);
		}
	}

#ifdef VOTER_DEBUG
	mca_log_info("%s VOTER:\n", votable->name);
	mca_log_info("\t\t\t\t\tclient\t\t\t\t\t\tenable\t\tvalue");
	for (int i = 0; i < votable->num_clients && votable->client_strs[i]; i++) {
		int j = 0;
		j = strlen(votable->client_strs[i]);
		if (i > 9)
			++j;
		mca_log_info("\t\t\t\t\t%d.%s%s\t%d\t\t\t%d\n", i, votable->client_strs[i],
			(j <= 1 ? "\t\t\t\t\t\t" : (j <= 5 ? "\t\t\t\t\t" : (j <= 9 ? "\t\t\t\t" :\
			(j <= 13 ? "\t\t\t" : (j <= 17 ? "\t\t" : (j <= 21 ? "\t" : "")))))),
			votable->votes[i].enabled, votable->votes[i].value);
	}
#endif

	votable->voted_on = true;
out:
	mca_unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL(mca_vote);

/**
 * vote_override() -
 *
 * @votable:		The votable object
 * @override_client:	The voting client that will override other client's
 *			votes, that are already present. When force_active
 *			and override votes are set on a votable, force_active's
 *			client will have the higher priority and it's vote will
 *			be the effective one.
 * @enabled:		This provides a means for the override client to exclude
 *			itself from election. This client's vote
 *			(the next argument) will be considered only when
 *			it has enabled its participation. When this is
 *			set true, this will force a value on a MIN/MAX votable
 *			irrespective of its current value.
 * @val:		The vote value. This will be effective only if enabled
 *			is set true.
 * Returns:
 *	The result of vote. 0 is returned if the vote
 *	is successfully set by the overriding client, when enabled is set.
 */
int mca_vote_override(struct mca_votable *votable, const char *override_client,
		  bool enabled, int val)
{
	int rc = 0;

	if (!votable || !override_client)
		return -EINVAL;

	mca_lock_votable(votable);
	if (votable->force_active) {
		votable->override_result = enabled ? val : -EINVAL;
		goto out;
	}

	if (enabled) {
		rc = votable->callback(votable, votable->data,
					val, override_client);
		if (!rc) {
			strncpy(votable->override_client, override_client, CLIENT_MAX_LEN);
			votable->override_client[CLIENT_MAX_LEN - 1] = '\0';
			votable->override_result = val;
			mca_log_err("ovrried vote %d", votable->override_result);
		}
	} else {
		rc = votable->callback(votable, votable->data,
			votable->effective_result,
			mca_get_client_str(votable, votable->effective_client_id));
		votable->override_result = -EINVAL;
		mca_log_err("cleared ovrried vote %d", votable->override_result);
	}

out:
	mca_unlock_votable(votable);
	return rc;
}
EXPORT_SYMBOL(mca_vote_override);

int mca_rerun_election(struct mca_votable *votable)
{
	int rc = 0;
	int effective_result;

	if (!votable)
		return -EINVAL;

	mca_lock_votable(votable);
	effective_result = mca_get_effective_result_locked(votable);
	if (votable->callback)
		rc = votable->callback(votable,
			votable->data,
			effective_result,
			mca_get_client_str(votable, votable->effective_client_id));
	mca_unlock_votable(votable);
	return rc;
}

EXPORT_SYMBOL(mca_rerun_election);
struct mca_votable *mca_find_votable(const char *name)
{
	unsigned long flags;
	struct mca_votable *v;
	bool found = false;

	if (!name)
		return NULL;

	spin_lock_irqsave(&votable_list_slock, flags);
	if (list_empty(&votable_list))
		goto out;

	list_for_each_entry(v, &votable_list, list) {
		if (strcmp(v->name, name) == 0) {
			found = true;
			break;
		}
	}
out:
	spin_unlock_irqrestore(&votable_list_slock, flags);

	if (found)
		return v;
	else
		return NULL;
}
EXPORT_SYMBOL(mca_find_votable);

static int mca_force_active_get(void *data, u64 *val)
{
	struct mca_votable *votable = data;

	*val = votable->force_active;

	return 0;
}

static int mca_force_active_set(void *data, u64 val)
{
	struct mca_votable *votable = data;
	int rc = 0;
	int effective_result;
	const char *client;

	mca_lock_votable(votable);
	votable->force_active = !!val;

	if (!votable->callback)
		goto out;

	if (votable->force_active) {
		rc = votable->callback(votable, votable->data,
			votable->force_val,
			DEBUG_FORCE_CLIENT);
	} else {
		if (votable->override_result != -EINVAL) {
			effective_result = votable->override_result;
			client = votable->override_client;
		} else {
			effective_result = votable->effective_result;
			client = mca_get_client_str(votable,
					votable->effective_client_id);
		}
		rc = votable->callback(votable, votable->data, effective_result,
					client);
	}
out:
	mca_unlock_votable(votable);
	return rc;
}
DEFINE_DEBUGFS_ATTRIBUTE(mca_votable_force_ops, mca_force_active_get, mca_force_active_set,
		"%lld\n");

static int mca_show_votable_clients(struct seq_file *m, void *data)
{
	struct mca_votable *votable = m->private;
	int i;
	char *type_str = "Unkonwn";
	const char *effective_client_str;

	mca_lock_votable(votable);

	for (i = 0; i < votable->num_clients; i++) {
		if (votable->client_strs[i]) {
			seq_printf(m, "%s: %s:\t\t\ten=%d v=%d\n",
					votable->name,
					votable->client_strs[i],
					votable->votes[i].enabled,
					votable->votes[i].value);
		}
	}

	switch (votable->type) {
	case MCA_VOTE_MIN:
		type_str = "Min";
		break;
	case MCA_VOTE_MAX:
		type_str = "Max";
		break;
	case MCA_VOTE_OR:
		type_str = "Set_Or";
		break;
	case MCA_VOTE_AND:
		type_str = "Set_And";
	}

	effective_client_str = mca_get_effective_client_locked(votable);
	seq_printf(m, "%s: effective=%s type=%s v=%d\n",
			votable->name,
			effective_client_str ? effective_client_str : "none",
			type_str,
			mca_get_effective_result_locked(votable));
	mca_unlock_votable(votable);

	return 0;
}

static int mca_votable_status_open(struct inode *inode, struct file *file)
{
	struct mca_votable *votable = inode->i_private;

	return single_open(file, mca_show_votable_clients, votable);
}

static const struct file_operations mca_votable_status_ops = {
	.owner		= THIS_MODULE,
	.open		= mca_votable_status_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

struct mca_votable *mca_create_votable(const char *name,
				int votable_type,
				int (*callback)(struct mca_votable *votable,
					void *data,
					int effective_result,
					const char *effective_client),
				int default_value,
				void *data)
{
	struct mca_votable *votable;
	unsigned long flags;

	if (!name)
		return ERR_PTR(-EINVAL);

	votable = mca_find_votable(name);
	if (votable)
		return votable;

	if (debug_root == NULL) {
		debug_root = debugfs_create_dir("business-votable", NULL);
		if (!debug_root) {
			mca_log_err("Couldn't create debug dir\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (votable_type >= NUM_VOTABLE_TYPES) {
		mca_log_err("Invalid votable_type specified for voter\n");
		return ERR_PTR(-EINVAL);
	}

	votable = kzalloc(sizeof(struct mca_votable), GFP_KERNEL);
	if (!votable)
		return ERR_PTR(-ENOMEM);

	votable->name = kstrdup(name, GFP_KERNEL);
	if (!votable->name) {
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	votable->num_clients = NUM_MAX_CLIENTS;
	votable->callback = callback;
	votable->type = votable_type;
	votable->data = data;
	votable->override_result = -EINVAL;
	mutex_init(&votable->vote_lock);

	/*
	 * Because effective_result and client states are invalid
	 * before the first vote, initialize them to -EINVAL
	 */
	votable->effective_result = -EINVAL;
	if (votable->type == MCA_VOTE_OR)
		votable->effective_result = 0;
	votable->effective_client_id = -EINVAL;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_add(&votable->list, &votable_list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	votable->root = debugfs_create_dir(name, debug_root);
	if (!votable->root) {
		mca_log_err("Couldn't create debug dir %s\n", name);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-ENOMEM);
	}

	votable->status_ent = debugfs_create_file("status", S_IFREG | 0444,
				  votable->root, votable,
				  &mca_votable_status_ops);
	if (!votable->status_ent) {
		mca_log_err("Couldn't create status dbg file for %s\n", name);
		debugfs_remove_recursive(votable->root);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-EEXIST);
	}

	debugfs_create_u32("force_val",
					S_IFREG | 0644,
					votable->root,
					&(votable->force_val));

	votable->force_active_ent = debugfs_create_file("force_active",
					S_IFREG | 0444,
					votable->root, votable,
					&mca_votable_force_ops);
	if (!votable->force_active_ent) {
		mca_log_err("Couldn't create force_active dbg file for %s\n", name);
		debugfs_remove_recursive(votable->root);
		kfree(votable->name);
		kfree(votable);
		return ERR_PTR(-EEXIST);
	}

	votable->default_value = default_value;

	return votable;
}
EXPORT_SYMBOL(mca_create_votable);

void mca_destroy_votable(struct mca_votable *votable)
{
	unsigned long flags;
	int i;

	if (!votable)
		return;

	spin_lock_irqsave(&votable_list_slock, flags);
	list_del(&votable->list);
	spin_unlock_irqrestore(&votable_list_slock, flags);

	debugfs_remove_recursive(votable->root);

	for (i = 0; i < votable->num_clients && votable->client_strs[i]; i++)
		kfree(votable->client_strs[i]);

	kfree(votable->name);
	kfree(votable);
}
EXPORT_SYMBOL(mca_destroy_votable);

MODULE_DESCRIPTION("mca voter");
MODULE_AUTHOR("getian@xiaomi.com");
MODULE_LICENSE("GPL v2");

