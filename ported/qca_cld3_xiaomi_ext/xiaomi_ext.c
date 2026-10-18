// SPDX-License-Identifier: GPL-2.0
/*
 * Xiaomi additions to the CAF qcacld-3.0 host driver (not in any public tree), reconstructed from the OS4 stock qca_cld3_*.ko
 *.  Part 1: p2phc sysfs knobs + statistics thread, MLME miracast optimisation timer.
 * Hook points in the public files are listed in work/re/src/qca_cld3_recipe/xiaomi_ext.patch.
 */
#include <wlan_hdd_includes.h>
#include "osif_psoc_sync.h"
#include <wlan_hdd_sysfs.h>
#include <linux/kthread.h>
#include <qdf_lock.h>
#include <qdf_timer.h>
#include <wlan_mlme_main.h>
#include <wlan_psoc_mlme_api.h>
#include <wma_api.h>
#include <wlan_hdd_object_manager.h>
#include <wlan_cm_api.h>
#include "p2phc.h"
#include "xiaomi_ext.h"

#define p2phc_info(fmt, ...) printk(KERN_INFO "p2phc-i: " fmt, ##__VA_ARGS__)

/* ---------------------------------------------------------------- p2phc statistics thread */
static int p2phc_thread(void *data)
{
	struct p2phc *p = data;
	struct dlhc *hc;

	p2phc_info("%s", __func__);
	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(sysctl_p2phc_interval * HZ);

		p2phc_info("###### TCP stat ######");
		hc = p->hc;
		spin_lock(&p->rx_tcp_lock);
		p2phc_info("i_uncompressed: %d", hc->tcp.sls_i_uncompressed);
		p2phc_info("i_compressed:   %d", hc->tcp.sls_i_compressed);
		p2phc_info("i_error:        %d", hc->tcp.sls_i_error);
		p2phc_info("i_tossed:       %d", hc->tcp.sls_i_tossed);
		p2phc_info("i_runt:         %d", hc->tcp.sls_i_runt);
		p2phc_info("i_badcheck:     %d", hc->tcp.sls_i_badcheck);
		spin_unlock(&p->rx_tcp_lock);
		spin_lock(&p->tx_tcp_lock);
		p2phc_info("o_uncompressed: %d", hc->tcp.sls_o_uncompressed);
		p2phc_info("o_compressed:   %d", hc->tcp.sls_o_compressed);
		p2phc_info("o_tcp:          %d", hc->tcp.sls_o_tcp);
		p2phc_info("o_searches:     %d", hc->tcp.sls_o_searches);
		p2phc_info("o_misses:       %d", hc->tcp.sls_o_misses);
		spin_unlock(&p->tx_tcp_lock);
		p2phc_info("###### UDP stat ######");
		spin_lock(&p->rx_udp_lock);
		p2phc_info("i_uncompressed: %d", hc->udp.sls_i_uncompressed);
		p2phc_info("i_compressed:   %d", hc->udp.sls_i_compressed);
		p2phc_info("i_error:        %d", hc->udp.sls_i_error);
		p2phc_info("i_tossed:       %d", hc->udp.sls_i_tossed);
		p2phc_info("i_runt:         %d", hc->udp.sls_i_runt);
		p2phc_info("i_badcheck:     %d", hc->udp.sls_i_badcheck);
		spin_unlock(&p->rx_udp_lock);
		spin_lock(&p->tx_udp_lock);
		p2phc_info("o_uncompressed: %d", hc->udp.sls_o_uncompressed);
		p2phc_info("o_compressed:   %d", hc->udp.sls_o_compressed);
		p2phc_info("o_searches:     %d", hc->udp.sls_o_searches);
		p2phc_info("o_misses:       %d", hc->udp.sls_o_misses);
		spin_unlock(&p->tx_udp_lock);
		p2phc_info("###### stat end ######");
	}
	return 0;
}

/* ---------------------------------------------------------------- p2phc sysfs (driver kobject) */
static ssize_t __hdd_sysfs_p2phc_enable_store(struct hdd_context *hdd_ctx, struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1] = { 0 };
	char *sptr, *token;
	uint32_t value = 0;
	int ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local), buf, count);
	if (ret) {
		hdd_err_rl("invalid input");
		return ret;
	}

	sptr = buf_local;
	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &value))
		return -EINVAL;

	if (sysctl_p2phc_enable == value)
		return count;
	if (value) {
		if (!p2phc_init())
			sysctl_p2phc_enable = value;
	} else {
		sysctl_p2phc_enable = 0;
		p2phc_cleanup();
	}
	return count;
}

static ssize_t hdd_sysfs_p2phc_enable_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
					    size_t count)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;
	int ret;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret != 0)
		return ret;

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy), &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_p2phc_enable_store(hdd_ctx, attr, buf, count);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

