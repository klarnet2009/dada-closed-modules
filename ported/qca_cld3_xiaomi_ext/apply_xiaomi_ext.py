"""apply_xiaomi_ext.py <pkgdir> : idempotent hook-point patches of the public CAF qcacld-3.0 package for the Xiaomi additions
.  Every edit is a (file, anchor, replacement) triple; an edit is skipped if its marker text is already present."""
import sys, re

pkg = sys.argv[1]


def edit(path, old, new, marker=None, count=1):
    p = pkg + "/" + path
    s = open(p, newline="").read()
    if (marker or new) in s:
        print("skip (already applied):", path, (marker or new)[:50].replace("\n", " "))
        return
    if old not in s:
        print("ANCHOR NOT FOUND:", path, old[:60].replace("\n", " "))
        sys.exit(1)
    s = s.replace(old, new, count)
    open(p, "w", newline="").write(s)
    print("patched:", path, (marker or new)[:50].replace("\n", " "))


# --- MLME: miracast optimisation state in the psoc ext object (appended: wake lock, timer)
edit("components/mlme/core/inc/wlan_mlme_main.h",
     "\twlan_scan_requester scan_requester_id;\n};",
     "\twlan_scan_requester scan_requester_id;\n\tqdf_wake_lock_t miracast_opt_wl;\n\tqdf_timer_t miracast_opt_timer;\n};",
     marker="miracast_opt_wl")
edit("components/mlme/core/src/wlan_mlme_vdev_mgr_interface.c",
     "\ttarget_if_mlme_register_tx_ops(\n\t\t\t&psoc_mlme->ext_psoc_ptr->mlme_tx_ops);\n\n\treturn QDF_STATUS_SUCCESS;",
     "\ttarget_if_mlme_register_tx_ops(\n\t\t\t&psoc_mlme->ext_psoc_ptr->mlme_tx_ops);\n\n\twlan_mlme_init_miracast_opt(psoc_mlme->ext_psoc_ptr);\n\n\treturn QDF_STATUS_SUCCESS;",
     marker="wlan_mlme_init_miracast_opt")
edit("components/mlme/core/src/wlan_mlme_vdev_mgr_interface.c",
     "\tif (psoc_mlme->ext_psoc_ptr) {\n\t\tqdf_mem_free(psoc_mlme->ext_psoc_ptr);",
     "\tif (psoc_mlme->ext_psoc_ptr) {\n\t\twlan_mlme_deinit_miracast_opt(psoc_mlme->ext_psoc_ptr);\n\t\tqdf_mem_free(psoc_mlme->ext_psoc_ptr);",
     marker="wlan_mlme_deinit_miracast_opt")
edit("components/mlme/core/src/wlan_mlme_vdev_mgr_interface.c",
     "QDF_STATUS psoc_mlme_ext_hdl_create(struct psoc_mlme_obj *psoc_mlme)\n{",
     "QDF_STATUS psoc_mlme_ext_hdl_create(struct psoc_mlme_obj *psoc_mlme)\n{",
     marker="xiaomi_ext.h")
s = open(pkg + "/components/mlme/core/src/wlan_mlme_vdev_mgr_interface.c", newline="").read()
if "xiaomi_ext.h" not in s:
    i = s.index("#include")
    s = s[:i] + '#include "xiaomi_ext.h"\n' + s[i:]
    open(pkg + "/components/mlme/core/src/wlan_mlme_vdev_mgr_interface.c", "w", newline="").write(s)

# --- HDD ioctl: DRIVER MIRACAST 128/129 (conn opt) start/stop the optimisation timer
edit("core/hdd/src/wlan_hdd_ioctl.c",
     """	case MIRACAST_CONN_OPT_ENABLED:
	case MIRACAST_CONN_OPT_DISABLED:
		{
			wma_cli_set_command(
				link_info->vdev_id,
				wmi_pdev_param_power_collapse_enable,
				(filter_type == MIRACAST_CONN_OPT_ENABLED ?
				 0 : 1), PDEV_CMD);
			return 0;
		}""",
     """	case MIRACAST_CONN_OPT_ENABLED:
		wlan_mlme_start_miracast_opt(hdd_ctx->psoc);
		wma_cli_set_command(0, wmi_pdev_param_power_collapse_enable,
				    0, PDEV_CMD);
		return 0;
	case MIRACAST_CONN_OPT_DISABLED:
		wma_cli_set_command(0, wmi_pdev_param_power_collapse_enable,
				    1, PDEV_CMD);
		wlan_mlme_stop_miracast_opt(hdd_ctx->psoc);
		return 0;""",
     marker="wlan_mlme_start_miracast_opt")
s = open(pkg + "/core/hdd/src/wlan_hdd_ioctl.c", newline="").read()
if "xiaomi_ext.h" not in s:
    i = s.index("#include")
    s = s[:i] + '#include "xiaomi_ext.h"\n' + s[i:]
    open(pkg + "/core/hdd/src/wlan_hdd_ioctl.c", "w", newline="").write(s)

# --- HDD sysfs: p2phc kobject + knobs
f = "core/hdd/src/wlan_hdd_sysfs.c"
edit(f, "static struct kobject *wlan_kobject;", "static struct kobject *wlan_kobject;\nstatic struct kobject *p2phc_kobject;",
     marker="p2phc_kobject;")
