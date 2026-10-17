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
struct { u8 raw[2464]; } dfx __aligned(8);
#define DFX_U32(off)	(*(u32 *)(dfx.raw + (off)))
/* 2288 bytes: pre-compact state (target counts +4/+8, pools +0x30/+0x58, retry_times +0x70) */
struct { u8 raw[2288]; } mci __aligned(8);
#define MCI_U32(off)	(*(u32 *)(mci.raw + (off)))
#define MCI_PTR(off)	(*(void **)(mci.raw + (off)))

int llm_enable;
int calculate_start;
static u8 dfx_enable;
static int predo_val;
static wait_queue_head_t predo_wq = __WAIT_QUEUE_HEAD_INITIALIZER(predo_wq);
unsigned int *dmabuf_data_tmp;
int dmabuf_pool_nums;
/* ThinLTO promoted this static (the stock symbol carries the .llvm.<hash> suffix) */
atomic_t largemem_data_tmp[2] __asm__("largemem_data_tmp.llvm.5013616374876433728");
struct cma;
struct mi_cma;
struct cma_ref {
	struct cma *cma;
	struct mi_cma *ent;
};
struct cma_ref cma_refs[17];
atomic_t cma_refs_count;
struct cma_align {
	unsigned long key;
	unsigned long val;
	int ref;
};
struct cma_align cma_pfn_to_align[100];
spinlock_t cma_pfn_to_align_lock;
u64 cma_alloc_from_pool_cnt;
u64 cma_alloc_from_orig_cnt;
int cma_releasing;
int cma_area_nums;
int shrinking;
int predo_pending;
int mem_fault_reporting;
int scene_code;
u8 sys_scene;
u8 sys_predo;
spinlock_t cma_lock;
struct mutex pending_lock;
struct mutex dmabuf_lock;
struct mutex largemem_lock;
struct mem_cgroup *current_write_memcg;
struct kprobe llm_kallsym_kp;
unsigned long (*llm_ksym_lookup_name)(const char *name);
unsigned long try_to_free_pages_symbol;

/* sys_mem_data: 4 slots of 56 bytes (stock g[0x18], 0x50, 0x88, 0xc0 = the pointer arrays) */
struct sys_mem_slot {
	u8 hdr[0x18];
	void **arr;			/* +0x18: dmabuf_pool_nums pointers to 12-byte records */
	struct list_head list;		/* +0x20 */
	u8 pad[8];
};
struct sys_mem_slot sys_mem_data[4];

/* the intervals come from the stock .data (hour: 3600 s, 5 per interval; day: 86400 s, 25) */
static struct ratelimit_state hour_limit = RATELIMIT_STATE_INIT("hour_limit", 3600 * HZ, 5);
static struct ratelimit_state day_limit = RATELIMIT_STATE_INIT("day_limit", 86400 * HZ, 25);

/* ---- callees that are not reconstructed yet: opaque placeholders (they must use their arguments) ---- */

static noinline __used void dfx_enable_change(int enable)
{
	asm volatile("" : : "r"(enable) : "memory");
}
static noinline __used void dfx_info_reset(void)
{
	/* placeholder: the statics below are written by code that is not reconstructed yet */
	asm volatile("" : : "r"(&dfx), "r"(&mci), "r"(&dmabuf_pool_nums), "r"(&dmabuf_data_tmp), "r"(&predo_val), "r"(cma_pfn_to_align), "r"(cma_refs), "r"(&cma_refs_count), "r"(&cma_alloc_from_pool_cnt), "r"(&cma_alloc_from_orig_cnt) : "memory");
}

static noinline __used void start_store_call(int v)
{
	asm volatile("" : : "r"(v) : "memory");
}

static noinline __used int mi_memcg_vendor_alloc(struct mem_cgroup *memcg, int flag)
{
	int r;

	asm volatile("" : "=r"(r) : "r"(memcg), "r"(flag) : "memory");
	return r;
}

/* ---- tracing helper ---- */

static noinline __used int tracing_mark_write(const char *buf)
{
	trace_puts(buf);
	return 0;
}

/* ---- small helpers ---- */

static noinline __used int get_scene(int code)
{
	unsigned int i = code - 101;

	return i < 5 ? i : -1;
}

static noinline __used bool check_report_limit(void)
{
	if (___ratelimit(&hour_limit, "check_report_limit"))
		return !___ratelimit(&day_limit, "check_report_limit");
	return true;
}

static noinline __used int get_time_bucket(u64 ns)
{
	int bucket = 0;

	if (ns > 8000000ULL) {
		if (ns <= 64000000ULL)
			bucket = 1;
		else if (ns <= 256000000ULL)
			bucket = 2;
		else if (ns > 1024000000ULL)
			bucket = 4;
		else
			bucket = 3;
	}
	return bucket;
}

/* ---- dfx sysfs: timeouts (ms, 1..10000) ---- */

#define PARSE_KSTR(buf, val)	kstrtouint(buf, 10, val)
#define PARSE_SSCANF(buf, val)	(sscanf(buf, "%u", val) != 1 ? -EINVAL : 0)
#define TIMEOUT_ATTR(name, off, parse)								\
static ssize_t name##_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)	\
{											\
	return sysfs_emit(buf, "%u\n", DFX_U32(off));					\
}											\
static ssize_t name##_store(struct kobject *kobj, struct kobj_attribute *attr,		\
			    const char *buf, size_t count)				\
{											\
	unsigned int val = 0;								\
	int ret = parse(buf, &val);							\
											\
	if (ret)									\
		return ret;								\
	if (val < 1 || val > 10000)							\
		return -EINVAL;								\
	DFX_U32(off) = val;								\
	return count;									\
}											\
static struct kobj_attribute __used dfx_##name##_attr = __ATTR(name, 0644, name##_show, name##_store)

TIMEOUT_ATTR(timeout_cma, 0x980, PARSE_KSTR);
TIMEOUT_ATTR(timeout_dma, 0x984, PARSE_KSTR);
TIMEOUT_ATTR(timeout_order0, 0x988, PARSE_KSTR);
TIMEOUT_ATTR(timeout_order4, 0x98c, PARSE_KSTR);
TIMEOUT_ATTR(timeout_order9, 0x990, PARSE_KSTR);
TIMEOUT_ATTR(timeout_direct_reclaim, 0x994, PARSE_KSTR);
TIMEOUT_ATTR(timeout_free, 0x998, PARSE_SSCANF);
TIMEOUT_ATTR(timeout_async_compact, 0x99c, PARSE_SSCANF);

static ssize_t enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", ~dfx_enable & 1);
}

static ssize_t reset_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int val = 0;
	int ret = kstrtoint(buf, 10, &val);

	if (ret)
		return ret;
	if (val < 0)
		return -EINVAL;
	if (val)
		dfx_info_reset();
	return count;
}

static void dfx_enable_control(int enable);

/* ---- top level sysfs ---- */

static ssize_t start_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int val = 0;
	int ret = kstrtoint(buf, 10, &val);

	if (ret)
		return ret;
	if (val < 0)
		return -EINVAL;
	start_store_call(val);
	return count;
}

static ssize_t shrink_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	if (READ_ONCE(llm_enable))
		pr_info("low_lat_mem: low_lat_mem shrink.\n");
	return count;
}

static void predo_trigger(int val);

static ssize_t predo_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int val = 0;
	int ret = kstrtoint(buf, 10, &val);

	if (ret)
		return ret;
	if (val < 0)
		return -EINVAL;
	if (READ_ONCE(llm_enable)) {
		predo_trigger(val);
		pr_info("low_lat_mem: low_lat_mem prdo: %d.\n", val);
	}
	return count;
}

