// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi thermal control interface (drivers/thermal/mi_thermal_interface.c).
 * Ported from MiCode ziyi-s-oss (kernel 5.4) and extended to the OS4 (HyperOS 4.0, kernel 6.6) stock module.
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/idr.h>
#include <linux/thermal.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/of.h>
#include <net/netlink.h>
#include <net/genetlink.h>
#include <linux/suspend.h>
#include <linux/cpu_cooling.h>
#include "panel_event_notifier.h"
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/kobject.h>
#include <linux/kernfs.h>
#include <linux/workqueue.h>
#include <linux/power_supply.h>
#include "base.h"

/* providers: mca_smart_charge.ko, mca_event.ko */
extern void mca_smartchg_set_scene(int scene);
extern void mca_event_block_notify(unsigned int type, unsigned long event, void *data);

#if defined(CONFIG_DRM_PANEL)
static struct drm_panel *prim_panel;
#endif
#define BOOST_BUFFER_SIZE 128
#define BOARD__BUFFER_SIZE 128
struct mi_thermal_device  {
	struct device *dev;
	struct class *class;
	struct attribute_group attrs;
};

struct freq_table {
	u32 frequency;
};

struct cpufreq_device {
	int id;
	unsigned int cpufreq_state;
	unsigned int max_level;
	struct freq_table *freq_table;	/* In descending order */
	struct cpufreq_policy *policy;
	struct list_head node;
	struct freq_qos_request qos_req;
};

#ifdef CONFIG_MI_THERMAL_MULTI_CHARGE
struct usb_monitor  {
	struct notifier_block psy_nb;
	struct work_struct	usb_state_work;
	int usb_online;
};
static struct usb_monitor usb_state;
static atomic_t charger_mode = ATOMIC_INIT(-1);
#endif

static struct mi_thermal_device mi_thermal_dev;
static struct mi_thermal_device mi_powersave_dev;
static bool screen_state;
static bool screen_light;
static int screen_last_status = 0;
static void *cookie = NULL;

static atomic_t temp_state = ATOMIC_INIT(0);
static atomic_t switch_mode = ATOMIC_INIT(-1);
static atomic_t balance_mode = ATOMIC_INIT(0);
static atomic_t modem_limit = ATOMIC_INIT(0);
static atomic_t market_download_limit = ATOMIC_INIT(0);
static atomic_t board_sensor_temp_comp_default = ATOMIC_INIT(0);
static atomic_t cpu_nolimit_temp_default = ATOMIC_INIT(0);
static atomic_t poor_modem_limit= ATOMIC_INIT(0);
static atomic_t wifi_limit = ATOMIC_INIT(0);
static atomic_t flash_state = ATOMIC_INIT(0);
static atomic_t torch_level = ATOMIC_INIT(0);
static atomic_t torch_real_level = ATOMIC_INIT(-1);
static atomic_t thermal_max_brightness = ATOMIC_INIT(0);
static atomic_t display_therm_temp = ATOMIC_INIT(0);
static atomic_t voice_limit = ATOMIC_INIT(0);
static atomic_t super_hdr = ATOMIC_INIT(0);
static atomic_t cloud_game = ATOMIC_INIT(0);
static atomic_t dynamic_tj = ATOMIC_INIT(0);
static atomic_t ntn_limit = ATOMIC_INIT(0);
static atomic_t modem_level = ATOMIC_INIT(0);
static atomic_t boot_complete = ATOMIC_INIT(0);
static atomic_t powersave_mode = ATOMIC_INIT(-1);
static atomic_t power_level = ATOMIC_INIT(-1);
static char boost_buf[128];
const char *board_sensor;
const char *ambient_sensor;
static char board_sensor_temp[128];
static char board_sensor_second_temp[128];
static char board_sensor_other_temp[128];
static char ambient_sensor_temp[128];
static LIST_HEAD(cpufreq_dev_list);
static DEFINE_MUTEX(cpufreq_list_lock);

static struct workqueue_struct *screen_state_wq;
static struct delayed_work screen_state_dw;

/* "%d\n" show / simple_strtol store pair for an atomic_t, plus its DEVICE_ATTR */
#define ATOMIC_ATTR(attr, var, show, store)						\
static ssize_t show(struct device *dev, struct device_attribute *a, char *buf)		\
{											\
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&var));			\
}											\
static ssize_t store(struct device *dev, struct device_attribute *a,			\
		const char *buf, size_t len)						\
{											\
	int val = -1;									\
											\
	val = simple_strtol(buf, NULL, 10);						\
	atomic_set(&var, val);								\
	return len;									\
}											\
static DEVICE_ATTR(attr, 0664, show, store)

