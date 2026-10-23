// SPDX-License-Identifier: GPL-2.0
/* mi_game: small helpers (task accounting accessors, break flags, trivial file ops). PARTIAL, DO NOT LOAD. */
#include "mi_game.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/arch_topology.h>

#pragma clang diagnostic ignored "-Wmissing-prototypes"

noinline int migt_enable(void);
extern void set_vip_prio(struct task_struct *p, int prio, int flag);
extern void restore_vip_prio(struct task_struct *p);

unsigned int num_sched_clusters __read_mostly = 2;
unsigned int sched_ravg_window __read_mostly = 20000000;
static unsigned int migt_gpsync __used;
module_param(migt_gpsync, uint, 0644);
unsigned int migt_walt_features = 1;

noinline int is_render_thread(struct task_struct *p)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	if (XR_GAME_VALID(t) && t->game)
		return t->game->type & 0xc0;
	return 0;
}

noinline struct mi_game_task *get_gt_task(struct task_struct *p)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	if (XR_GAME_VALID(t))
		return t->game;
	return NULL;
}

static noinline void set_render_flag(struct task_struct *p, enum MI_RENDER_TYPE idx)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	if (XR_GAME_VALID(t) && t->game) {
		t->game->type |= 0x40 << idx;
		set_vip_prio(p, 30, 1);
	}
}

static noinline void clean_render_flag(struct task_struct *p, enum MI_RENDER_TYPE idx)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	if (XR_GAME_VALID(t) && t->game) {
		t->game->type &= ~(0x40 << idx);
		restore_vip_prio(p);
	}
}

/* stock BTF: (task, game task, char *, int rule_type); the last two are unused (kCFI hash 0x3155a5ab needs them) */
void clear_vip_task_type(struct task_struct *p, struct mi_game_task *g, char *unused_name, int rule_type)
{
	if (p && g && (g->type & 0x810)) {
		g->type &= ~0x810;
		restore_vip_prio(p);
	}
}

static int migt_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static int migt_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int migt_release(struct inode *inode, struct file *file)
{
	return 0;
}

static int migt_reset_open(struct inode *inode, struct file *file)
{
	return 0;
}

void migt_free(void *p)
{
	kvfree(p);
}

unsigned int migt_thresh __asm__("migt_thresh.llvm.597625021974665233") __attribute__((visibility("hidden"))) = 18;

/* ThinLTO-promoted static of the stock (hidden global with the .llvm suffix); initial value -1 */
int last_traced_uid __asm__("last_traced_uid.llvm.597625021974665233") __attribute__((visibility("hidden"))) = -1;

static __always_inline bool uid_is_traced(struct task_struct *p)
{
	bool ret = false;

	rcu_read_lock();
	if (likely(rcu_dereference(p->real_cred)))
		ret = task_uid(p).val == last_traced_uid;
	rcu_read_unlock();
	return ret;
}

/* game_task / game_vip_task / game_super_task / game_ip_task: flags of the task of the traced (game) uid */
noinline int game_task(struct task_struct *p)
{
	int en = migt_enable();

	if (!p || !en)
		return 0;
	return uid_is_traced(p);
}

noinline int game_vip_task(struct task_struct *p)
{
	struct plat_task_struct *t;
	int en = migt_enable();
	u32 type;

	if (!p || !en || !uid_is_traced(p))
		return 0;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !t->game)
		return 0;
	type = t->game->type;
	if (type & 0x20)
		return 1;
	return (type & 0x8) ? 0 : (type & 0x1d0);
}

noinline int game_super_task(struct task_struct *p)
{
	struct plat_task_struct *t;
	int en = migt_enable();

	if (!p || !en || !uid_is_traced(p))
		return 0;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !t->game)
		return 0;
	return t->game->type & 0x100;
}

noinline int game_ip_task(struct task_struct *p)
{
	struct plat_task_struct *t;
	int en = migt_enable();
	u32 type;

	if (!p || !en || !uid_is_traced(p))
		return 0;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !t->game)
		return 0;
	type = t->game->type;
	if (type & 0x28)
		return 1;
	return !!(type & 0x1d4);
}