edit(f,
     """	wlan_kobject = kobject_create_and_add("wlan", driver_kobject);
	if (!wlan_kobject) {""",
     """	p2phc_kobject = kobject_create_and_add("p2phc", driver_kobject);
	if (!p2phc_kobject) {
		hdd_err("could not allocate p2phc kobject");
		kobject_put(driver_kobject);
		driver_kobject = NULL;
		return;
	}

	wlan_kobject = kobject_create_and_add("wlan", driver_kobject);
	if (!wlan_kobject) {""", marker='kobject_create_and_add("p2phc"')
edit(f,
     """static void hdd_sysfs_destroy_driver_root_obj(void)
{
	if (wlan_kobject) {""",
     """static void hdd_sysfs_destroy_driver_root_obj(void)
{
	if (p2phc_kobject) {
		kobject_put(p2phc_kobject);
		p2phc_kobject = NULL;
	}

	if (wlan_kobject) {""", marker="kobject_put(p2phc_kobject)")
edit(f, "\thdd_sysfs_create_version_interface(hdd_ctx->psoc);\n\thdd_sysfs_mem_stats_create(wlan_kobject);",
     "\thdd_sysfs_create_version_interface(hdd_ctx->psoc);\n\thdd_sysfs_p2phc_switch_create(p2phc_kobject);\n\thdd_sysfs_mem_stats_create(wlan_kobject);",
     marker="hdd_sysfs_p2phc_switch_create(p2phc_kobject)")
edit(f, "\thdd_sysfs_mem_stats_destroy(wlan_kobject);\n\thdd_sysfs_destroy_version_interface();",
     "\thdd_sysfs_mem_stats_destroy(wlan_kobject);\n\thdd_sysfs_p2phc_switch_destroy(p2phc_kobject);\n\thdd_sysfs_destroy_version_interface();",
     marker="hdd_sysfs_p2phc_switch_destroy(p2phc_kobject)")
s = open(pkg + "/" + f, newline="").read()
if "xiaomi_ext.h" not in s:
    i = s.index("#include")
    s = s[:i] + '#include "xiaomi_ext.h"\n' + s[i:]
    open(pkg + "/" + f, "w", newline="").write(s)

# --- module exit: stop p2phc after the driver is unloaded
f = "core/hdd/src/wlan_hdd_main.c"
s = open(pkg + "/" + f, newline="").read()
pat = "static void __exit hdd_module_exit(void)\n{\n\thdd_driver_unload();\n"
if pat + "\tp2phc_cleanup();" not in s:
    s = s.replace(pat, pat + "\tp2phc_cleanup();\n")
    if "xiaomi_ext.h" not in s:
        i = s.index("#include")
        s = s[:i] + '#include "xiaomi_ext.h"\n' + s[i:]
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "p2phc_cleanup")
else:
    print("skip:", f)

# --- build list
edit("g4_modules.bzl", '    "xiaomi_ext/xiaomi_ext.c",\n',
     '    "xiaomi_ext/xiaomi_ext.c",\n    "xiaomi_ext/p2phc_core.c",\n    "xiaomi_ext/dlhc.c",\n', marker="xiaomi_ext/dlhc.c")
edit("g4_modules.bzl", '_fixed_ipaths = [\n    "api/fw",\n', '_fixed_ipaths = [\n    "xiaomi_ext",\n    "api/fw",\n', marker='"xiaomi_ext",\n    "api/fw"')
s = open(pkg + "/BUILD.bazel", newline="").read()
if "CONFIG_QCOM_MINIDUMP=1" not in s:
    s = s.replace('"CONFIG_QCOM_VA_MINIDUMP=1"', '"CONFIG_QCOM_VA_MINIDUMP=1", "CONFIG_QCOM_MINIDUMP=1"')
    open(pkg + "/BUILD.bazel", "w", newline="").write(s)
    print("patched: BUILD.bazel CONFIG_QCOM_MINIDUMP")

# --- functions taken from newer CAF code (present in the stock, absent in CLO r1-07500): appended at the end of the file
import os
here = os.path.dirname(os.path.abspath(__file__))


def append(path, snippet, marker):
    p = pkg + "/" + path
    s = open(p, newline="").read()
    if marker in s:
        print("skip (already applied):", path, marker)
        return
    s += open(here + "/" + snippet, newline="").read()
    open(p, "w", newline="").write(s)
    print("appended:", path, marker)


append("components/pmo/core/src/wlan_pmo_ns.c", "snip_pmo_ns.c", "pmo_core_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev)\n{")
append("components/pmo/dispatcher/src/wlan_pmo_ucfg_api.c", "snip_pmo_ucfg.c", "ucfg_pmo_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev)\n{")
append("components/tdls/dispatcher/src/wlan_tdls_api.c", "snip_tdls_api.c", "bool wlan_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev, struct qdf_mac_addr *mac_addr)\n{")
append("components/tdls/dispatcher/src/wlan_tdls_ucfg_api.c", "snip_tdls_ucfg.c", "bool ucfg_tdls_is_key_install_allowed(struct wlan_objmgr_vdev *vdev, struct qdf_mac_addr *mac_addr)\n{")
append("core/wma/src/wma_power.c", "snip_wma_power.c", "void wma_is_both_psd_eirp_support_present_for_sp(struct reg_tpc_power_info *param, bool *value)\n{")