static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
			    const char *buf, size_t count)
{
	int val = 0;
	int ret = kstrtoint(buf, 10, &val);

	if (ret)
		return ret;
	if (val < 0)
		return -EINVAL;
	dfx_enable_control(val);
	return count;
}

/* ---- pre-compact sysfs ---- */

static ssize_t retry_times_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", MCI_U32(0x70));
}

static ssize_t retry_times_store(struct kobject *kobj, struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int val = 0;

	if (sscanf(buf, "%u", &val) != 1 || val > 100)
		count = -EINVAL;
	else if (val)
		MCI_U32(0x70) = val;
	return count;
}

static ssize_t target_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "order4:%u\norder%d:%u\n", MCI_U32(4), 9, MCI_U32(8));
}

struct pre_compact_pool_hdr {
	u8 pad[0x18];
	atomic_t collected;
};

static ssize_t curr_collected_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "order4:%u\norder%d:%u\n",
			  atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x30))->collected), 9,
			  atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x58))->collected));
}

/* ---- allocation latency start handlers (tracepoint probes, all identical) ---- */

#define START_HANDLER(name)								\
static void name(void *data, u64 *start)						\
{											\
	*start = sched_clock();								\
}

START_HANDLER(cma_alloc_start_handler)
START_HANDLER(dma_heap_buffer_alloc_start_handler)
START_HANDLER(mm_direct_reclaim_start_handler)
START_HANDLER(alloc_pages_slowpath_start_handler)

/* ---- end handlers (not reconstructed yet: opaque placeholders with the hook prototypes) ---- */

static void cma_alloc_end_handler(void *data, unsigned long long stime, unsigned long count)
{
	asm volatile("" : : "r"(stime), "r"(count) : "memory");
}

static void dma_heap_buffer_alloc_end_handler(void *data, unsigned long long stime, size_t len,
					      struct dma_buf *dma_buf)
{
	asm volatile("" : : "r"(stime), "r"(len), "r"(dma_buf) : "memory");
}

static void mm_direct_reclaim_end_handler(void *data, unsigned int order, u64 stime)
{
	asm volatile("" : : "r"(order), "r"(stime) : "memory");
}

static void alloc_pages_slowpath_end_handler(void *data, gfp_t *gfp_mask, unsigned int order,
					     unsigned long alloc_start, u64 stime,
					     unsigned long did_some_progress,
					     unsigned long pages_reclaimed, int retry_loop_count)
{
	asm volatile("" : : "r"(gfp_mask), "r"(order), "r"(alloc_start), "r"(stime),
		     "r"(did_some_progress), "r"(pages_reclaimed), "r"(retry_loop_count) : "memory");
}

/* dfx_enable holds the "disabled" state: enable_show prints its inverse */
static noinline __used void dfx_enable_control(int enable)
{
	u8 disabled = dfx_enable;

	if (enable) {
		if (disabled)
			dfx_enable_change(1);
	} else if (!(disabled & 1)) {
		unregister_trace_android_vh_cma_alloc_lat_start(cma_alloc_start_handler, NULL);
		unregister_trace_android_vh_cma_alloc_lat_end(cma_alloc_end_handler, NULL);
		unregister_trace_android_vh_dma_heap_buffer_alloc_lat_start(dma_heap_buffer_alloc_start_handler, NULL);
		unregister_trace_android_vh_dma_heap_buffer_alloc_lat_end(dma_heap_buffer_alloc_end_handler, NULL);
		unregister_trace_android_vh_mm_direct_reclaim_start(mm_direct_reclaim_start_handler, NULL);
		unregister_trace_android_vh_mm_direct_reclaim_end(mm_direct_reclaim_end_handler, NULL);
		unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_pages_slowpath_start_handler, NULL);
		unregister_trace_android_vh_alloc_pages_slowpath_end(alloc_pages_slowpath_end_handler, NULL);
	}
	dfx_enable = !enable;
}

/* ---- predo / scratch data ---- */

static noinline __used void predo_trigger(int val)
{
	predo_val = val;
	if (READ_ONCE(llm_enable)) {
		if (READ_ONCE(calculate_start))
			wake_up_interruptible(&predo_wq);
	}
}

static noinline __used void largemem_data_tmp_init(void)
{
	atomic_set(&largemem_data_tmp[0], 0);
	atomic_set(&largemem_data_tmp[1], 0);
}

static noinline __used int dmabuf_data_tmp_init(void)
{
	int i;

	dmabuf_data_tmp = kmalloc(dmabuf_pool_nums * sizeof(*dmabuf_data_tmp), GFP_KERNEL | __GFP_ZERO);
	if (!dmabuf_data_tmp) {
		pr_info("low_lat_mem: low_lat_mem dmabuf_data_tmp alloc failed.\n");
		return -ENOMEM;
	}
	for (i = 0; i < dmabuf_pool_nums; i++)
		dmabuf_data_tmp[i] = 0;
	return 0;
}

static noinline __used void dmabuf_data_tmp_free(void)
{
	kfree(dmabuf_data_tmp);
}

static void mi_try_alloc_pages_gfp(void *data, struct page **page, unsigned int order,
				   gfp_t gfp, enum zone_type type)
{
	int idx;

	if (!READ_ONCE(llm_enable) || !READ_ONCE(calculate_start))
		return;
	if (order == 4 && (gfp & __GFP_DIRECT_RECLAIM))
		idx = 0;
	else if (order == 9 && (gfp & __GFP_DIRECT_RECLAIM))
		idx = 1;
	else
		return;
	atomic_inc(&largemem_data_tmp[idx]);
}

struct mi_smart_refill_pool_stat {
	int id;
	u8 pad[0x24];
	u64 from_pool;		/* +0x28 */
	u64 from_buddy;		/* +0x30 */
};

#define SMART_REFILL_HANDLER(name)							\
static void name(void *data, struct mi_smart_refill_pool_stat *pool, unsigned long order,	\
		 int from_buddy)							\
{											\
	if (!pool)									\
		return;									\
	if (from_buddy)									\
		pool->from_buddy++;							\
	else										\
		pool->from_pool++;							\
	if (!READ_ONCE(llm_enable) || !READ_ONCE(calculate_start))			\
		return;									\
	if (pool->id < 0 || !dmabuf_data_tmp)						\
		return;									\
	atomic_inc((atomic_t *)&dmabuf_data_tmp[pool->id]);				\
}

SMART_REFILL_HANDLER(mi_smart_refill_alloc)
SMART_REFILL_HANDLER(mi_smart_refill_extend_alloc)

/* ---- memcg vendor data ---- */

static void mi_memcg_alloc(void *data, struct mem_cgroup *memcg)
{
	if (mi_memcg_vendor_alloc(memcg, 1))
		pr_info("low_lat_mem: low_lat_mem mi_memcg alloc failed.\n");
}

struct mi_memcg_data {
	u8 pad[0x20];
	void **arr;		/* +0x20: dmabuf_pool_nums pointers */
};

static void mi_memcg_free(void *data, struct mem_cgroup *memcg)
{
	struct mi_memcg_data **slot = (struct mi_memcg_data **)((char *)memcg + 0x8f8);
	struct mi_memcg_data *d = *slot;
	int i;

	if (!d)
		return;
	for (i = 0; i < dmabuf_pool_nums; i++)
		kfree(d->arr[i]);
	if (d->arr)
		kfree(d->arr);
	*slot = NULL;
	kfree(d);
	pr_info("low_lat_mem: low_lat_mem mi_memcg free.\n");
}