/* "%s" show / snprintf(buf) store pair for a char buffer */
#define BUF_ATTR(attr, var, show, store)						\
static ssize_t show(struct device *dev, struct device_attribute *a, char *buf)		\
{											\
	return snprintf(buf, PAGE_SIZE, var);						\
}											\
static ssize_t store(struct device *dev, struct device_attribute *a,			\
		const char *buf, size_t len)						\
{											\
	snprintf(var, BOARD__BUFFER_SIZE, buf);						\
	return len;									\
}											\
static DEVICE_ATTR(attr, 0664, show, store)

static int cpufreq_set_level(struct cpufreq_device *cdev,
		unsigned long state)
{
	/* Request state should be less than max_level */
	if (WARN_ON(state > cdev->max_level))
		return -EINVAL;

	/* Check if the old cooling action is same as new cooling action */
	if (cdev->cpufreq_state == state)
		return 0;

	cdev->cpufreq_state = state;
	return freq_qos_update_request(&cdev->qos_req,cdev->freq_table[state].frequency);
}

void cpu_limits_set_level(unsigned int cpu, unsigned int max_freq)
{
	struct cpufreq_device *cpufreq_dev;
	unsigned int level = 0;

	list_for_each_entry(cpufreq_dev, &cpufreq_dev_list, node) {
		if (cpufreq_dev->id == cpu) {
			for (level = 0; level <= cpufreq_dev->max_level; level++) {
				int target_freq = cpufreq_dev->freq_table[level].frequency;
				if (max_freq >= target_freq) {
					cpufreq_set_level(cpufreq_dev, level);
					break;
				}
			}
			break;
		}
	}
}

static unsigned int find_next_max(struct cpufreq_frequency_table *table,
		unsigned int prev_max)
{
	struct cpufreq_frequency_table *pos;
	unsigned int max = 0;
	cpufreq_for_each_valid_entry(pos, table) {
		if (pos->frequency > max && pos->frequency < prev_max)
			max = pos->frequency;
	}
	return max;
}

static int cpu_thermal_init(void)
{
	int cpu, ret;
	struct cpufreq_policy *policy;

	for_each_possible_cpu(cpu) {
		unsigned int i;
		unsigned int freq;
		struct cpufreq_device *cpufreq_dev;

		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
					__func__, cpu);
			return -ESRCH;
		}
		printk(KERN_ERR "%s cpu=%d\n", __func__, cpu);

		i = cpufreq_table_count_valid_entries(policy);
		if (!i) {
			pr_debug("%s: CPUFreq table not found or has no valid entries\n",
					__func__);
			return -ENODEV;
		}

		cpufreq_dev = kzalloc(sizeof(*cpufreq_dev), GFP_KERNEL);
		if (!cpufreq_dev)
			return -ENOMEM;

		cpufreq_dev->policy = policy;
		/* max_level is an index, not a counter */
		cpufreq_dev->max_level = i - 1;
		cpufreq_dev->id = policy->cpu;

		cpufreq_dev->freq_table = kmalloc_array(i,
				sizeof(*cpufreq_dev->freq_table),
				GFP_KERNEL);
		if (!cpufreq_dev->freq_table) {
			kfree(cpufreq_dev);
			return -ENOMEM;
		}

		/* Fill freq-table in descending order of frequencies */
		for (i = 0, freq = -1; i <= cpufreq_dev->max_level; i++) {
			freq = find_next_max(policy->freq_table, freq);
			cpufreq_dev->freq_table[i].frequency = freq;

			/* Warn for duplicate entries */
			if (!freq)
				pr_warn("%s: table has duplicate entries\n", __func__);
			else
				pr_debug("%s: freq:%u KHz\n", __func__, freq);
		}

		ret = freq_qos_add_request(&policy->constraints,
				&cpufreq_dev->qos_req, FREQ_QOS_MAX,
				cpufreq_dev->freq_table[0].frequency);
		if (ret < 0) {
			kfree(cpufreq_dev->freq_table);
			kfree(cpufreq_dev);
			pr_err("%s: Failed to add freq constraint (%d)\n", __func__,
					ret);
			return ret;
		}
		mutex_lock(&cpufreq_list_lock);
		list_add(&cpufreq_dev->node, &cpufreq_dev_list);
		mutex_unlock(&cpufreq_list_lock);
	}
	return ret;
}

