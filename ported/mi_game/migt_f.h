/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: /dev/migt ioctl (G1z, 18.10.2026). Included at the end of params.c after migt_e.h.
 * Logic recovered from the OS4 stock disassembly (jump table of 11 commands), NOT hardware-tested: DO NOT LOAD.
 */
#include <linux/uaccess.h>

extern void trigger_mist_load_tracking(void *wq);
extern void notify_gflt_frame_period_thresh(int thresh);
extern void flt_update_fps_time(s64 vsync);
extern void flt_update_sf(struct frame_sf *sf);
extern void flt_adjust_dequeue(void);
extern void flt_update_gpu(s64 t);
extern void flt_update_fence_pending(void);
extern void mi_set_flt_target_delta(int delta);
extern void mist_frame_load_reset(void);
extern void mist_frame_load_history_update(int trigger_source);

static unsigned long ceiling_next_update;
static unsigned int render_pid;

/* cmd 1: the game signals a frame ("boost coming") */
static __always_inline void migt_ioctl_boost(void)
{
	unsigned int uid = current_uid().val;
	int thresh;

	trigger_mist_load_tracking(migt_wq);
	if (uid != last_traced_uid) {
		if (ceiling_freq_limit_work == 1) {
			cancel_delayed_work_sync(&ceiling_rem);
			ceiling_next_update = jiffies;
			queue_delayed_work_on(WORK_CPU_UNBOUND, migt_wq, &ceiling_rem, 1);
		}
		reset_render_info(0);
		reset_render_info(1);
		sys_migt_count = 0;
		mist_frame_load_reset();
		last_traced_uid = uid;
		force_reset_runtime = 1;
	}
	debugfs_walt_features = (debugfs_walt_features & ~1) | !!migt_walt_features;
	if (migt_boost_policy) {
		update_render_info(current, 0);
		if (migt_enabled && migt_boost_policy) {
			thresh = migt_thresh;
			notify_gflt_frame_period_thresh(thresh);
			sys_migt_count++;
			if (sys_migt_count > first_lunch_fps && migt_boost_policy >= 2) {
				if (sys_migt_count % itask_detect_interval == 0 || current->pid != render_pid) {
					glk_freq_limit_start = 1;
					if (current->pid != render_pid) {
						if (unlikely(migt_debug))
							printk(KERN_INFO "migt: render pid change from %d to %d\n", render_pid,
							       current->pid);
						if (!render_update)
							goto after_add;
					}
					add_work_to_migt(current_uid().val);
				}
			}
after_add:
			if (sys_migt_count <= first_lunch_fps)
				glk_freq_limit_start = 0;
			render_pid = current->pid;
			{
				u64 now = sched_clock();

				if (now - last_update_time >= (u64)migt_thresh * 1000000)
					over_thresh_count++;
				last_update_time = now;
			}
			migt_boost_flag = 0;
			boost_cycle = 0;
			if (ceiling_freq_limit_work == 1 && (long)(ceiling_next_update - jiffies) < 0) {
				ceiling_next_update = jiffies + msecs_to_jiffies(ceiling_slack_ms >> 1);
				cancel_delayed_work_sync(&ceiling_rem);
				queue_delayed_work_on(WORK_CPU_UNBOUND, migt_wq, &ceiling_rem,
						      msecs_to_jiffies(ceiling_slack_ms));
			}
			switch (migt_boost_policy) {
			case 0:
				break;
			case 1:
				if (high_resolution_enable) {
					if (hrtimer_active(&hrtimer))
						hrtimer_cancel(&hrtimer);
					hrtimer_start_range_ns(&hrtimer, (s64)thresh * 1000000, 0, HRTIMER_MODE_REL);
				} else {
					cancel_delayed_work_sync(&migt_work);
					queue_delayed_work_on(WORK_CPU_UNBOUND, migt_wq, &migt_work, msecs_to_jiffies(thresh));
				}
				break;
			case 2:
				break;
			default:
				printk(KERN_ERR "migt: invalid version number %d\n", migt_boost_policy);
			}
		}
	}
	mist_frame_load_history_update(1);
	if (mark_rule2_enable)
		migt_item_index = atomic_inc_return((atomic_t *)&migt_item_index) % 10;
}