/* ---- scene_* show (the common printer is not reconstructed yet) ---- */

static noinline __used ssize_t scene_alloc_info_show_common(char *buf, int scene)
{
	ssize_t r;

	asm volatile("" : "=r"(r) : "r"(buf), "r"(scene) : "memory");
	return r;
}

#define SCENE_SHOW(name, idx)								\
static ssize_t name(struct kobject *kobj, struct kobj_attribute *attr, char *buf) \
{ \
	return scene_alloc_info_show_common(buf, idx); \
}

SCENE_SHOW(scene_fling_show, 0)
SCENE_SHOW(scene_unlock_show, 1)
SCENE_SHOW(scene_cold_start_show, 2)
SCENE_SHOW(scene_camera_show, 3)
SCENE_SHOW(scene_game_show, 4)

/* ---- pool hit rate ---- */

LIST_HEAD(mi_smart_refill_pools);
EXPORT_SYMBOL(mi_smart_refill_pools);
extern void mi_smart_refill_update(void);

struct mi_smart_refill_pool_ent {
	struct list_head list;
	u8 pad[8];
	u64 hit;		/* +0x18 */
	u64 miss;		/* +0x20 */
};
#define DFX_U64(off)	(*(u64 *)(dfx.raw + (off)))

static ssize_t pool_hit_rate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct mi_smart_refill_pool_ent *e;
	u64 hit = 0, total = 0, pool, orig;
	ssize_t len;

	mi_smart_refill_update();
	list_for_each_entry(e, &mi_smart_refill_pools, list) {
		hit += e->hit;
		total += e->hit + e->miss;
	}
	DFX_U64(0x970) = hit;
	DFX_U64(0x978) = total;
	len = sysfs_emit_at(buf, 0, "===big_order_pool===\n");
	len += sysfs_emit_at(buf, len, "order4  hit: %llu total: %llu\n", DFX_U64(0x940), DFX_U64(0x948));
	len += sysfs_emit_at(buf, len, "order%d hit: %llu total: %llu\n", 9, DFX_U64(0x950), DFX_U64(0x958));
	len += sysfs_emit_at(buf, len, "\n===cma_pool===\n");
	pool = cma_alloc_from_pool_cnt;
	orig = cma_alloc_from_orig_cnt;
	DFX_U64(0x960) = pool;
	DFX_U64(0x968) = pool + orig;
	len += sysfs_emit_at(buf, len, "hit: %llu total: %llu\n", pool, pool + orig);
	len += sysfs_emit_at(buf, len, "\n===dma_pool===\n");
	len += sysfs_emit_at(buf, len, "hit: %llu total: %llu\n", DFX_U64(0x970), DFX_U64(0x978));
	return len;
}

/* ---- memcg control files (cgroup v1/v2 cftypes) ---- */

static int memcg_read_appname(struct seq_file *sf, void *v)
{
	struct cgroup_subsys_state *css = of_css(sf->private);
	char *mc;

	if (!css || !*(char **)((char *)css + 0x8f8))
		return -1;
	mc = *(char **)((char *)css + 0x8f8);
	seq_printf(sf, "%s\n", mc + 0x40);
	return 0;
}

static int memcg_read_version(struct seq_file *sf, void *v)
{
	struct cgroup_subsys_state *css = of_css(sf->private);
	char *mc;

	if (!css || !*(char **)((char *)css + 0x8f8))
		return -1;
	mc = *(char **)((char *)css + 0x8f8);
	seq_printf(sf, "%s\n", mc + 0x72);
	return 0;
}

static ssize_t memcg_write_appname(struct kernfs_open_file *of, char *buf, size_t nbytes, loff_t off)
{
	char *s = strim(buf);
	int n = min_t(int, strlen(s) + 1, 50);
	struct cgroup_subsys_state *css = of_css(of);
	char *mc;

	if (!css || !*(char **)((char *)css + 0x8f8))
		return -1;
	mc = *(char **)((char *)css + 0x8f8) + 0x40;
	snprintf(mc, n, "%s", s);
	pr_info("low_lat_mem: low_lat_mem appname: %s", mc);
	return nbytes;
}

static ssize_t memcg_write_version(struct kernfs_open_file *of, char *buf, size_t nbytes, loff_t off)
{
	char *s = strim(buf);
	int n = min_t(int, strlen(s) + 1, 30);
	struct cgroup_subsys_state *css = of_css(of);
	char *mc;

	if (!css || !*(char **)((char *)css + 0x8f8))
		return -1;
	mc = *(char **)((char *)css + 0x8f8) + 0x72;
	snprintf(mc, n, "%s", s);
	pr_info("low_lat_mem: low_lat_mem version: %s", mc);
	return nbytes;
}

static ssize_t memcg_write_start(struct kernfs_open_file *of, char *buf, size_t nbytes, loff_t off)
{
	struct mem_cgroup *memcg;
	int val = 0, ret, id;

	ret = kstrtoint(strim(buf), 16, &val);
	if (ret)
		return ret;
	memcg = mem_cgroup_from_css(of_css(of));
	if (!val)
		current_write_memcg = NULL;
	else if (val == 1)
		current_write_memcg = memcg;
	id = current_write_memcg ? *(int *)((char *)memcg + 0xe8) : 0;
	pr_info("low_lat_mem: low_lat_mem start: %d, current_write_memcg: %d. \n", val, id);
	return nbytes;
}

/* ---- pre-compact pools (order 4 / order 9 pages kept ready for allocation) ---- */

struct pre_compact_pool {
	spinlock_t lock;			/* +0 */
	int order;				/* +4 */
	struct list_head items;			/* +8 */
	atomic_t count;			/* +0x18 */
};

/* private copy of mm/internal.h prep_compound_page() (not exported) */
static void mi_prep_compound_tail(struct page *head, int tail_idx)
{
	struct page *p = head + tail_idx;

	p->mapping = TAIL_MAPPING;
	set_compound_head(p, head);
	p->private = 0;
}

static void mi_folio_set_order(struct folio *folio, unsigned int order)
{
	if (WARN_ON_ONCE(!order || !folio_test_large(folio)))
		return;
	folio->_flags_1 = (folio->_flags_1 & ~0xffUL) | order;
	folio->_folio_nr_pages = 1U << order;
}

/* the symbol keeps the stock name; mm.h already declares a global prep_compound_page() */
static __used void llm_prep_compound_page(struct page *page, unsigned int order) __asm__("prep_compound_page");
static __used void llm_prep_compound_page(struct page *page, unsigned int order)
{
	int i;
	int nr_pages = 1 << order;

	__SetPageHead(page);
	for (i = 1; i < nr_pages; i++)
		mi_prep_compound_tail(page, i);
	mi_folio_set_order((struct folio *)page, order);
	atomic_set(&((struct folio *)page)->_entire_mapcount, -1);
	atomic_set(&((struct folio *)page)->_nr_pages_mapped, 0);
	atomic_set(&((struct folio *)page)->_pincount, 0);
}

static noinline __used struct page *pre_compact_pool_remove(struct pre_compact_pool *pool)
{
	struct page *page;
	unsigned long flags;

	spin_lock_irqsave(&pool->lock, flags);
	page = list_first_entry_or_null(&pool->items, struct page, lru);
	if (page) {
		list_del(&page->lru);
		atomic_dec(&pool->count);
		mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, -(1L << pool->order));
	}
	spin_unlock_irqrestore(&pool->lock, flags);
	return page;
}

