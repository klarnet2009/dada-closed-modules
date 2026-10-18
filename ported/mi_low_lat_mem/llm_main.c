#include "llm_common.h"

struct llm_dfx_t dfx __aligned(8);
struct llm_mci_t mci __aligned(8);
int llm_enable;
int calculate_start;
unsigned int *dmabuf_data_tmp;
int dmabuf_pool_nums;
atomic_t largemem_data_tmp[2] __asm__("largemem_data_tmp.llvm.5013616374876433728");
struct cma_ref cma_refs[17];
atomic_t cma_refs_count;
struct cma_pfn_to_align cma_pfn_to_align[100];
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
struct kprobe llm_kallsym_kp = { .symbol_name = "kallsyms_lookup_name" };	/* .data with the pointer reloc in the stock */
unsigned long (*llm_ksym_lookup_name)(const char *name);
unsigned long try_to_free_pages_symbol;
struct sys_mem_slot sys_mem_data[4];
struct list_head cma_tmp_list;
spinlock_t cma_tmp_lock;
static int predo_val;
static wait_queue_head_t predo_wq = __WAIT_QUEUE_HEAD_INITIALIZER(predo_wq);
/* ThinLTO promoted this static (the stock symbol carries the .llvm.<hash> suffix) */
/* sys_mem_data: 4 slots of 56 bytes (stock g[0x18], 0x50, 0x88, 0xc0 = the pointer arrays) */
/* the intervals come from the stock .data (hour: 3600 s, 5 per interval; day: 86400 s, 25) */
static noinline __used void init_mem_data_tmp(void);
noinline __used void record_mem_data(int idx, bool sys);
static const char *scene_name[4] = { "unlock", "launch", "cam", "game" };
noinline __used void start_store_call(int v)
{
	int idx;

	if (!mutex_trylock(&pending_lock))
		return;
	if (v <= 200) {
		idx = v - 102;
		if ((unsigned int)idx < 4) {
			if (!READ_ONCE(calculate_start)) {
				calculate_start = 1;
				scene_code = v;
				sys_scene = 1;
				pr_info("low_lat_mem: low_lat_mem memcg_stat start, scene: %s, code: %d.\n", scene_name[idx], v);
				if (READ_ONCE(llm_enable))
					init_mem_data_tmp();
			}
		} else if (v == 101) {
			if (!READ_ONCE(calculate_start)) {
				calculate_start = 1;
				scene_code = 101;
				pr_info("low_lat_mem: low_lat_mem memcg_stat start, scene: fling.\n");
			}
		}
	} else {
		idx = v - 202;
		if ((unsigned int)idx < 4) {
			if (READ_ONCE(calculate_start)) {
				calculate_start = 0;
				scene_code = 0;
				pr_info("low_lat_mem: low_lat_mem memcg_stat end, scene: %s, code: %d.\n", scene_name[idx], v);
				if (READ_ONCE(llm_enable)) {
					record_mem_data(idx, sys_scene);
					init_mem_data_tmp();
				}
				sys_scene = 0;
			}
		} else if (v == 201) {
			if (READ_ONCE(calculate_start)) {
				calculate_start = 0;
				scene_code = 0;
				pr_info("low_lat_mem: low_lat_mem memcg_stat end, scene: fling.\n");
			}
		}
	}
	mutex_unlock(&pending_lock);
}
noinline __used int mi_memcg_vendor_alloc(struct mem_cgroup *memcg, bool flag)
{
	struct llm_vendor **slot = (struct llm_vendor **)((char *)memcg + 0x8f8);
	struct llm_vendor *e;
	gfp_t gfp = (flag & 1) ? (GFP_ATOMIC | __GFP_ZERO) : (GFP_KERNEL | __GFP_ZERO);
	int i, j, ret;

	e = kmalloc(sizeof(*e), gfp);
	if (!e)
		return -ENOMEM;
	INIT_LIST_HEAD(&e->list);
	e->arr = kmalloc(dmabuf_pool_nums * sizeof(void *), gfp);
	if (!e->arr) {
		ret = -ENOMEM;
		goto free_e;
	}
	for (i = 0; i < dmabuf_pool_nums; i++) {
		e->arr[i] = kmalloc(12, gfp);
		if (!e->arr[i]) {
			for (j = 0; j < i; j++)
				kfree(e->arr[j]);
			ret = -ENOMEM;
			goto free_arr;
		}
	}
	*slot = NULL;
	if (cmpxchg(slot, NULL, e)) {
		for (i = 0; i < dmabuf_pool_nums; i++)
			kfree(e->arr[i]);
		ret = 0;
		goto free_arr;
	}
	return 0;
free_arr:
	kfree(e->arr);
free_e:
	kfree(e);
	return ret;
}
/* ---- tracing helper ---- */
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
void predo_trigger(int val);
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
/* ---- pre-compact sysfs ---- */
static ssize_t retry_times_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", MCI_U32(0x70));
}
static ssize_t retry_times_store(struct kobject *kobj, struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned int val = 0;

	if (sscanf(buf, "%u", &val) != 1 || __builtin_expect_with_probability(val > 100, 1, 0.9))
		count = -EINVAL;
	else if (val)
		MCI_U32(0x70) = val;
	return count;
}
static ssize_t target_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "order4:%u\norder%d:%u\n", MCI_U32(4), 9, MCI_U32(8));
}
static ssize_t curr_collected_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "order4:%u\norder%d:%u\n",
			  atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x30))->collected), 9,
			  atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x58))->collected));
}
/* ---- predo / scratch data ---- */
noinline __used void predo_trigger(int val)
{
	WRITE_ONCE(predo_val, val);
	if (READ_ONCE(llm_enable)) {
		if (!READ_ONCE(calculate_start))
			wake_up_interruptible(&predo_wq);
	}
}
noinline __used void largemem_data_tmp_init(void)
{
	atomic_set(&largemem_data_tmp[0], 0);
	atomic_set(&largemem_data_tmp[1], 0);
}
noinline __used int dmabuf_data_tmp_init(void)
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
noinline __used void dmabuf_data_tmp_free(void)
{
	kfree(dmabuf_data_tmp);
}
void mi_try_alloc_pages_gfp(void *data, struct page **page, unsigned int order,
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
#define SMART_REFILL_HANDLER(name, linkage)						\
linkage void name(void *data, void *pool_p, struct page *page, int from_buddy)		\
{											\
	struct mi_smart_refill_pool_stat *pool = pool_p;				\
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
SMART_REFILL_HANDLER(mi_smart_refill_alloc, static)
SMART_REFILL_HANDLER(mi_smart_refill_extend_alloc, )	/* GLOBAL in the stock symtab */

/* ---- memcg vendor data ---- */
void mi_memcg_alloc(void *data, struct mem_cgroup *memcg)
{
	if (mi_memcg_vendor_alloc(memcg, 1))
		pr_info("low_lat_mem: low_lat_mem mi_memcg alloc failed.\n");
}
void mi_memcg_free(void *data, struct mem_cgroup *memcg)
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
/* ---- scene_* show: per (process type, alloc type) latency histogram of one scene ---- */
LIST_HEAD(mi_smart_refill_pools);
EXPORT_SYMBOL_GPL(mi_smart_refill_pools);
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
	if (__builtin_expect_with_probability(!val, 1, 0.45))
		current_write_memcg = NULL;
	else if (val == 1)
		current_write_memcg = memcg;
	id = current_write_memcg ? *(int *)((char *)memcg + 0xe8) : 0;
	pr_info("low_lat_mem: low_lat_mem start: %d, current_write_memcg: %d. \n", val, id);
	return nbytes;
}
/* ---- pre-compact pools (order 4 / order 9 pages kept ready for allocation) ---- */
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
__used void llm_prep_compound_page(struct page *page, unsigned int order) __asm__("prep_compound_page");
static __always_inline void prep_compound_body(struct page *page, unsigned int order)
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
__used void llm_prep_compound_page(struct page *page, unsigned int order)
{
	prep_compound_body(page, order);
}
static __always_inline struct page *pool_remove_body(struct pre_compact_pool *pool)
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
static noinline __used struct page *pre_compact_pool_remove(struct pre_compact_pool *pool)
{
	return pool_remove_body(pool);
}
noinline __used struct page *alloc_pre_compact_page(gfp_t gfp, int order)
{
	struct page *page;

	if (order != 9 && order != 4)
		return NULL;
	page = pre_compact_pool_remove(((struct pre_compact_pool **)(mci.raw + 0x10))[order]);
	if (page) {
		barrier(); /* hack: keeps the stock order cbz(page) before tbz(GFP_COMP) */
		if (gfp & __GFP_COMP)
			llm_prep_compound_page(page, order);
	}
	return page;
}
static __always_inline void pools_exit_body(void)
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
static noinline __used void pre_compact_pools_exit(void)
{
	pools_exit_body();
}
static noinline __used void collect_pages(int order, unsigned int count);
static void do_compact_mem_collect(void *data, struct compact_control *cc, bool *bypass)
{
	bool enough;
	u32 t4, t9;

	if (!*(u8 *)mci.raw)
		return;
	if (scene_code > 0) {
		*bypass = true;
		return;
	}
	t9 = MCI_U32(8);
	if (atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x58))->collected) < t9)
		collect_pages(9, t9 - atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x58))->collected));
	t4 = MCI_U32(4);
	if (atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x30))->collected) < t4)
		collect_pages(4, t4 - atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x30))->collected));
	if (atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x30))->collected) < t4)
		enough = false;
	else
		enough = atomic_read(&((struct pre_compact_pool_hdr *)MCI_PTR(0x58))->collected) >= t9;
	*(u8 *)(mci.raw + 1) = enough;
	*bypass = enough;
}
static void do_alloc_pages_big_order(void *data, gfp_t gfp_mask, int order, int alloc_flags,
				     int migratetype, struct page **page)
{
	struct page *p;

	if (!(gfp_mask & 0x400))
		return;
	if ((unsigned int)(scene_code - 0x6a) < 0xfffffffcU)
		return;
	if (order != 9 && order != 4)
		return;
	if (*page)
		return;
	if (order == 4) {
		DFX_U64(0x948)++;
		barrier();	/* keep the two increments apart like the stock */
	} else {
		DFX_U64(0x958)++;
		barrier();
	}
	p = pre_compact_pool_remove(((struct pre_compact_pool **)(mci.raw + 0x10))[order]);
	if (!p) {
		*page = NULL;
		return;
	}
	if (gfp_mask & __GFP_COMP)
		prep_compound_body(p, order);
	*page = p;
	if (order == 4)
		DFX_U64(0x940)++;
	else
		DFX_U64(0x950)++;
}
static ssize_t stats_history_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	/* G1w, from the stock disassembly: table of the (up to 30) pre-compact history records of mci (0x48 bytes each, fields from +0x78), oldest first */
	char tbuf[20] = {};
	struct tm tm = {};
	size_t len;
	unsigned int n, cnt = 0;
	unsigned long i, j, k;

	if (!mci.hist_cnt)
		return sysfs_emit(buf, "No statistics data available\n");
	len = sysfs_emit_at(buf, 0,
			    "Index         Time           Mem(MB)    Frag4    Frag%d    T4    A4    T%d    A%d    Rate4 Rate%d Remain4  Remain%d  Dur(us)  Success\n",
			    9, 9, 9, 9, 9);
	n = mci.hist_cnt < 30 ? mci.hist_cnt : 30;
	if (!n)
		return len;
	int idx[30] = {};

	for (i = 0; i < n; i++)
		if (mci.hist[i].time)
			idx[cnt++] = i;
	if (cnt >= 2)
		for (i = 0; i < cnt - 1; i++)
			for (j = 0; j < (unsigned int)(cnt - 1 - i); j++) {
				u64 a = mci.hist[idx[j]].time, b;

				if (a && (b = mci.hist[idx[j + 1]].time) && a > b)
					swap(idx[j], idx[j + 1]);
			}
	for (k = 0; k < cnt; k++) {
		struct pc_rec *r = &mci.hist[idx[k]];
		u32 b4 = r->c4, b9 = r->c9;
		u32 a4 = r->n4 < b4 ? 0 : r->n4 - b4, a9 = r->n9 < b9 ? 0 : r->n9 - b9;
		u32 rate4 = r->t4 ? a4 * 100 / r->t4 : 100, rate9 = r->t9 ? a9 * 100 / r->t9 : 100;

		time64_to_tm(r->time / 1000000000, 28800, &tm);
		snprintf(tbuf, sizeof(tbuf), "%02d-%02d %02d:%02d:%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		len += sysfs_emit_at(buf, len,
				     "%-9u %-19s %-9lu %u-%-5u %u-%-5u %-5u %-5u %-5u %-5u %-3u%s  %-3u%s  %-8u %-8u %-8lu %s\n",
				     (unsigned int)k + 1, tbuf, (unsigned long)((r->pages * 4) >> 10), r->f4, r->f4a,
				     r->f9, r->f9a, r->t4, a4, r->t9, a9, rate4, "%", rate9, "%", r->n4, r->n9,
				     (unsigned long)(r->dur / 1000), r->success ? "true" : "false");
		if (len > 3895)
			break;
	}
	return len;
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
#ifdef __GENKSYMS__
/* CRC pad: genksyms-only type so that the export CRC equals the stock one (code and ABI untouched), see STATUS.md */
struct fcrcEXcPTS6sOm *low_lat_mem_kobject;
#else
struct kobject *low_lat_mem_kobject;
#endif
EXPORT_SYMBOL(low_lat_mem_kobject);
noinline __used void mi_pre_compact_exit(void)
{
	unregister_trace_android_vh_compact_bypass(do_compact_mem_collect, NULL);
	unregister_trace_android_vh_alloc_pages_reclaim_bypass(do_alloc_pages_big_order, NULL);
	sysfs_remove_group(low_lat_mem_kobject, &pre_compact_attr_group);
	pre_compact_pools_exit();
	pr_info("low_lat_mem: mi_pre_compact_exit\n");
}
/* config of one qcom_dma_heaps pool (only the two fields used here) */
/* how many pages the dma-buf pool may give back, depending on the zone watermarks */
static void mi_smart_refill_pool_shrink(void *data, void *pool_p, gfp_t gfp_mask,
					int nr, int *out)
{
	struct mi_pool_cfg *pool = pool_p;
	struct zone *zone;
	long free;
	int v, hi, lo, mn;

	if (!READ_ONCE(llm_enable))
		return;
	if (!pool->enabled)
		return;
	zone = &NODE_DATA(0)->node_zones[(0x250015U >> ((gfp_mask & 0xf) * 2)) & 3];
	(void)raw_smp_processor_id();		/* the stock has a dead mrs TPIDR_EL1 here */
	hi = high_wmark_pages(zone);
	lo = low_wmark_pages(zone);
	mn = min_wmark_pages(zone);
	free = zone_page_state(zone, NR_FREE_PAGES);
	if (free >= hi)
		v = 0;
	else if (free >= lo)
		v = (nr << pool->order) / 4;
	else if (free < mn)
		v = nr << pool->order;
	else
		v = (nr << pool->order) / 2;
	*out = v;
}
int mi_smart_refill_init(int *dmabuf_pool_nums)
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
EXPORT_SYMBOL_GPL(mi_smart_refill_init);
/* ---- cma pool shrinker ---- */
static struct shrinker pool_shrinker;
static unsigned long cma_pool_shrink_count(struct shrinker *shrinker, struct shrink_control *sc);
static unsigned long cma_pool_shrink_scan(struct shrinker *shrinker, struct shrink_control *sc);
/* noinline keeps the stock `bl` in init_module, but the attribute token must not reach genksyms (export CRC) */
#ifdef __GENKSYMS__
#define LLM_EXPORT_NOINLINE
#else
#define LLM_EXPORT_NOINLINE noinline
#endif
LLM_EXPORT_NOINLINE int cma_pool_init_shrinker(void)
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
EXPORT_SYMBOL_GPL(cma_pool_init_shrinker);
/* ---- cma pfn -> alignment table (100 entries, 24 bytes each) ---- */
noinline __used int get_cma_align(unsigned long key, struct cma_pfn_to_align *out)
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
noinline __used void put_cma_align(unsigned long key, unsigned long val, unsigned int ref)
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
noinline __used void clear_cma_align(unsigned long key)
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
noinline __used int cma_area_init(struct cma *cma, void *data)
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
static atomic_t cur_index;
/*
 * G1w (from the stock disassembly): end of a statistics window: store the largemem / dmabuf counters and the per-area CMA peaks of the
 * window into slot[n] of a ring of 3 samples, n = ++cur_index % 3. The slot is sys_mem_data[idx] for the system scenes (sys bit 0) or
 * the vendor data of current_write_memcg (+8) otherwise.
 */