# --- wmi: out-of-line is_both_psd_eirp_support_present_for_sp with the two extra level-count conditions of the stock
edit("cmn/wmi/src/wmi_unified_tlv.c",
     "static inline\nbool is_both_psd_eirp_support_present_for_sp(wmi_unified_t wmi_handle,\n\t\t\t\t\t     struct reg_tpc_power_info *param)\n{\n\treturn (is_service_enabled_tlv(wmi_handle,\n\t\t    WMI_SERVICE_BOTH_PSD_EIRP_FOR_AP_SP_CLIENT_SP_SUPPORT) &&\n\t\t(param->power_type_6g == REG_STANDARD_POWER_AP ||\n\t\t\tparam->is_power_type_client_sp));\n}",
     "bool is_both_psd_eirp_support_present_for_sp(wmi_unified_t wmi_handle,\n\t\t\t\t\t     struct reg_tpc_power_info *param)\n{\n\treturn (is_service_enabled_tlv(wmi_handle,\n\t\t    WMI_SERVICE_BOTH_PSD_EIRP_FOR_AP_SP_CLIENT_SP_SUPPORT) &&\n\t\t(param->power_type_6g == REG_STANDARD_POWER_AP ||\n\t\t\tparam->is_power_type_client_sp) &&\n\t\tparam->num_psd_pwr_levels && param->num_eirp_pwr_levels);\n}",
     marker="param->num_psd_pwr_levels && param->num_eirp_pwr_levels")

# --- g4l: MLME generic cfg gets the Xiaomi INI "eht_mlo_crypto_open_allow" (u32, 0..1, default 0, between monitor_mode_concurrency/WDS and
# tx_retry_multiplier in the ini table) -> gen field at +0x470 (after oem_eht_mlo_crypto_bitmap), getter wlan_mlme_get_eht_mlo_crypto_config
edit("components/mlme/dispatcher/inc/wlan_mlme_public_struct.h",
     "\tuint32_t oem_eht_mlo_crypto_bitmap;\n#endif",
     "\tuint32_t oem_eht_mlo_crypto_bitmap;\n\tuint32_t eht_mlo_crypto_open_allow;\n#endif",
     marker="eht_mlo_crypto_open_allow;")
edit("components/mlme/dispatcher/inc/cfg_mlme_generic.h",
     "#define CFG_GENERIC_ALL \\\n",
     """#ifdef WLAN_FEATURE_11BE
#define CFG_EHT_MLO_CRYPTO_OPEN_ALLOW CFG_INI_UINT( \\
		"eht_mlo_crypto_open_allow", \\
		0, \\
		1, \\
		0, \\
		CFG_VALUE_OR_DEFAULT, \\
		"Allow EHT/MLO connection to open (no RSN) APs")

#define CFG_EHT_MLO_CRYPTO_OPEN_ALLOW_SUPPORTED \\
			CFG(CFG_EHT_MLO_CRYPTO_OPEN_ALLOW)
#else
#define CFG_EHT_MLO_CRYPTO_OPEN_ALLOW_SUPPORTED
#endif

#define CFG_GENERIC_ALL \\
""", marker="CFG_EHT_MLO_CRYPTO_OPEN_ALLOW_SUPPORTED\n")
edit("components/mlme/dispatcher/inc/cfg_mlme_generic.h",
     "\tCFG_WDS_MODE_ALL \\\n\tCFG(CFG_TX_RETRY_MULTIPLIER) \\\n",
     "\tCFG_WDS_MODE_ALL \\\n\tCFG_EHT_MLO_CRYPTO_OPEN_ALLOW_SUPPORTED \\\n\tCFG(CFG_TX_RETRY_MULTIPLIER) \\\n",
     marker="CFG_EHT_MLO_CRYPTO_OPEN_ALLOW_SUPPORTED \\\n\tCFG(CFG_TX")
edit("components/mlme/core/src/wlan_mlme_main.c",
     "\tgen->oem_eht_mlo_crypto_bitmap =\n\t\t\t\tcfg_get(psoc, CFG_OEM_EHT_MLO_CRYPTO_BITMAP);\n",
     "\tgen->oem_eht_mlo_crypto_bitmap =\n\t\t\t\tcfg_get(psoc, CFG_OEM_EHT_MLO_CRYPTO_BITMAP);\n\tgen->eht_mlo_crypto_open_allow =\n\t\t\t\tcfg_get(psoc, CFG_EHT_MLO_CRYPTO_OPEN_ALLOW);\n",
     marker="cfg_get(psoc, CFG_EHT_MLO_CRYPTO_OPEN_ALLOW)")