static noinline __used struct page *alloc_pre_compact_page(gfp_t gfp, int order)
{
	struct page *page;

	if (order != 9 && order != 4)
		return NULL;
	page = pre_compact_pool_remove(((struct pre_compact_pool **)(mci.raw + 0x10))[order]);
	if (page && (gfp & __GFP_COMP))
		llm_prep_compound_page(page, order);
	return page;
}

static noinline __used void pre_compact_pools_exit(void)
{
	struct pre_compact_pool *pool;
	struct page *page;

	pool = MCI_PTR(0x30);
	if (pool) {
		while ((page = pre_compact_pool_remove(pool)))
			__free_pages(page, pool->order);
		kfree(pool);
		MCI_PTR(0x30) = NULL;
	}
	pool = MCI_PTR(0x58);
	if (pool) {
		while ((page = pre_compact_pool_remove(pool)))
			__free_pages(page, pool->order);
		kfree(pool);
		MCI_PTR(0x58) = NULL;
	}
}

static void do_compact_mem_collect(void *data, struct compact_control *cc, bool *bypass)
{
	asm volatile("" : : "r"(cc), "r"(bypass) : "memory");
}

static void do_alloc_pages_big_order(void *data, gfp_t gfp_mask, int order, int alloc_flags,
				     int migratetype, struct page **page)
{
	asm volatile("" : : "r"(gfp_mask), "r"(order), "r"(alloc_flags), "r"(migratetype), "r"(page) : "memory");
}

static ssize_t stats_history_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t r;

	asm volatile("" : "=r"(r) : "r"(buf) : "memory");
	return r;
}

static struct kobj_attribute __used pre_compact_retry_times_attr = __ATTR(retry_times, 0644, retry_times_show, retry_times_store);
static struct kobj_attribute __used pre_compact_curr_collected_count_attr = __ATTR_RO(curr_collected_count);
static struct kobj_attribute __used pre_compact_target_count_attr = __ATTR_RO(target_count);
static struct kobj_attribute __used pre_compact_stats_history_attr = __ATTR_RO(stats_history);
static struct attribute *pre_compact_attrs[] = {
	&pre_compact_retry_times_attr.attr,
	&pre_compact_curr_collected_count_attr.attr,
	&pre_compact_target_count_attr.attr,
	&pre_compact_stats_history_attr.attr,
	NULL,
};
static const struct attribute_group pre_compact_attr_group = {
	.name = "pre_compact",
	.attrs = pre_compact_attrs,
};
struct kobject *low_lat_mem_kobject;
EXPORT_SYMBOL(low_lat_mem_kobject);

static noinline __used void mi_pre_compact_exit(void)
{
	unregister_trace_android_vh_compact_bypass(do_compact_mem_collect, NULL);
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(do_alloc_pages_big_order, NULL);
	sysfs_remove_group(low_lat_mem_kobject, &pre_compact_attr_group);
	pre_compact_pools_exit();
	pr_info("low_lat_mem: mi_pre_compact_exit\n");
}

/* ---- smart refill (qcom_dma_heaps pools) ---- */

extern struct tracepoint __tracepoint_mi_smart_refill_pool_shrink;
extern struct tracepoint __tracepoint_mi_smart_refill_alloc;
extern struct tracepoint __tracepoint_mi_smart_refill_extend_alloc;
extern int mi_smart_refill_pools_init(struct list_head *pools, int dmabuf_pool_nums);

static void mi_smart_refill_pool_shrink(void *data, void *pool, void *a, int b, unsigned long c)
{
	asm volatile("" : : "r"(pool), "r"(a), "r"(b), "r"(c) : "memory");
}

int mi_smart_refill_init(int dmabuf_pool_nums)
{
	int ret;

	mi_smart_refill_pools_init(&mi_smart_refill_pools, dmabuf_pool_nums);
	ret = tracepoint_probe_register(&__tracepoint_mi_smart_refill_pool_shrink,
					mi_smart_refill_pool_shrink, NULL);
	if (ret) {
		pr_err("register mi_smart_refill_pool_shrink failed! ret=%d\n", ret);
		return ret;
	}
	ret = tracepoint_probe_register(&__tracepoint_mi_smart_refill_alloc, mi_smart_refill_alloc, NULL);
	if (ret) {
		pr_err("register mi_smart_refill_alloc failed! ret=%d\n", ret);
		goto unreg_shrink;
	}
	ret = tracepoint_probe_register(&__tracepoint_mi_smart_refill_extend_alloc,
					mi_smart_refill_extend_alloc, NULL);
	if (ret) {
		pr_err("register mi_smart_refill_extend_alloc failed! ret=%d\n", ret);
		tracepoint_probe_unregister(&__tracepoint_mi_smart_refill_alloc, mi_smart_refill_alloc, NULL);
unreg_shrink:
		tracepoint_probe_unregister(&__tracepoint_mi_smart_refill_pool_shrink,
					    mi_smart_refill_pool_shrink, NULL);
	}
	return ret;
}
EXPORT_SYMBOL(mi_smart_refill_init);

/* ---- cma pool shrinker ---- */

static struct shrinker pool_shrinker;

static unsigned long cma_pool_shrink_count(struct shrinker *shrinker, struct shrink_control *sc);
static unsigned long cma_pool_shrink_scan(struct shrinker *shrinker, struct shrink_control *sc);

int cma_pool_init_shrinker(void)
{
	static bool registered;

	if (!registered) {
		pool_shrinker.count_objects = cma_pool_shrink_count;
		pool_shrinker.scan_objects = cma_pool_shrink_scan;
		pool_shrinker.seeks = DEFAULT_SEEKS;
		register_shrinker(&pool_shrinker, "low_lat_mem");
		registered = true;
	}
	return 0;
}
EXPORT_SYMBOL(cma_pool_init_shrinker);

/* ---- cma pfn -> alignment table (100 entries, 24 bytes each) ---- */


static noinline __used int get_cma_align(unsigned long key, struct cma_align *out)
{
	int i, ret = -ENOENT;

	spin_lock(&cma_pfn_to_align_lock);
	for (i = 0; i < 100; i++) {
		if (cma_pfn_to_align[i].key == key) {
			*out = cma_pfn_to_align[i];
			ret = 0;
			break;
		}
	}
	spin_unlock(&cma_pfn_to_align_lock);
	return ret;
}

static noinline __used void put_cma_align(unsigned long key, unsigned long val, int ref)
{
	int i;

	spin_lock(&cma_pfn_to_align_lock);
	for (i = 0; i < 100; i++) {
		if (!cma_pfn_to_align[i].key) {
			cma_pfn_to_align[i].key = key;
			cma_pfn_to_align[i].val = val;
			cma_pfn_to_align[i].ref = ref;
			break;
		}
	}
	spin_unlock(&cma_pfn_to_align_lock);
}

static noinline __used void clear_cma_align(unsigned long key)
{
	int i;

	spin_lock(&cma_pfn_to_align_lock);
	for (i = 0; i < 100; i++) {
		if (cma_pfn_to_align[i].key == key) {
			memset(&cma_pfn_to_align[i], 0, sizeof(cma_pfn_to_align[i]));
			break;
		}
	}
	spin_unlock(&cma_pfn_to_align_lock);
}

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
struct list_head cma_tmp_list;
spinlock_t cma_tmp_lock;

static noinline __used int cma_area_init(struct cma *cma, void *data)
{
	unsigned int *count = data;
	struct mi_cma *e;
	unsigned int idx;

	if (!count || *count > 16)
		return -EINVAL;
	idx = (*count)++;
	e = kmalloc(sizeof(*e), GFP_KERNEL | __GFP_ZERO);
	if (!e)
		return -ENOMEM;
	cma_refs[idx].cma = cma;
	cma_refs[idx].ent = e;
	atomic_inc(&cma_refs_count);
	e->idx = idx;
	e->count = 0;
	spin_lock_init(&e->lock);
	INIT_LIST_HEAD(&e->head);
	return 0;
}

