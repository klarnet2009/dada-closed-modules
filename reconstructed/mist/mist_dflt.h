/* SPDX-License-Identifier: GPL-2.0 */
/* mist-dflt private types (needs spinlock/pmu_lib/dcvs headers first) */
#ifndef _MIST_DFLT_H
#define _MIST_DFLT_H

/* number of frames kept in the history ring */
#define MIST_FRAME_HIST		101
#define MIST_MAX_HW		3	/* DDR, LLCC, L3 */
#define MIST_MAX_CLUSTER	3
#define MIST_MAX_CPUS		8

/* one cpufreq(MHz) -> memfreq(kHz) mapping point */
struct mist_map_entry {
	u32 cpufreq_mhz;
	u32 memfreq_khz;
};

/* cpus of one cluster */
struct mist_cluster_cpus {
	u32 cpu_num;
	u32 cpus[MIST_MAX_CPUS];
	u32 weight;
	u32 resv;
};

/* per frame load history record (304 bytes) */
struct frame_load {
	u64 freq[MIST_MAX_CLUSTER];	/* l/m/b cluster frequency */
	u64 resv[5];
	u32 ddr_freq;
	u64 q2q;			/* us */
	u64 ipc[MIST_MAX_CPUS];
	u64 miss[MIST_MAX_CPUS];
	u64 instructions[MIST_MAX_CPUS];
	u32 stall_ratio[MIST_MAX_CPUS];
};

/* per cpu pmu sampling state (240 bytes) */
struct dflt_latency_cpu_stats {
	u64 prev_ctrs[3];
	u64 curr_ctrs[3];
	u64 delta_ctrs[3];
	struct qcom_pmu_data raw_ctrs;
	bool idle_sample;
	ktime_t sample_ts;
	ktime_t last_sample_ts;
	spinlock_t ctrs_lock;
	u32 freq_mhz;
	u32 spm[4];
	u64 ll_miss;
	u32 ips;
	bool dflt_latency_sample_start;
};

typedef void (*mi_dflt_bw_update_cb)(void);
typedef void (*mi_dflt_lat_update_cb)(int cpu, ktime_t ts, struct qcom_pmu_data *data);
typedef void (*mi_dflt_lat_cal_cb)(void);

#endif /* _MIST_DFLT_H */