static void destory_thermal_cpu(void)
{
	struct cpufreq_device *priv, *tmp;
	list_for_each_entry_safe(priv, tmp, &cpufreq_dev_list, node) {
		freq_qos_remove_request(&priv->qos_req);
		list_del(&priv->node);
		kfree(priv->freq_table);
		kfree(priv);
	}
}

ATOMIC_ATTR(temp_state, temp_state, thermal_temp_state_show, thermal_temp_state_store);

static ssize_t
cpu_limits_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t
cpu_limits_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int cpu;
	unsigned int max;

	if (sscanf(buf, "cpu%u %u", &cpu, &max) != 2) {
		pr_err("input param error, can not prase param\n");
		return -EINVAL;
	}
	cpu_limits_set_level(cpu, max);

	return len;
}

static DEVICE_ATTR(cpu_limits, 0664,
		cpu_limits_show, cpu_limits_store);

static ssize_t
thermal_sconfig_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&switch_mode));
}

static ssize_t
thermal_sconfig_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int val = -1;

	val = simple_strtol(buf, NULL, 10);

	atomic_set(&switch_mode, val);
	mca_smartchg_set_scene(val);

	return len;
}

static DEVICE_ATTR(sconfig, 0664,
		thermal_sconfig_show, thermal_sconfig_store);

static ssize_t thermal_screen_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", screen_state);
}

static DEVICE_ATTR(screen_state, 0664,
		thermal_screen_state_show, NULL);

static ssize_t
thermal_boost_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, boost_buf);
}

static ssize_t
thermal_boost_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	snprintf(boost_buf, BOOST_BUFFER_SIZE, buf);
	return len;
}

static DEVICE_ATTR(boost, 0644,
		thermal_boost_show, thermal_boost_store);

static ssize_t
thermal_board_sensor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (!board_sensor)
		board_sensor = "invalid";

	return snprintf(buf, PAGE_SIZE, "%s", board_sensor);
}

static DEVICE_ATTR(board_sensor, 0664,
		thermal_board_sensor_show, NULL);

static ssize_t
thermal_board_sensor_temp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, board_sensor_temp);
}

static ssize_t
thermal_board_sensor_temp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int temp;

	snprintf(board_sensor_temp, BOARD__BUFFER_SIZE, buf);
	temp = simple_strtol(buf, NULL, 10);
	mca_event_block_notify(6, 62, &temp);

	return len;
}

static DEVICE_ATTR(board_sensor_temp, 0664,
		thermal_board_sensor_temp_show, thermal_board_sensor_temp_store);

BUF_ATTR(board_sensor_second_temp, board_sensor_second_temp,
	thermal_board_sensor_second_temp_show, thermal_board_sensor_second_temp_store);
BUF_ATTR(board_sensor_other_temp, board_sensor_other_temp,
	thermal_board_sensor_other_temp_show, thermal_board_sensor_other_temp_store);

ATOMIC_ATTR(balance_mode, balance_mode, thermal_balance_mode_show, thermal_balance_mode_store);
ATOMIC_ATTR(modem_limit, modem_limit, thermal_modem_limit_show, thermal_modem_limit_store);
ATOMIC_ATTR(wifi_limit, wifi_limit, thermal_wifi_limit_show, thermal_wifi_limit_store);
ATOMIC_ATTR(flash_state, flash_state, thermal_flash_state_show, thermal_flash_state_store);
ATOMIC_ATTR(torch_level, torch_level, thermal_torch_level_show, thermal_torch_level_store);
ATOMIC_ATTR(torch_real_level, torch_real_level, thermal_torch_real_level_show, thermal_torch_real_level_store);
ATOMIC_ATTR(thermal_max_brightness, thermal_max_brightness, thermal_max_brightness_show, thermal_max_brightness_store);
ATOMIC_ATTR(display_therm_temp, display_therm_temp, display_therm_temp_show, display_therm_temp_store);
ATOMIC_ATTR(voice_limit, voice_limit, voice_limit_show, voice_limit_store);
ATOMIC_ATTR(super_hdr, super_hdr, super_hdr_show, super_hdr_store);
ATOMIC_ATTR(cloud_game, cloud_game, cloud_game_show, cloud_game_store);
ATOMIC_ATTR(dynamic_tj, dynamic_tj, thermal_dynamic_tj_show, thermal_dynamic_tj_store);
ATOMIC_ATTR(market_download_limit, market_download_limit,
	thermal_market_download_limit_show, thermal_market_download_limit_store);