static noinline __used void init_mem_data_tmp(void)
{
	struct cma_tmp_ent *e;
	int i;

	atomic_set(&largemem_data_tmp[0], 0);
	atomic_set(&largemem_data_tmp[1], 0);
	if (dmabuf_data_tmp) {
		for (i = 0; i < dmabuf_pool_nums; i++)
			dmabuf_data_tmp[i] = 0;
	}
	spin_lock(&cma_tmp_lock);
	list_for_each_entry(e, &cma_tmp_list, list) {
		WRITE_ONCE(e->cur.counter, 0);
		WRITE_ONCE(e->peak, 0);
	}
	spin_unlock(&cma_tmp_lock);
}

/* ======== G1n: CMA parking pool (vendor hooks cma_alloc_bypass / cma_alloc_end / cma_release_bypass) ======== */

/* the stock keeps this one out of line (symbol list_add_tail: the kernel inline with the head folded in; the name
 * cannot be reused here because it would merge with the header inline) */
static noinline __used void llm_list_add_tail(struct list_head *entry)
{
	list_add_tail(entry, &cma_tmp_list);
}

static noinline __used struct mi_cma *find_mi_cma(struct cma *cma)
{
	int i;

	if (!cma)
		return NULL;
#pragma clang loop unroll(full)
	for (i = 0; i < 17; i++)
		if (cma_refs[i].cma == cma)
			return cma_refs[i].ent;
	return NULL;
}

static noinline __used int put_page_to_mi_cma(struct cma *cma, unsigned long pfn,
					      unsigned long count, unsigned int align)
{
	struct mi_cma *mi = find_mi_cma(cma);
	struct cma_page_ent *ent;

	/* every path returns the printk() result (the stock leaves w0 untouched) */
	if (!mi)
		return pr_info("low_lat_mem: low_lat_mem, put cma is NULL \n");
	ent = kzalloc(sizeof(*ent), GFP_ATOMIC);
	if (!ent)
		return pr_err("low_lat_mem: kzalloc failed.\n");
	ent->align = align;
	ent->count = count;
	ent->pfn = pfn;
	spin_lock(&mi->lock);
	list_add(&ent->list, &mi->head);
	mi->count++;
	spin_unlock(&mi->lock);
	return pr_info("low_lat_mem: low_lat_mem, put cma pfn:%lu, count:%lu, total:%d \n", pfn, count, mi->count);
}

static noinline __used struct page *get_page_from_mi_cma(struct cma *cma, unsigned long count,
							  unsigned int align)
{
	struct mi_cma *mi = find_mi_cma(cma);
	struct cma_page_ent *ent, *n;
	struct page *page = NULL;

	if (!mi)
		return NULL;
	spin_lock(&mi->lock);
	if (!mi->count) {
		spin_unlock(&mi->lock);
		pr_info("low_lat_mem: low_lat_mem, get cma is NULL, count: %lu, id: %d\n", count, mi->idx);
		return NULL;
	}
	list_for_each_entry_safe(ent, n, &mi->head, list) {
		if (!ent)
			break;
		if (ent->count == count && ent->align == align) {
			pr_info("low_lat_mem: low_lat_mem, get cma pfn:%lu, count:%lu, total:%d\n",
				ent->pfn, count, mi->count);
			page = pfn_to_page(ent->pfn);
			list_del(&ent->list);
			kfree(ent);
			mi->count--;
			break;
		}
	}
	spin_unlock(&mi->lock);
	return page;
}

static noinline __used void charge_cma_mem(struct cma *cma, unsigned long pfn, unsigned long size,
					   unsigned int align, bool alloc)
{
	struct mi_cma *mi;
	struct cma_tmp_ent *e = NULL, *t;
	int idx, started;

	started = READ_ONCE(calculate_start);
	if (!cma || !started)
		return;
	mi = find_mi_cma(cma);
	if (!mi)
		return;
	idx = mi->idx;
	spin_lock(&cma_tmp_lock);
	list_for_each_entry(t, &cma_tmp_list, list)
		if (t->idx == idx && t->size == size && t->align == align) {
			e = t;
			break;
		}
	if (!e) {
		e = kzalloc(sizeof(*e), GFP_ATOMIC);
		if (!e) {
			spin_unlock(&cma_tmp_lock);
			return;
		}
		e->idx = idx;
		e->size = size;
		e->align = align;
		llm_list_add_tail(&e->list);
	}
	spin_unlock(&cma_tmp_lock);
	pr_info("low_lat_mem: low_lat_mem, cma alloc index: %d, pfn:%lu, size: %lu, align:%u, alloc:%d \n",
		idx, pfn, size, align, alloc & 1);
	if (alloc)
		atomic_inc(&e->cur);
	else
		atomic_dec(&e->cur);
	if (atomic_read(&e->cur) > atomic_read((atomic_t *)&e->peak))
		e->peak = atomic_read(&e->cur);
}

static void mi_cma_alloc_vh(void *data, struct cma *cma, unsigned long count, unsigned int align,
			    gfp_t gfp_mask, struct page **page, bool *bypass)
{
	unsigned long pfn;
	bool hit;

	if (count < 1000 || !READ_ONCE(llm_enable) || READ_ONCE(predo_pending) ||
	    !READ_ONCE(calculate_start) || READ_ONCE(shrinking))
		return;
	*page = get_page_from_mi_cma(cma, count, align);
	if (!*page) {
		cma_alloc_from_orig_cnt++;
		hit = false;
		pfn = 0;
		*bypass = false;
	} else {
		cma_alloc_from_pool_cnt++;
		hit = true;
		*bypass = true;
		pfn = page_to_pfn(*page);
	}
	pr_info("low_lat_mem: low_lat_mem, cma alloc bypass:%d \n", hit);
	charge_cma_mem(cma, pfn, count, align, true);
}

static void mi_cma_alloc_end(void *data, struct cma *cma, unsigned long pfn_unused,
			     const struct page *pages, unsigned long count, unsigned int align, int ret)
{
	unsigned long pfn;
	int started;

	if (count < 1000 || !READ_ONCE(llm_enable) || READ_ONCE(predo_pending))
		return;
	started = READ_ONCE(calculate_start);
	if (!pages || !started)
		return;
	pfn = page_to_pfn(pages);
	put_cma_align(pfn, count, align);
	pr_info("low_lat_mem: low_lat_mem, cma alloc pfn:%lu, size: %lu, align:%u \n", pfn, count, align);
}

static void mi_cma_release_end(void *data, struct cma *cma, const struct page *pages,
			       unsigned long count, bool *bypass)
{
	struct cma_align ca = {};
	unsigned long pfn;
	bool done = false;

	if (count >= 1000 && READ_ONCE(llm_enable) && !READ_ONCE(predo_pending) &&
	    READ_ONCE(calculate_start) && !READ_ONCE(shrinking)) {
		pfn = page_to_pfn(pages);
		if (!get_cma_align(pfn, &ca) && ca.val) {
			if (ca.val != count) {
				clear_cma_align(pfn);
			} else {
				put_page_to_mi_cma(cma, pfn, count, ca.ref);
				pr_info("low_lat_mem: low_lat_mem, release_end pfn:%lu, size: %lu, align:%u \n",
					pfn, count, ca.ref);
				charge_cma_mem(cma, pfn, count, ca.ref, false);
				done = true;
			}
		}
	}
	*bypass = done;
}