static ssize_t __hdd_sysfs_p2phc_interval_store(struct hdd_context *hdd_ctx, struct kobj_attribute *attr,
						const char *buf, size_t count)
{
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1] = { 0 };
	char *sptr, *token;
	uint32_t value = 0;
	int ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local), buf, count);
	if (ret) {
		hdd_err_rl("invalid input");
		return ret;
	}

	sptr = buf_local;
	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &value))
		return -EINVAL;

	if (value >= 1 && value <= 20)
		sysctl_p2phc_interval = value;
	else
		p2phc_info("Time interval does not meet the range");
	return count;
}

static ssize_t hdd_sysfs_p2phc_interval_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
					      size_t count)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;
	int ret;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret != 0)
		return ret;

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy), &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_p2phc_interval_store(hdd_ctx, attr, buf, count);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

static ssize_t __hdd_sysfs_p2phc_debug_store(struct hdd_context *hdd_ctx, struct kobj_attribute *attr,
					     const char *buf, size_t count)
{
	char buf_local[MAX_SYSFS_USER_COMMAND_SIZE_LENGTH + 1] = { 0 };
	char *sptr, *token;
	uint32_t value = 0;
	uint32_t mode;
	int ret;

	if (!wlan_hdd_validate_modules_state(hdd_ctx))
		return -EINVAL;

	ret = hdd_sysfs_validate_and_copy_buf(buf_local, sizeof(buf_local), buf, count);
	if (ret) {
		hdd_err_rl("invalid input");
		return ret;
	}

	sptr = buf_local;
	token = strsep(&sptr, " ");
	if (!token)
		return -EINVAL;
	if (kstrtou32(token, 0, &value))
		return -EINVAL;

	if (sysctl_p2phc_enable) {
		sysctl_p2phc_debug = value;
		mode = value;
	} else {
		mode = sysctl_p2phc_debug;
	}

	if (mode == 0) {
		if (g_p2phc.thread) {
			kthread_stop(g_p2phc.thread);
			g_p2phc.thread = NULL;
		}
	} else if (mode == 1) {
		if (!g_p2phc.thread) {
			g_p2phc.thread = kthread_run(p2phc_thread, &g_p2phc, "p2phc_thread");
			if (IS_ERR_OR_NULL(g_p2phc.thread)) {
				printk(KERN_ERR "p2phc-e: %s kthread_run err", __func__);
				unregister_netdevice_notifier(&p2phc_nb);
			}
		}
	} else {
		hdd_err_rl("invalid input");
	}
	return count;
}

static ssize_t hdd_sysfs_p2phc_debug_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf,
					   size_t count)
{
	struct osif_psoc_sync *psoc_sync;
	struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);
	ssize_t errno_size;
	int ret;

	ret = wlan_hdd_validate_context(hdd_ctx);
	if (ret != 0)
		return ret;

	errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy), &psoc_sync);
	if (errno_size)
		return errno_size;

	errno_size = __hdd_sysfs_p2phc_debug_store(hdd_ctx, attr, buf, count);

	osif_psoc_sync_op_stop(psoc_sync);

	return errno_size;
}

#define P2PHC_SHOW(name)                                                                                     \
	static ssize_t __hdd_sysfs_p2phc_##name##_show(struct hdd_context *hdd_ctx, char *buf)               \
	{                                                                                                    \
		if (!wlan_hdd_validate_modules_state(hdd_ctx))                                               \
			return -EINVAL;                                                                      \
		return scnprintf(buf, 8, "%d\n", sysctl_p2phc_##name);                                       \
	}                                                                                                    \
	static ssize_t hdd_sysfs_p2phc_##name##_show(struct kobject *kobj, struct kobj_attribute *attr,      \
						     char *buf)                                              \
	{                                                                                                    \
		struct osif_psoc_sync *psoc_sync;                                                            \
		struct hdd_context *hdd_ctx = cds_get_context(QDF_MODULE_ID_HDD);                            \
		ssize_t errno_size;                                                                          \
		int ret;                                                                                     \
                                                                                                             \
		ret = wlan_hdd_validate_context(hdd_ctx);                                                    \
		if (ret != 0)                                                                                \
			return ret;                                                                          \
		errno_size = osif_psoc_sync_op_start(wiphy_dev(hdd_ctx->wiphy), &psoc_sync);                 \
		if (errno_size)                                                                              \
			return errno_size;                                                                   \
		errno_size = __hdd_sysfs_p2phc_##name##_show(hdd_ctx, buf);                                  \
		osif_psoc_sync_op_stop(psoc_sync);                                                           \
		return errno_size;                                                                           \
	}

P2PHC_SHOW(debug)
P2PHC_SHOW(interval)
P2PHC_SHOW(enable)

static struct kobj_attribute sysfs_sysctl_p2phc_debug = {
	.attr = { .name = "p2phc_debug", .mode = 0660 },
	.show = hdd_sysfs_p2phc_debug_show,
	.store = hdd_sysfs_p2phc_debug_store,
};

static struct kobj_attribute sysfs_sysctl_p2phc_interval = {
	.attr = { .name = "p2phc_interval", .mode = 0660 },
	.show = hdd_sysfs_p2phc_interval_show,
	.store = hdd_sysfs_p2phc_interval_store,
};

