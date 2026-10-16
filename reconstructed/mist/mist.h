/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mist: soc performance tuner (Xiaomi, "dflt" = DDR/LLCC/L3 frame latency tuner,
 * "gflt" = GPU frame latency tuner, "lc" = load calibration glue).
 * Reconstructed from the stock module (perfC).
 *
 * NOTE: the export CRCs of functions taking pointers to big structs (struct devfreq,
 * struct cpufreq_policy) depend on which headers were included before the EXPORT_SYMBOL
 * (genksyms only expands types that are defined at that point), so each .c file includes
 * exactly the headers the stock object evidently had; this header stays include-free.
 */
#ifndef _MIST_H
#define _MIST_H

#define MIST_VERSION		"3.5.0"

struct devfreq;
struct workqueue_struct;

struct gflt_load_info {
	u64 total_time;
	u64 busy_time;
	u64 ram_time;
	u64 ram_wait;
};

enum SOC_FREQ_MOD {
	CPU_DEV,
	GPU_DEV,
	DDR_DEV,
	L3_DEV,
	LLCC_DEV,
	SOC_DEV_NUM
};

typedef int (*MI_SOC_FREQ_ADJHOOK)(struct devfreq *devf, unsigned int freq);

/* dflt */
int mist_perf_api_enable(void);
void trigger_mist_dflt(void);
int mist_dflt_init(void);

/* gflt */
void trigger_mist_gflt(struct workqueue_struct *migt_wq);
int mist_gflt_init(void);
int mi_get_gflt_enable(void);
struct gflt_load_info get_adreno_gflt_load(void);
struct gflt_load_info get_gpubw_gflt_load(void);

#endif /* _MIST_H */