/* ---- G14c: GC booster (new in the global OS4.0.0.7 stock; recovered from its disassembly, NOT hardware-tested) ---- */
/* type bit 0x1000 = gc thread, 0x2000 = gc main thread (set by migt_hardwork from the comm of the thread) */
noinline int game_gc_task(struct task_struct *p)
{
	struct plat_task_struct *t;
	int en = migt_enable();

	if (!p || !en || !uid_is_traced(p))
		return 0;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !t->game)
		return 0;
	return t->game->type & 0x3000;
}

noinline int game_main_gc_task(struct task_struct *p)
{
	struct plat_task_struct *t;
	int en = migt_enable();

	if (!p || !en || !uid_is_traced(p))
		return 0;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !t->game)
		return 0;
	return t->game->type & 0x2000;
}

static __always_inline void gc_booster_reset_inl(void)
{
	if (gc_booster_enable)
		gc_booster_enable = 0;
	atomic_set(&is_doing_gc, 0);
	memset(gc_main_thread_name, 0, sizeof(gc_main_thread_name));
	memset(gc_thread_name, 0, sizeof(gc_thread_name));
}

noinline void gc_booster_reset(void)
{
	gc_booster_reset_inl();
}

/* plat hook 0x28 (plat_mi_game_schedule): the gc main thread goes to sleep (not preempted): the gc is over */
static void migt_game_schedule(struct task_struct *prev, struct task_struct *next, struct rq *rq)
{
	if (!GC_NAMES_SET())
		return;
	if (game_main_gc_task(prev) && prev != next && !prev->on_rq && READ_ONCE(prev->__state))
		atomic_set(&is_doing_gc, 0);
}

static int set_gc_booster_enable(const char *val, const struct kernel_param *kp)
{
	int enable = 0, ret;

	if (sscanf(val, "%d", &enable) != 1)
		return -EINVAL;
	ret = param_set_int(val, kp);
	if (ret)
		return ret;
	if (!enable)
		gc_booster_reset_inl();
	return 0;
}

/* thread name params: strip the trailing newline of "echo name > ..." and copy (param_set_copystring) */
static int param_set_task_name(const char *val, const struct kernel_param *kp)
{
	char *s = (char *)val;
	size_t len = strlen(s);

	if (len && s[len - 1] == '\n')
		s[len - 1] = '\0';
	return param_set_copystring(val, kp);
}

static const struct kernel_param_ops param_ops_gc_booster_enable = { .set = set_gc_booster_enable, .get = param_get_int };
static const struct kernel_param_ops param_ops_task_name = { .set = param_set_task_name, .get = param_get_string };
static struct kparam_string gc_thread_name_string = { .maxlen = sizeof(gc_thread_name), .string = gc_thread_name };
static struct kparam_string gc_main_thread_name_string = { .maxlen = sizeof(gc_main_thread_name), .string = gc_main_thread_name };
module_param_cb(gc_booster_enable, &param_ops_gc_booster_enable, &gc_booster_enable, 0644);
module_param_cb(gc_thread_name, &param_ops_task_name, &gc_thread_name_string, 0644);
module_param_cb(gc_main_thread_name, &param_ops_task_name, &gc_main_thread_name_string, 0644);

void mi_cpu_coregroup_mask(int cpu, struct cpumask *mask)
{
	struct cpu_topology *t = &cpu_topology[cpu];
	int i;

	if (t->cluster_id == -1)
		return;
	for_each_possible_cpu(i)
		if (t->cluster_id == cpu_topology[i].cluster_id)
			cpumask_set_cpu(i, mask);
}

bool game_vvip_task(struct task_struct *p)
{
	return game_vip_task(p) != 0;
}

bool game_ssuper_task(struct task_struct *p)
{
	return game_super_task(p) != 0;
}

/* ---- G1w (17.10.2026): get_cluster_type / is_render_sibling / mi_set_sf_flag / set_vip_flag, from the OS4 disassembly (replace the Kleaf stubs) ---- */
extern unsigned int mark_rule1_enable, mark_debug, game_app_select_sf;

/* cpu -> cluster index map built lazily from cpu_topology (33rd word = "initialised" flag) */
static int cpu_cluster_map[33];