static struct kobj_attribute sysfs_sysctl_p2phc_enable = {
	.attr = { .name = "p2phc_enable", .mode = 0666 },
	.show = hdd_sysfs_p2phc_enable_show,
	.store = hdd_sysfs_p2phc_enable_store,
};

int hdd_sysfs_p2phc_switch_create(struct kobject *driver_kobject)
{
	int error;

	if (!driver_kobject) {
		hdd_err("could not get driver kobject!");
		return -EINVAL;
	}

	error = sysfs_create_file(driver_kobject, &sysfs_sysctl_p2phc_debug.attr);
	if (error) {
		hdd_err("Failed to create sysfs file p2phc_debug");
		return -EINVAL;
	}

	error = sysfs_create_file(driver_kobject, &sysfs_sysctl_p2phc_interval.attr);
	if (error) {
		hdd_err("Failed to create sysfs file p2phc_interval");
		sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_debug.attr);
		return -EINVAL;
	}

	error = sysfs_create_file(driver_kobject, &sysfs_sysctl_p2phc_enable.attr);
	if (error) {
		hdd_err("Failed to create sysfs file p2phc_enable");
		sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_debug.attr);
		sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_interval.attr);
		return -EINVAL;
	}
	return error;
}

void hdd_sysfs_p2phc_switch_destroy(struct kobject *driver_kobject)
{
	if (!driver_kobject) {
		hdd_err("could not get p2phc kobject!");
		return;
	}

	sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_enable.attr);
	sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_interval.attr);
	sysfs_remove_file(driver_kobject, &sysfs_sysctl_p2phc_debug.attr);
}

/* ---------------------------------------------------------------- MLME miracast optimisation */
void wlan_mlme_miracast_opt_timer_handler(void *arg);

QDF_STATUS wlan_mlme_start_miracast_opt(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext;

	mlme_debug("enter");
	mlme_psoc_ext = wlan_psoc_mlme_get_ext_hdl(psoc);
	if (!mlme_psoc_ext)
		return QDF_STATUS_E_INVAL;

	qdf_timer_mod(&mlme_psoc_ext->miracast_opt_timer, 5000);
	qdf_wake_lock_timeout_acquire(&mlme_psoc_ext->miracast_opt_wl, 20000);
	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_mlme_stop_miracast_opt(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext;

	mlme_debug("enter");
	mlme_psoc_ext = wlan_psoc_mlme_get_ext_hdl(psoc);
	if (!mlme_psoc_ext)
		return QDF_STATUS_E_INVAL;

	qdf_timer_sync_cancel(&mlme_psoc_ext->miracast_opt_timer);
	qdf_wake_lock_release(&mlme_psoc_ext->miracast_opt_wl, 0x20);
	mlme_debug("stop miracast_opt");
	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_mlme_init_miracast_opt(struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext)
{
	mlme_debug("enter");
	qdf_timer_init(NULL, &mlme_psoc_ext->miracast_opt_timer, wlan_mlme_miracast_opt_timer_handler, mlme_psoc_ext,
		       QDF_TIMER_TYPE_SW);
#line 9045
	qdf_wake_lock_create(&mlme_psoc_ext->miracast_opt_wl, "miracast_opt");
	return QDF_STATUS_SUCCESS;
}

void wlan_mlme_miracast_opt_timer_handler(void *arg)
{
	struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext = arg;

	mlme_debug("enter");
	wma_cli_set_command(0, wmi_pdev_param_power_collapse_enable, 1, PDEV_CMD);
	qdf_wake_lock_release(&mlme_psoc_ext->miracast_opt_wl, 0x20);
}

QDF_STATUS wlan_mlme_deinit_miracast_opt(struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext)
{
	mlme_debug("enter");
	qdf_timer_stop(&mlme_psoc_ext->miracast_opt_timer);
	qdf_timer_sync_cancel(&mlme_psoc_ext->miracast_opt_timer);
#line 9056
	qdf_wake_lock_destroy(&mlme_psoc_ext->miracast_opt_wl);
	return QDF_STATUS_SUCCESS;
}

/* ---------------------------------------------------------------- misc */
bool cm_is_vdev_connected(struct wlan_objmgr_vdev *vdev);

bool hdd_cm_is_connected(struct wlan_hdd_link_info *link_info)
{
	struct wlan_objmgr_vdev *vdev;
	bool connected = false;

	vdev = hdd_objmgr_get_vdev_by_user(link_info, (wlan_objmgr_ref_dbgid)0x58);
	if (!vdev)
		return false;

	if (!(wlan_vdev_mlme_get_opmode(vdev) & ~QDF_P2P_CLIENT_MODE))
		connected = cm_is_vdev_connected(vdev);

	hdd_objmgr_put_vdev_by_user(vdev, (wlan_objmgr_ref_dbgid)0x58);
	return connected;
}