edit("components/mlme/dispatcher/src/wlan_mlme_api.c",
     "\t*oem_eht_cfg = mlme_obj->cfg.gen.oem_eht_mlo_crypto_bitmap;\n\n\treturn QDF_STATUS_SUCCESS;\n}\n",
     """\t*oem_eht_cfg = mlme_obj->cfg.gen.oem_eht_mlo_crypto_bitmap;

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,
					       uint32_t *open_allow)
{
	struct wlan_mlme_psoc_ext_obj *mlme_obj;

	mlme_obj = mlme_get_psoc_ext_obj(psoc);
	if (!mlme_obj)
		return QDF_STATUS_E_FAILURE;

	*open_allow = mlme_obj->cfg.gen.eht_mlo_crypto_open_allow;

	return QDF_STATUS_SUCCESS;
}
""", marker="wlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,\n\t\t\t\t\t       uint32_t *open_allow)\n{")
edit("components/mlme/dispatcher/inc/wlan_mlme_api.h",
     "QDF_STATUS wlan_mlme_get_oem_eht_mlo_config(struct wlan_objmgr_psoc *psoc,\n\t\t\t\t\t    uint32_t *oem_eht_cfg);\n#else",
     """QDF_STATUS wlan_mlme_get_oem_eht_mlo_config(struct wlan_objmgr_psoc *psoc,
					    uint32_t *oem_eht_cfg);

/**
 * wlan_mlme_get_eht_mlo_crypto_config() - Get the EHT/MLO open crypto allow cfg
 * @psoc: PSOC object manager.
 * @open_allow: Pointer to fill the value
 *
 * Return: QDF_STATUS.
 */
QDF_STATUS wlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,
					       uint32_t *open_allow);
#else""", marker="wlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,\n\t\t\t\t\t       uint32_t *open_allow);")
edit("components/mlme/dispatcher/inc/wlan_mlme_api.h",
     "\t*oem_eht_cfg = 0x0;\n\treturn QDF_STATUS_SUCCESS;\n}\n",
     "\t*oem_eht_cfg = 0x0;\n\treturn QDF_STATUS_SUCCESS;\n}\n\nstatic inline QDF_STATUS\nwlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,\n\t\t\t\t    uint32_t *open_allow)\n{\n\t*open_allow = 0x0;\n\treturn QDF_STATUS_SUCCESS;\n}\n",
     marker="wlan_mlme_get_eht_mlo_crypto_config(struct wlan_objmgr_psoc *psoc,\n\t\t\t\t    uint32_t *open_allow)")

# --- g4l: four more Xiaomi INI items found in the stock cfg_meta_lookup_table (the public CAF table has 1222 entries, the stock 1227):
#   mi_sap_only_allow_sta_dfs_indoor_chan (policy_mgr), g_mwdl_count (hdd: number of extra "mwdl%d" p2p interfaces),
#   gEnableNUDFailureNotify / gEnableBmissNoity (dp, next to gEnableNUDTracking).  All UINT, default 0 (no explicit default store in the stock cfg_parse).
P = "components/cmn_services/policy_mgr/"
edit(P + "inc/cfg_policy_mgr.h",
     "#define CFG_MOVE_SAP_GO_1ST_ON_DFS_STA_CSA \\\n",
     """#define CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN \\
CFG_INI_UINT("mi_sap_only_allow_sta_dfs_indoor_chan", 0, 1, 0, CFG_VALUE_OR_DEFAULT, \\
	     "Allow SAP only on STA DFS/indoor channel (Xiaomi)")

#define CFG_MOVE_SAP_GO_1ST_ON_DFS_STA_CSA \\
""", marker="CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN \\\nCFG_INI_UINT")
edit(P + "inc/cfg_policy_mgr.h",
     "\t\tCFG(CFG_STA_SAP_SCC_ON_DFS_CHAN)\\\n",
     "\t\tCFG(CFG_STA_SAP_SCC_ON_DFS_CHAN)\\\n\t\tCFG(CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN)\\\n",
     marker="CFG(CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN)")
edit(P + "src/wlan_policy_mgr_i.h", "\tuint8_t go_force_scc;\n",
     "\tuint8_t go_force_scc;\n\tbool mi_sap_only_allow_sta_dfs_indoor_chan;\n", marker="bool mi_sap_only_allow_sta_dfs_indoor_chan;")
edit(P + "src/wlan_policy_mgr_ucfg.c",
     "\tcfg->sta_sap_scc_on_dfs_chnl =\n\t\tcfg_get(psoc, CFG_STA_SAP_SCC_ON_DFS_CHAN);\n",
     "\tcfg->sta_sap_scc_on_dfs_chnl =\n\t\tcfg_get(psoc, CFG_STA_SAP_SCC_ON_DFS_CHAN);\n\tcfg->mi_sap_only_allow_sta_dfs_indoor_chan =\n\t\tcfg_get(psoc, CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN);\n",
     marker="cfg_get(psoc, CFG_MI_SAP_ONLY_ALLOW_STA_DFS_INDOOR_CHAN)")
append(P + "src/wlan_policy_mgr_get_set_utils.c", "snip_pm_mi_sap.c", "bool policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan(struct wlan_objmgr_psoc *psoc)\n{")
edit(P + "inc/wlan_policy_mgr_api.h",
     "bool policy_mgr_get_sta_sap_scc_allowed_on_indoor_chnl(\n\t\t\t\tstruct wlan_objmgr_psoc *psoc);\n",
     "bool policy_mgr_get_sta_sap_scc_allowed_on_indoor_chnl(\n\t\t\t\tstruct wlan_objmgr_psoc *psoc);\n\n/**\n * policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan() - Xiaomi INI mi_sap_only_allow_sta_dfs_indoor_chan\n * @psoc: psoc object\n *\n * Return: value of the INI (true when the policy manager context is missing)\n */\nbool policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan(struct wlan_objmgr_psoc *psoc);\n",
     marker="bool policy_mgr_is_sap_only_allow_sta_dfs_indoor_chan(struct wlan_objmgr_psoc *psoc);")