/* give every parked allocation back to its CMA area (called through the shrinker) */
static noinline __used int shrink_cma_pool(void)
{
	LIST_HEAD(tmp);
	struct cma_page_ent *ent, *next;
	int total = 0;
	int i;

	shrinking = 1;
	spin_lock(&cma_pfn_to_align_lock);
	memset(cma_pfn_to_align, 0, sizeof(cma_pfn_to_align));
	spin_unlock(&cma_pfn_to_align_lock);
	for (i = 0; i < atomic_read(&cma_refs_count); i++) {
		struct cma *cma = cma_refs[i].cma;
		struct mi_cma *mi;

		if (!cma)
			continue;
		mi = cma_refs[i].ent;
		if (!mi || list_empty(&mi->head))
			continue;
		pr_info("low lat mem, cma shrink :%s \n", (char *)cma + 0x40);
		spin_lock(&mi->lock);
		list_for_each_entry_safe(ent, next, &mi->head, list)
			list_move(&ent->list, &tmp);
		mi->count = 0;
		spin_unlock(&mi->lock);
		list_for_each_entry_safe(ent, next, &tmp, list) {
			cma_releasing = 1;
			cma_release(cma, pfn_to_page(ent->pfn), ent->count);
			cma_releasing = 0;
			pr_info("low lat mem, cma shrink pfn:%lu, size: %lu, align:%u \n", ent->pfn, ent->count,
				ent->align);
			list_del(&ent->list);
			total += ent->count;
			kfree(ent);
		}
	}
	shrinking = 0;
	return total;
}

static unsigned long cma_pool_shrink_count(struct shrinker *shrinker, struct shrink_control *sc)
{
	unsigned long free = 0, reserve = 0, total = 0;
	struct zone *zone;
	int i;

	if (!READ_ONCE(llm_enable))
		return 0;
	for_each_populated_zone(zone) {
		free += zone_page_state(zone, NR_FREE_PAGES);
		reserve += zone->_watermark[WMARK_MIN] + zone->nr_reserved_highatomic;
	}
	if (free >= reserve)
		return 0;
	for (i = 0; i < atomic_read(&cma_refs_count); i++) {
		struct mi_cma *mi = cma_refs[i].ent;
		struct cma_page_ent *ent;

		if (!mi)
			continue;
		spin_lock(&mi->lock);
		list_for_each_entry(ent, &mi->head, list)
			total += ent->count;
		spin_unlock(&mi->lock);
	}
	return total << 2;
}

static unsigned long cma_pool_shrink_scan(struct shrinker *shrinker, struct shrink_control *sc)
{
	if (!READ_ONCE(llm_enable) || !sc->nr_to_scan)
		return 0;
	return shrink_cma_pool();
}

/* ======== G1n: top level enable (the dfx `enable` file uses enable_store above) ======== */

static ssize_t llm_enable_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	unsigned int val = 0;
	int ret = kstrtoint(buf, 10, (int *)&val);

	if (ret)
		return ret;
	if (val > 1)
		return -EINVAL;
	llm_enable = val;
	pr_info("low_lat_mem: low_lat_mem enable : %d\n", val);
	return count;
}

/* ======== G1n: scratch data of the predo worker / misight (sys_mem_data = 4 slots of dmabuf counters) ======== */

static noinline __used int sys_mem_data_dmabuf_init(void)
{
	int i, j;

	if (!dmabuf_pool_nums)
		return -ENOMEM;
	for (i = 0; i < 4; i++) {
		struct sys_mem_slot *s = &sys_mem_data[i];

		INIT_LIST_HEAD(&s->list);
		s->arr = kmalloc_array(dmabuf_pool_nums, sizeof(void *), GFP_KERNEL);
		if (!s->arr)
			goto fail;
		for (j = 0; j < dmabuf_pool_nums; j++)
			s->arr[j] = kzalloc(12, GFP_KERNEL);
	}
	return 0;
fail:
	while (--i >= 0)
		kfree(sys_mem_data[i].arr);
	return -ENOMEM;
}

static noinline __used void sys_mem_data_dmabuf_free(void)
{
	int i, j;

	if (!dmabuf_pool_nums)
		return;
	for (i = 0; i < 4; i++) {
		struct sys_mem_slot *s = &sys_mem_data[i];

		for (j = 0; j < dmabuf_pool_nums; j++)
			kfree(s->arr[j]);
		kfree(s->arr);
		s->arr = NULL;
	}
}

/* ======== G1n: pre-compact page pools ======== */

static noinline __used void collect_pages(int order, unsigned int count)
{
	struct pre_compact_pool *pool;
	unsigned long flags;
	struct page *page;

	if ((order != 9 && order != 4) || !count)
		return;
	pool = ((struct pre_compact_pool **)(mci.raw + 0x10))[order];
	if (!pool)
		return;
	do {
		page = __alloc_pages(0x021121ca, order, 0, NULL);
		if (!page)
			break;
		spin_lock_irqsave(&pool->lock, flags);
		list_add(&page->lru, &pool->items);
		spin_unlock_irqrestore(&pool->lock, flags);
		atomic_inc(&pool->count);
		mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, 1L << pool->order);
	} while (--count);
}

static struct pre_compact_pool *pool_new(int order)
{
	struct pre_compact_pool *p = kmalloc(sizeof(*p), GFP_KERNEL);

	if (!p)
		return NULL;
	INIT_LIST_HEAD(&p->items);
	spin_lock_init(&p->lock);
	p->order = order;
	atomic_set(&p->count, 0);
	return p;
}

static noinline __used int mi_pre_compact_init(void)
{
	struct pre_compact_pool *pool;
	struct page *page;
	int ret;

	MCI_U32(0) = 0;				/* strh wzr,[mci] */
	*(u64 *)(mci.raw + 4) = 0;
	*(u64 *)(mci.raw + 0x68) = 0;
	MCI_U32(0x70) = 3;			/* retry_times */
	MCI_U32(0x8e8) = 0;
	if (MCI_PTR(0x30) || MCI_PTR(0x58)) {
		pr_warn("low_lat_mem: pre_compact pools already initialized, cleaning up\n");
		pre_compact_pools_exit();
	}
	pool = pool_new(4);
	if (!pool) {
		MCI_PTR(0x30) = NULL;
		pr_err("low_lat_mem: pre_compact pools init fail %d\n", -ENOMEM);
		return -ENOMEM;
	}
	MCI_PTR(0x30) = pool;
	pool = pool_new(9);
	if (!pool) {
		pool = MCI_PTR(0x30);
		MCI_PTR(0x58) = NULL;
		if (pool) {
			while ((page = pre_compact_pool_remove(pool)))
				__free_pages(page, pool->order);
			kfree(pool);
		}
		pr_err("low_lat_mem: pre_compact pools init fail %d\n", -ENOMEM);
		return -ENOMEM;
	}
	MCI_PTR(0x58) = pool;
	ret = register_trace_android_vh_compact_bypass(do_compact_mem_collect, NULL);
	if (ret)
		pr_err("low_lat_mem: register vh compact_bypass failed! ret=%d\n", ret);
	ret = register_trace_android_vh_alloc_pages_reclaim_bypass(do_alloc_pages_big_order, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh alloc_pages_reclaim_bypass failed! ret=%d\n", ret);
		unregister_trace_android_vh_compact_bypass(do_compact_mem_collect, NULL);
		pr_err("low_lat_mem: register pre_compact hooks fail: %d", ret);
		goto out_exit;
	}
	llm_kallsym_kp.symbol_name = "kallsyms_lookup_name";
	if (register_kprobe(&llm_kallsym_kp)) {
		pr_info("low_lat_mem: kallsyms_lookup_name addr not available\n");
		ret = -1;
		goto sym_fail;
	}
	llm_ksym_lookup_name = (void *)llm_kallsym_kp.addr;
	unregister_kprobe(&llm_kallsym_kp);
	try_to_free_pages_symbol = llm_ksym_lookup_name("try_to_free_pages");
	if (!try_to_free_pages_symbol) {
		ret = -1;
		goto sym_fail;
	}
	ret = sysfs_create_group(low_lat_mem_kobject, &pre_compact_attr_group);
	if (ret) {
		pr_err("low_lat_mem: add pre_compact sysfs fail %d\n", ret);
		goto sym_fail;
	}
	pr_info("low_lat_mem: mi_pre_compact_init success\n");
	return 0;
sym_fail:
	pr_err("low_lat_mem: search pre_compact symbols fail: %d", ret);
	unregister_trace_android_vh_compact_bypass(do_compact_mem_collect, NULL);
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(do_alloc_pages_big_order, NULL);
out_exit:
	pre_compact_pools_exit();
	return ret;
}

