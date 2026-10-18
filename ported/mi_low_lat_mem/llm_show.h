/* SPDX-License-Identifier: GPL-2.0 */
/* mi_low_lat_mem: dfx sysfs histograms pre_compact_time / pre_compact_success_rate (G1x, from the OS4 stock disassembly; not byte-identical). */

#define LLM_PCT(cnt, tot)	((tot) >= 1 ? (cnt) * 100 / (tot) : 0)
#define DFXC(off)		atomic_read(DFX_ATOMIC(off))

/* free time (5 bucket counters at 0x8b4, u64 total ns at 0x8c8, max ns at 0x8d0, freq sum/count at 0x920/0x928) and
 * async compact time (counters 0x8d4, total 0x8e8, max 0x8f0, freq 0x930/0x938) */
static noinline __used ssize_t pre_compact_time_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	u64 avg1 = 0, avg2 = 0, freq1 = 0, freq2 = 0;
	int tot1 = 0, tot2 = 0, len;

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
	len += sysfs_emit_at(buf, len, "free_%llu_%llu      %-4d %3u%%\n", 0ULL, 8ULL, DFXC(0x8b4), LLM_PCT(DFXC(0x8b4), tot1));
	len += sysfs_emit_at(buf, len, "free_%llu_%llu      %-4d %3u%%\n", 8ULL, 64ULL, DFXC(0x8b8), LLM_PCT(DFXC(0x8b8), tot1));
	len += sysfs_emit_at(buf, len, "free_%llu_%llu      %-4d %3u%%\n", 64ULL, 256ULL, DFXC(0x8bc), LLM_PCT(DFXC(0x8bc), tot1));
	len += sysfs_emit_at(buf, len, "free_%llu_%llu      %-4d %3u%%\n", 256ULL, 1024ULL, DFXC(0x8c0), LLM_PCT(DFXC(0x8c0), tot1));
	len += sysfs_emit_at(buf, len, "free_1024_INF      %-4d %3u%%\n", DFXC(0x8c4), LLM_PCT(DFXC(0x8c4), tot1));
	len += sysfs_emit_at(buf, len, "avg_ms: %llu\n", avg1);
	len += sysfs_emit_at(buf, len, "max_ms: %ld\n", (long)(DFX_U32(0x8d0) / 1000000));
	len += sysfs_emit_at(buf, len, "freq_avg_kHz: %llu\n", freq1);
	len += sysfs_emit_at(buf, len, "\n");
	len += sysfs_emit_at(buf, len, "ASYNC_COMPACT_TIME total cnt:%d, unit(ms)\n", tot2);
	len += sysfs_emit_at(buf, len, "async_%llu_%llu      %-4d %3u%%\n", 0ULL, 8ULL, DFXC(0x8d4), LLM_PCT(DFXC(0x8d4), tot2));
	len += sysfs_emit_at(buf, len, "async_%llu_%llu      %-4d %3u%%\n", 8ULL, 64ULL, DFXC(0x8d8), LLM_PCT(DFXC(0x8d8), tot2));
	len += sysfs_emit_at(buf, len, "async_%llu_%llu      %-4d %3u%%\n", 64ULL, 256ULL, DFXC(0x8dc), LLM_PCT(DFXC(0x8dc), tot2));
	len += sysfs_emit_at(buf, len, "async_%llu_%llu      %-4d %3u%%\n", 256ULL, 1024ULL, DFXC(0x8e0), LLM_PCT(DFXC(0x8e0), tot2));
	len += sysfs_emit_at(buf, len, "async_1024_INF      %-4d %3u%%\n", DFXC(0x8e4), LLM_PCT(DFXC(0x8e4), tot2));
	len += sysfs_emit_at(buf, len, "avg_ms: %llu\n", avg2);
	len += sysfs_emit_at(buf, len, "max_ms: %ld\n", (long)(DFX_U32(0x8f0) / 1000000));
	len += sysfs_emit_at(buf, len, "freq_avg_kHz: %llu\n", freq2);
	return len;
}

/* fill-level histogram of the order4 (counters at 0x8f4) and order9 (0x908) pools: 5 buckets of 20 % each */
static noinline __used ssize_t pre_compact_success_rate_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	char name[20] = {};
	int tot4, tot9, len;

	tot4 = DFXC(0x8f4) + DFXC(0x8f8) + DFXC(0x8fc) + DFXC(0x900) + DFXC(0x904);
	tot9 = DFXC(0x908) + DFXC(0x90c) + DFXC(0x910) + DFXC(0x914) + DFXC(0x918);
	len = sysfs_emit_at(buf, 0, "\n");
	len += sysfs_emit_at(buf, len, "%s total:%d\n", "success_rate_order4", tot4);
	len += sysfs_emit_at(buf, len, "rate_0_20      %-4d %3u%%\n", DFXC(0x8f4), LLM_PCT(DFXC(0x8f4), tot4));
	len += sysfs_emit_at(buf, len, "rate_20_40     %-4d %3u%%\n", DFXC(0x8f8), LLM_PCT(DFXC(0x8f8), tot4));
	len += sysfs_emit_at(buf, len, "rate_40_60     %-4d %3u%%\n", DFXC(0x8fc), LLM_PCT(DFXC(0x8fc), tot4));
	len += sysfs_emit_at(buf, len, "rate_60_80     %-4d %3u%%\n", DFXC(0x900), LLM_PCT(DFXC(0x900), tot4));
	len += sysfs_emit_at(buf, len, "rate_80_100    %-4d %3u%%\n", DFXC(0x904), LLM_PCT(DFXC(0x904), tot4));
	len += sysfs_emit_at(buf, len, "\n");
	snprintf(name, 20, "success_rate_order%d", 9);
	len += sysfs_emit_at(buf, len, "%s total:%d\n", name, tot9);
	len += sysfs_emit_at(buf, len, "rate_0_20      %-4d %3u%%\n", DFXC(0x908), LLM_PCT(DFXC(0x908), tot9));
	len += sysfs_emit_at(buf, len, "rate_20_40     %-4d %3u%%\n", DFXC(0x90c), LLM_PCT(DFXC(0x90c), tot9));
	len += sysfs_emit_at(buf, len, "rate_40_60     %-4d %3u%%\n", DFXC(0x910), LLM_PCT(DFXC(0x910), tot9));
	len += sysfs_emit_at(buf, len, "rate_60_80     %-4d %3u%%\n", DFXC(0x914), LLM_PCT(DFXC(0x914), tot9));
	len += sysfs_emit_at(buf, len, "rate_80_100    %-4d %3u%%\n", DFXC(0x918), LLM_PCT(DFXC(0x918), tot9));
	return len;
}