noinline enum mi_cluster_type get_cluster_type(int cpu)
{
	struct cpumask mask;
	int i, j, id = 0;
	unsigned int n;

	if (unlikely((unsigned int)cpu > 31))
		return 0;
	if (!cpu_cluster_map[32]) {
		for_each_online_cpu(i) {
			cpumask_clear(&mask);
			if (cpu_topology[i].cluster_id != -1) {
				for_each_possible_cpu(j)
					if (cpu_topology[j].cluster_id == cpu_topology[i].cluster_id)
						cpumask_set_cpu(j, &mask);
			}
			if (cpumask_first_and(&mask, cpu_online_mask) == i) {
				for_each_cpu(j, &mask)
					cpu_cluster_map[j] = id;
				id++;
			}
		}
		if (!cpu_cluster_map[32])
			cpu_cluster_map[32] = 1;
	}
	n = cpu_cluster_map[cpu];
	return n < num_sched_clusters ? n : 0;
}

/* rule 1: a render-flagged waker wakes a "Thread-" task of the traced uid: make the wakee a vip (0x810) and clear the waker's mark (0x400) */
static noinline void mi_set_sf_flag(struct task_struct *waker, struct task_struct *p, int rule_type)
{
	struct plat_task_struct *tw, *tp;
	struct mi_game_task *gw, *gp;

	if (!waker || !p)
		return;
	tw = get_xr_task_struct(waker);
	if (!XR_GAME_VALID(tw) || !(gw = tw->game))
		return;
	get_task_struct(p);
	tp = get_xr_task_struct(p);
	if (XR_GAME_VALID(tp) && (gp = tp->game) && (gw->type & 0x400) && migt_enable() &&
	    uid_is_traced(p) && game_app_select_sf == 1 && !strncmp(p->comm, "Thread-", 7)) {
		gw->type &= ~0x400;
		if (!(gp->type & 0x810)) {
			gp->type |= 0x810;
			set_vip_prio(p, 30, 1);
			if (mark_debug)
				trace_printk("mi_set_sf_flag rule1 mark waker_flag=%d,wakee_flag=%d,waker->comm=%s,wakee->comm=%s\n",
					     gw->type, gp->type, waker->comm, p->comm);
		}
	}
	put_task_struct(p);
}

noinline void set_vip_flag(struct task_struct *p)
{
	struct plat_task_struct *t;
	struct mi_game_task *gt;

	if (!migt_enable() || !mark_rule1_enable)
		return;
	t = get_xr_task_struct(p);
	if (!XR_GAME_VALID(t) || !(gt = t->game))
		return;
	if (!strncmp(current->comm, "TimerDispatch", 14)) {
		if (game_app_select_sf == 2) {
			if (!strncmp(p->comm, "surfaceflinger", 14) && !(gt->type & 0x400))
				gt->type |= 0x400;
		} else if (game_app_select_sf == 1) {
			if (!strncmp(p->comm, "app", 3) && !(gt->type & 0x400))
				gt->type |= 0x400;
		}
	}
	mi_set_sf_flag(current, p, 1);
}

static void android_rvh_try_to_wake_up_success(void *unused, struct task_struct *p)
{
	set_vip_flag(p);
	if (GC_NAMES_SET() && game_main_gc_task(p) && !atomic_read(&is_doing_gc))
		atomic_set(&is_doing_gc, 1);
}

noinline int migt_enable(void)
{
	debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	return migt_boost_policy;
}

static bool migt_enable_gpsync(void)
{
	debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	return migt_boost_policy && migt_gpsync;
}

struct sort_item {
	u64 pad;
	u64 val;
};

static int cmp(const void *a, const void *b)
{
	u64 x = ((const struct sort_item *)a)->val, y = ((const struct sort_item *)b)->val;

	if (x > y)
		return -1;
	return x < y;
}

void game_get_ravg_window(unsigned int window)
{
	sched_ravg_window = window;
}