edit("core/hdd/inc/hdd_config.h",
     "\t\t\"This ini is used to disable P2P concurrency\")\n",
     """\t\t"This ini is used to disable P2P concurrency")

/*
 * <ini>
 * g_mwdl_count - number of additional "mwdl%d" P2P interfaces (Xiaomi)
 * @Min: 0
 * @Max: 8
 * @Default: 0
 *
 * </ini>
 */
#define CFG_MWDL_COUNT CFG_INI_UINT( \\
		"g_mwdl_count", \\
		0, \\
		8, \\
		0, \\
		CFG_VALUE_OR_DEFAULT, \\
		"Number of additional mwdl P2P interfaces")
""", marker="#define CFG_MWDL_COUNT CFG_INI_UINT(")
edit("core/hdd/inc/hdd_config.h", "\tCFG(CFG_NO_P2P_CONCURRENCY) \\\n",
     "\tCFG(CFG_NO_P2P_CONCURRENCY) \\\n\tCFG(CFG_MWDL_COUNT) \\\n", marker="CFG(CFG_MWDL_COUNT)")

# hdd_open_p2p_interface(hdd_ctx, is_mwdl): second parameter selects the "mwdl%d" interface name; mission mode opens g_mwdl_count of them
f = "core/hdd/src/wlan_hdd_main.c"
s = open(pkg + "/" + f, newline="").read()
if "is_mwdl" not in s:
    n0 = s.count("hdd_open_p2p_interface(struct hdd_context *hdd_ctx)\n{")
    s = s.replace("hdd_open_p2p_interface(struct hdd_context *hdd_ctx)\n{", "hdd_open_p2p_interface(struct hdd_context *hdd_ctx,\n\t\t\t\t\t       int is_mwdl)\n{")
    s = s.replace("hdd_open_p2p_interface(hdd_ctx);", "hdd_open_p2p_interface(hdd_ctx, false);")
    old = "\tstatus = hdd_open_adapter_no_trans(hdd_ctx, QDF_P2P_DEVICE_MODE,\n\t\t\t\t\t   \"p2p%d\","
    assert old in s
    s = s.replace(old, "\tstatus = hdd_open_adapter_no_trans(hdd_ctx, QDF_P2P_DEVICE_MODE,\n\t\t\t\t\t   is_mwdl == 0 ? \"p2p%d\" : \"mwdl%d\",")
    old = "\tstatus = hdd_open_p2p_interface(hdd_ctx, false);\n\tif (status)\n\t\tgoto err_close_adapters;\n"
    assert old in s
    s = s.replace(old, old + "\n\tmwdl_count = cfg_get(hdd_ctx->psoc, CFG_MWDL_COUNT);\n\thdd_debug(\"p2p_test mwdl_count: %d\", mwdl_count);\n\tfor (i = 0; i < mwdl_count; i++) {\n\t\tstatus = hdd_open_p2p_interface(hdd_ctx, true);\n\t\tif (status)\n\t\t\tgoto err_close_adapters;\n\t}\n")
    old = "\tbool eht_capab = 0;\n\n\tucfg_mlme_get_dot11p_mode(hdd_ctx->psoc, &dot11p_mode);\n\tucfg_psoc_mlme_get_11be_capab(hdd_ctx->psoc, &eht_capab);\n\n\t/* Create only 802.11p interface? */"
    assert old in s
    s = s.replace(old, old.replace("\tbool eht_capab = 0;\n", "\tbool eht_capab = 0;\n\tint mwdl_count, i;\n"))
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "hdd_open_p2p_interface is_mwdl", n0)
else:
    print("skip:", f, "is_mwdl")

D = "components/dp/"
edit(D + "dispatcher/inc/wlan_dp_cfg.h",
     "#define CFG_DP_ENABLE_NUD_TRACKING_ALL \\\n\t\t\tCFG(CFG_DP_ENABLE_NUD_TRACKING)\n",
     """#define CFG_DP_ENABLE_NUD_FAILURE_NOTIFY \\
		CFG_INI_UINT("gEnableNUDFailureNotify", \\
		 0, \\
		 4, \\
		 0, \\
		 CFG_VALUE_OR_DEFAULT, "NUD failure event notification mode (Xiaomi)")

#define CFG_DP_ENABLE_BMISS_NOTIFY \\
		CFG_INI_UINT("gEnableBmissNoity", \\
		 0, \\
		 1, \\
		 0, \\
		 CFG_VALUE_OR_DEFAULT, "Beacon miss event notification (Xiaomi)")

#define CFG_DP_ENABLE_NUD_TRACKING_ALL \\
			CFG(CFG_DP_ENABLE_NUD_TRACKING) \\
			CFG(CFG_DP_ENABLE_NUD_FAILURE_NOTIFY) \\
			CFG(CFG_DP_ENABLE_BMISS_NOTIFY)
""", marker="CFG_DP_ENABLE_NUD_FAILURE_NOTIFY \\\n\t\tCFG_INI_UINT")
edit(D + "core/inc/wlan_dp_priv.h", "\tuint8_t enable_nud_tracking;\n",
     "\tuint8_t enable_nud_tracking;\n\tuint8_t nud_failure_notify;\n\tuint8_t bmiss_notify;\n", marker="uint8_t nud_failure_notify;")
