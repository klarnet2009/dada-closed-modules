#include "llm_common.h"

static u8 dfx_enable;
static noinline __used void dfx_enable_change(int enable);
static noinline __used void dfx_info_reset(void)
{
	struct list_head *pos;
	int t, k, j, i;

#define Z32(off)	WRITE_ONCE(*(u32 *)(dfx.raw + (off)), 0)
#define Z64(off)	WRITE_ONCE(*(u64 *)(dfx.raw + (off)), 0)
	spin_lock((spinlock_t *)(dfx.raw + 0x8b0));
	for (t = 0; t < 5; t++)
#pragma clang loop unroll(disable)
		for (k = 0; k < 3; k++)
#pragma clang loop unroll(full)
			for (j = 0; j < 4; j++) {
				int n = k * 4 + j;

#pragma clang loop unroll(full)
				for (i = 0; i < 5; i++)
					Z32(0x12c + t * 0xf0 + (n * 5 + i) * 4);
				Z64(0x5e0 + t * 0x60 + n * 8);
				Z32(0x7c0 + t * 0x30 + n * 4);
			}
	for (i = 0; i < 5; i++) {
		Z32(0x8b4 + i * 4);
		Z32(0x8b4 + 0x20 + i * 4);
	}
	Z64(0x8b4 + 0x14);
	Z32(0x8b4 + 0x1c);
	Z64(0x8b4 + 0x34);
	Z32(0x8b4 + 0x3c);
	Z64(0x8b4 + 0x6c);
	Z32(0x8b4 + 0x74);
	Z64(0x8b4 + 0x7c);
	Z32(0x8b4 + 0x84);
	for (i = 0; i < 5; i++) {
		Z32(0x8b4 + 0x40 + i * 4);
		Z32(0x8b4 + 0x54 + i * 4);
	}
	cma_alloc_from_pool_cnt = 0;
	cma_alloc_from_orig_cnt = 0;
	Z64(0x8b4 + 0x8c);
	Z64(0x8b4 + 0x94);
	Z64(0x8b4 + 0x9c);
	Z64(0x8b4 + 0xa4);
	Z64(0x8b4 + 0xbc);
	Z64(0x8b4 + 0xc4);
	list_for_each(pos, &mi_smart_refill_pools) {
		u64 *st = (u64 *)((char *)pos + 0x18);

		st[0] = 0;
		st[1] = 0;
	}
	spin_unlock((spinlock_t *)(dfx.raw + 0x8b0));
#undef Z32
#undef Z64
}
noinline __used int tracing_mark_write(const char *buf)
{
	trace_puts(buf);
	return 0;
}
/* ---- small helpers ---- */
noinline __used int get_scene(int code)
{
	unsigned int i = code - 101;

	return i < 5 ? i : -1;
}
__used bool check_report_limit(void)
{
	static struct ratelimit_state hour_limit = RATELIMIT_STATE_INIT("hour_limit", 3600 * HZ, 5);
	static struct ratelimit_state day_limit = RATELIMIT_STATE_INIT("day_limit", 86400 * HZ, 25);

	if (__builtin_expect_with_probability(___ratelimit(&hour_limit, "check_report_limit"), 1, 0.9))
		return !___ratelimit(&day_limit, "check_report_limit");
	return true;
}
__used int get_time_bucket(u64 ns)
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
void dfx_enable_control(int enable);
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
/* ---- allocation latency start handlers (tracepoint probes, all identical) ---- */
START_HANDLER(cma_alloc_start_handler)
START_HANDLER(dma_heap_buffer_alloc_start_handler)
START_HANDLER(mm_direct_reclaim_start_handler)
START_HANDLER(alloc_pages_slowpath_start_handler)

/* ---- end handlers: real code further down (after the fault record helpers) ---- */
static void cma_alloc_end_handler(void *data, unsigned long long stime, unsigned long count);
static void dma_heap_buffer_alloc_end_handler(void *data, unsigned long long stime, size_t len, struct dma_buf *dma_buf);
static void mm_direct_reclaim_end_handler(void *data, unsigned int order, u64 stime);
static void alloc_pages_slowpath_end_handler(void *data, gfp_t *gfp_mask, unsigned int order, unsigned long alloc_start,
					     u64 stime, unsigned long did_some_progress, unsigned long pages_reclaimed,
					     int retry_loop_count);
