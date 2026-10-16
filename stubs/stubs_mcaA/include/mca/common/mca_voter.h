/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_voter.h - interface of the MCA votable framework (provider: mca_common.ko).
 * Reconstructed from the stock mca_common BTF (prototypes) and the XRING mca_voter.c (private struct).
 * struct mca_votable is private to the provider; users only see the forward declaration.
 */
#ifndef __MCA_VOTER_H__
#define __MCA_VOTER_H__

#include <linux/types.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>

struct mca_votable;

enum mca_votable_type {
	MCA_VOTE_MIN = 0,
	MCA_VOTE_MAX,
	MCA_VOTE_OR,
	MCA_VOTE_AND,
};

void mca_lock_votable(struct mca_votable *votable);
void mca_unlock_votable(struct mca_votable *votable);
bool mca_is_override_vote_enabled_locked(struct mca_votable *votable);
bool mca_is_override_vote_enabled(struct mca_votable *votable);
bool mca_is_client_vote_enabled_locked(struct mca_votable *votable, const char *client_str);
bool mca_is_client_vote_enabled(struct mca_votable *votable, const char *client_str);
int mca_get_client_vote_locked(struct mca_votable *votable, const char *client_str);
int mca_get_client_vote(struct mca_votable *votable, const char *client_str);
int mca_get_effective_result_locked(struct mca_votable *votable);
int mca_get_effective_result(struct mca_votable *votable);
const char *mca_get_effective_client_locked(struct mca_votable *votable);
const char *mca_get_effective_client(struct mca_votable *votable);
int mca_vote(struct mca_votable *votable, const char *client_str, bool enabled, int val);
int mca_vote_override(struct mca_votable *votable, const char *override_client, bool override_en, int val);
int mca_rerun_election(struct mca_votable *votable);
struct mca_votable *mca_find_votable(const char *name);
struct mca_votable *mca_create_votable(const char *name, int votable_type,
			int (*callback)(struct mca_votable *votable, void *data,
					int effective_result, const char *effective_client),
			int default_value, void *data);
void mca_destroy_votable(struct mca_votable *votable);

#endif /* __MCA_VOTER_H__ */
