// SPDX-License-Identifier: GPL-2.0
/*
 * Stub provider of the mca_common.ko exports (votable API).  The struct bodies below are private to the real
 * provider (mca_voter.c) but part of the symbol CRCs: they must match the stock BTF exactly.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/debugfs.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_voter.h>

#define NUM_MAX_CLIENTS		32
#define CLIENT_MAX_LEN		50

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

void mca_lock_votable(struct mca_votable *votable) { }
EXPORT_SYMBOL(mca_lock_votable);
void mca_unlock_votable(struct mca_votable *votable) { }
EXPORT_SYMBOL(mca_unlock_votable);
bool mca_is_override_vote_enabled_locked(struct mca_votable *votable) { return false; }
EXPORT_SYMBOL(mca_is_override_vote_enabled_locked);
bool mca_is_override_vote_enabled(struct mca_votable *votable) { return false; }
EXPORT_SYMBOL(mca_is_override_vote_enabled);
bool mca_is_client_vote_enabled_locked(struct mca_votable *votable, const char *client_str) { return false; }
EXPORT_SYMBOL(mca_is_client_vote_enabled_locked);
bool mca_is_client_vote_enabled(struct mca_votable *votable, const char *client_str) { return false; }
EXPORT_SYMBOL(mca_is_client_vote_enabled);
int mca_get_client_vote_locked(struct mca_votable *votable, const char *client_str) { return 0; }
EXPORT_SYMBOL(mca_get_client_vote_locked);
int mca_get_client_vote(struct mca_votable *votable, const char *client_str) { return 0; }
EXPORT_SYMBOL(mca_get_client_vote);
int mca_get_effective_result_locked(struct mca_votable *votable) { return 0; }
EXPORT_SYMBOL(mca_get_effective_result_locked);
int mca_get_effective_result(struct mca_votable *votable) { return 0; }
EXPORT_SYMBOL(mca_get_effective_result);
const char *mca_get_effective_client_locked(struct mca_votable *votable) { return NULL; }
EXPORT_SYMBOL(mca_get_effective_client_locked);
const char *mca_get_effective_client(struct mca_votable *votable) { return NULL; }
EXPORT_SYMBOL(mca_get_effective_client);
int mca_vote(struct mca_votable *votable, const char *client_str, bool enabled, int val) { return 0; }
EXPORT_SYMBOL(mca_vote);
int mca_vote_override(struct mca_votable *votable, const char *override_client, bool enabled, int val) { return 0; }
EXPORT_SYMBOL(mca_vote_override);
int mca_rerun_election(struct mca_votable *votable) { return 0; }
EXPORT_SYMBOL(mca_rerun_election);
struct mca_votable *mca_find_votable(const char *name) { return NULL; }
EXPORT_SYMBOL(mca_find_votable);
struct mca_votable *mca_create_votable(const char *name, int votable_type,
				int (*callback)(struct mca_votable *votable, void *data,
					int effective_result, const char *effective_client),
				int default_value, void *data) { return NULL; }
EXPORT_SYMBOL(mca_create_votable);
void mca_destroy_votable(struct mca_votable *votable) { }
EXPORT_SYMBOL(mca_destroy_votable);
MODULE_LICENSE("GPL");