edit(D + "core/src/wlan_dp_main.c",
     "\tconfig->enable_nud_tracking = cfg_get(psoc, CFG_DP_ENABLE_NUD_TRACKING);\n",
     "\tconfig->enable_nud_tracking = cfg_get(psoc, CFG_DP_ENABLE_NUD_TRACKING);\n\tconfig->nud_failure_notify = cfg_get(psoc, CFG_DP_ENABLE_NUD_FAILURE_NOTIFY);\n\tconfig->bmiss_notify = cfg_get(psoc, CFG_DP_ENABLE_BMISS_NOTIFY);\n",
     marker="cfg_get(psoc, CFG_DP_ENABLE_NUD_FAILURE_NOTIFY)")
edit(D + "dispatcher/src/wlan_dp_ucfg_api.c",
     "\treturn dp_ctx->dp_cfg.enable_nud_tracking;\n}\n",
     """\treturn dp_ctx->dp_cfg.enable_nud_tracking;
}

bool ucfg_dp_is_nud_failure_event_notify(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_dp_psoc_context *dp_ctx = dp_psoc_get_priv(psoc);

	if (!dp_ctx) {
		dp_err("Unable to get DP context");
		return false;
	}
	return dp_ctx->dp_cfg.nud_failure_notify == 4;
}

bool ucfg_dp_bmiss_event_notify(struct wlan_objmgr_psoc *psoc)
{
	struct wlan_dp_psoc_context *dp_ctx = dp_psoc_get_priv(psoc);

	if (!dp_ctx) {
		dp_err("DP context is NULL");
		return false;
	}
	return dp_ctx->dp_cfg.bmiss_notify == 1;
}
""", marker="bool ucfg_dp_bmiss_event_notify(struct wlan_objmgr_psoc *psoc)\n{")
edit(D + "dispatcher/inc/wlan_dp_ucfg_api.h",
     "uint8_t ucfg_dp_nud_tracking_enabled(struct wlan_objmgr_psoc *psoc);\n",
     "uint8_t ucfg_dp_nud_tracking_enabled(struct wlan_objmgr_psoc *psoc);\n\n/**\n * ucfg_dp_is_nud_failure_event_notify() - NUD failure notification mode == 4 (Xiaomi)\n * @psoc: PSOC Handle\n */\nbool ucfg_dp_is_nud_failure_event_notify(struct wlan_objmgr_psoc *psoc);\n\n/**\n * ucfg_dp_bmiss_event_notify() - beacon miss notification enabled (Xiaomi)\n * @psoc: PSOC Handle\n */\nbool ucfg_dp_bmiss_event_notify(struct wlan_objmgr_psoc *psoc);\n",
     marker="bool ucfg_dp_bmiss_event_notify(struct wlan_objmgr_psoc *psoc);")

# --- g4l: wlan_cm_is_eht_allowed_for_current_security: Xiaomi version (INI eht_mlo_crypto_open_allow decides for APs without RSN IE; the OEM bitmap is
# fetched only when an RSN IE is present)
f = "cmn/umac/mlme/connection_mgr/core/src/wlan_cm_bss_scoring.c"
s = open(pkg + "/" + f, newline="").read()
if "eht_mlo_crypto_open_allow" not in s:
    old1 = "\tuint32_t oem_eht_cfg = 0x0;\n\tbool mlie_present;\n"
    old2 = """	/* Get the OEM EHT configuration. */
	status = wlan_mlme_get_oem_eht_mlo_config(psoc, &oem_eht_cfg);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlme_rl_nofl_err("OEM EHT cfg get failed");
		return false;
	}

	if (!entry->ie_list.rsn) {
		mlme_debug(QDF_MAC_ADDR_FMT ": RSN IE not present, is allowed: %d",
			   QDF_MAC_ADDR_REF(entry->bssid.bytes), WLAN_CRYPTO_NONE_OEM_EHT_CFG_ALLOWED(oem_eht_cfg));
		return WLAN_CRYPTO_NONE_OEM_EHT_CFG_ALLOWED(oem_eht_cfg);
	}
"""
    new2 = """	status = wlan_mlme_get_eht_mlo_crypto_config(psoc, &open_allow);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlme_rl_nofl_err("EHT mlo crypto cfg get failed");
		return false;
	}

	if (!entry->ie_list.rsn) {
		mlme_debug(QDF_MAC_ADDR_FMT ": RSN IE not present",
			   QDF_MAC_ADDR_REF(entry->bssid.bytes));
		if (entry->neg_sec_info.authmodeset & ((1U << WLAN_CRYPTO_AUTH_NONE) |
				(1U << WLAN_CRYPTO_AUTH_OPEN) |
				(1U << WLAN_CRYPTO_AUTH_AUTO))) {
			if (!open_allow)
				return false;
		} else if (!(entry->neg_sec_info.authmodeset & (1U << WLAN_CRYPTO_AUTH_WPA)) ||
			   !open_allow) {
			return false;
		}

		mlme_debug("AP crypto allow %d, ini config %d",
			   entry->neg_sec_info.authmodeset,
			   open_allow);
		return true;
	}

	/* Get the OEM EHT configuration. */
	status = wlan_mlme_get_oem_eht_mlo_config(psoc, &oem_eht_cfg);
	if (QDF_IS_STATUS_ERROR(status)) {
		mlme_rl_nofl_err("OEM EHT cfg get failed");
		return false;
	}
"""
    assert s.count(old1) == 1 and s.count(old2) == 1, (s.count(old1), s.count(old2))
    s = s.replace(old1, "\tuint32_t oem_eht_cfg = 0x0;\n\tuint32_t open_allow = 0x0;\n\tbool mlie_present;\n")
    s = s.replace(old2, new2)
    s = s.replace("\tstatus = wlan_mlme_get_eht_mlo_crypto_config(psoc, &open_allow);", "\t/* eht_mlo_crypto_open_allow (Xiaomi INI) */\n\tstatus = wlan_mlme_get_eht_mlo_crypto_config(psoc, &open_allow);", 1)
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "wlan_cm_is_eht_allowed_for_current_security")
else:
    print("skip:", f, "eht_allowed")