/* ---- small helpers (G1t, from the stock disassembly) ---- */
extern unsigned int stask_prefer_cpu, render_prefer_cpu, vip_prefer_cpu, ip_prefer_cpu;
extern unsigned long migt_reserve_cpus;
extern int choose_cpu_exclusive_enable, choose_cpu_exclusive_enable_vote;
extern struct plat_task_struct *get_xr_task_struct(struct task_struct *p);
static bool game_render_task_valid(struct plat_task_struct *xr);

u64 get_migt_thresh(void)
{
	return migt_thresh;
}

int get_cur_render_uid(void)
{
	return last_traced_uid;
}

static int get_migt_flag(char *buf, const struct kernel_param *kp)
{
	return 0;
}

u64 fas_sched_clock(void)
{
	return sched_clock();
}

int package_runtime_should_stop(void)
{
	return migt_boost_policy == 0;
}

void trigger_ceiling_limit(void)
{
}

/* reserved cpu mask = the four prefer-cpu votes that are set (render, vip, stask, ip) */
noinline void update_migt_reserve_cpus(void)
{
	migt_reserve_cpus = 0;
	if (render_prefer_cpu != -1)
		set_bit(render_prefer_cpu, &migt_reserve_cpus);
	if (vip_prefer_cpu != -1)
		set_bit(vip_prefer_cpu, &migt_reserve_cpus);
	if (stask_prefer_cpu != -1)
		set_bit(stask_prefer_cpu, &migt_reserve_cpus);
	if (ip_prefer_cpu != -1)
		set_bit(ip_prefer_cpu, &migt_reserve_cpus);
}

noinline void migt_choose_exclusive_cpu_reset(void)
{
	choose_cpu_exclusive_enable = 0;
	choose_cpu_exclusive_enable_vote = 1;
	stask_prefer_cpu = -1;
	render_prefer_cpu = -1;
	ip_prefer_cpu = -1;
	vip_prefer_cpu = -1;
	update_migt_reserve_cpus();
}

static int migt_reset(struct inode *inode, struct file *file)
{
	migt_boost_policy = 0;
	migt_choose_exclusive_cpu_reset();
	gc_booster_reset();
	return 0;
}

static __always_inline bool migt_reserve_usable(void)
{
	u32 reserve;

	if (!migt_enable() || !choose_cpu_exclusive_enable || !choose_cpu_exclusive_enable_vote)
		return false;
	reserve = migt_reserve_cpus;	/* G14c: read after the votes (stock order, the gc check below is a volatile read) */
	return reserve && !(reserve & ~cpumask_bits(cpu_online_mask)[0]) && !atomic_read(&is_doing_gc);	/* G14c: no exclusive cpus during a gc */
}

noinline bool migt_choose_exclusive_cpu_enable(void)
{
	return migt_reserve_usable();
}

bool migt_skip_cpus(struct cpumask *mask)
{
	if (!migt_reserve_usable())
		return false;
	cpumask_bits(mask)[0] = 0;
	cpumask_bits(mask)[0] = migt_reserve_cpus;
	return true;
}

/* render task = vendor game task of type 0xc0 (bits: render / wrender) */
bool game_render_task(struct task_struct *p)
{
	struct plat_task_struct *xr = get_xr_task_struct(p);

	return game_render_task_valid(xr);
}

static bool game_render_task_valid(struct plat_task_struct *xr)
{
	return XR_GAME_VALID(xr) && xr->game && (xr->game->type & 0xc0);
}

noinline void wake_render(struct task_struct *p)
{
	if (game_render_task(p)) {
		struct plat_task_struct *cur = get_xr_task_struct(current);

		if (XR_GAME_VALID(cur) && cur->game)
			cur->game->wake_render++;
	}
}

static void xr_game_find_energy_hook(struct task_struct *p, bool *need_idle)
{
	wake_render(p);
	*need_idle = *need_idle ? true : game_vip_task(p) != 0;
}

bool game_stask_pipeline(struct task_struct *p)
{
	bool enabled = migt_enable();

	if (p && enabled && uid_is_traced(p))
		get_xr_task_struct(p);
	return false;
}

bool game_render_pipeline(struct task_struct *p)
{
	bool enabled = migt_enable();

	if (p && enabled && uid_is_traced(p))
		get_xr_task_struct(p);
	return false;
}

