//#define DEBUG
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include "perfmgr.h"

#define MAX_REMAP 4
#define DDR_MINFREQ 547000
#define L3_MINFREQ 307200

static int d_boost = 0;
module_param(d_boost, int, 0644);
MODULE_PARM_DESC(d_boost, "d_boost");

static int l3freq_boost = 0;
module_param(l3freq_boost, int, 0644);
MODULE_PARM_DESC(l3freq_boost, "l3freq_boost");

static int dcvs_timeout_ms = 1500;
module_param(dcvs_timeout_ms, int, 0644);
MODULE_PARM_DESC(dcvs_timeout_ms, "dcvs_timeout_ms");

struct dcvsfreq_remap {
	unsigned int cpufreq_khz;
	unsigned int dcvsfreq_khz;
};

struct dcvsfreq_remap ddr_remap_table[MAX_REMAP];
struct dcvsfreq_remap l3_remap_table[MAX_REMAP];

static struct delayed_work dcvsfreq_release_work;

static void perfmgr_boost_dcvsfreq_online(int freq, u32 hw_type)
{
	int i;
	struct dcvsfreq_remap remap_table[MAX_REMAP];
	int MINFREQ;
	if (hw_type == DCVS_DDR) {
		memcpy(remap_table, ddr_remap_table, sizeof(remap_table));
		MINFREQ = DDR_MINFREQ;
	}
	if (hw_type == DCVS_L3) {
		memcpy(remap_table, l3_remap_table, sizeof(remap_table));
		MINFREQ = L3_MINFREQ;
	}
	pr_debug("%s freq = %d ddrfreq_boost %d \n", __func__, freq, d_boost);
	pr_debug("%s freq = %d l3freq_boost %d \n", __func__, freq,
		 l3freq_boost);

	if (freq) {
		for (i = 0; i < MAX_REMAP; i++) {
			if (freq > remap_table[i].cpufreq_khz) {
				boost_dcvs_freq(remap_table[i].dcvsfreq_khz,
						hw_type);
				perfmgr_dbg(debug_mask_perfmgr,PERFMGR_DCVS,
					"%s hw_type is %d  input cpu freq %d remap_table[%d].CPU_freq %d remap_table[%d]DCVS_freq %d \n",
					__func__, hw_type, freq, i,
					remap_table[i].cpufreq_khz, i,
					remap_table[i].dcvsfreq_khz);
				return;
			}
		}
		boost_dcvs_freq(MINFREQ, hw_type);
	} else {
		boost_dcvs_freq(MINFREQ, hw_type);
	}
	perfmgr_dbg(debug_mask_perfmgr,PERFMGR_DCVS,
					"%s hw_type is %d  input cpu freq %d final dcvs freq is %d \n",
					__func__, hw_type, freq, MINFREQ);
}

static int perfmgr_ddrfreq_remap_set(const char *buf,
				     const struct kernel_param *param)
{
	int ret;
	char *sptr, *token, *str;
	u32 val, i;

	str = kstrdup(buf, GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	sptr = str;
	for (i = 0; i < MAX_REMAP; i++) {
		token = strsep(&sptr, ":");
		if (!token)
			return -EINVAL;
		ret = kstrtouint(token, 10, &val);
		if (ret < 0)
			return -EINVAL;
		val = max(val, 0U);
		val = min(val, U32_MAX);
		ddr_remap_table[i].cpufreq_khz = val;
		token = strsep(&sptr, " ");
		if (!token)
			return -EINVAL;
		ret = kstrtouint(token, 10, &val);
		if (ret < 0)
			return -EINVAL;
		ddr_remap_table[i].dcvsfreq_khz = val;
	}
	return ret;
}

static int perfmgr_l3freq_remap_set(const char *buf,
				    const struct kernel_param *param)
{
	int ret;
	char *sptr, *token, *str;
	u32 val, i;

	str = kstrdup(buf, GFP_KERNEL);
	if (!str)
		return -ENOMEM;
	sptr = str;
	for (i = 0; i < MAX_REMAP; i++) {
		token = strsep(&sptr, ":");
		if (!token)
			return -EINVAL;
		ret = kstrtouint(token, 10, &val);
		if (ret < 0)
			return -EINVAL;
		val = max(val, 0U);
		val = min(val, U32_MAX);
		l3_remap_table[i].cpufreq_khz = val;
		token = strsep(&sptr, " ");
		if (!token)
			return -EINVAL;
		ret = kstrtouint(token, 10, &val);
		if (ret < 0)
			return -EINVAL;
		l3_remap_table[i].dcvsfreq_khz = val;
	}
	return ret;
}

static int perfmgr_ddrfreq_remap_get(char *buf,
				     const struct kernel_param *param)
{
	unsigned int cnt = 0;
	if (d_boost) {
		unsigned int i;
		cnt += scnprintf(buf, PAGE_SIZE,
				 "CPU freq (kHz)\tMem freq (kHz)\n");
		for (i = 0; i < MAX_REMAP && ddr_remap_table[i].cpufreq_khz;
		     i++)
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
					 "%14u\t%14u\n",
					 ddr_remap_table[i].cpufreq_khz,
					 ddr_remap_table[i].dcvsfreq_khz);
		if (cnt < PAGE_SIZE)
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	}
	return cnt;
}

