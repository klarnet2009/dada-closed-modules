// SPDX-License-Identifier: GPL-2.0
/*
 * dcvs_main - Xiaomi "DCVS Arbi" module (OS4 / HyperOS 4.0, Xiaomi 15 "dada").
 *
 * Reconstructed from the disassembly of the stock module (no public source exists): a sysfs knob set under
 * /sys/devices/system/cpu/dcvs_arbi/ddr/ and a few hooks that the Xiaomi-patched qcom memlat driver calls to modify the
 * DDR vote (mi_* functions); it signals user space through is_ready_send/mi_wait_queue/stat_data (exported by qcom_aoss).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <linux/of.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <soc/qcom/pmu_lib.h>

#define MAX_FREQ_TABLE		20
#define MAX_MEMLAT_GRPS		5

enum common_ev_idx { INST_IDX, CYC_IDX, FE_STALL_IDX, BE_STALL_IDX, NUM_COMMON_EVS };
enum grp_ev_idx { MISS_IDX, WB_IDX, ACC_IDX, NUM_GRP_EVS };

/* layout of the Xiaomi-patched struct cpu_stats of qcom-dcvs (memlat.c) as recovered from the BTF of the stock module */
struct cpu_ctrs {
	u64 common_ctrs[NUM_COMMON_EVS];
	u64 grp_ctrs[MAX_MEMLAT_GRPS][NUM_GRP_EVS];
};

struct cpu_stats {
	struct cpu_ctrs prev;
	struct cpu_ctrs curr;
	struct cpu_ctrs delta;
	struct cpu_ctrs delta_pre;
	struct qcom_pmu_data raw_ctrs;
	bool idle_sample;
	ktime_t sample_ts;
	ktime_t last_sample_ts;
	spinlock_t ctrs_lock;
	u32 freq_mhz;
	u32 fe_stall_pct;
	u32 be_stall_pct;
	u32 ipm[MAX_MEMLAT_GRPS];
	u32 wb_pct[MAX_MEMLAT_GRPS];
};

struct cpufreq_memfreq_map {
	unsigned int cpufreq_mhz;
	unsigned int memfreq_khz;
};

/* provided by qcom_aoss (Xiaomi additions) */
extern void stat_data(int val);
extern bool is_ready_send;
extern wait_queue_head_t mi_wait_queue;

static bool is_dcvs_arbi_enable;
static bool is_second;
static bool is_dcvs_arbi_partial_on;
static bool is_down;
static bool is_up;
static bool is_change_right;
static int is_test;
static struct kobject *cpu_kobj;
static struct kobject *dcvs_arbi_kobj;
static struct kobject *ddr_kobj;
static bool is_partial_on;
static u32 freq_table_size;
static u32 ddr_freq_table[MAX_FREQ_TABLE];

bool dcvs_arbi_get_enable_status(void)
{
	return is_dcvs_arbi_enable;
}
EXPORT_SYMBOL(dcvs_arbi_get_enable_status);

void mi_record_pre_sampling_stats(struct cpu_stats *stats)
{
	if (is_dcvs_arbi_enable)
		memcpy(&stats->delta_pre, &stats->delta, sizeof(stats->delta));
}
EXPORT_SYMBOL(mi_record_pre_sampling_stats);

void mi_calculate_sampling_stats(struct cpu_stats *stats)
{
}
EXPORT_SYMBOL(mi_calculate_sampling_stats);

u32 mi_recalculate_freq_mhz(struct cpu_ctrs *delta, s64 delta_us, u32 freq_mhz)
{
	if (is_dcvs_arbi_enable && !is_second)
		freq_mhz = (delta->common_ctrs[CYC_IDX] - delta->common_ctrs[BE_STALL_IDX]) / delta_us;
	return freq_mhz;
}
EXPORT_SYMBOL(mi_recalculate_freq_mhz);

void mi_cpufreq_to_memfreq(struct cpufreq_memfreq_map *map, u32 cpu_mhz)
{
	if (is_dcvs_arbi_enable && !is_second && !is_dcvs_arbi_partial_on) {
		if (is_down == true)
			is_down = false;
		if (is_up == true)
			is_up = false;
	}
}
EXPORT_SYMBOL(mi_cpufreq_to_memfreq);