ATOMIC_ATTR(board_sensor_temp_comp, board_sensor_temp_comp_default,
	thermal_board_sensor_temp_comp_show, thermal_board_sensor_temp_comp_store);
ATOMIC_ATTR(poor_modem_limit, poor_modem_limit, thermal_poor_modem_limit_show, thermal_poor_modem_limit_store);
ATOMIC_ATTR(cpu_nolimit_temp, cpu_nolimit_temp_default,
	thermal_cpu_nolimit_temp_show, thermal_cpu_nolimit_temp_store);
ATOMIC_ATTR(ntn_limit, ntn_limit, thermal_ntn_limit_show, thermal_ntn_limit_store);
ATOMIC_ATTR(modem_level, modem_level, thermal_modem_level_show, thermal_modem_level_store);

static ssize_t
thermal_ambient_sensor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s", "ABT-SENSOR");
}

static DEVICE_ATTR(ambient_sensor, 0664,
		thermal_ambient_sensor_show, NULL);

BUF_ATTR(ambient_sensor_temp, ambient_sensor_temp,
	thermal_ambient_sensor_temp_show, thermal_ambient_sensor_temp_store);

ATOMIC_ATTR(boot_complete, boot_complete, thermal_boot_complete_show, thermal_boot_complete_store);

#ifdef CONFIG_MI_THERMAL_MULTI_CHARGE
ATOMIC_ATTR(charger_temp, charger_mode, thermal_charger_temp_show, thermal_charger_temp_store);

static ssize_t thermal_usb_online_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", usb_state.usb_online);
}

static DEVICE_ATTR(usb_online, 0664,
		thermal_usb_online_show, NULL);

static int usb_online_callback(struct notifier_block *nb,
		unsigned long val, void *data)
{
	struct power_supply *psy = data;

	if (strcmp(psy->desc->name, "usb"))
		return NOTIFY_OK;
	schedule_work(&usb_state.usb_state_work);

	return NOTIFY_OK;
}

static void usb_online_work(struct work_struct *work)
{
	static struct power_supply *usb_psy;
	union power_supply_propval ret = {0,};
	int err = 0;

	if (!usb_psy)
		usb_psy = power_supply_get_by_name("usb");

	if (usb_psy) {
		err = power_supply_get_property(usb_psy,
				POWER_SUPPLY_PROP_ONLINE, &ret);
		if (err) {
			pr_err("usb online read error:%d\n",err);
			return;
		}
		usb_state.usb_online = ret.intval;
		if (mi_thermal_dev.dev)
			sysfs_notify(&mi_thermal_dev.dev->kobj, NULL, "usb_online");
	}
}
#endif

ATOMIC_ATTR(powersave_mode, powersave_mode, thermal_powersave_mode_show, thermal_powersave_mode_store);
ATOMIC_ATTR(power_level, power_level, thermal_power_level_show, thermal_power_level_store);

static int of_parse_thermal_message(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "thermal-message");
	if (!np)
		return -EINVAL;

	if (of_property_read_string(np, "board-sensor", &board_sensor))
		return -EINVAL;

	pr_info("%s board sensor: %s\n", __func__, board_sensor);

	return 0;
}

static int of_parse_mi_powersave(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "power_save");
	if (!np)
		return -EINVAL;

	return 0;
}

