/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mi_low_lat_mem: the four allocation-latency END handlers (cma_alloc, dma_heap_buffer_alloc, mm_direct_reclaim,
 * alloc_pages_slowpath), G1x 18.10.2026. Recovered from the OS4 stock disassembly: NOT byte-identical, NOT hardware-tested.
 * Included at the end of mi_low_lat_mem.c (needs the fault records, check_report_limit, get_time_bucket, dfx / mci blobs).
 *
 * Common pattern: scene = scene_code - 101 (0..4, else drop); latency dt = sched_clock() - stime; (cma / dma / order4/9: per
 * (scene, process type, bucket) counters + total/max under the dfx lock); if dt > the dfx timeout of the type: printk +
 * tracing_mark_write report with the pre-compact snapshot, and a MiSight fault record (rate limited, claimed by cmpxchg).
 */
extern bool is_qos_task(struct task_struct *p);

static struct mem_alloc_fault_data mem_fault_data;

/* pool entry of mi_smart_refill_pools as read by the stock (node at +0x10: type at node-0x10, value at node-8, counters at node+0x18/+0x20) */
struct pool_stat_ent {
	int type;
	int pad;
	u32 val;
	u32 pad2;
	struct list_head list;
	u64 pad3;
	u64 a, b;
};

/* pre-compact snapshot of the last history slot: targets, pool fill levels at the time, current collected counts */
struct llm_snap {
	u32 t4, p4, c4, t9, p9, c9;
};

static __always_inline void llm_snapshot(struct llm_snap *s, bool want4, bool want9)
{
	u32 cnt = mci.hist_cnt;

	memset(s, 0, sizeof(*s));
	if (cnt) {
		struct pc_rec *rec = &mci.hist[(cnt - 1) % 30];

		if (want4) {
			s->t4 = rec->t4;
			s->p4 = rec->n4;
			s->c4 = smp_load_acquire((u32 *)((u8 *)MCI_PTR(0x30) + 0x18));
		}
		if (want9) {
			s->t9 = rec->t9;
			s->p9 = rec->n9;
			s->c9 = smp_load_acquire((u32 *)((u8 *)MCI_PTR(0x58) + 0x18));
		}
	}
}

/* process class of current: 0 qos, 1 main (oom_score_adj == 0), 2 background */
static __always_inline int llm_proc_type(void)
{
	struct task_struct *leader;
	bool qos = is_qos_task(current);
	int t;

	leader = current->group_leader;
	if (!leader || !leader->signal)
		t = 2;
	else
		t = 1 + (leader->signal->oom_score_adj != 0);
	return qos ? 0 : t;
}

static __always_inline const char *llm_app_name(void)
{
	char *vend = current_write_memcg ? *(char **)((char *)current_write_memcg + 0x8f8) : NULL;

	return vend ? vend + 0x40 : "unknown";
}

#define LLM_SCENE_NAME(scene)	((char *)dfx.raw + 0x8c + (scene) * 20)
#define LLM_PROC_NAME(proc)	((char *)dfx.raw + 0xf0 + (proc) * 20)

/* per (scene, proc, bucket) counter + total / max latency of one handler type (0 cma, 1 dma, 2 order4, 3 order9) */
static __always_inline void llm_account(short scene, int proc, int type, u64 dt)
{
	atomic_inc((atomic_t *)&dfx.cnt[scene][proc][type][get_time_bucket(dt)]);
	spin_lock((spinlock_t *)(dfx.raw + 0x8b0));
	dfx.tot[scene][proc][type] += dt;
	if (dt > dfx.max[scene][proc][type])
		dfx.max[scene][proc][type] = dt;
	spin_unlock((spinlock_t *)(dfx.raw + 0x8b0));
}

/* claim the fault record and fill it; the MiSight callback sends it (not enabled here) */
static __always_inline void llm_report_mem_alloc(u64 dt, u32 type, u32 order, u32 len, int scene, int proc, const char *app,
						 const char *ver)
{
	if (check_report_limit())
		return;
	if (cmpxchg(&mem_fault_reporting, 0, 1))
		return;
	fill_mem_alloc_fault_data(&mem_fault_data, dt, type, order, len, scene, proc, app, ver);
	pr_warn_ratelimited("low_lat_mem: mem_fault_events is not enabled\n");
}