extern bool plat_is_relaxed_rt(struct task_struct *p);

/* a traced-uid render/vip task may only use the reserved cpus: is p such a task (type bits 0x1e0) */
bool migt_cpu_check_rt(struct task_struct *p)
{
	struct plat_task_struct *xr;
	bool enabled;

	if (!migt_reserve_usable())
		return false;
	enabled = migt_enable();
	if (!p || !enabled || !uid_is_traced(p))
		return false;
	xr = get_xr_task_struct(p);
	if (XR_GAME_VALID(xr) && xr->game)
		return (u16)xr->game->type & 0x1e0;
	return false;
}

bool migt_should_skip_this_cpu_rt(struct task_struct *p, int cpu)
{
	if (!migt_reserve_usable())
		return false;
	return test_bit((unsigned int)cpu, &migt_reserve_cpus);	/* G14i: stock zero-extends the index */
}

/* skip mask for a relaxed rt task: the reserved cpus that are not idle */
bool migt_skip_cpus_rt(struct task_struct *p, struct cpumask *mask)
{
	int cpu;

	if (!migt_reserve_usable())
		return false;
	cpumask_bits(mask)[0] = 0;
	cpumask_bits(mask)[0] = migt_reserve_cpus;
	if (plat_is_relaxed_rt(p)) {
		for_each_cpu(cpu, mask) {
			if (!*(u32 *)((u8 *)per_cpu_ptr(&runqueues, cpu) + 4))
				cpumask_clear_cpu(cpu, mask);
		}
	}
	return !cpumask_empty(mask);	/* G14i: stock ldr w8 (32 cpu bits) */
}

static __always_inline struct mi_game_task *xr_game_of(struct task_struct *p)
{
	struct plat_task_struct *xr = get_xr_task_struct(p);

	return XR_GAME_VALID(xr) ? xr->game : NULL;
}

noinline bool is_uuser_set_vip(struct task_struct *p)
{
	struct mi_game_task *gt = xr_game_of(p);
	bool enabled = migt_enable();

	if (!p || !enabled || !uid_is_traced(p))
		return false;
	return gt && ((gt->type >> 5) & 1);
}

noinline bool is_uuser_set_ip(struct task_struct *p)
{
	struct mi_game_task *gt = xr_game_of(p);
	bool enabled = migt_enable();

	if (!p || !enabled || !uid_is_traced(p))
		return false;
	return gt && ((gt->type >> 3) & 1);
}

/* user space marks p as VIP (bit 0x20, clears IP 0x8) / unmarks it */
noinline void user_set_game_vip(struct task_struct *p, bool set)
{
	struct mi_game_task *gt;

	if (!p)
		return;
	gt = xr_game_of(p);
	if (!gt)
		return;
	if (set) {
		gt->type = (gt->type & ~0x8) | 0x20;
		set_vip_prio(p, 30, 1);
	} else {
		gt->type &= ~0x20;
		restore_vip_prio(p);
	}
}

noinline void user_set_game_ip(struct task_struct *p, bool set)
{
	struct mi_game_task *gt;

	if (!p)
		return;
	gt = xr_game_of(p);
	if (!gt)
		return;
	if (set) {
		gt->type = (gt->type & ~0x20) | 0x8;
		set_vip_prio(p, 30, 1);
	} else {
		gt->type &= ~0x8;
		restore_vip_prio(p);
	}
}