/* cmd 4: remember the traced uid in the 10-entry ceiling user list (oldest dropped) */
static __always_inline void migt_ioctl_ceiling_user(void)
{
	unsigned int n = ceiling_users[10], uid = last_traced_uid, cnt = min(n, 10U), i;

	if (unlikely(migt_debug)) {
		printk(KERN_INFO "--------ceiling_user list--------\n");
		for (i = 0; i < n && i < 10; i++)
			printk(KERN_INFO "%d ", ceiling_users[i]);
	}
	for (i = 0; i < cnt; i++)
		if (ceiling_users[i] == uid)
			return;
	if (unlikely(migt_debug))
		printk(KERN_INFO "\nadd uid %d to ceiling users\n", uid);
	if (n >= 10) {
		for (i = 0; i + 1 < cnt; i++)
			ceiling_users[i] = ceiling_users[i + 1];
		ceiling_users[9] = uid;
		return;
	}
	ceiling_users[cnt] = uid;
	ceiling_users[10]++;
}

static __used long migt_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct frame_sf sf = {};
	int v = 0;

	if (unlikely(migt_debug))
		printk(KERN_ERR "migt: migt boost comming. uid %d %d %d %llu", current_uid().val, 1117, cmd & 0xff,
		       ktime_to_us(ktime_get()));
	switch (cmd & 0xff) {
	case 1:
		migt_ioctl_boost();
		break;
	case 3:
		update_render_info(current, 1);
		break;
	case 4:
		migt_ioctl_ceiling_user();
		break;
	case 7:
		flt_adjust_dequeue();
		break;
	case 8:
		flt_update_gpu(arg);
		break;
	case 9:
		if ((cmd & 0x3fff0000) != 0x200000)
			break;
		if (copy_from_user(&sf, (void __user *)arg, sizeof(sf)))
			break;
		if (last_traced_uid != sf.current_uid)
			break;
		flt_update_fps_time(sf.vsyncPeriod);
		flt_update_sf(&sf);
		break;
	case 10:
		if (copy_from_user(&v, (void __user *)arg, sizeof(v)))
			break;
		if (v < 1)
			mi_set_flt_target_delta(0);
		else
			mi_set_flt_target_delta(100000 / (u32)v);
		break;
	case 11:
		if (unlikely(migt_debug))
			printk_deferred("FENCE_PENDING\n");
		flt_update_fence_pending();
		break;
	default:
		break;
	}
	return 0;
}

/* ---- G1ab (18.10.2026): migt_hardwork, recovered from the OS4 stock disassembly (was a stub) ---- */
#include <linux/sort.h>
extern void set_vip_prio(struct task_struct *p, int prio, int flag);
extern void restore_vip_prio(struct task_struct *p);
extern struct mi_plat_user_struct *get_xr_user_struct(struct user_struct *user);

/* the two render threads of the traced game, copied lockless out of render_info[] (stock: same TU as migt_hardwork) */
struct rend_info_s {	/* BTF of the stock, 64 bytes; only valid / name are read here */
	u32 f0, f4;
	bool valid;
	int f12;
	unsigned long last_update_jiffies;
	char name[16];
	u8 pad[24];
};
static struct rend_info_s rinfo_back[2] __used;

static noinline bool is_render_sibling(struct task_struct *p)
{
	int i;

	for (i = 0; i < 2; i++)
		if (rinfo_back[i].valid && !strncmp(rinfo_back[i].name, p->comm, strlen(rinfo_back[i].name)))
			return true;
	return false;
}

bool is_frender_sibling(struct task_struct *p)
{
	return is_render_sibling(p);
}

static int game_setup_wrender_list(struct task_struct *p, int tgid, struct mi_game_task *gt, int rule_type);
extern void mark_rollover_window_task(int rule);
extern void clean_task_window(int rule_type);

struct user_array_ent {
	struct mi_game_task *gt;
	u64 key;		/* wake_render count: the array is sorted by it, biggest first */
};
static struct user_array_ent user_array[1024];
static unsigned int user_array_num;

static int cmp(const void *a, const void *b);
struct sort_item;

/* inline copy of is_render_thread() (the stock inlines it here, calls it elsewhere) */
static __always_inline int is_render_thread_inl(struct task_struct *p)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	if (XR_GAME_VALID(t) && t->game)
		return t->game->type & 0xc0;
	return 0;
}

static __always_inline struct mi_game_task *get_gt_task_inl(struct task_struct *p)
{
	struct plat_task_struct *t = get_xr_task_struct(p);

	return XR_GAME_VALID(t) ? t->game : NULL;
}

