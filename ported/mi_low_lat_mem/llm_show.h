/* SPDX-License-Identifier: GPL-2.0 */
/* mi_low_lat_mem: dfx sysfs histograms pre_compact_time / pre_compact_success_rate (G1x, from the OS4 stock disassembly; not byte-identical). */

#define LLM_PCT(cnt, tot)	((tot) >= 1 ? (cnt) * 100 / (tot) : 0)
#define DFXC(off)		atomic_read(DFX_ATOMIC(off))
/*
 * G1aj: the stock loads every bucket counter twice with ldar: once for the sum and once per output line, where the value is
 * reused for the count and for the percentage (20 ldar per function, ours had 30 with the macro expanded three times).
 */
#define LLM_LINE(len, fmt, off, tot, ...) do { \
		int v_ = DFXC(off); \
		len += sysfs_emit_at(buf, len, fmt __VA_OPT__(,) __VA_ARGS__, v_, LLM_PCT(v_, tot)); \
	} while (0)

/* free time (5 bucket counters at 0x8b4, u64 total ns at 0x8c8, max ns at 0x8d0, freq sum/count at 0x920/0x928) and
 * async compact time (counters 0x8d4, total 0x8e8, max 0x8f0, freq 0x930/0x938) */
static noinline __used ssize_t pre_compact_time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	u64 avg1 = 0, avg2 = 0, freq1 = 0, freq2 = 0;
	int tot1 = 0, tot2 = 0;
	ssize_t len;

	if (DFX_U64_AT(0x8c8)) {
		tot1 = DFXC(0x8b4) + DFXC(0x8b8) + DFXC(0x8bc) + DFXC(0x8c0) + DFXC(0x8c4);
		if (tot1 >= 1)
			avg1 = DFX_U64_AT(0x8c8) / tot1 / 1000000;
	}
	if (DFX_U32(0x928))
		freq1 = DFX_U64_AT(0x920) / DFX_U32(0x928);
	if (DFX_U64_AT(0x8e8)) {
		tot2 = DFXC(0x8d4) + DFXC(0x8d8) + DFXC(0x8dc) + DFXC(0x8e0) + DFXC(0x8e4);
		if (tot2 >= 1)
			avg2 = DFX_U64_AT(0x8e8) / tot2 / 1000000;
	}
	if (DFX_U32(0x938))
		freq2 = DFX_U64_AT(0x930) / DFX_U32(0x938);
	len = sysfs_emit_at(buf, 0, "FREE_TIME total cnt:%d, unit(ms)\n", tot1);
	LLM_LINE(len, "free_%llu_%llu      %-4d %3u%%\n", 0x8b4, tot1, 0ULL, 8ULL);
	LLM_LINE(len, "free_%llu_%llu      %-4d %3u%%\n", 0x8b8, tot1, 8ULL, 64ULL);
	LLM_LINE(len, "free_%llu_%llu      %-4d %3u%%\n", 0x8bc, tot1, 64ULL, 256ULL);
	LLM_LINE(len, "free_%llu_%llu      %-4d %3u%%\n", 0x8c0, tot1, 256ULL, 1024ULL);
	LLM_LINE(len, "free_1024_INF      %-4d %3u%%\n", 0x8c4, tot1);
	len += sysfs_emit_at(buf, len, "avg_ms: %llu\n", avg1);
	len += sysfs_emit_at(buf, len, "max_ms: %ld\n", (long)(DFX_U32(0x8d0) / 1000000));
	len += sysfs_emit_at(buf, len, "freq_avg_kHz: %llu\n", freq1);
	len += sysfs_emit_at(buf, len, "\n");
	len += sysfs_emit_at(buf, len, "ASYNC_COMPACT_TIME total cnt:%d, unit(ms)\n", tot2);
	LLM_LINE(len, "async_%llu_%llu      %-4d %3u%%\n", 0x8d4, tot2, 0ULL, 8ULL);
	LLM_LINE(len, "async_%llu_%llu      %-4d %3u%%\n", 0x8d8, tot2, 8ULL, 64ULL);
	LLM_LINE(len, "async_%llu_%llu      %-4d %3u%%\n", 0x8dc, tot2, 64ULL, 256ULL);
	LLM_LINE(len, "async_%llu_%llu      %-4d %3u%%\n", 0x8e0, tot2, 256ULL, 1024ULL);
	LLM_LINE(len, "async_1024_INF      %-4d %3u%%\n", 0x8e4, tot2);
	len += sysfs_emit_at(buf, len, "avg_ms: %llu\n", avg2);
	len += sysfs_emit_at(buf, len, "max_ms: %ld\n", (long)(DFX_U32(0x8f0) / 1000000));
	len += sysfs_emit_at(buf, len, "freq_avg_kHz: %llu\n", freq2);
	return len;
}

/* fill-level histogram of the order4 (counters at 0x8f4) and order9 (0x908) pools: 5 buckets of 20 % each */
static noinline __used ssize_t pre_compact_success_rate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char name[20] = {};
	int tot4, tot9;
	ssize_t len;

	tot4 = DFXC(0x8f4) + DFXC(0x8f8) + DFXC(0x8fc) + DFXC(0x900) + DFXC(0x904);
	tot9 = DFXC(0x908) + DFXC(0x90c) + DFXC(0x910) + DFXC(0x914) + DFXC(0x918);
	len = sysfs_emit_at(buf, 0, "\n");
	len += sysfs_emit_at(buf, len, "%s total:%d\n", "success_rate_order4", tot4);
	LLM_LINE(len, "rate_0_20      %-4d %3u%%\n", 0x8f4, tot4);
	LLM_LINE(len, "rate_20_40     %-4d %3u%%\n", 0x8f8, tot4);
	LLM_LINE(len, "rate_40_60     %-4d %3u%%\n", 0x8fc, tot4);
	LLM_LINE(len, "rate_60_80     %-4d %3u%%\n", 0x900, tot4);
	LLM_LINE(len, "rate_80_100    %-4d %3u%%\n", 0x904, tot4);
	len += sysfs_emit_at(buf, len, "\n");
	snprintf(name, 20, "success_rate_order%d", 9);
	len += sysfs_emit_at(buf, len, "%s total:%d\n", name, tot9);
	LLM_LINE(len, "rate_0_20      %-4d %3u%%\n", 0x908, tot9);
	LLM_LINE(len, "rate_20_40     %-4d %3u%%\n", 0x90c, tot9);
	LLM_LINE(len, "rate_40_60     %-4d %3u%%\n", 0x910, tot9);
	LLM_LINE(len, "rate_60_80     %-4d %3u%%\n", 0x914, tot9);
	LLM_LINE(len, "rate_80_100    %-4d %3u%%\n", 0x918, tot9);
	return len;
}