/* ======== G1n: dfx sysfs group ======== */

static noinline __used ssize_t pre_compact_time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t r;

	asm volatile("" : "=r"(r) : "r"(buf) : "memory");
	return r;
}

static noinline __used ssize_t pre_compact_success_rate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	ssize_t r;

	asm volatile("" : "=r"(r) : "r"(buf) : "memory");
	return r;
}

static struct kobj_attribute dfx_scene_cold_start_attr = __ATTR(scene_cold_start, 0444, scene_cold_start_show, NULL);
static struct kobj_attribute dfx_scene_fling_attr = __ATTR(scene_fling, 0444, scene_fling_show, NULL);
static struct kobj_attribute dfx_scene_unlock_attr = __ATTR(scene_unlock, 0444, scene_unlock_show, NULL);
static struct kobj_attribute dfx_scene_game_attr = __ATTR(scene_game, 0444, scene_game_show, NULL);
static struct kobj_attribute dfx_scene_camera_attr = __ATTR(scene_camera, 0444, scene_camera_show, NULL);
static struct kobj_attribute dfx_pre_compact_time_attr = __ATTR(pre_compact_time, 0444, pre_compact_time_show, NULL);
static struct kobj_attribute dfx_pre_compact_success_rate_attr = __ATTR(pre_compact_success_rate, 0444, pre_compact_success_rate_show, NULL);
static struct kobj_attribute dfx_pool_hit_rate_attr = __ATTR(pool_hit_rate, 0444, pool_hit_rate_show, NULL);
static struct kobj_attribute dfx_enable_attr = __ATTR(enable, 0644, enable_show, enable_store);
static struct kobj_attribute dfx_reset_attr = __ATTR(reset, 0200, NULL, reset_store);
static struct attribute *dfx_attrs[] = {
	&dfx_scene_cold_start_attr.attr, &dfx_scene_fling_attr.attr, &dfx_scene_unlock_attr.attr,
	&dfx_scene_game_attr.attr, &dfx_scene_camera_attr.attr, &dfx_pre_compact_time_attr.attr,
	&dfx_pre_compact_success_rate_attr.attr, &dfx_pool_hit_rate_attr.attr,
	&dfx_timeout_cma_attr.attr, &dfx_timeout_dma_attr.attr, &dfx_timeout_order0_attr.attr,
	&dfx_timeout_order4_attr.attr, &dfx_timeout_order9_attr.attr, &dfx_timeout_direct_reclaim_attr.attr,
	&dfx_timeout_free_attr.attr, &dfx_timeout_async_compact_attr.attr,
	&dfx_enable_attr.attr, &dfx_reset_attr.attr, NULL,
};
static const struct attribute_group dfx_attr_group = {
	.name = "dfx",
	.attrs = dfx_attrs,
};

static void llm_dfx_unregister_hooks(void)
{
	unregister_trace_android_vh_cma_alloc_lat_start(cma_alloc_start_handler, NULL);
	unregister_trace_android_vh_cma_alloc_lat_end(cma_alloc_end_handler, NULL);
	unregister_trace_android_vh_dma_heap_buffer_alloc_lat_start(dma_heap_buffer_alloc_start_handler, NULL);
	unregister_trace_android_vh_dma_heap_buffer_alloc_lat_end(dma_heap_buffer_alloc_end_handler, NULL);
	unregister_trace_android_vh_mm_direct_reclaim_start(mm_direct_reclaim_start_handler, NULL);
	unregister_trace_android_vh_mm_direct_reclaim_end(mm_direct_reclaim_end_handler, NULL);
	unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_pages_slowpath_start_handler, NULL);
	unregister_trace_android_vh_alloc_pages_slowpath_end(alloc_pages_slowpath_end_handler, NULL);
}

static noinline __used int mi_dfx_exit(void)
{
	sysfs_remove_group(low_lat_mem_kobject, &dfx_attr_group);
	llm_dfx_unregister_hooks();
	dfx_info_reset();
	return 0;
}

#define DFX_NAME(off, str)	strscpy((char *)dfx.raw + (off), (str), 20)

static noinline __used int mi_dfx_init(void)
{
	int ret;

	/* hook registration is done by dfx_enable_change() (not reconstructed yet), the names/stats here */
	DFX_NAME(0x00, "cma_alloc");
	DFX_NAME(0x14, "dma_alloc");
	DFX_NAME(0x28, "order4_alloc");
	snprintf((char *)dfx.raw + 0x3c, 20, "order%d_alloc", 9);
	DFX_NAME(0x50, "free_time");
	DFX_NAME(0x64, "async_compact_time");
	DFX_NAME(0x8c, "fling");
	DFX_NAME(0xa0, "unlock");
	DFX_NAME(0xb4, "cold_start");
	DFX_NAME(0xc8, "camera");
	DFX_NAME(0xdc, "game");
	DFX_NAME(0xf0, "qos");
	DFX_NAME(0x104, "main_thread");
	DFX_NAME(0x118, "background");
	dfx.raw[0xe0] = 0;
	memset(dfx.raw + 0x12c, 0, 0x978 - 0x12c);
	DFX_U32(0x980) = 100;
	DFX_U32(0x984) = 200;
	DFX_U32(0x988) = 600;
	DFX_U32(0x98c) = 100;
	DFX_U32(0x990) = 100;
	DFX_U32(0x994) = 100;
	DFX_U32(0x998) = 100;
	DFX_U32(0x99c) = 500;
	dfx_enable_change(~dfx_enable & 1);
	ret = sysfs_create_group(low_lat_mem_kobject, &dfx_attr_group);
	if (ret) {
		pr_err("low_lat_mem: add dfx_sysfs fail %d\n", ret);
		llm_dfx_unregister_hooks();
	}
	return ret;
}

/* ======== G1n: module init / exit ======== */

static noinline __used int predo_store_call(void *unused)
{
	/* the predo worker thread (predo_worker): not reconstructed yet */
	asm volatile("" : : "r"(unused) : "memory");
	return 0;
}

static struct task_struct *predo_worker_task;

static struct kobj_attribute start_attr = __ATTR(start, 0664, NULL, start_store);
static struct kobj_attribute predo_attr = __ATTR(predo, 0664, NULL, predo_store);
static struct kobj_attribute shrink_attr = __ATTR(shrink, 0664, NULL, shrink_store);
static struct kobj_attribute enable_attr = __ATTR(enable, 0664, NULL, llm_enable_store);
static struct attribute *low_lat_mem_sysfs_attrs[] = {
	&start_attr.attr, &predo_attr.attr, &shrink_attr.attr, &enable_attr.attr, NULL,
};
static const struct attribute_group low_lat_mem_sysfs_group = {
	.attrs = low_lat_mem_sysfs_attrs,
};
static const struct attribute_group *low_lat_mem_sysfs_groups[] = {
	&low_lat_mem_sysfs_group, NULL,
};