/* dfx_enable holds the "disabled" state: enable_show prints its inverse */
static noinline __used void dfx_enable_change(int enable)
{
	int ret;

	if (!enable) {
		unregister_trace_android_vh_cma_alloc_lat_start(cma_alloc_start_handler, NULL);
		unregister_trace_android_vh_cma_alloc_lat_end(cma_alloc_end_handler, NULL);
		unregister_trace_android_vh_dma_heap_buffer_alloc_lat_start(dma_heap_buffer_alloc_start_handler, NULL);
		unregister_trace_android_vh_dma_heap_buffer_alloc_lat_end(dma_heap_buffer_alloc_end_handler, NULL);
		unregister_trace_android_vh_mm_direct_reclaim_start(mm_direct_reclaim_start_handler, NULL);
		unregister_trace_android_vh_mm_direct_reclaim_end(mm_direct_reclaim_end_handler, NULL);
		unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_pages_slowpath_start_handler, NULL);
		unregister_trace_android_vh_alloc_pages_slowpath_end(alloc_pages_slowpath_end_handler, NULL);
		return;
	}
	ret = register_trace_android_vh_cma_alloc_lat_start(cma_alloc_start_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register cma_alloc_lat_start failed! ret=%d\n", ret);
		goto unreg0;
	}
	ret = register_trace_android_vh_cma_alloc_lat_end(cma_alloc_end_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register cma_alloc_lat_end failed! ret=%d\n", ret);
		goto unreg1;
	}
	ret = register_trace_android_vh_dma_heap_buffer_alloc_lat_start(dma_heap_buffer_alloc_start_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register dma_heap_buffer_alloc_lat_start failed! ret=%d\n", ret);
		goto unreg2;
	}
	ret = register_trace_android_vh_dma_heap_buffer_alloc_lat_end(dma_heap_buffer_alloc_end_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register dma_heap_buffer_alloc_lat_end failed! ret=%d\n", ret);
		goto unreg3;
	}
	ret = register_trace_android_vh_mm_direct_reclaim_start(mm_direct_reclaim_start_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register mm_direct_reclaim_start failed! ret=%d\n", ret);
		goto unreg4;
	}
	ret = register_trace_android_vh_mm_direct_reclaim_end(mm_direct_reclaim_end_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register mm_direct_reclaim_end failed! ret=%d\n", ret);
		goto unreg5;
	}
	ret = register_trace_android_vh_alloc_pages_slowpath_start(alloc_pages_slowpath_start_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register alloc_pages_slowpath_start failed! ret=%d\n", ret);
		goto unreg6;
	}
	ret = register_trace_android_vh_alloc_pages_slowpath_end(alloc_pages_slowpath_end_handler, NULL);
	if (ret) {
		pr_err("low_lat_mem: register alloc_pages_slowpath_end failed! ret=%d\n", ret);
		goto unreg7;
	}
	return;
unreg7:
	unregister_trace_android_vh_alloc_pages_slowpath_start(alloc_pages_slowpath_start_handler, NULL);
unreg6:
	unregister_trace_android_vh_mm_direct_reclaim_end(mm_direct_reclaim_end_handler, NULL);
unreg5:
	unregister_trace_android_vh_mm_direct_reclaim_start(mm_direct_reclaim_start_handler, NULL);
unreg4:
	unregister_trace_android_vh_dma_heap_buffer_alloc_lat_end(dma_heap_buffer_alloc_end_handler, NULL);
unreg3:
	unregister_trace_android_vh_dma_heap_buffer_alloc_lat_start(dma_heap_buffer_alloc_start_handler, NULL);
unreg2:
	unregister_trace_android_vh_cma_alloc_lat_end(cma_alloc_end_handler, NULL);
unreg1:
	unregister_trace_android_vh_cma_alloc_lat_start(cma_alloc_start_handler, NULL);