/* plat func 22: the cpu a game task is pinned to (or -1) */
int migt_choose_exclusive_cpu(struct task_struct *p)
{
	struct plat_task_struct *xr;
	int en;

	if (!migt_reserve_usable())
		return -1;
	en = migt_enable();	/* G14i: stock calls it (debugfs_walt_features refresh) before the p test */
	if (!p || !en)
		return -1;
	if (!uid_is_traced(p))
		return -1;
	if (game_super_task(p))
		return stask_prefer_cpu;
	xr = get_xr_task_struct(p);
	if (XR_GAME_VALID(xr) && xr->game && (xr->game->type & 0xc0))
		return render_prefer_cpu;
	if (is_uuser_set_vip(p))
		return vip_prefer_cpu;
	return is_uuser_set_ip(p) ? ip_prefer_cpu : -1;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("migt-driver by David");

/* walt cluster order hook for game tasks: which cluster is preferred and whether the energy evaluation is skipped */
void xr_game_walt_get_indicies(struct task_struct *p, int *order_index, int *end_index,
					     int num_clusters, bool *skip_walt_indicies, bool *energy_eval_needed)
{
	int big = num_clusters > 2;
	int pref;

	debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	if (!migt_boost_policy || choose_cluster_disable)
		return;
	if (force_cluster_sched_enable)
		*end_index = 0;
	if (game_super_task(p)) {
		*energy_eval_needed = false;
		*order_index = big;
		pref = stask_prefer_cluster;
		if (num_clusters == 2 || pref) {
			*order_index = pref;
			*end_index = 0;
		}
	} else if (is_render_thread(p)) {
		*energy_eval_needed = false;
		*order_index = big;
		pref = render_prefer_cluster;
		if (num_clusters == 2 || pref) {
			*order_index = pref;
			*end_index = 0;
		}
	} else if (game_vip_task(p)) {
		*energy_eval_needed = false;
		*order_index = big;
		pref = vip_prefer_cluster;
		*order_index = (pref || num_clusters == 2) ? pref : big;
	} else if (game_ip_task(p)) {
		*energy_eval_needed = false;
		pref = ip_prefer_cluster;
		if (pref)
			*order_index = pref;
	} else {
		return;
	}
	*skip_walt_indicies = true;
}


/*
 * load-balance veto (G1ab): may thread p move from cpu "src" to cpu "dst"? Returns 1 to forbid. A game thread class
 * (stask / render / vip / ip) is kept inside its *_prefer_cluster. NOT registered anywhere in the stock either.
 */
extern int force_forbidden_walt_lb;
extern unsigned int task_enqueue_time_toolong;

bool game_lb_control(struct task_struct *p, int src, int dst, int unused)
{
	struct plat_task_struct *xr = get_xr_task_struct(p);
	struct mi_game_task *gt = XR_GAME_VALID(xr) ? xr->game : NULL;
	bool vip, ip, render, super_t;
	int ffwl;
	u64 now;

	if (!migt_enable() || !p || !gt || !force_forbidden_walt_lb)
		return 0;
	now = sched_clock();
	if (gt->enqueue_tm && p->se.on_rq && now - gt->enqueue_tm > (u64)task_enqueue_time_toolong * 1000000)
		return 0;
	super_t = game_super_task(p);
	if (dst == 31 && !super_t && stask_prefer_cluster == 3)
		return 1;
	xr = get_xr_task_struct(p);
	render = XR_GAME_VALID(xr) && xr->game ? !(xr->game->type & 0xc0) : true;
	if (dst == 31 && render && render_prefer_cluster == 3)
		return 1;
	if (game_super_task(p) && (int)stask_prefer_cluster >= 1 && get_cluster_type(dst) != stask_prefer_cluster &&
	    get_cluster_type(src) == stask_prefer_cluster)
		return 1;
	xr = get_xr_task_struct(p);
	if (XR_GAME_VALID(xr) && xr->game && (xr->game->type & 0xc0) && (int)render_prefer_cluster >= 1 &&
	    get_cluster_type(dst) != render_prefer_cluster && get_cluster_type(src) == render_prefer_cluster)
		return 1;
	ffwl = force_forbidden_walt_lb;	/* g62a: the stock reads it once for both choices (S+200) */
	vip = ffwl > 1 ? game_vip_task(p) : is_uuser_set_vip(p);
	if (vip && (int)vip_prefer_cluster >= 1 && get_cluster_type(dst) != vip_prefer_cluster &&
	    get_cluster_type(src) == vip_prefer_cluster)
		return 1;
	ip = ffwl > 1 ? game_ip_task(p) : is_uuser_set_ip(p);
	if (ip && (int)ip_prefer_cluster >= 1 && get_cluster_type(dst) != ip_prefer_cluster &&
	    get_cluster_type(src) == ip_prefer_cluster)
		return 1;
	return 0;
}