# --- g4l: use the newer CAF NS-offload "dynamic enable" scheme (r1-09600): pmo core enable/disable_in_fwr lose the dynamic checks, hdd calls ucfg get/set
f = "components/pmo/core/src/wlan_pmo_ns.c"
s = open(pkg + "/" + f, newline="").read()
a = s.index("QDF_STATUS pmo_core_enable_ns_offload_in_fwr(")
b = s.index("QDF_STATUS\npmo_core_get_ns_offload_params(")
new = open(here + "/tag_pmo_ns_fwr.c", newline="").read()
if "skip_ns_dynamic_check" in s[a:b]:
    s = s[:a] + new + "\n" + s[b:]
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "ns offload in fwr (r1-09600)")
else:
    print("skip:", f, "ns offload in fwr")
edit("components/pmo/dispatcher/inc/wlan_pmo_ucfg_api.h", "#endif /* end  of _WLAN_PMO_UCFG_API_H_ */",
     """/**
 * ucfg_pmo_get_ns_offload_enable_dynamic() - get NS offload dynamic enable
 * @vdev: vdev objmgr handle
 *
 * Return: true is NS offload is dynamically disabled else false
 */
bool
ucfg_pmo_get_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev);

/**
 * ucfg_pmo_set_ns_offload_enable_dynamic() - Set NS offload dynamic enable
 * @vdev: vdev objmgr handle
 * @trigger: pmo trigger
 * @ns_offload_enable_dyn: NS offload enable dynamic
 *
 * Return: None
 */
void
ucfg_pmo_set_ns_offload_enable_dynamic(struct wlan_objmgr_vdev *vdev,
				       enum pmo_offload_trigger trigger,
				       bool ns_offload_enable_dyn);
#endif /* end  of _WLAN_PMO_UCFG_API_H_ */""", marker="ucfg_pmo_get_ns_offload_enable_dynamic() - get NS offload dynamic enable")
f = "core/hdd/src/wlan_hdd_power.c"
edit(f, "\t\thdd_debug(\"NS offload is not required\");\n\t\tgoto free_req;\n\t}\n\n\tif (ucfg_pmo_get_arp_ns_offload_dynamic_disable(vdev)) {",
     "\t\thdd_debug(\"NS offload is not required\");\n\t\tgoto free_req;\n\t}\n\n\tucfg_pmo_set_ns_offload_enable_dynamic(vdev, trigger, true);\n\n\tif (!ucfg_pmo_get_ns_offload_enable_dynamic(vdev)) {\n\t\thdd_debug(\"NS offload is dynamically disabled\");\n\t\tgoto free_req;\n\t}\n\n\tif (ucfg_pmo_get_arp_ns_offload_dynamic_disable(vdev)) {",
     marker="NS offload is dynamically disabled")
edit(f, "\tstatus = ucfg_pmo_disable_ns_offload_in_fwr(vdev, trigger);\n\tif (status != QDF_STATUS_SUCCESS)\n\t\thdd_err(\"Failed to disable NS Offload\");",
     "\tif (!ucfg_pmo_get_ns_offload_enable_dynamic(vdev)) {\n\t\thdd_debug(\"NS offload is already dynamically disabled\");\n\t\tgoto out;\n\t}\n\n\tucfg_pmo_set_ns_offload_enable_dynamic(vdev, trigger, false);\n\tstatus = ucfg_pmo_disable_ns_offload_in_fwr(vdev, trigger);\n\tif (status != QDF_STATUS_SUCCESS)\n\t\thdd_err(\"Failed to disable NS Offload\");",
     marker="NS offload is already dynamically disabled")

# --- g4l: the OS4 stock of qca6750 / wcn7750 contains the OEM-data smem mailbox code (sme_smem_oem_event_*, hdd_oem_event_smem_cb, pld_oem_event_smem_write,
# wma_oem_smem_event_handler) and the +8 bytes in struct mac_context behind it: CONFIG_FEATURE_SMEM_MAILBOX is on for these two chips as well
for chip in ("qca6750", "wcn7750"):
    p = pkg + "/g4_%s_defconfig" % chip
    s = open(p, newline="").read()
    if "CONFIG_FEATURE_SMEM_MAILBOX=y" not in s:
        open(p, "w", newline="").write(s.rstrip("\n") + "\nCONFIG_FEATURE_SMEM_MAILBOX=y\n")
        print("patched:", p, "FEATURE_SMEM_MAILBOX")

