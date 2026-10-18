/* SPDX-License-Identifier: GPL-2.0 */
/* mi_low_lat_mem: shared types / macros / extern declarations of the two translation units (G1ab: the dfx group is a second TU as in the stock) */
#ifndef _LLM_COMMON_H
#define _LLM_COMMON_H

// SPDX-License-Identifier: GPL-2.0
/*
 * mi_low_lat_mem: Xiaomi low latency memory module (staging), HyperOS 4.0.
 * Reconstructed from the OS4 stock disassembly (no public source). COMPILES-only / PARTIAL:
 * only the small functions (sysfs attribute handlers, tracepoint helpers, small utility
 * functions) are written; the large engines (pre_do_compact, cma_alloc_end_handler, the
 * misight event code, the init/exit functions ...) are NOT reconstructed yet (see STATUS.md).
 * NOT hardware-tested. DO NOT LOAD.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/ratelimit.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/memcontrol.h>
#include <linux/shrinker.h>
#include <linux/wait.h>
#include <linux/seq_file.h>
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#include <linux/kernfs.h>
#include <linux/kprobes.h>
#include <linux/cma.h>
#include <linux/kthread.h>
#include <linux/cgroup.h>
#include <linux/dma-buf.h>
#include <trace/hooks/mm.h>
#include <trace/hooks/dmabuf.h>
/* ---- global data (the stock keeps these as separate objects; offsets are those of the stock) ---- */
/*
 * The stock keeps the cross-TU state as separate GLOBAL objects (names/sizes below are those of
 * the stock symbol table, G1n): the module was built from several source files.
 */