static void cma_alloc_end_handler(void *data, unsigned long long stime, unsigned long count)
{
	char ver[32] = "1.0.0";
	unsigned int s = scene_code - 101;
	short scene = s < 5 ? (int)s : -1;
	struct pool_type_ent *e;
	struct llm_snap sn;
	u32 dma9 = 0, dma4 = 0;
	const char *app;
	int proc;
	u64 dt;

	if (scene < 0)
		return;
	char buf[256] = {};
	proc = llm_proc_type();
	dt = sched_clock() - stime;
	llm_account(scene, proc, 0, dt);
	if (dt <= (u64)DFX_U32(0x980) * 1000000)
		return;
	app = llm_app_name();
	llm_snapshot(&sn, true, true);
	mi_smart_refill_update();
	list_for_each_entry(e, &mi_smart_refill_pools, list) {
		if (e->type == 1)
			dma4 = e->val;
		else if (e->type == 0)
			dma9 = e->val;
	}
	printk(KERN_INFO "low_lat_mem: mem_alloc_timeout type:cma count:%lu, time:%llu ms, scene:%s app:%s ver:%s comm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u (%d-dma)%d (4-dma)%d (cma-orig-cnt)%llu (cma-pool-cnt)%llu proc_type:%s\n",
	       count, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4, sn.p4, sn.c4,
	       9, sn.t9, sn.p9, sn.c9, 9, dma9, dma4, cma_alloc_from_orig_cnt, cma_alloc_from_pool_cnt, LLM_PROC_NAME(proc));
	snprintf(buf, 256,
		 "I|%d|mem_alloc_timeout type:cma count:%lu, time:%llu ms, scene:%s app:%s ver:%s comm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u (%d-dma)%d (4-dma)%d (cma-orig-cnt)%llu (cma-pool-cnt)%llu proc_type:%s\n",
		 current->tgid, count, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4,
		 sn.p4, sn.c4, 9, sn.t9, sn.p9, sn.c9, 9, dma9, dma4, cma_alloc_from_orig_cnt, cma_alloc_from_pool_cnt,
		 LLM_PROC_NAME(proc));
	tracing_mark_write(buf);
	llm_report_mem_alloc(dt, 0, 0, count, scene, proc, app, ver);
}

static void dma_heap_buffer_alloc_end_handler(void *data, unsigned long long stime, size_t len, struct dma_buf *dma_buf)
{
	char ver[32] = "1.0.0";
	unsigned int s = scene_code - 101;
	short scene = s < 5 ? (int)s : -1;
	struct pool_stat_ent *e;
	struct llm_snap sn;
	u32 dma9 = 0, dma4 = 0;
	u64 a9 = 0, b9 = 0, a4 = 0, b4 = 0;
	const char *app;
	int proc;
	u64 dt;

	if (scene < 0)
		return;
	char buf[256] = {};
	proc = llm_proc_type();
	dt = sched_clock() - stime;
	llm_account(scene, proc, 1, dt);
	if (dt <= (u64)DFX_U32(0x984) * 1000000)
		return;
	app = llm_app_name();
	llm_snapshot(&sn, true, true);
	mi_smart_refill_update();
	list_for_each_entry(e, &mi_smart_refill_pools, list) {
		if (e->type == 1) {
			dma4 = e->val;
			a4 = e->a;
			b4 = e->b;
		} else if (e->type == 0) {
			dma9 = e->val;
			a9 = e->a;
			b9 = e->b;
		}
	}
	printk(KERN_INFO "low_lat_mem: mem_alloc_timeout type:dma len:%lu time:%llu ms scene:%s app:%s ver:%s icomm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u (%d-dma)%d (4-dma)%d (%d-dma-cnt)%llu-%llu (4-dma-cnt)%llu-%llu proc_type:%s\n",
	       len, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4, sn.p4, sn.c4,
	       9, sn.t9, sn.p9, sn.c9, 9, dma9, dma4, 9, b9, a9, b4, a4, LLM_PROC_NAME(proc));
	snprintf(buf, 256,
		 "I|%d|mem_alloc_timeout type:dma len:%lu time:%llu ms scene:%s app:%s ver:%s comm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u (%d-dma)%d (4-dma)%d (%d-dma-cnt)%llu-%llu (4-dma-cnt)%llu-%llu proc_type:%s\n",
		 current->tgid, len, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4,
		 sn.p4, sn.c4, 9, sn.t9, sn.p9, sn.c9, 9, dma9, dma4, 9, b9, a9, b4, a4, LLM_PROC_NAME(proc));
	tracing_mark_write(buf);
	llm_report_mem_alloc(dt, 1, 0, len, scene, proc, app, ver);
}