#define GT_TASK(g)	((g) ? (g)->task : NULL)
#define TASK_WTS_PTR(t)	((u8 *)((unsigned long)(t) + 0xbc0))
#define TASK_WTS_DEMAND(t)	(*(u32 *)(TASK_WTS_PTR(t) + 0x20))

/* drop the stale marks of a thread that is no longer what it was marked as */
static __always_inline void gt_clear_marks(struct mi_game_task *g)
{
	struct task_struct *t = g->task;

	if (t && is_render_thread_inl(t)) {
		if (!set_render_as_stask)
			g->type &= ~0x100;
	} else {
		if (g->type & 0x100)
			restore_vip_prio(t);
		g->type &= ~0x1d6;
	}
}

static __always_inline void render_info_snapshot(int i)
{
	struct render_info_s *ri = &render_info[i];
	unsigned int seq;

	rinfo_back[i].valid = false;
	do {
		seq = read_seqcount_begin((seqcount_t *)&ri->seq);
		rinfo_back[i].valid = ri->valid;
		memcpy(rinfo_back[i].name, ri->name, sizeof(ri->name));
	} while (read_seqcount_retry((seqcount_t *)&ri->seq, seq));
}

/*
 * The "migt_thread" worker: woken by add_work_to_migt(uid) with g_work = the uid of the traced game. It walks the
 * threads of that uid, marks the render threads, picks the busiest ones (wake_render count) as stask / vip / ip
 * tasks and clears the marks of the rest.
 */