/* 2464 bytes: names (20-byte slots), statistics, the 8 timeout thresholds (ms) at +0x980 .. +0x99c */
#define DFX_U32(off)	(*(u32 *)(dfx.raw + (off)))
/* 2288 bytes: pre-compact state (target counts +4/+8, pools +0x30/+0x58, retry_times +0x70) */
#define MCI_U32(off)	(*(u32 *)(mci.raw + (off)))
extern struct list_head mi_smart_refill_pools;
#define MCI_PTR(off)	(*(void **)(mci.raw + (off)))
struct cma;
struct mi_cma;
struct cma_ref {
	struct cma *cma;
	struct mi_cma *ent;
};
struct cma_pfn_to_align {
	unsigned long key;
	unsigned long val;
	int ref;
};
struct sys_mem_slot {
	u32 a[3], b[3];			/* +0 / +0xc: two triples of counters (sum / 3 = order4 / ext targets), ring of 3 samples */
	void **arr;			/* +0x18: dmabuf_pool_nums pointers to 12-byte records */
	struct list_head list;		/* +0x20 */
	int cnt;			/* +0x30: entries on list */
	u8 pad[4];
};
/* ---- callees that are not reconstructed yet: opaque placeholders (they must use their arguments) ---- */
/* forward: struct cma_tmp_ent lives further down */
struct cma_tmp_ent;
struct llm_vendor {
	u8 pad[0x20];
	void **arr;			/* +0x20: dmabuf_pool_nums pointers to 12-byte records */
	struct list_head list;		/* +0x28 */
	u8 pad2[0x90 - 0x38];
};
#define PARSE_KSTR(buf, val)	kstrtouint(buf, 10, val)
#define PARSE_SSCANF(buf, val)	(sscanf(buf, "%u", val) != 1 ? -EINVAL : 0)
struct pre_compact_pool_hdr {
	u8 pad[0x18];
	atomic_t collected;
};
#define START_HANDLER(name)								\
static void name(void *data, u64 *start)						\
{											\
	*start = sched_clock();								\
}
struct mi_smart_refill_pool_stat {
	int id;
	u8 pad[0x24];
	u64 from_pool;		/* +0x28 */
	u64 from_buddy;		/* +0x30 */
};
struct mi_memcg_data {
	u8 pad[0x20];
	void **arr;		/* +0x20: dmabuf_pool_nums pointers */
};
#define DFX_U64_AT(off)	(*(u64 *)(dfx.raw + (off)))
#define DFX_ATOMIC(off)		((atomic_t *)(dfx.raw + (off)))
#define SCENE_SHOW(name, idx)								\
static ssize_t name(struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
{ \
	return scene_alloc_info_show_common(buf, idx); \
}
extern void mi_smart_refill_update(void);
struct mi_smart_refill_pool_ent {
	struct list_head list;
	u8 pad[8];
	u64 hit;		/* +0x18 */
	u64 miss;		/* +0x20 */
};
#define DFX_U64(off)	(*(u64 *)(dfx.raw + (off)))
struct pre_compact_pool {
	spinlock_t lock;			/* +0 */
	int order;				/* +4 */
	struct list_head items;			/* +8 */
	atomic_t count;			/* +0x18 */
};
/* ---- smart refill (qcom_dma_heaps pools) ---- */
extern struct tracepoint __tracepoint_mi_smart_refill_pool_shrink;
extern struct tracepoint __tracepoint_mi_smart_refill_alloc;
extern struct tracepoint __tracepoint_mi_smart_refill_extend_alloc;
extern int mi_smart_refill_pools_init(struct list_head *pools, int *dmabuf_pool_nums);
struct mi_pool_cfg {
	int pad0;
	int order;		/* +4 */
	int pad8;
	int enabled;		/* +0xc */
};
/* ---- per-CMA-area bookkeeping ---- */
/* per-CMA-area pool of released allocations (BTF: struct mi_cma, 32 bytes) */
struct mi_cma {
	int idx;
	int count;
	spinlock_t lock;
	int pad;
	struct list_head head;
};
/* one parked allocation of a mi_cma area (kmalloc 40) */
struct cma_page_ent {
	unsigned int align;		/* +0 */
	unsigned long count;		/* +8 */
	unsigned long pfn;		/* +0x10 */
	struct list_head list;		/* +0x18 */
};
/* usage statistics per (area, size, align) kept on cma_tmp_list (kmalloc 48) */
struct cma_tmp_ent {
	int idx;			/* +0 */
	unsigned long size;		/* +8 */
	unsigned int align;		/* +0x10 */
	atomic_t cur;			/* +0x14 */
	int peak;			/* +0x18 */
	struct list_head list;		/* +0x20 */
};
struct sys_mem_info {		/* 13 u32, kB */
	u32 freeram, bufferram, cached, active_anon, inactive_anon, active_file, inactive_file, freeswap;
	u32 anon_mapped, file_mapped, free_cma, slab_reclaimable, slab_unreclaimable;
};
extern void si_swapinfo(struct sysinfo *);
#define KB(pages)	((u32)max_t(long, (long)(pages), 0) << (PAGE_SHIFT - 10))
/* si_meminfo/si_swapinfo + the node/zone counters the stock reads (all clamped at 0, in kB) */
struct mci_slot { u8 b[0x48]; };
/* G1t: record types with the names/fields of the stock BTF (the names enter the kCFI hash of the fill_* helpers) */
struct pre_compact_fault_data {
	u64 ts;
	u64 elapsed_ns;
	int type;			/* index into the mem_type_str table */
	u32 target_count_4, compact_count_4, target_count_ext, compact_count_ext;
	u32 frag_before, frag_after;
	u32 compact_times;
	struct sys_mem_info sys_info;	/* +0x30 */
};
_Static_assert(sizeof(struct pre_compact_fault_data) == 104, "pre_compact_fault_data");
/* pre-compact record of mci: 30 slots of 0x48 bytes at mci+0x78, counter at mci+0x8e8 (fields +0x90/+0x94/+0xa0/+0xa4 of the slot) */
struct mem_alloc_fault_data {
	u64 ts;
	u64 elapsed_ns;
	int type;
	u32 order;			/* +0x14 */
	u32 target_count_4, compact_count_4, curr_count_4;
	u32 target_count_ext, compact_count_ext, curr_count_ext;
	u32 dma_curr_count_4, dma_curr_count_ext;
	u32 len;			/* +0x38 */
	u32 scene;			/* +0x3c */
	int proc_type;			/* +0x40 */
	char app_name[64];		/* +0x44 */
	char app_ver[32];		/* +0x84 */
	u32 prio;			/* +0xa4 */
	struct sys_mem_info sys_info;	/* +0xa8 */
};
_Static_assert(sizeof(struct mem_alloc_fault_data) == 224, "mem_alloc_fault_data");
struct pool_type_ent {		/* node of the list walked in the stock: type at node-0x10, value at node-8 */
	int type;
	int pad;
	u32 val;
	u32 pad2;
	struct list_head list;
};
/* ---- MiSight (cdev_tevent, provider os4_miev) event of a fault record ---- */
struct cdev_tevent;
extern struct cdev_tevent *cdev_tevent_alloc(u32 id);
extern int cdev_tevent_add_int(struct cdev_tevent *ev, const char *key, s64 val);
extern int cdev_tevent_add_str(struct cdev_tevent *ev, const char *key, const char *val);
extern void cdev_tevent_write(struct cdev_tevent *ev);
extern void cdev_tevent_destroy(struct cdev_tevent *ev);
#define TEV_INT(ev, key, val)	(cdev_tevent_add_int(ev, key, val) == -1)
#define TEV_STR(ev, key, val)	(cdev_tevent_add_str(ev, key, val) == -1)
/* ======== G1t: pre_do_compact (from the stock disassembly; NOT byte-identical, NOT hardware-tested) ======== */
#include <linux/compaction.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#define DFX_ATOMIC(off)		((atomic_t *)(dfx.raw + (off)))
/* time bucket array of 6 counters: the stock has the array-bounds trap on the index */
#define DFX_ABKT(off, i)	((*(atomic_t (*)[6])(dfx.raw + (off)))[i])
#define MCI_POOL_COUNT(off)	smp_load_acquire((u32 *)((u8 *)MCI_PTR(off) + 0x18))
/* claim the fault record (cmpxchg on mem_fault_reporting), fill it; sending is done by the misight callback (not enabled here) */
/* open-coded for_each_zone() of the single node (next_zone() is not exported); MAX_NR_ZONES is 5 here */
#define llm_for_each_zone(zone)									for (zone = contig_page_data.node_zones; zone;						     zone = zone < zone->zone_pgdat->node_zones + MAX_NR_ZONES - 1 ? zone + 1 : NULL)
/* typed view of the per-(scene, process type, handler type) latency statistics (offsets of the stock; the stock indexes them with
   array-bounds traps): count[scene 5][proc 3][type 4 = cma/dma/order4/order9][bucket 5] at +0x12c, total ns at +0x5e0, max ns at +0x7c0 */
struct llm_dfx_t {
	union {
		u8 raw[2464];
		struct {
			u8 pad0[0x12c];
			u32 cnt[5][3][4][5];
			u8 pad1[4];
			u64 tot[5][3][4];
			u32 max[5][3][4];
		};
	};
};
/* one record of the pre-compact history ring (0x48 bytes, 30 slots at mci+0x78, counter at mci+0x8e8) */
struct pc_rec {
	u64 pages;			/* +0x00 free pages before the round */
	u32 f4, f9, f4a, f9a;		/* +0x08 fragmentation before / after (order 4, 9) */
	u32 t4, t9;			/* +0x18 targets */
	u32 c4, c9;			/* +0x20 pool counts before */
	u32 n4, n9;			/* +0x28 pool counts after */
	u8 success;			/* +0x30 */
	u8 pad[7];
	u64 time;			/* +0x38 ktime_get_real */
	u64 dur;			/* +0x40 compact time ns */
};
_Static_assert(sizeof(struct pc_rec) == 0x48, "pc_rec");
struct llm_mci_t {
	union {
		u8 raw[2288];
		struct {
			u8 pad0[0x78];
			struct pc_rec hist[30];
			u32 hist_cnt;
		};
	};
};
extern struct llm_dfx_t dfx;
extern struct llm_mci_t mci;
extern int llm_enable;
extern int calculate_start;
extern unsigned int *dmabuf_data_tmp;
extern int dmabuf_pool_nums;
extern atomic_t largemem_data_tmp[2] __asm__("largemem_data_tmp.llvm.5013616374876433728");
extern struct cma_ref cma_refs[17];
extern atomic_t cma_refs_count;
extern struct cma_pfn_to_align cma_pfn_to_align[100];
extern spinlock_t cma_pfn_to_align_lock;
extern u64 cma_alloc_from_pool_cnt;
extern u64 cma_alloc_from_orig_cnt;
extern int cma_releasing;
extern int cma_area_nums;
extern int shrinking;
extern int predo_pending;
extern int mem_fault_reporting;
extern int scene_code;
extern u8 sys_scene;
extern u8 sys_predo;
extern spinlock_t cma_lock;
extern struct mutex pending_lock;
extern struct mutex dmabuf_lock;
extern struct mutex largemem_lock;
extern struct mem_cgroup *current_write_memcg;
extern struct kprobe llm_kallsym_kp;
extern unsigned long (*llm_ksym_lookup_name)(const char *name);
extern unsigned long try_to_free_pages_symbol;
extern struct sys_mem_slot sys_mem_data[4];
extern struct list_head cma_tmp_list;
extern spinlock_t cma_tmp_lock;
/* functions of the dfx TU that the main TU calls (GLOBAL in the stock) */
extern int tracing_mark_write(const char *buf);
extern int get_scene(int code);
extern bool check_report_limit(void);
extern int get_time_bucket(u64 ns);
extern void dfx_enable_control(int enable);
extern int mi_dfx_init(void);
extern int mi_dfx_exit(void);
extern void fill_sys_mem_info(struct sys_mem_info *out);
extern void fill_pre_compact_fault_data(struct pre_compact_fault_data *f, u64 elapsed_ns, u32 type, u32 frag_before, 						 u32 frag_after, u32 compact_times);
extern void fill_mem_alloc_fault_data(struct mem_alloc_fault_data *f, u64 elapsed_ns, u32 type, u32 order, u32 len, 					      u32 scene, u32 proc_type, const char *app_name, const char *app_ver);
extern void pre_compact_misight_event_cb(void *data, int error_code);
extern void mem_alloc_misight_event_cb(void *data, int error_code);
struct sys_mem_info;
struct pre_compact_fault_data;
struct mem_alloc_fault_data;
#ifndef __GENKSYMS__
extern struct kobject *low_lat_mem_kobject;
#endif
/* functions of the main TU that the dfx TU calls */
extern void predo_trigger(int val);
#endif
