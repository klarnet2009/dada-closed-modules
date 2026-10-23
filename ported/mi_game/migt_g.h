/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_game: mark rules 2 / 3 (G1ab, 18.10.2026). Included at the end of params.c (before migt_a.h).
 * mark_rollover_window_task, clean_task_window and game_setup_wrender_list recovered from the OS4 stock
 * disassembly, NOT hardware-tested: DO NOT LOAD.
 */

/* per frame-window slot (10 windows x 50 pids) a pid of a thread that was marked in that window; 0 = free */
extern u32 frame_migt_window[10][50];

/* rule 2: the threads that were seen in the windows get the vip mark (0x10); "UnityMain" also the stask mark (0x100) */
void mark_rollover_window_task(int rule_type)
{
	int j, i;

	for (j = 0; j < 10; j++) {
		for (i = 0; i < 50; i++) {
			struct task_struct *t;
			struct plat_task_struct *xr;
			struct mi_game_task *gt;
			int pid = smp_load_acquire(&frame_migt_window[j][i]);

			if (!pid)
				continue;
			rcu_read_lock();
			t = find_task_by_vpid(pid);
			if (!t) {
				rcu_read_unlock();
				frame_migt_window[j][i] = 0;
				continue;
			}
			get_task_struct(t);
			rcu_read_unlock();
			xr = get_xr_task_struct(t);
			/* g62a: the stock clears the slot only when the pid is gone (S+60); marked and non-game tasks stay
			 * in the window, so clean_task_window() can restore them at the rollover */
			if (!XR_GAME_VALID(xr) || !(gt = xr->game)) {
				put_task_struct(t);
				continue;
			}
			if (!strncmp(t->comm, "UnityMain", 9))
				gt->type |= 0x100;
			if (mark_debug)
				trace_printk("rule2 %s:rule%d mark---i=%d,j=%d,pid=%d,tsk=%s\n", __func__, rule_type, i, j, pid,
					     t->comm);
			if (!(gt->type & 0x10)) {
				gt->type |= 0x10;
				set_vip_prio(t, 30, 1);
			}
			put_task_struct(t);
		}
	}
}

/* the next rollover: forget the vip mark of the threads of the window that comes up (migt_item_index) */
void clean_task_window(int rule_type)
{
	unsigned int row = smp_load_acquire(&migt_item_index);
	int i;

	/* g62a: plain [row][i] indexing: the UBSAN bounds check gives the stock brk #0x5512 for row > 9 (S+2c) */
	for (i = 0; i < 50; i++) {
		struct task_struct *t;
		struct plat_task_struct *xr;
		struct mi_game_task *gt;
		int pid = smp_load_acquire(&frame_migt_window[row][i]);

		if (!pid)
			continue;
		rcu_read_lock();
		t = find_task_by_vpid(pid);
		if (!t) {
			rcu_read_unlock();
			frame_migt_window[row][i] = 0;
			continue;
		}
		get_task_struct(t);
		rcu_read_unlock();
		xr = get_xr_task_struct(t);
		/* g62a: no slot clear on this path in the stock (S+13c -> put -> S+6c) */
		if (!XR_GAME_VALID(xr) || !(gt = xr->game)) {
			put_task_struct(t);
			continue;
		}
		if (mark_debug)
			trace_printk("clean_task_window rule2 clean---p:%s,vip_flag=%d,migt_item_index=%d,task_pid[%d]=%d\n",
				     t->comm, gt->type, row, i, pid);
		gt->type &= ~0x810;
		restore_vip_prio(t);
		frame_migt_window[row][i] = 0;
		put_task_struct(t);
	}
}

/*
 * rule 3: thread p of process tgid matches an entry of the "render" list (process name or "*", thread name prefix):
 * remember it in the "wrender" list once and make it a vip.
 */
static int game_setup_wrender_list(struct task_struct *p, int tgid, struct mi_game_task *gt, int rule_type)
{
	struct task_struct *tgl;
	struct game_spid *e, *w;
	int ret = 1;

	rcu_read_lock();
	tgl = find_task_by_vpid(tgid);
	if (!tgl) {
		rcu_read_unlock();
		return 1;
	}
	get_task_struct(tgl);
	rcu_read_unlock();
	for (e = (struct game_spid *)game_task_render_list.first; e; e = (struct game_spid *)e->node.next) {
		if (strcmp(e->process_name, "*") && strncmp(tgl->comm, e->process_name, 16))
			continue;
		if (strncmp(p->comm, e->thread_name, strlen(e->thread_name)))
			continue;
		for (w = (struct game_spid *)game_task_wrender_list.first; w; w = (struct game_spid *)w->node.next)
			if (w->pid == p->pid && w->uid == tgid)
				goto out;
		w = game_add_task(&game_task_wrender_list);
		if (!w) {
			ret = -ENOMEM;
			goto out;
		}
		strncpy(w->process_name, e->process_name, 16);
		w->process_name[15] = 0;
		strncpy(w->thread_name, e->thread_name, 16);
		w->thread_name[15] = 0;
		w->uid = tgid;
		w->pid = p->pid;
		gt->type |= 0x10;
		set_vip_prio(p, 30, 1);
		if (mark_debug)
			trace_printk("game_setup_wrender_list rule3 mark process_name=%s,thread_name=%s\n", w->process_name,
				     w->thread_name);
	}
out:
	put_task_struct(tgl);
	return ret;
}