unreg0:
	return;
}
noinline __used void dfx_enable_control(int enable)
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
/* dfx statistics: u32 bucket counters at 0x12c [scene 0xf0][proc 0x50][type 0x14][bucket 4], u64 totals at 0x5e0 [0x60][0x20][8],
 * u32 max latency (ns) at 0x7c0 [0x30][0x10][4]; names: alloc types at dfx+0 (20 bytes each), process types at dfx+0xf0 */
static noinline __used ssize_t scene_alloc_info_show_common(char *buf, unsigned int scene)
{
	int total[3][4] = {};
	u64 avg[3][4] = {};
	int proc, t;
	ssize_t len = 0;

	for (proc = 0; proc < 3; proc++) {
#pragma clang loop unroll(disable)
		for (t = 0; t < 4; t++) {
			int cnt = 0, b;

			/* the stock reads the counters with acquire loads (ldar) */
			for (b = 0; b < 5; b++)
				cnt += smp_load_acquire(&dfx.cnt[scene][proc][t][b]);
			total[proc][t] = cnt;
			if (cnt >= 1)
				avg[proc][t] = dfx.tot[scene][proc][t] / cnt / 1000000;
		}
	}
	for (proc = 0; proc < 3; proc++) {
		len += sysfs_emit_at(buf, len, "===proc_type: %s===\n", (char *)dfx.raw + 0xf0 + proc * 20);
#pragma clang loop unroll(disable)
		for (t = 0; t < 4; t++) {
			char *name = (char *)dfx.raw + t * 20;
			u32 *c = dfx.cnt[scene][proc][t];

			len += sysfs_emit_at(buf, len, "%s total:%d\n", name, total[proc][t]);
			len += sysfs_emit_at(buf, len, "%s_%llu_%llu  %d\n", name, 0ULL, 8ULL, smp_load_acquire(&c[0]));
			len += sysfs_emit_at(buf, len, "%s_%llu_%llu  %d\n", name, 8ULL, 64ULL, smp_load_acquire(&c[1]));
			len += sysfs_emit_at(buf, len, "%s_%llu_%llu  %d\n", name, 64ULL, 256ULL, smp_load_acquire(&c[2]));
			len += sysfs_emit_at(buf, len, "%s_%llu_%llu  %d\n", name, 256ULL, 1024ULL, smp_load_acquire(&c[3]));
			len += sysfs_emit_at(buf, len, "%s_1024_INF  %d\n", name, smp_load_acquire(&c[4]));
			len += sysfs_emit_at(buf, len, "avg_ms: %llu\n", avg[proc][t]);
			len += sysfs_emit_at(buf, len, "max_ms: %ld\n\n", (long)(dfx.max[scene][proc][t] / 1000000));
		}
	}
	return len;
}
SCENE_SHOW(scene_fling_show, 0)
SCENE_SHOW(scene_unlock_show, 1)
SCENE_SHOW(scene_cold_start_show, 2)
SCENE_SHOW(scene_camera_show, 3)
SCENE_SHOW(scene_game_show, 4)

