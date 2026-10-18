/* Xiaomi additions to qcacld-3.0 (reconstructed from OS4 stock, ): prototypes used by the hooks in the public files. */
#ifndef XIAOMI_EXT_H
#define XIAOMI_EXT_H

#include <qdf_status.h>

struct kobject;
struct wlan_objmgr_psoc;
struct wlan_mlme_psoc_ext_obj;

int hdd_sysfs_p2phc_switch_create(struct kobject *driver_kobject);
void hdd_sysfs_p2phc_switch_destroy(struct kobject *driver_kobject);

QDF_STATUS wlan_mlme_start_miracast_opt(struct wlan_objmgr_psoc *psoc);
QDF_STATUS wlan_mlme_stop_miracast_opt(struct wlan_objmgr_psoc *psoc);
QDF_STATUS wlan_mlme_init_miracast_opt(struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext);
QDF_STATUS wlan_mlme_deinit_miracast_opt(struct wlan_mlme_psoc_ext_obj *mlme_psoc_ext);

int p2phc_cleanup(void);

#endif