static struct attribute *mi_thermal_dev_attr_group[] = {
	&dev_attr_temp_state.attr,
	&dev_attr_cpu_limits.attr,
	&dev_attr_sconfig.attr,
	&dev_attr_screen_state.attr,
	&dev_attr_boost.attr,
	&dev_attr_board_sensor.attr,
	&dev_attr_board_sensor_temp.attr,
	&dev_attr_board_sensor_second_temp.attr,
	&dev_attr_board_sensor_other_temp.attr,
	&dev_attr_balance_mode.attr,
	&dev_attr_modem_limit.attr,
	&dev_attr_wifi_limit.attr,
	&dev_attr_flash_state.attr,
	&dev_attr_torch_level.attr,
	&dev_attr_torch_real_level.attr,
	&dev_attr_thermal_max_brightness.attr,
	&dev_attr_display_therm_temp.attr,
	&dev_attr_voice_limit.attr,
	&dev_attr_super_hdr.attr,
	&dev_attr_cloud_game.attr,
	&dev_attr_dynamic_tj.attr,
	&dev_attr_market_download_limit.attr,
	&dev_attr_board_sensor_temp_comp.attr,
	&dev_attr_poor_modem_limit.attr,
	&dev_attr_cpu_nolimit_temp.attr,
	&dev_attr_ntn_limit.attr,
	&dev_attr_modem_level.attr,
	&dev_attr_ambient_sensor.attr,
	&dev_attr_ambient_sensor_temp.attr,
	&dev_attr_boot_complete.attr,
#ifdef CONFIG_MI_THERMAL_MULTI_CHARGE
	&dev_attr_charger_temp.attr,
	&dev_attr_usb_online.attr,
#endif
	NULL,
};

static struct attribute *mi_powersave_dev_attr_group[] = {
	&dev_attr_powersave_mode.attr,
	&dev_attr_power_level.attr,
	NULL,
};

static void screen_state_for_thermal_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *client_data)
{
	if (!notification) {
		printk(KERN_ERR "%s:Invalid notification\n", __func__);
		return;
	}

	if(notification->notif_data.early_trigger) {
		return;
	}
	if(tag == PANEL_EVENT_NOTIFICATION_PRIMARY){
		switch (notification->notif_type) {
			case DRM_PANEL_EVENT_UNBLANK:
				screen_light = true;
				break;
			case DRM_PANEL_EVENT_BLANK:
			case DRM_PANEL_EVENT_BLANK_LP:
				screen_light = false;
				break;
			default:
				return;
		}
	}
	if (screen_light)
		screen_state = 1;
	else
		screen_state = 0;
	if (screen_last_status != screen_state) {
		sysfs_notify(&mi_thermal_dev.dev->kobj, NULL, "screen_state");
		screen_last_status = screen_state;
	}
}

static int thermal_check_panel(struct device_node *np)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		goto out;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			prim_panel = panel;
			break;
		}else{
			prim_panel = NULL;
		}
	}
	if (PTR_ERR(prim_panel) == -EPROBE_DEFER) {
		pr_err("%s ERROR: Cannot fine prim_panel of node!", __func__);
	}
out:
	return 0;
}

/* look up the "thermal" class through sysfs and create a device with an attribute group on it */
static __always_inline void create_class_node(struct mi_thermal_device *md,
		struct attribute **attrs, const char *name, const char *fn)
{
	int ret = 0;
	struct kernfs_node *sysfs_sd = NULL;
	struct kernfs_node *thermal_sd = NULL;
	struct kernfs_node *class_sd = NULL;
	struct class *cls = NULL;
	struct subsys_private *cp = NULL;
	struct kobject *kobj_tmp = NULL;

	sysfs_sd = kernel_kobj->sd->parent;
	if (!sysfs_sd) {
		printk(KERN_ERR "%s: sysfs_sd is NULL\n", fn);
	} else {
		class_sd = kernfs_find_and_get(sysfs_sd, "class");
		if (!class_sd) {
			printk(KERN_ERR "%s:can not find class_sd\n", fn);
		} else {
			thermal_sd = kernfs_find_and_get(class_sd, "thermal");
			if (thermal_sd) {
				kobj_tmp = (struct kobject *)thermal_sd->priv;
				if (kobj_tmp) {
					cp = to_subsys_private(kobj_tmp);
					cls = (struct class *)cp->class;
				} else {
					printk(KERN_ERR "%s:can not find thermal kobj\n", fn);
				}
			} else {
				printk(KERN_ERR "%s:can not find thermal_sd\n", fn);
			}
		}
	}
	if (!md->class && cls) {
		md->class = cls;
		md->dev = device_create(md->class, NULL, 'H', NULL, name);
		if (!md->dev) {
			pr_err("%s create device dev err\n", fn);
			return;
		}
		md->attrs.attrs = attrs;
		ret = sysfs_create_group(&md->dev->kobj, &md->attrs);
		if (ret) {
			pr_err("%s ERROR: Cannot create sysfs structure!:%d\n", fn, ret);
			return;
		}
	}
}