# --- g4l: Xiaomi NUD-failure notification: cm_disconnect_roam_invoke_fail does not deauth but raises a data stall event when INI gEnableNUDFailureNotify == 4
# and exactly one STA session is active; hdd_data_stall_event_notify() (wrapper, out of line in the stock) calls the (noinline) hdd_data_stall_send_event
f = "components/umac/mlme/connection_mgr/core/src/wlan_cm_roam_fw_sync.c"
s = open(pkg + "/" + f, newline="").read()
if "hdd_data_stall_event_notify" not in s:
    old = "\tif (rso_cfg->roam_invoke_source == CM_ROAMING_HOST ||\n\t    (rso_cfg->roam_invoke_source == CM_ROAMING_NUD_FAILURE &&"
    assert s.count(old) == 1
    new = """	pm_ctx = wlan_objmgr_psoc_get_comp_private_obj(psoc,
						       WLAN_UMAC_COMP_POLICY_MGR);
	if (!pm_ctx)
		QDF_TRACE_ERROR(QDF_MODULE_ID_PE, "Policy manager context is NULL");
	else
		sta_sessions = pm_ctx->no_of_active_sessions[PM_STA_MODE];

	nud_notify = ucfg_dp_is_nud_failure_event_notify(psoc);
	if (nud_notify && sta_sessions == 1 &&
	    rso_cfg->roam_invoke_source == CM_ROAMING_NUD_FAILURE &&
	    (nud_disconnect || rso_cfg->is_forced_roaming)) {
		rso_cfg->roam_invoke_source = CM_SOURCE_INVALID;
		if (rso_cfg->is_forced_roaming)
			rso_cfg->is_forced_roaming = false;
		mlme_err("Occur nud failure and trigger roam fail, notify fail event but don't deauth");
		hdd_data_stall_event_notify(1000);
		return status;
	}

"""
    s = s.replace(old, new + old)
    old = "\tstruct rso_config *rso_cfg;\n\n\tpsoc = wlan_vdev_get_psoc(vdev);\n\tif (!psoc) {\n\t\tmlme_err(CM_PREFIX_FMT \"psoc not found\","
    assert s.count(old) == 1
    s = s.replace(old, "\tstruct rso_config *rso_cfg;\n\tstruct policy_mgr_psoc_priv_obj *pm_ctx;\n\tuint8_t sta_sessions = 0;\n\tbool nud_notify;\n" + old[len("\tstruct rso_config *rso_cfg;\n"):])
    i = s.index("#include")
    s = s[:i] + '#include "wlan_policy_mgr_i.h"\n#include "wlan_dp_ucfg_api.h"\nvoid hdd_data_stall_event_notify(uint32_t reason);\n' + s[i:]
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "nud failure notify")
else:
    print("skip:", f, "nud notify")
f = "core/hdd/src/wlan_hdd_data_stall_detection.c"
s = open(pkg + "/" + f, newline="").read()
if "hdd_data_stall_event_notify" not in s:
    i = s.index("void hdd_data_stall_send_event(uint32_t reason)\n{")
    j = s.index("\n}\n", i) + 3
    s = s[:j] + "\nvoid hdd_data_stall_event_notify(uint32_t reason)\n{\n\thdd_data_stall_send_event(reason);\n}\n" + s[j:]
    open(pkg + "/" + f, "w", newline="").write(s)
    print("patched:", f, "hdd_data_stall_event_notify")

# --- codegen hint (HACK, listed in STATUS.md): functions that the stock keeps out of line although they are static in the public source
for f, old, new in [
    ("core/hdd/src/wlan_hdd_data_stall_detection.c", "static void hdd_data_stall_send_event(uint32_t reason)\n{", "static noinline void hdd_data_stall_send_event(uint32_t reason)\n{"),
    ("core/hdd/src/wlan_hdd_main.c", "static int hdd_disable_wifi(struct hdd_context *hdd_ctx)\n{\n\tint ret;", "static noinline int hdd_disable_wifi(struct hdd_context *hdd_ctx)\n{\n\tint ret;"),
    ("core/mac/src/pe/lim/lim_utils.c", "static void lim_ht_switch_chnl_req(struct pe_session *session)\n{", "static noinline void lim_ht_switch_chnl_req(struct pe_session *session)\n{"),
    ("core/hdd/src/wlan_hdd_main.c", "static QDF_STATUS hdd_open_p2p_interface(struct hdd_context *hdd_ctx,\n\t\t\t\t\t       int is_mwdl)\n{", "static noinline QDF_STATUS hdd_open_p2p_interface(struct hdd_context *hdd_ctx,\n\t\t\t\t\t       int is_mwdl)\n{"),
]:
    s = open(pkg + "/" + f, newline="").read()
    if old in s:
        open(pkg + "/" + f, "w", newline="").write(s.replace(old, new))
        print("noinline:", f, new[:60])
    elif new in s:
        print("skip:", f)
    else:
        print("NOINLINE ANCHOR NOT FOUND:", f, old[:50].replace("\n", " "))

# --- g4l: hunks of newer CAF code (r1-09600) that hunkmerge.py accepted (they improved the byte-identity of the file's functions against the stock);
# applied last, idempotent (patch -N)
import glob, subprocess
for pf in sorted(glob.glob(here + "/tagpatches/*.patch")):
    r = subprocess.run("cd %s && patch -p0 -F3 -N --no-backup-if-mismatch -s < %s" % (pkg, pf), shell=True, capture_output=True, text=True)
    print("tagpatch:", os.path.basename(pf), "rc", r.returncode)