void mi_calculate_mon_sampling_freq(u32 hw_type, struct cpu_stats *stats, int max_cpu)
{
	if (is_dcvs_arbi_enable) {
		barrier();
		if (likely(hw_type == 0)) {
		if (!is_second) {
			struct cpu_ctrs *d = &stats->delta, *p = &stats->delta_pre;

			if (d->common_ctrs[INST_IDX] > p->common_ctrs[INST_IDX] * 2 &&
			    d->grp_ctrs[0][MISS_IDX] * 2 < p->grp_ctrs[0][MISS_IDX])
				is_down = true;
			if (d->common_ctrs[BE_STALL_IDX] * 2 < p->common_ctrs[BE_STALL_IDX] ||
			    d->grp_ctrs[0][WB_IDX] > p->grp_ctrs[0][WB_IDX] * 10)
				is_down = true;
			if (d->common_ctrs[BE_STALL_IDX] > p->common_ctrs[BE_STALL_IDX] * 2 ||
			    p->grp_ctrs[0][MISS_IDX] * 2 < d->grp_ctrs[0][MISS_IDX])
				is_up = true;
		}
		}
	}
}
EXPORT_SYMBOL(mi_calculate_mon_sampling_freq);

void mi_update_memlat_fp_vote(u32 max_freqs, int hw_type)
{
	if (is_dcvs_arbi_enable) {
		barrier();
		if (likely(hw_type == 0 && !is_second)) {
			if (max_freqs < 3000000) {
				if (is_change_right == false && is_test == 33) {
					stat_data(3000);
					is_ready_send = true;
					is_change_right = true;
					wake_up_interruptible(&mi_wait_queue);
				}
			}
		}
	}
}
EXPORT_SYMBOL(mi_update_memlat_fp_vote);

void mi_update_memlat_fp_vote_revert(u32 max_freqs, int hw_type)
{
	if (hw_type)
		return;
	if (is_change_right == true) {
		if (is_dcvs_arbi_enable && is_test == 66) {
			is_change_right = false;
			stat_data(0);
			is_ready_send = true;
			wake_up_interruptible(&mi_wait_queue);
		}
		if (!is_dcvs_arbi_enable || is_second == true) {
			is_change_right = false;
			stat_data(0);
			is_ready_send = true;
			wake_up_interruptible(&mi_wait_queue);
		}
	}
}
EXPORT_SYMBOL(mi_update_memlat_fp_vote_revert);

u32 update_new_freq(int hw_type, u32 freq)
{
	if (hw_type == 3 && is_dcvs_arbi_enable) {
		if (is_test == 22)
			return 1;
		if (is_test == -22)
			return 0;
		return (is_second | (is_test != 33)) ? freq : 0;
	}
	return freq;
}
EXPORT_SYMBOL(update_new_freq);

static int find_and_parse_dcvs_node(void)
{
	struct device_node *dcvs_node, *ddr_node, *tbl_node;
	const u32 *freq;
	int ret;
	int i;

	dcvs_node = of_find_compatible_node(NULL, NULL, "qcom,dcvs");
	if (!dcvs_node) {
		pr_err("Failed to find qcom,dcvs node\n");
		ret = -ENODEV;
		goto out;
	}
	ddr_node = of_get_child_by_name(dcvs_node, "ddr");
	if (!ddr_node) {
		pr_err("Failed to find ddr node under qcom,dcvs\n");
		ret = -ENODEV;
		goto out;
	}
	if (!of_device_is_compatible(ddr_node, "qcom,dcvs-hw")) {
		pr_err("ddr node is not dcvs-hw compatible\n");
		ret = -ENODEV;
		goto out;
	}
	{
		int len = 0;

		ret = -ENODEV;
		tbl_node = of_parse_phandle(ddr_node, "qcom,freq-tbl", 0);
		if (!tbl_node) {
			pr_err("Failed to find qcom,freq-tbl node\n");
			goto out;
		}
		freq = of_get_property(tbl_node, "qcom,freq-tbl", &len);
		if (!freq) {
			ret = -ENODATA;
			pr_err("Failed to get qcom,freq-tbl property\n");
			goto out;
		}
		freq_table_size = len / sizeof(u32);
	}
	if (freq_table_size > MAX_FREQ_TABLE) {
		pr_warn("Frequency table too large, truncating to %zu entries\n", (size_t)MAX_FREQ_TABLE);
		freq_table_size = MAX_FREQ_TABLE;
	}
	for (i = 0; i < freq_table_size; i++) {
		ddr_freq_table[i] = freq[i];
		pr_info("DDR freq[%d]: %u Hz\n", i, ddr_freq_table[i]);
	}
	ret = 0;
out:
	return ret;
}

static ssize_t dcvs_arbi_enable_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", is_dcvs_arbi_enable);
}

static ssize_t dcvs_arbi_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = kstrtobool(buf, &is_dcvs_arbi_enable);

	return ret < 0 ? ret : count;
}

static ssize_t dcvs_arbi_second_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", !is_second);
}