static __used int migt_hardwork(void *data)
{
	unsigned int *work = data;

	for (;;) {
		struct user_struct *user;
		struct mi_plat_user_struct *xu;
		struct mi_game_task *gt;
		struct list_head *pos;
		LIST_HEAD(delay_head);
		int n = 0, i;

		set_current_state(TASK_INTERRUPTIBLE);
		if (!smp_load_acquire(&migt_has_work_todo)) {
			schedule();
			if (kthread_should_stop())
				break;
		}
		set_current_state(TASK_RUNNING);
		if (!*work)
			goto done;
		user = find_user(KUIDT_INIT(*work));
		if (!user)
			goto reset;
		xu = get_xr_user_struct((struct user_struct *)user);
		if (!xu)
			goto free;
		render_info_snapshot(0);
		render_info_snapshot(1);
		read_lock_irq(&xu->lock);
		if (list_empty(&xu->list)) {
			read_unlock_irq(&xu->lock);
			goto free;
		}
		for (pos = xu->list.next; pos != &xu->list; pos = pos->next) {
			struct task_struct *task;
			struct mi_game_task *g;

			gt = list_entry(pos, struct mi_game_task, list);
			if (!gt)
				break;
			task = gt->task;
			if (!task)
				break;
			if (unlikely(!refcount_inc_not_zero(&task->usage)))
				continue;
			get_task_struct(task);
			refcount_dec(&task->usage);
			g = get_gt_task_inl(task);
			if (!g) {
				put_task_struct(task);
				continue;
			}
			if ((int)g->f116 < 0)
				goto delay;
			BUG_ON(gt->f116 != user->uid.val);
			if (mark_rule3_enable)
				game_setup_wrender_list(task, task->tgid, g, 0);
			if (mark_rule1_enable && (g->type & 0x810)) {
				g->type &= ~0x810;
				restore_vip_prio(task);
			}
			if (need_reset_runtime_everytime | force_reset_runtime)
				memset(g->f124, 0, sizeof(g->f124));
			if (is_render_thread_inl(task) || is_render_sibling(task)) {
				struct task_struct *t2;

				if (is_render_sibling(task))
					gt_clear_marks(g);
				if (need_reset_runtime_everytime | force_reset_runtime)
					g->fps_mexec = 0;
				t2 = g->task;
				if (set_render_as_stask) {
					g->type |= 0x100;
					set_vip_prio(t2, 30, 1);
				} else {
					g->type |= 0x10;
				}
				if (t2 && TASK_WTS_PTR(t2))
					top_task_util[0] = TASK_WTS_DEMAND(t2) / (sched_ravg_window >> 10);
				goto delay;
			}
			if (!g->wake_render) {
				gt_clear_marks(g);
				if (need_reset_runtime_everytime | force_reset_runtime)
					g->fps_mexec = 0;
				goto delay;
			}
			user_array[n].gt = g;
			user_array[n].key = g->wake_render;
			if (need_reset_runtime_everytime | force_reset_runtime)
				g->fps_mexec = 0;
			if (unlikely(n >= 1023)) {
				printk(KERN_ERR "migt-gtc: too many VIP task\n");
				n = 1024;
				break;
			}
			n++;
			continue;
delay:
			list_add(&g->delay_list, &delay_head);
		}
		read_unlock_irq(&xu->lock);
		for (pos = delay_head.next; pos != &delay_head;) {
			struct list_head *cur = pos;

			pos = pos->next;
			gt = list_entry(cur, struct mi_game_task, delay_list);
			if (!gt)
				continue;
			list_del_init(&gt->delay_list);
			if (gt->task)
				put_task_struct(gt->task);
		}
		if (n >= 1) {
			unsigned int max_i = 0, mindex = 0, sel;
			u64 max_r = 0, m_exec = 0;
			int cand;

			user_array_num = n;
			sort(user_array, n, sizeof(user_array[0]), (cmp_func_t)cmp, NULL);
			if (pick_stask_policy == 1) {
				cand = min((int)stask_candidate_num, n);
				if (user_stask[0] && user_stask[0] != '\n') {
					int found = -1;

					for (i = 0; i < user_array_num; i++) {
						struct mi_game_task *g = user_array[i].gt;

						if (g && g->task && sysfs_streq(g->task->comm, user_stask)) {
							found = i;
							break;
						}
					}
					if (found >= 0) {
						max_i = mindex = found;
						max_r = user_array[found].gt->fps_mexec;
						m_exec = user_array[found].gt->fps_exec_est;
						goto picked;
					}
					if (migt_debug)
						printk(KERN_INFO "migt-gtc: user stask %.*s not found in wake_array!\n",
						       (int)strcspn(user_stask, "\n"), user_stask);
				}
				for (i = 0; i < cand; i++) {
					struct mi_game_task *g = user_array[i].gt;

					if (g->fps_mexec > max_r) {
						max_r = g->fps_mexec;
						max_i = i;
					}
					if (g->fps_exec_est > m_exec) {
						m_exec = g->fps_exec_est;
						mindex = i;
					}
				}
picked:
				if (migt_debug)
					printk(KERN_INFO "migt-gtc: max %d mindex %d, max_r %llu, m_exec %llu, V: %d\n",
					       max_i, mindex, max_r, m_exec, 50);
				sel = max_r >= 50 ? max_i : mindex;
			} else {
				sel = 0;
			}
			for (i = 0; i < n; i++) {
				struct task_struct *t;

				if (!user_array[i].gt) {
					printk(KERN_ERR "migt-gtc: %d: task is NULL\n", i);
					continue;
				}
				if (i < 20 && migt_debug)
					printk(KERN_ERR "migt-gtc: %3d:comm %16s pid %6d key:%llu load %llu est %llu sup:%d\n",
					       i, user_array[i].gt->task->comm, user_array[i].gt->task->pid,
					       user_array[i].key, user_array[i].gt->fps_mexec,
					       user_array[i].gt->fps_exec_est, sel);
				gt_clear_marks(user_array[i].gt);
				t = GT_TASK(user_array[i].gt);
				if (sel == i) {
					user_array[i].gt->type |= 0x100;
					set_vip_prio(t, 30, 1);
					if (t && TASK_WTS_PTR(t))
						top_task_util[1] = TASK_WTS_DEMAND(t) / (sched_ravg_window >> 10);
				} else if (i < (int)(stask_candidate_num + vip_task_max_num)) {
					user_array[i].gt->type |= 0x10;
				} else if (i < (int)(stask_candidate_num + vip_task_max_num + ip_task_max_num)) {
					user_array[i].gt->type |= 0x4;
				}
				if (need_reset_runtime_everytime | force_reset_runtime)
					user_array[i].gt->wake_render = 0;
				put_task_struct(GT_TASK(user_array[i].gt));
				user_array[i].gt = NULL;
				user_array[i].key = 0;
			}
			user_array_num = 0;
		}
		if (mark_rule2_enable)
			mark_rollover_window_task(2);
		force_reset_runtime = 0;
free:
		free_uid(user);
reset:
done:
		*work = 0;
		migt_has_work_todo = 0;
	}
	printk(KERN_INFO "migt-gtc: migt_ca has exit\n");
	return 0;
}