static void mm_direct_reclaim_end_handler(void *data, unsigned int order, u64 stime)
{
	char ver[32] = "1.0.0";
	char buf[256] = {};
	unsigned int s = scene_code - 101;
	short scene = s < 5 ? (int)s : -1;
	struct llm_snap sn;
	const char *app;
	int proc;
	u64 dt;

	if (scene < 0)
		return;
	dt = sched_clock() - stime;
	if (dt <= (u64)DFX_U32(0x994) * 1000000)
		return;
	app = llm_app_name();
	proc = llm_proc_type();
	llm_snapshot(&sn, true, true);
	printk(KERN_INFO "low_lat_mem: mem_alloc_timeout type:direct_reclaim time:%llu ms order:%u scene:%s app:%s ver:%s comm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u proc_type:%s\n",
	       div_u64(dt, 1000000), order, LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4, sn.p4, sn.c4,
	       9, sn.t9, sn.p9, sn.c9, LLM_PROC_NAME(proc));
	snprintf(buf, 256,
		 "I|%d|mem_alloc_timeout type:direct_reclaim time:%llu ms order:%u scene:%s app:%s ver:%s comm=%s prio=%d (4)%u-%u-%u (%d)%u-%u-%u proc_type:%s\n",
		 current->tgid, div_u64(dt, 1000000), order, LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, sn.t4,
		 sn.p4, sn.c4, 9, sn.t9, sn.p9, sn.c9, LLM_PROC_NAME(proc));
	tracing_mark_write(buf);
}

/* order 0 / 4 / 9 slowpath allocations that may direct-reclaim: type index 6 / 2 / 3 of mem_type_str */
static void alloc_pages_slowpath_end_handler(void *data, gfp_t *gfp_mask, unsigned int order, unsigned long alloc_start,
					     u64 stime, unsigned long did_some_progress, unsigned long pages_reclaimed,
					     int retry_loop_count)
{
	char ver[32] = "1.0.0";
	unsigned int s = scene_code - 101;
	short scene = s < 5 ? (int)s : -1;
	struct llm_snap sn;
	const char *app;
	u32 tmo, t, p, c;
	int proc, tix, type;
	u64 dt;

	if (!(*gfp_mask & __GFP_DIRECT_RECLAIM))
		return;
	if (order != 9 && order != 4 && order != 0)
		return;
	if (scene < 0)
		return;
	char buf[256] = {};
	dt = sched_clock() - stime;
	proc = llm_proc_type();
	tix = order == 4 ? 2 : 3;
	type = order == 0 ? 6 : tix;
	tmo = DFX_U32(order == 0 ? 0x988 : order == 4 ? 0x98c : 0x990);
	if (order)
		llm_account(scene, proc, tix, dt);
	if (dt <= (u64)tmo * 1000000)
		return;
	app = llm_app_name();
	llm_snapshot(&sn, order == 4, order == 9);
	t = order == 4 ? sn.t4 : sn.t9;
	p = order == 4 ? sn.p4 : sn.p9;
	c = order == 4 ? sn.c4 : sn.c9;
	printk(KERN_INFO "low_lat_mem: mem_alloc_timeout type:order_%u time:%llu ms scene:%s app:%s ver:%s comm=%s prio=%d %u-%u-%u proc_type:%s\n",
	       order, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, t, p, c,
	       LLM_PROC_NAME(proc));
	snprintf(buf, 256, "I|%d|mem_alloc_timeout type:order_%u time:%llu ms scene:%s app:%s ver:%s comm=%s prio=%d %u-%u-%u proc_type:%s\n",
		 current->tgid, order, div_u64(dt, 1000000), LLM_SCENE_NAME(scene), app, ver, current->comm, current->prio, t, p, c,
		 LLM_PROC_NAME(proc));
	tracing_mark_write(buf);
	if (check_report_limit())
		return;
	if (cmpxchg(&mem_fault_reporting, 0, 1))
		return;
	fill_mem_alloc_fault_data(&mem_fault_data, dt, type, order, 0, scene, proc, app, ver);
	pr_warn_ratelimited("low_lat_mem: mem_fault_events is not enabled\n");
}