static int perfmgr_l3freq_remap_get(char *buf, const struct kernel_param *param)
{
	unsigned int cnt = 0;
	if (l3freq_boost) {
		unsigned int i;
		cnt += scnprintf(buf, PAGE_SIZE,
				 "CPU freq (kHz)\tMem freq (kHz)\n");
		for (i = 0; i < MAX_REMAP && l3_remap_table[i].cpufreq_khz; i++)
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
					 "%14u\t%14u\n",
					 l3_remap_table[i].cpufreq_khz,
					 l3_remap_table[i].dcvsfreq_khz);
		if (cnt < PAGE_SIZE)
			cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt, "\n");
	}
	return cnt;
}

module_param_call(d_table, perfmgr_ddrfreq_remap_set, perfmgr_ddrfreq_remap_get,
		  NULL, 0644);
MODULE_PARM_DESC(d_table, "d_table");

module_param_call(l3freq_remap_table, perfmgr_l3freq_remap_set,
		  perfmgr_l3freq_remap_get, NULL, 0644);
MODULE_PARM_DESC(l3freq_remap_table, "l3freq_remap_table");

static void perfmgr_dcvsfreq_release_work(struct work_struct *work)
{
	pr_debug("%s \n", __func__);
	if (d_boost)
		perfmgr_boost_dcvsfreq_online(0, DCVS_DDR);
	if (l3freq_boost)
		perfmgr_boost_dcvsfreq_online(0, DCVS_L3);
}

void perfmgr_boost_dcvsfreq_and_timeout(unsigned int maxfreq)
{
	if (d_boost || l3freq_boost) {
		cancel_delayed_work(&dcvsfreq_release_work);
		if (d_boost)
			perfmgr_boost_dcvsfreq_online(maxfreq, DCVS_DDR);
		if (l3freq_boost)
			perfmgr_boost_dcvsfreq_online(maxfreq, DCVS_L3);
		schedule_delayed_work(&dcvsfreq_release_work,
				      msecs_to_jiffies(dcvs_timeout_ms));
	}
}
//ddrfreq 547000 768000 1555000 1708000 2092000 2736000 3187000 3686000 4224000
//L3freq 307200 384000 499200 614400 729600 844800 940800 1036800 1132800 1248000 1344000 1440000 1555200 1651200 1747200 1843200 1939200 2035200
int perfmgr_remap_init(void)
{
	pr_debug("%s \n", __func__);
	ddr_remap_table[0].cpufreq_khz = 1500000;
	ddr_remap_table[0].dcvsfreq_khz = 3187000;
	ddr_remap_table[1].cpufreq_khz = 1400000;
	ddr_remap_table[1].dcvsfreq_khz = 2736000;
	ddr_remap_table[2].cpufreq_khz = 1300000;
	ddr_remap_table[2].dcvsfreq_khz = 2092000;
	ddr_remap_table[3].cpufreq_khz = 1200000;
	ddr_remap_table[3].dcvsfreq_khz = 1708000;

	l3_remap_table[0].cpufreq_khz = 1700000;
	l3_remap_table[0].dcvsfreq_khz = 1689600;
	l3_remap_table[1].cpufreq_khz = 1500000;
	l3_remap_table[1].dcvsfreq_khz = 1478400;
	l3_remap_table[2].cpufreq_khz = 1300000;
	l3_remap_table[2].dcvsfreq_khz = 1267200;
	l3_remap_table[3].cpufreq_khz = 1200000;
	l3_remap_table[3].dcvsfreq_khz = 1056000;

	INIT_DELAYED_WORK(&dcvsfreq_release_work,
			  perfmgr_dcvsfreq_release_work);
	return 0;
}