noinline __used void record_mem_data(int idx, bool sys)
{
	struct sys_mem_slot *slot;
	struct cma_tmp_ent *e, *t;
	char *vend;
	int n, i;

	if (sys & 1) {
		slot = &sys_mem_data[idx];
		if (!slot)
			return;
	} else {
		if (!current_write_memcg)
			return;
		vend = *(char **)((char *)current_write_memcg + 0x8f8);
		if (!vend)
			return;
		slot = (struct sys_mem_slot *)(vend + 8);
		if (!slot)
			return;
	}
	n = atomic_inc_return(&cur_index) % 3;
	mutex_lock(&largemem_lock);
	slot->a[n] = atomic_read(&largemem_data_tmp[0]);
	slot->b[n] = atomic_read(&largemem_data_tmp[1]);
	mutex_unlock(&largemem_lock);
	pr_info("low_lat_mem: low_lat_mem, record largemem_data\n");
	spin_lock(&cma_tmp_lock);
	list_for_each_entry(e, &cma_tmp_list, list) {
		int peak;

		pr_info("low_lat_mem: low_lat_mem, record cma_data start\n");
		peak = READ_ONCE(e->peak);
		spin_lock(&cma_lock);
		list_for_each_entry(t, &slot->list, list)
			if (t->idx == e->idx && t->size == e->size && t->align == e->align)
				break;
		if (&t->list == &slot->list)
			t = NULL;
		spin_unlock(&cma_lock);
		if (t) {
			if (peak > READ_ONCE(t->peak))
				t->peak = peak;
		} else if (peak) {
			t = kmalloc(sizeof(*t), GFP_ATOMIC | __GFP_ZERO);
			if (!t)
				break;
			t->idx = e->idx;
			t->size = e->size;
			t->align = e->align;
			t->peak = peak;
			spin_lock(&cma_lock);
			__list_add(&t->list, slot->list.prev, &slot->list);
			slot->cnt++;
			spin_unlock(&cma_lock);
		}
		pr_info("low_lat_mem: low_lat_mem, cma_tmp_list i: %d, size: %lu, align:%u, nums:%d\n", e->idx, e->size, e->align,
			slot->cnt);
	}
	spin_unlock(&cma_tmp_lock);
	if (slot->arr) {
		mutex_lock(&dmabuf_lock);
		for (i = 0; i < dmabuf_pool_nums; i++) {
			u32 *rec = slot->arr[i];

			if (!rec)
				continue;
			rec[n] = READ_ONCE(dmabuf_data_tmp[i]);
			pr_info("low_lat_mem: low_lat_mem, dmabuf:%d, %d.\n", i, rec[n]);
		}
		mutex_unlock(&dmabuf_lock);
		pr_info("low_lat_mem: low_lat_mem, mi_mem:%d, %d.\n", slot->a[n], slot->b[n]);
	}
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
/* the stock symbol is named list_add_tail (a private non-inline copy taking only the entry) */
static noinline __used void llm_list_add_tail(struct list_head *entry) __asm__("list_add_tail");
static noinline __used void llm_list_add_tail(struct list_head *entry)
{
	__list_add(entry, cma_tmp_list.prev, &cma_tmp_list);
}
noinline __used struct mi_cma *find_mi_cma(struct cma *cma)
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
void mi_cma_alloc_vh(void *data, struct cma *cma, unsigned long count, unsigned int align,
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
void mi_cma_alloc_end(void *data, struct cma *cma, unsigned long pfn_unused,
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
void mi_cma_release_end(void *data, struct cma *cma, const struct page *pages,
			       unsigned long count, bool *bypass)
{
	unsigned long pfn;
	bool done = false;

	if (count >= 1000) {
		struct cma_pfn_to_align ca = {};

		if (READ_ONCE(llm_enable) && !READ_ONCE(predo_pending) &&
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
	}
	*bypass = done;
}
/* give every parked allocation back to its CMA area (called through the shrinker) */
noinline __used int shrink_cma_pool(void)
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
	/* next_zone() is not exported: the stock open-codes the walk over the single node (node_id == 0 filter like pre_do_compact) */
	llm_for_each_zone(zone) {
		if (contig_page_data.node_id || !managed_zone(zone))
			continue;
		free += zone_page_state(zone, NR_FREE_PAGES);
		reserve += min_wmark_pages(zone);
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
	unsigned int nr = sc->nr_to_scan;
	unsigned long freed = 0;

	if (READ_ONCE(llm_enable) && nr)
		freed = shrink_cma_pool();
	return freed;
}
/* ======== G1n: top level enable (the dfx `enable` file uses enable_store above) ======== */
static ssize_t enable_store(struct kobject *kobj, struct kobj_attribute *attr,
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
noinline __used int sys_mem_data_dmabuf_init(void)
{
	int i, j;

	if (!dmabuf_pool_nums)
		return -ENOMEM;
	for (i = 0; i < 4; i++) {
		struct sys_mem_slot *s = &sys_mem_data[i];

		INIT_LIST_HEAD(&s->list);
		s->arr = kzalloc(sizeof(void *) * dmabuf_pool_nums, GFP_KERNEL); /* G1aj: stock __kmalloc(n * 8, GFP_KERNEL | __GFP_ZERO) = 0xdc0, no overflow check */
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
noinline __used void sys_mem_data_dmabuf_free(void)
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

	if (order != 9 && order != 4)
		return;
	if (!count)
		return;
	pool = ((struct pre_compact_pool **)(mci.raw + 0x10))[order];
	if (!pool)
		return;
	do {
		page = alloc_pages(0x021121ca, order);
		if (!page)
			break;
		spin_lock_irqsave(&pool->lock, flags);
		list_add(&page->lru, &pool->items);
		spin_unlock_irqrestore(&pool->lock, flags);
		atomic_inc(&pool->count);
		mod_node_page_state(NODE_DATA(0), (enum node_stat_item)34, (long)(1 << pool->order));
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
noinline __used int mi_pre_compact_init(void)
{
	struct pre_compact_pool *pool;
	struct page *page;
	int ret;

	MCI_U32(0) = 0;				/* strh wzr,[mci] */
	*(u64 *)(mci.raw + 4) = 0;
	*(u64 *)(mci.raw + 0x68) = 0;
	MCI_U32(0x70) = 3;			/* retry_times */
	mci.hist_cnt = 0;
	if (MCI_PTR(0x30) || MCI_PTR(0x58)) {
		pr_warn("low_lat_mem: pre_compact pools already initialized, cleaning up\n");
		pools_exit_body();
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
/* ======== G1n: module init / exit ======== */
extern void mi_smart_refill_dynamic_page_pool_refill(int idx, int avg);	/* qcom_dma_heaps import, prototype from the call site */
noinline __used void pre_do_compact(int order4, int order_ext);
/*
 * The predo worker thread (G1w, from the stock disassembly): sleeps on predo_wq, then for a scene code (301 fling, 302..305 the four
 * scenes) takes the per-scene counters (sys_mem_data[idx] or the vendor slot of current_write_memcg), derives the order-4 / order-9
 * targets (sum of 3 counters, average, clamps 32..64 / 0..4), runs pre_do_compact() and refills every dmabuf pool.
 */
noinline __used int predo_store_call(void *unused)
{
	while (!kthread_should_stop()) {
		struct sys_mem_slot *slot = NULL;
		int val, idx, i, sum1, sum2, avg1, avg2, o4, oe, avg;

		wait_event_interruptible(predo_wq, READ_ONCE(predo_val) || kthread_should_stop());
		if (kthread_should_stop())
			break;
		val = xchg(&predo_val, 0);
		if (val < 1)
			continue;
		if (!mutex_trylock(&pending_lock))
			continue;
		predo_pending = 1;
		{
		char buf[128] = {};

		if (val == 301) {
			char *vend;

			if (!current_write_memcg)
				goto done_keep;
			vend = *(char **)((char *)current_write_memcg + 0x8f8);
			if (!vend)
				goto done;
			slot = (struct sys_mem_slot *)(vend + 8);
			sys_predo = 1;
			pr_info("low_lat_mem: low_lat_mem predo scene: fling.\n");
		} else {
			idx = val - 302;
			if ((unsigned int)idx >= 4)
				goto done;
			slot = &sys_mem_data[idx];
			pr_info("low_lat_mem: low_lat_mem predo scene: %s, code: %d.\n", scene_name[idx], val);
			sys_predo = 1;
		}
		if (!slot || READ_ONCE(calculate_start))
			goto done;
		mutex_lock(&largemem_lock);
		sum1 = slot->a[0] + slot->a[1] + slot->a[2];
		avg1 = sum1 / 3;
		mutex_unlock(&largemem_lock);
		mutex_lock(&largemem_lock);
		sum2 = slot->b[0] + slot->b[1] + slot->b[2];
		avg2 = sum2 / 3;
		mutex_unlock(&largemem_lock);
		o4 = sum1 > 191 ? 64 : max(avg1, 32);
		oe = sum2 > 11 ? 4 : max(avg2, 0);
		pre_do_compact(o4, oe);
		pr_info("low_lat_mem: low_lat_mem precompact.\n");
		if (!slot->arr)
			goto done;
		mutex_lock(&dmabuf_lock);
		for (i = 0; i < dmabuf_pool_nums; i++) {
			int *rec = slot->arr[i];	/* signed in the stock (smull by 3) */
			u64 t0;

			avg = (rec[0] + rec[1] + rec[2] + 3) / 3;
			t0 = sched_clock();
			mi_smart_refill_dynamic_page_pool_refill(i, avg);
			snprintf(buf, sizeof(buf), "I|%d|i:%d avg:%d refill_duration:%lu ms\n", current->tgid, i, avg,
				 (unsigned long)((sched_clock() - t0) / 1000000));
			tracing_mark_write(buf);
			pr_info("low_lat_mem: low_lat_mem prdo dmabuf: %d, avg: %d.\n", val, avg);
		}
		mutex_unlock(&dmabuf_lock);
		pr_info("low_lat_mem: low_lat_mem prefill.\n");
		pr_info("low_lat_mem: low_lat_mem premigrate.\n");
done:
		sys_predo = 0;
		}
done_keep:
		predo_pending = 0;
		mutex_unlock(&pending_lock);
	}
	return 0;
}
static struct task_struct *predo_worker_task;
static struct kobj_attribute start_attr = __ATTR(start, 0664, NULL, start_store);
static struct kobj_attribute predo_attr = __ATTR(predo, 0664, NULL, predo_store);
static struct kobj_attribute shrink_attr = __ATTR(shrink, 0664, NULL, shrink_store);
static struct kobj_attribute enable_attr = __ATTR(enable, 0664, NULL, enable_store);
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
	mi_smart_refill_init(&dmabuf_pool_nums);
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
/* keep every address-taken callback alive (gives them the KCFI type word like the stock) */
static const void *const mi_low_lat_mem_callbacks[] __used = { start_store, shrink_store, predo_store, enable_store, retry_times_show, retry_times_store, target_count_show, curr_collected_count_show, mi_try_alloc_pages_gfp, mi_smart_refill_alloc, mi_smart_refill_extend_alloc, mi_memcg_alloc, mi_memcg_free, memcg_read_appname, memcg_read_version, memcg_write_appname, memcg_write_version, memcg_write_start, predo_trigger, largemem_data_tmp_init, dmabuf_data_tmp_init, do_compact_mem_collect, do_alloc_pages_big_order, llm_prep_compound_page };
/* ======== G1r: fragmentation score, sys mem snapshot and the pre-compact fault record (from the stock disassembly) ======== */
/* fragmentation score of the system for allocation order @order: per zone
 * frag = (free - suitable << order) * 100 / free, weighted by the share of the zone in its node */
static __used unsigned int calculate_fragmentation_score(unsigned int order)
{
	unsigned int score = 0;
	struct zone *zone;

	_Static_assert(offsetof(struct pglist_data, node_id) == 0x2100, "node_id");
	llm_for_each_zone(zone) {
		if (contig_page_data.node_id || !populated_zone(zone))
			continue;
		unsigned long free = 0, suitable = 0;
		unsigned long frag = 0;
		int o;

		unsigned long nr[11];

#pragma clang loop unroll(full)
		for (o = 0; o < 11; o++)
			nr[o] = zone->free_area[o].nr_free;
#pragma clang loop unroll(full)
		for (o = 0; o < 11; o++)
			free += nr[o] << o;
#pragma clang loop unroll(full)
		/* G1ai: the stock tests order > o only for o = 4..8 (5 csel); o = 9 and 10 are unconditional (callers pass 4 or 9 only) */
		for (o = 4; o < 9; o++)
			suitable += (u64)(order <= o) * (nr[o] << ((o - order) & 63));
		suitable += nr[9] << (9 - order);
		suitable += nr[10] << (10 - order);
		if (free)
			frag = (u32)((free - (suitable << order)) * 100 / (u32)free);
		score += frag * zone->present_pages / (zone->zone_pgdat->node_present_pages + 1);
	}
	return score;
}
static struct pre_compact_fault_data fault_data;
static __always_inline void dump_trace_and_send_event(u64 arg, u32 type, u32 a, u32 b, u32 c)
{
	if (cmpxchg(&mem_fault_reporting, 0, 1))
		return;
	fill_pre_compact_fault_data(&fault_data, arg, type, a, b, c);
	pr_warn_ratelimited("low_lat_mem: mem_fault_events is not enabled\n");
}
/* percentage buckets of the pool fill level: 5 u32 counters at @base: <=20, <=40, <=60, <=80, rest */
static __always_inline void dfx_fill_bucket(unsigned int pct, int base)
{
	/* the stock keeps one atomic increment per bucket (no merged offset) */
	if (pct <= 20)
		atomic_inc(DFX_ATOMIC(base));
	else if (pct <= 40)
		atomic_inc(DFX_ATOMIC(base + 4));
	else if (pct <= 60)
		atomic_inc(DFX_ATOMIC(base + 8));
	else if (pct <= 80)
		atomic_inc(DFX_ATOMIC(base + 12));
	else
		atomic_inc(DFX_ATOMIC(base + 16));
}
noinline __used void pre_do_compact(int order4, int order_ext)
{
	char buf[256] = {};
	unsigned long free_sum = 0, high_sum = 0;
	u64 ttfp = 0, total = 0, t0;
	unsigned int freq_b, freq_a, freq_avg = 0;
	unsigned int pct4, pct9;
	struct zone *zone;
	u32 max_time;
	int retry = 0;

	if (!(order4 | order_ext)) {
		pr_info("low_lat_mem: target order4 & %d all 0\n", 0);
		return;
	}
	*(u16 *)mci.raw = 1;			/* strh #1: enable = 1, enough = 0 */
	MCI_U32(4) = order4;
	MCI_U32(8) = order_ext;
	llm_for_each_zone(zone) {
		if (!*(int *)((u8 *)&contig_page_data + 0x2100) && managed_zone(zone)) {
			free_sum += zone_page_state(zone, NR_FREE_PAGES);
			high_sum += high_wmark_pages(zone);
		}
	}
	if (free_sum < high_sum) {
		freq_b = cpufreq_get(raw_smp_processor_id());
		t0 = sched_clock();
		if (try_to_free_pages_symbol)
			((unsigned long (*)(struct zonelist *, int, gfp_t, nodemask_t *))try_to_free_pages_symbol)(
				(struct zonelist *)((u8 *)&contig_page_data + 0x2080), 9, 0xc00, NULL);
		ttfp = sched_clock() - t0;
		freq_a = cpufreq_get(raw_smp_processor_id());
		atomic_inc(&DFX_ABKT(0x8b4, get_time_bucket(ttfp)));
		DFX_U64(0x8c8) += ttfp;
		if (ttfp > DFX_U32(0x8d0))
			DFX_U32(0x8d0) = ttfp;
		freq_avg = (freq_a + freq_b) / 2;
		DFX_U64(0x920) += freq_avg;
		DFX_U32(0x928)++;
		if (ttfp > (u64)DFX_U32(0x998) * 1000000) {
			printk(KERN_INFO "low_lat_mem: try_to_free_pages_timeout time:%lu ms freq_avg:%u t:%d-%d\n",
			       (unsigned long)div_u64(ttfp, 1000000), freq_avg, order4, order_ext);
			snprintf(buf, 256, "I|%d|try_to_free_pages_timeout time:%lu ms freq_avg:%u t:%d-%d\n", current->tgid,
				 (unsigned long)div_u64(ttfp, 1000000), freq_avg, order4, order_ext);
			tracing_mark_write(buf);
			if (!check_report_limit())
				dump_trace_and_send_event(ttfp, 4, 0, 0, 0);
		}
	}
	MCI_U32(0x68) = calculate_fragmentation_score(9);
	for (;;) {
		u64 pages = 0, dt;
		unsigned int f4, f9, f4a, f9a, c4, c9, fr1, fr2;
		struct pc_rec *rc;
		u32 idx;

		llm_for_each_zone(zone)
			if (managed_zone(zone))
				pages += zone_page_state(zone, NR_FREE_PAGES);
		f4 = calculate_fragmentation_score(4);
		f9 = calculate_fragmentation_score(9);
		c4 = MCI_POOL_COUNT(0x30);
		c9 = MCI_POOL_COUNT(0x58);
		fr1 = cpufreq_get(raw_smp_processor_id());
		t0 = sched_clock();
		compact_node_async(0);
		dt = sched_clock() - t0;
		fr2 = cpufreq_get(raw_smp_processor_id());
		total += dt;
		f4a = calculate_fragmentation_score(4);
		f9a = calculate_fragmentation_score(9);
		idx = mci.hist_cnt;
		mci.hist_cnt = idx + 1;
		rc = &mci.hist[idx % 30];
		rc->pages = pages;
		rc->f4 = f4;
		rc->f9 = f9;
		rc->f4a = f4a;
		rc->f9a = f9a;
		rc->t4 = MCI_U32(4);
		rc->t9 = MCI_U32(8);
		rc->c4 = c4;
		rc->c9 = c9;
		rc->n4 = MCI_POOL_COUNT(0x30);
		rc->n9 = MCI_POOL_COUNT(0x58);
		rc->success = mci.raw[1];
		rc->time = ktime_get_real();
		rc->dur = dt;
		MCI_U32(0x6c) = calculate_fragmentation_score(9);
		freq_avg = (fr1 + fr2) / 2;
		if (mci.raw[1])
			break;
		if (++retry == MCI_U32(0x70)) {
			unsigned int before, after;

			printk(KERN_INFO "low_lat_mem: %s retry times > %d, Notify memory recycling\n", __func__, retry);
			before = MCI_U32(0x68);
			after = MCI_U32(0x6c);
			if (before > after) {
				unsigned int improved = (before - after) * 100 / (before + 1);

				printk(KERN_INFO "low_lat_mem: %s fragmentation improved by %u%%\n", __func__, improved);
				if (improved < 20)
					printk(KERN_INFO "low_lat_mem: %s fragment improve < 20%%, trigger recycling\n", __func__);
			} else {
				printk(KERN_INFO "low_lat_mem: %s fragmentation not improved or worsened\n", __func__);
			}
			break;
		}
		msleep(100);
	}
	pct4 = pct9 = 100;
	if (MCI_U32(4))
		pct4 = MCI_POOL_COUNT(0x30) * 100 / MCI_U32(4);
	if (MCI_U32(8))
		pct9 = MCI_POOL_COUNT(0x58) * 100 / MCI_U32(8);
	if (total) {
		atomic_inc(&DFX_ABKT(0x8d4, get_time_bucket(total)));
		DFX_U64(0x8e8) += total;
		max_time = DFX_U32(0x8f0);
		if (total > max_time) {
			DFX_U32(0x8f0) = total;
			max_time = total;
		}
		DFX_U64(0x930) += freq_avg;
		DFX_U32(0x938)++;
		if (total > (u64)DFX_U32(0x99c) * 1000000) {
			printk(KERN_INFO "low_lat_mem: pre_compact_timeout time:%lu ms retry:%d frag_b:%u frag_a:%u t:%d-%d c:%d-%d\n",
			       (unsigned long)(max_time / 1000000), retry, MCI_U32(0x68), MCI_U32(0x6c), order4, order_ext, MCI_POOL_COUNT(0x30),
			       MCI_POOL_COUNT(0x58));
			snprintf(buf, 256, "I|%d|pre_compact_timeout time:%lu ms retry:%d frag_b:%u frag_a:%u t:%d-%d c:%d-%d\n",
				 current->tgid, (unsigned long)div_u64(total, 1000000), retry, MCI_U32(0x68), MCI_U32(0x6c), order4,
				 order_ext, MCI_POOL_COUNT(0x30), MCI_POOL_COUNT(0x58));
			tracing_mark_write(buf);
			if (!check_report_limit())
				dump_trace_and_send_event(total, 5, MCI_U32(0x68), MCI_U32(0x6c), retry);
		}
	}
	dfx_fill_bucket(pct4, 0x8f4);
	dfx_fill_bucket(pct9, 0x908);
	printk(KERN_INFO "low_lat_mem: %s target order4:%u order%d:%u current order4:%u order%d:%u\n", __func__, MCI_U32(4), 9,
	       MCI_U32(8), MCI_POOL_COUNT(0x30), 9, MCI_POOL_COUNT(0x58));
	snprintf(buf, 256,
		 "I|%d|target order4:%u order%d:%u current order4:%u order%d:%u async_compact time:%lu ms try_to_free_pages time:%lu ms\n",
		 current->tgid, MCI_U32(4), 9, MCI_U32(8), MCI_POOL_COUNT(0x30), 9, MCI_POOL_COUNT(0x58),
		 (unsigned long)div_u64(total, 1000000), (unsigned long)div_u64(ttfp, 1000000));
	tracing_mark_write(buf);
	mci.raw[0] = 0;
}
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(MINIDUMP);