static void create_thermal_message_node(void)
{
	create_class_node(&mi_thermal_dev, mi_thermal_dev_attr_group, "thermal_message", __func__);
}

static void create_mi_powersave_node(void)
{
	create_class_node(&mi_powersave_dev, mi_powersave_dev_attr_group, "power_save", __func__);
}

static void destroy_thermal_message_node(void)
{
	printk(KERN_ERR "%s:destroy_thermal_message_node", __func__);
	sysfs_remove_group(&mi_thermal_dev.dev->kobj, &mi_thermal_dev.attrs);
	if (NULL != mi_thermal_dev.class){
		device_destroy(mi_thermal_dev.class,'H');
		mi_thermal_dev.class = NULL;
	}
}

static void destroy_mi_powersave_node(void)
{
	printk(KERN_ERR "%s:destroy_mi_powersave_node", __func__);
	sysfs_remove_group(&mi_powersave_dev.dev->kobj, &mi_powersave_dev.attrs);
	if (NULL != mi_powersave_dev.class){
		device_destroy(mi_powersave_dev.class,'H');
		mi_powersave_dev.class = NULL;
	}
}

static void screen_state_check(struct work_struct *work)
{
#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	struct device_node *node;
	void *pvt_data = NULL;
	int error = 0;
	static int retry_count = 10;

	node = of_find_node_by_name(NULL, "thermal-screen");
	if (!node) {
		pr_err("%s ERROR: Cannot find node with panel!", __func__);
		return;
	}

	error = thermal_check_panel(node);
	if (prim_panel) {
		if (!cookie)
		{
		cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
				PANEL_EVENT_NOTIFIER_CLIENT_THERMAL, prim_panel,
				screen_state_for_thermal_callback, pvt_data);
		if (IS_ERR(cookie))
			printk(KERN_ERR "%s:Failed to register for prim_panel events\n", __func__);
		}
	} else if (retry_count > 0) {
		printk(KERN_ERR "%s:prim_panel is NULL Failed to register for panel events\n", __func__);
		retry_count--;
		queue_delayed_work(screen_state_wq, &screen_state_dw, 5 * HZ);
	}
#endif
}

static int __init mi_thermal_interface_init(void)
{
	int result;

#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	screen_state_wq = create_singlethread_workqueue("screen_state_wq");
	if (screen_state_wq) {
		INIT_DELAYED_WORK(&screen_state_dw, screen_state_check);
		queue_delayed_work(screen_state_wq, &screen_state_dw, 5 * HZ);
	}
#endif

	cpu_thermal_init();
	result = of_parse_thermal_message();
	if (result)
		printk(KERN_ERR "%s:Thermal: Can not parse thermal message node, return %d\n", __func__,result);
	create_thermal_message_node();
	result = of_parse_mi_powersave();
	if (result)
		printk(KERN_ERR "%s:Thermal: Can not parse mi_powersave node, return %d\n", __func__,result);
	create_mi_powersave_node();

#ifdef CONFIG_MI_THERMAL_MULTI_CHARGE
	INIT_WORK(&usb_state.usb_state_work, usb_online_work);
	usb_state.psy_nb.notifier_call=usb_online_callback;
	result =  power_supply_reg_notifier(&usb_state.psy_nb);
	if (result < 0) {
		pr_err("usb online notifier registration error. return: %d\n",
			result);
	}
#endif

	return 0;
}

static void __exit mi_thermal_interface_exit(void)
{
#if defined(CONFIG_OF) && defined(CONFIG_DRM_PANEL)
	if (screen_state_wq) {
		cancel_delayed_work_sync(&screen_state_dw);
		destroy_workqueue(screen_state_wq);
	}

	if (prim_panel && !IS_ERR(cookie)) {
		panel_event_notifier_unregister(cookie);
	} else {
		printk(KERN_ERR "%s:prim_panel_event_notifier_unregister falt\n", __func__);
	}
#endif
	destroy_thermal_message_node();
	destroy_mi_powersave_node();
	destory_thermal_cpu();
}

module_init(mi_thermal_interface_init);
module_exit(mi_thermal_interface_exit);

MODULE_AUTHOR("Xiaomi thermal team");
MODULE_DESCRIPTION("Xiaomi thermal control  interface");
MODULE_LICENSE("GPL v2");