static struct cftype memcg_ctrl_files[] = {
	{ .name = "start", .write = memcg_write_start },
	{ .name = "appname", .seq_show = memcg_read_appname, .write = memcg_write_appname },
	{ .name = "version", .seq_show = memcg_read_version, .write = memcg_write_version },
	{ },
};

static int __init low_lat_mem_init(void)
{
	int ret;

	mutex_init(&pending_lock);
	spin_lock_init(&cma_lock);
	mutex_init(&dmabuf_lock);
	mutex_init(&largemem_lock);
	spin_lock_init(&cma_pfn_to_align_lock);
	predo_pending = 0;
	calculate_start = 0;
	llm_enable = 0;
	shrinking = 0;
	mi_smart_refill_init(dmabuf_pool_nums);
	INIT_LIST_HEAD(&cma_tmp_list);
	cma_for_each_area(cma_area_init, &cma_area_nums);
	spin_lock_init(&cma_tmp_lock);
	smp_wmb();
	atomic_set(&largemem_data_tmp[0], 0);
	atomic_set(&largemem_data_tmp[1], 0);
	if (dmabuf_data_tmp_init() || sys_mem_data_dmabuf_init()) {
		kfree(dmabuf_data_tmp);
		return -ENOMEM;
	}
	if (cma_pool_init_shrinker())
		pr_err("low_lat_mem: cma pool register failed!");
	if (cgroup_subsys_on_dfl(memory_cgrp_subsys)) {
		pr_err("low_lat_mem: low_lat_mem support memcg v2\n");
		cgroup_add_dfl_cftypes(&memory_cgrp_subsys, memcg_ctrl_files);
	} else {
		pr_err("low_lat_mem: low_lat_mem support memcg v1\n");
		cgroup_add_legacy_cftypes(&memory_cgrp_subsys, memcg_ctrl_files);
	}
	low_lat_mem_kobject = kobject_create_and_add("low_lat_mem", kernel_kobj);
	if (!low_lat_mem_kobject) {
		ret = -ENOMEM;
		goto rm_cft;
	}
	ret = sysfs_create_groups(low_lat_mem_kobject, low_lat_mem_sysfs_groups);
	if (ret) {
		kobject_put(low_lat_mem_kobject);
		goto rm_cft;
	}
	mi_pre_compact_init();
	mi_dfx_init();
	ret = mi_memcg_vendor_alloc(root_mem_cgroup, true);
	if (ret) {
		pr_info("low_lat_mem: low_lat_mem mi_memcg alloc failed.\n");
		goto rm_cft;
	}
	predo_worker_task = kthread_create_on_node(predo_store_call, NULL, NUMA_NO_NODE, "predo_worker");
	if (IS_ERR(predo_worker_task)) {
		pr_err("low_lat_mem: failed to create predo_worker thread\n");
		ret = PTR_ERR(predo_worker_task);
		goto rm_cft;
	}
	wake_up_process(predo_worker_task);
	ret = register_trace_android_rvh_try_alloc_pages_gfp(mi_try_alloc_pages_gfp, NULL);
	if (ret) {
		pr_err("low_lat_mem: register rvh_try_alloc_pages_gfp failed! ret=%d\n", ret);
		goto rm_cft;
	}
	ret = register_trace_android_vh_mem_cgroup_alloc(mi_memcg_alloc, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh_mem_cgroup_alloc failed! ret=%d\n", ret);
		goto rm_cft;
	}
	ret = register_trace_android_vh_mem_cgroup_free(mi_memcg_free, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh_mem_cgroup_free failed! ret=%d\n", ret);
		goto unreg_alloc;
	}
	ret = register_trace_android_vh_cma_alloc_bypass(mi_cma_alloc_vh, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh_cma_alloc_bypass failed! ret=%d\n", ret);
		goto unreg_free;
	}
	ret = register_trace_android_vh_cma_alloc_end(mi_cma_alloc_end, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh_cma_alloc_end failed! ret=%d\n", ret);
		goto unreg_bypass;
	}
	ret = register_trace_android_vh_cma_release_bypass(mi_cma_release_end, NULL);
	if (ret) {
		pr_err("low_lat_mem: register vh_cma_release_bypass failed! ret=%d\n", ret);
		unregister_trace_android_vh_cma_alloc_end(mi_cma_alloc_end, NULL);
unreg_bypass:
		unregister_trace_android_vh_cma_alloc_bypass(mi_cma_alloc_vh, NULL);
unreg_free:
		unregister_trace_android_vh_mem_cgroup_free(mi_memcg_free, NULL);
unreg_alloc:
		unregister_trace_android_vh_mem_cgroup_alloc(mi_memcg_alloc, NULL);
		goto rm_cft;
	}
	pr_info("low_lat_mem: %s ok.\n", "low_lat_mem_init");
	return 0;
rm_cft:
	cgroup_rm_cftypes(memcg_ctrl_files);
	return ret;
}

static void __exit low_lat_mem_exit(void)
{
	sys_mem_data_dmabuf_free();
	kfree(dmabuf_data_tmp);
	mi_dfx_exit();
	if (predo_worker_task) {
		kthread_stop(predo_worker_task);
		predo_worker_task = NULL;
	}
	mi_pre_compact_exit();
	unregister_trace_android_vh_mem_cgroup_alloc(mi_memcg_alloc, NULL);
	unregister_trace_android_vh_mem_cgroup_free(mi_memcg_free, NULL);
	unregister_trace_android_vh_cma_alloc_bypass(mi_cma_alloc_vh, NULL);
	unregister_trace_android_vh_cma_alloc_end(mi_cma_alloc_end, NULL);
	unregister_trace_android_vh_cma_release_bypass(mi_cma_release_end, NULL);
	if (low_lat_mem_kobject) {
		kobject_put(low_lat_mem_kobject);
		low_lat_mem_kobject = NULL;
	}
	cgroup_rm_cftypes(memcg_ctrl_files);
	pr_info("low_lat_mem: %s\n", "low_lat_mem_exit");
}

module_init(low_lat_mem_init);
module_exit(low_lat_mem_exit);

/* keep every address-taken callback alive (gives them the KCFI type word like the stock) */
static const void *const mi_low_lat_mem_callbacks[] __used = {
	start_store, shrink_store, predo_store, enable_store, enable_show, reset_store,
	retry_times_show, retry_times_store, target_count_show, curr_collected_count_show,
	cma_alloc_start_handler, dma_heap_buffer_alloc_start_handler,
	mm_direct_reclaim_start_handler, alloc_pages_slowpath_start_handler,
	mi_try_alloc_pages_gfp, mi_smart_refill_alloc, mi_smart_refill_extend_alloc,
	mi_memcg_alloc, mi_memcg_free, get_time_bucket, dfx_enable_control, scene_fling_show, scene_unlock_show, scene_cold_start_show, scene_camera_show, scene_game_show, pool_hit_rate_show, memcg_read_appname, memcg_read_version, memcg_write_appname, memcg_write_version, memcg_write_start, check_report_limit, predo_trigger, largemem_data_tmp_init, dmabuf_data_tmp_init, do_compact_mem_collect, do_alloc_pages_big_order, llm_prep_compound_page,
};

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(MINIDUMP);