/* ---- pool hit rate ---- */
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
#include "llm_show.h"
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
noinline __used int mi_dfx_exit(void)
{
	sysfs_remove_group(low_lat_mem_kobject, &dfx_attr_group);
	llm_dfx_unregister_hooks();
	dfx_info_reset();
	return 0;
}
#define DFX_NAME(off, str)	strscpy((char *)dfx.raw + (off), (str), 20)
noinline __used int mi_dfx_init(void)
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
	/* G1ai: the stock clears the typed arrays [scene 5][proc 3][type 4][bucket 5] with loops (t, k not unrolled), the spinlock at +0x8b0 and then
	   field by field (no memset); the gap +0x8f4..+0x91f and the cma pool counters are NOT touched here (they are only cleared by dfx_info_reset) */
	{
		int t, k, j, i;

		spin_lock_init((spinlock_t *)(dfx.raw + 0x8b0));
		for (t = 0; t < 5; t++)
#pragma clang loop unroll(disable)
			for (k = 0; k < 3; k++)
#pragma clang loop unroll(full)
				for (j = 0; j < 4; j++) {
#pragma clang loop unroll(full)
					for (i = 0; i < 5; i++)
						WRITE_ONCE(dfx.cnt[t][k][j][i], 0);
					WRITE_ONCE(dfx.tot[t][k][j], 0);
					WRITE_ONCE(dfx.max[t][k][j], 0);
				}
		for (i = 0; i < 5; i++) {
			WRITE_ONCE(DFX_U32(0x8b4 + i * 4), 0);
			WRITE_ONCE(DFX_U32(0x8b4 + 0x20 + i * 4), 0);
		}
		WRITE_ONCE(DFX_U64(0x8b4 + 0x14), 0);
		WRITE_ONCE(DFX_U32(0x8b4 + 0x1c), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x34), 0);
		WRITE_ONCE(DFX_U32(0x8b4 + 0x3c), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x6c), 0);
		WRITE_ONCE(DFX_U32(0x8b4 + 0x74), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x7c), 0);
		WRITE_ONCE(DFX_U32(0x8b4 + 0x84), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x8c), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x94), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0x9c), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0xa4), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0xac), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0xb4), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0xbc), 0);
		WRITE_ONCE(DFX_U64(0x8b4 + 0xc4), 0);
	}
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
noinline __used void fill_sys_mem_info(struct sys_mem_info *out)
{
	struct sysinfo si = {};
	long cached;

	si_meminfo(&si);
	si_swapinfo(&si);
	cached = max_t(long, global_node_page_state(NR_FILE_PAGES), 0) -
		 (max_t(long, global_node_page_state(NR_SWAPCACHE), 0) + si.bufferram);
	out->freeram = (u32)si.freeram << (PAGE_SHIFT - 10);
	out->bufferram = (u32)si.bufferram << (PAGE_SHIFT - 10);
	out->cached = KB(cached);
	out->active_anon = KB(global_node_page_state(NR_ACTIVE_ANON));
	out->inactive_anon = KB(global_node_page_state(NR_INACTIVE_ANON));
	out->active_file = KB(global_node_page_state(NR_ACTIVE_FILE));
	out->inactive_file = KB(global_node_page_state(NR_INACTIVE_FILE));
	out->freeswap = (u32)si.freeswap << (PAGE_SHIFT - 10);
	out->anon_mapped = KB(global_node_page_state(NR_ANON_MAPPED));
	out->file_mapped = KB(global_node_page_state(NR_FILE_MAPPED));
	out->free_cma = KB(global_zone_page_state(NR_FREE_CMA_PAGES));
	out->slab_reclaimable = KB(global_node_page_state(NR_SLAB_RECLAIMABLE_B));
	out->slab_unreclaimable = KB(global_node_page_state(NR_SLAB_UNRECLAIMABLE_B));
}
noinline __used void fill_pre_compact_fault_data(struct pre_compact_fault_data *f, u64 elapsed_ns, u32 type, u32 frag_before,
						 u32 frag_after, u32 compact_times)
{
	f->ts = ktime_get_real();
	f->elapsed_ns = elapsed_ns;
	f->type = type;
	if (type == 5) {
		u32 cnt;

		f->frag_before = frag_before;
		f->frag_after = frag_after;
		f->compact_times = compact_times;
		cnt = mci.hist_cnt;
		if (cnt) {
			struct pc_rec *rec = &mci.hist[(cnt - 1) % 30];

			f->target_count_4 = rec->t4;
			f->compact_count_4 = rec->n4;
			f->target_count_ext = rec->t9;
			f->compact_count_ext = rec->n9;
		}
	}
	fill_sys_mem_info(&f->sys_info);
}
noinline __used void fill_mem_alloc_fault_data(struct mem_alloc_fault_data *f, u64 elapsed_ns, u32 type, u32 order, u32 len,
					      u32 scene, u32 proc_type, const char *app_name, const char *app_ver)
{
	u32 cnt;

	f->ts = ktime_get_real();
	f->elapsed_ns = elapsed_ns;
	f->type = type;
	f->order = order;
	f->len = len;
	f->scene = scene;
	f->proc_type = proc_type;
	f->prio = current->prio;
	if (app_name)
		strscpy(f->app_name, app_name, sizeof(f->app_name));
	if (app_ver)
		strscpy(f->app_ver, app_ver, sizeof(f->app_ver));
	cnt = mci.hist_cnt;
	if (cnt) {
		struct pc_rec *rec = &mci.hist[(cnt - 1) % 30];

		f->target_count_4 = rec->t4;
		f->compact_count_4 = rec->n4;
		f->curr_count_4 = smp_load_acquire((u32 *)((u8 *)MCI_PTR(0x30) + 0x18));
		f->target_count_ext = rec->t9;
		f->compact_count_ext = rec->n9;
		f->curr_count_ext = smp_load_acquire((u32 *)((u8 *)MCI_PTR(0x58) + 0x18));
	}
	if (type <= 1) {
		struct list_head *pos;

		mi_smart_refill_update();
		list_for_each(pos, &mi_smart_refill_pools) {
			struct pool_type_ent *e = container_of(pos, struct pool_type_ent, list);

			if (e->type == 0)
				f->dma_curr_count_ext = e->val;
			else if (e->type == 1)
				f->dma_curr_count_4 = e->val;
		}
	}
	fill_sys_mem_info(&f->sys_info);
}
static const char * const mem_type_str[7] = { "cma", "dma", "order4", "order9", "free_time", "async_compact_time", "order0" };
static const char * const proc_type_str[3] = { "qos", "main", "bg" };
noinline __used void pre_compact_misight_event_cb(void *data, int error_code)
{
	struct pre_compact_fault_data *f = data;
	struct cdev_tevent *ev;

	if (!f) {
		printk(KERN_ERR "low_lat_mem: Invalid fault_data in callback\n");
		goto reset;
	}
	printk(KERN_INFO "low_lat_mem: dumpTrace completed with error_code=%d, sending MiSight event\n", error_code);
	ev = cdev_tevent_alloc(0x35c387ad);
	if (unlikely(!ev)) {
		printk(KERN_ERR "low_lat_mem: Failed to allocate MiSight event\n");
		goto reset;
	}
	if (TEV_INT(ev, "MemTime", f->elapsed_ns / 1000000) ||
	    TEV_STR(ev, "MemType", (u32)f->type <= 6 ? mem_type_str[f->type] : "unknown") ||
	    TEV_INT(ev, "TargetCnt4", f->target_count_4) || TEV_INT(ev, "CompactCnt4", f->compact_count_4) ||
	    TEV_INT(ev, "TargetCnt9", f->target_count_ext) || TEV_INT(ev, "CompactCnt9", f->compact_count_ext) ||
	    TEV_INT(ev, "FragStart", f->frag_before) || TEV_INT(ev, "FragEnd", f->frag_after) ||
	    TEV_INT(ev, "CompactTimes", f->compact_times) || TEV_INT(ev, "Free", f->sys_info.freeram) ||
	    TEV_INT(ev, "Buff", f->sys_info.bufferram) || TEV_INT(ev, "Cache", f->sys_info.cached) ||
	    TEV_INT(ev, "ActiveAnon", f->sys_info.active_anon) || TEV_INT(ev, "InactiveAnon", f->sys_info.inactive_anon) ||
	    TEV_INT(ev, "ActiveFile", f->sys_info.active_file) || TEV_INT(ev, "InactiveFile", f->sys_info.inactive_file) ||
	    TEV_INT(ev, "FreeSwap", f->sys_info.freeswap) || TEV_INT(ev, "AnonMapped", f->sys_info.anon_mapped) ||
	    TEV_INT(ev, "FileMapped", f->sys_info.file_mapped) || TEV_INT(ev, "CmaFree", f->sys_info.free_cma) ||
	    TEV_INT(ev, "Sreclaimable", f->sys_info.slab_reclaimable) ||
	    TEV_INT(ev, "Sunreclaim", f->sys_info.slab_unreclaimable)) {
		cdev_tevent_destroy(ev);
		goto reset;
	}
	cdev_tevent_write(ev);
	printk(KERN_INFO "low_lat_mem: MiSight event sent successfully\n");
	cdev_tevent_destroy(ev);
reset:
	mem_fault_reporting = 0;
}
noinline __used void mem_alloc_misight_event_cb(void *data, int error_code)
{
	struct mem_alloc_fault_data *f = data;
	struct cdev_tevent *ev;

	if (!f) {
		printk(KERN_ERR "low_lat_mem: Invalid fault_data in callback\n");
		goto reset;
	}
	printk(KERN_INFO "low_lat_mem: dumpTrace completed with error_code=%d, sending MiSight event\n", error_code);
	ev = cdev_tevent_alloc(0x35c387ad);
	if (unlikely(!ev)) {
		printk(KERN_ERR "low_lat_mem: Failed to allocate MiSight event\n");
		goto reset;
	}
	if (TEV_INT(ev, "MemTime", f->elapsed_ns / 1000000) ||
	    TEV_STR(ev, "MemType", (u32)f->type <= 6 ? mem_type_str[f->type] : "unknown") ||
	    TEV_INT(ev, "MemLen", f->len) || TEV_INT(ev, "Scene", f->scene) || TEV_STR(ev, "AppName", f->app_name) ||
	    TEV_STR(ev, "AppVersion", f->app_ver) || TEV_INT(ev, "Prio", f->prio) ||
	    TEV_STR(ev, "ProcType", (u32)f->proc_type <= 2 ? proc_type_str[f->proc_type] : "unknown") ||
	    TEV_INT(ev, "TargetCnt4", f->target_count_4) || TEV_INT(ev, "CompactCnt4", f->compact_count_4) ||
	    TEV_INT(ev, "CurrCnt4", f->curr_count_4) || TEV_INT(ev, "TargetCnt9", f->target_count_ext) ||
	    TEV_INT(ev, "CompactCnt9", f->compact_count_ext) || TEV_INT(ev, "CurrCnt9", f->curr_count_ext) ||
	    TEV_INT(ev, "DmaCnt4", f->dma_curr_count_4) || TEV_INT(ev, "DmaCnt9", f->dma_curr_count_ext) ||
	    TEV_INT(ev, "Free", f->sys_info.freeram) || TEV_INT(ev, "Buff", f->sys_info.bufferram) ||
	    TEV_INT(ev, "Cache", f->sys_info.cached) || TEV_INT(ev, "ActiveAnon", f->sys_info.active_anon) ||
	    TEV_INT(ev, "InactiveAnon", f->sys_info.inactive_anon) || TEV_INT(ev, "ActiveFile", f->sys_info.active_file) ||
	    TEV_INT(ev, "InactiveFile", f->sys_info.inactive_file) || TEV_INT(ev, "FreeSwap", f->sys_info.freeswap) ||
	    TEV_INT(ev, "AnonMapped", f->sys_info.anon_mapped) || TEV_INT(ev, "FileMapped", f->sys_info.file_mapped) ||
	    TEV_INT(ev, "CmaFree", f->sys_info.free_cma) || TEV_INT(ev, "Sreclaimable", f->sys_info.slab_reclaimable) ||
	    TEV_INT(ev, "Sunreclaim", f->sys_info.slab_unreclaimable)) {
		cdev_tevent_destroy(ev);
		goto reset;
	}
	cdev_tevent_write(ev);
	printk(KERN_INFO "low_lat_mem: MiSight event sent successfully\n");
	cdev_tevent_destroy(ev);
reset:
	mem_fault_reporting = 0;
}
static const void *const llm_dfx_callbacks[] __used = { enable_show, reset_store, cma_alloc_start_handler, dma_heap_buffer_alloc_start_handler, mm_direct_reclaim_start_handler, alloc_pages_slowpath_start_handler, get_time_bucket, dfx_enable_control, scene_fling_show, scene_unlock_show, scene_cold_start_show, scene_camera_show, scene_game_show, pool_hit_rate_show, check_report_limit, enable_store };
#include "llm_end_handlers.h"