static ssize_t dcvs_arbi_second_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	int ret = kstrtoint(buf, 10, &val);

	if (ret < 0)
		return ret;
	switch (val) {
	case 0:
		is_second = true;
		break;
	case 1:
		is_second = false;
		break;
	default:
		is_test = val;
		break;
	}
	return count;
}

static ssize_t dcvs_arbi_partial_on_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", is_partial_on);
}

static ssize_t dcvs_arbi_partial_on_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret = kstrtobool(buf, &is_partial_on);

	is_dcvs_arbi_partial_on = is_partial_on;
	return ret < 0 ? ret : count;
}

static struct kobj_attribute dcvs_arbi_enable_attr = __ATTR(dcvs_arbi_enable, 0664, dcvs_arbi_enable_show, dcvs_arbi_enable_store);
static struct kobj_attribute dcvs_arbi_second_attr = __ATTR(dcvs_arbi_second, 0664, dcvs_arbi_second_show, dcvs_arbi_second_store);
static struct kobj_attribute dcvs_arbi_partial_on_attr = __ATTR(dcvs_arbi_partial_on, 0664, dcvs_arbi_partial_on_show, dcvs_arbi_partial_on_store);

static int __init dcvs_main_init(void)
{
	struct device *cpu_dev;
	int ret;

	cpu_dev = bus_get_dev_root(&cpu_subsys);
	if (!cpu_dev) {
		pr_err("Cannot get cpu subsystem dev root\n");
		return -ENODEV;
	}
	cpu_kobj = &cpu_dev->kobj;
	if (!cpu_dev->kobj.sd) {
		pr_err("Invalid cpu kobject\n");
		put_device(cpu_dev);
		return -ENODEV;
	}
	dcvs_arbi_kobj = kobject_create_and_add("dcvs_arbi", cpu_kobj);
	if (!dcvs_arbi_kobj) {
		pr_err("Failed to create dcvs_arbi kobject\n");
		ret = -ENOMEM;
		goto put_cpu;
	}
	ddr_kobj = kobject_create_and_add("ddr", dcvs_arbi_kobj);
	if (!ddr_kobj) {
		pr_err("Failed to create ddr kobject\n");
		ret = -ENOMEM;
		goto put_arbi;
	}
	ret = sysfs_create_file(ddr_kobj, &dcvs_arbi_enable_attr.attr);
	if (ret) {
		pr_err("Failed to create dcvs_arbi_enable attribute\n");
		goto put_ddr;
	}
	ret = sysfs_create_file(ddr_kobj, &dcvs_arbi_second_attr.attr);
	if (ret) {
		pr_err("Failed to create dcvs_arbi_second attribute\n");
		goto put_ddr;
	}
	ret = sysfs_create_file(ddr_kobj, &dcvs_arbi_partial_on_attr.attr);
	if (ret) {
		pr_err("Failed to create dcvs_arbi_partial_on attribute\n");
		goto put_ddr;
	}
	ret = find_and_parse_dcvs_node();
	if (ret) {
		pr_err("Failed to parse DDR frequency table\n");
		sysfs_remove_file(ddr_kobj, &dcvs_arbi_enable_attr.attr);
		sysfs_remove_file(ddr_kobj, &dcvs_arbi_partial_on_attr.attr);
		sysfs_remove_file(ddr_kobj, &dcvs_arbi_second_attr.attr);
		goto put_ddr;
	}
	pr_info("DCVS Arbi Initialized\n");
	return 0;

put_ddr:
	kobject_put(ddr_kobj);
put_arbi:
	kobject_put(dcvs_arbi_kobj);
put_cpu:
	put_device(cpu_dev);
	return ret;
}

static void __exit dcvs_main_exit(void)
{
	sysfs_remove_file(ddr_kobj, &dcvs_arbi_enable_attr.attr);
	sysfs_remove_file(ddr_kobj, &dcvs_arbi_second_attr.attr);
	sysfs_remove_file(ddr_kobj, &dcvs_arbi_partial_on_attr.attr);
	if (ddr_kobj) {
		kobject_put(ddr_kobj);
		ddr_kobj = NULL;
	}
	if (dcvs_arbi_kobj) {
		kobject_put(dcvs_arbi_kobj);
		dcvs_arbi_kobj = NULL;
	}
	if (cpu_kobj) {
		put_device(kobj_to_dev(cpu_kobj));
		cpu_kobj = NULL;
	}
	pr_info("DCVS Arbi Unloaded\n");
}

module_init(dcvs_main_init);
module_exit(dcvs_main_exit);
MODULE_DESCRIPTION("Dcvs Arbi Moudle");
MODULE_LICENSE("GPL");
