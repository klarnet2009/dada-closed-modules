#define CHECK_VBUS_9V_HIGH_TH		7600
#define CHECK_VBUS_5V_LOW_TH		6000
#define DEFAULT_PD_CURRENT_MA		500

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info, int target_volt);
static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info);
static void strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info);
static void strategy_wls_revchg_monitor_workfunc(struct work_struct *work);
static int strategy_buckchg_check_charger_change(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_start(struct strategy_buckchg_dev *info);
static void strategy_buckchg_sw_cv_stop(struct strategy_buckchg_dev *info);
static void strategy_buckchg_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work);
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work);
static void strategy_csd_pulse_process_workfunc(struct work_struct *work);
static void strategy_source_status_monitor_workfunc(struct work_struct *work);
static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work);
static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work);
static void strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work);
static void strategy_buckchg_otg_monitor_workfunc(struct work_struct *work);
static int strategy_buckchg_process_event(int event, int value, void *data);
static int strategy_buckchg_get_status(int status, void *value, void *data);
static int strategy_buckchg_set_config(int config, int value, void *data);
static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *nb, unsigned long event, void *data);
static int strategy_buckchg_panel_notifier_cb(struct notifier_block *nb, unsigned long event, void *data);

static struct strategy_buckchg_dev *g_buckchg_info;

static int strategy_class_buckchg_ops_set_input_volt(struct strategy_buckchg_dev *info,
	int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_input_volt_lmt(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_input_volt_lmt(AUX_BUCK_CHARGER, mv);

	return ret;
}

static int strategy_class_buckchg_ops_set_chg(struct strategy_buckchg_dev *info, bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_set_chg(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_chg(AUX_BUCK_CHARGER, en);

	return ret;
}

static int strategy_class_buckchg_ops_set_opt_fws(struct strategy_buckchg_dev *info, int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_opt_fws(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_opt_fws(AUX_BUCK_CHARGER, mv);

	return ret;
}

static int strategy_class_buckchg_ops_adc_enable(struct strategy_buckchg_dev *info, bool en)
{
	int ret;

	ret = platform_class_buckchg_ops_adc_enable(MAIN_BUCK_CHARGER, en);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_adc_enable(AUX_BUCK_CHARGER, en);

	return ret;
}

static int strategy_class_buckchg_ops_set_usb_aicl_cont_thd(struct strategy_buckchg_dev *info, int mv)
{
	int ret;

	ret = platform_class_buckchg_ops_set_usb_aicl_cont_thd(MAIN_BUCK_CHARGER, mv);
	if (info && info->support_multi_buck)
		ret |= platform_class_buckchg_ops_set_usb_aicl_cont_thd(AUX_BUCK_CHARGER, mv);

	return ret;
}

/*
 * G13b: parse_dt / parse_dt_override rebuilt from the OS4 stock (inlined into strategy_buckchg_class_probe):
 * property names, defaults and destinations are the stock ones (stock rodata strings + mov immediates).
 */
static void strategy_buckchg_parse_dt(struct strategy_buckchg_dev *info)
{
	struct device_node *np = info->dev->of_node;
	u32 idata[REV_USBIN_TYPE_MAX] = { 0 };
	struct otg_incold_threshold otg_thr[OTG_INCOLD_THRESHOLD_MAX];
	int ret = 0;
	int i, groups;

	mca_parse_dts_u32(np, "batt_type", &info->batt_type, 0);
	mca_parse_dts_u32(np, "support_multi_buck", &info->support_multi_buck, STATEGY_SUPPORT_MULTI_BUCK);
	mca_parse_dts_u32(np, "in_dcp", &info->in_dcp, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_pd", &info->in_pd, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_pps", &info->in_pps, info->in_pd);
	mca_parse_dts_u32(np, "in_hvdcp", &info->in_hvdcp, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_hvdcp3", &info->in_hvdcp3, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_hvdcp3p5", &info->in_hvdcp3p5, CHARGE_DCP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_cdp", &info->in_cdp, CHARGE_CDP_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "in_sdp", &info->in_sdp, CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "in_float", &info->in_float, CHARGE_FLOAT_INPUT_DEFAULT);
	mca_parse_dts_u32(np, "chg_dcp", &info->chg_dcp, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_pd", &info->chg_pd, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_pps", &info->chg_pps, info->chg_pd);
	mca_parse_dts_u32(np, "chg_hvdcp", &info->chg_hvdcp, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_hvdcp3", &info->chg_hvdcp3, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_hvdcp3p5", &info->chg_hvdcp3p5, CHARGE_DCP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_cdp", &info->chg_cdp, CHARGE_CDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_sdp", &info->chg_sdp, CHARGE_SDP_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_float", &info->chg_float, CHARGE_FLOAT_CHARGE_DEFAULT);
	mca_parse_dts_u32(np, "chg_batt_auth_failed", &info->chg_batt_auth_failed, CHARGE_BATT_AUTH_FAIL_DEFAULT);
	mca_parse_dts_u32(np, "ffc_temp_low", &info->ffc_temp_low, ALLOW_FFC_TEMP_LOW_THR);
	mca_parse_dts_u32(np, "ffc_temp_high", &info->ffc_temp_high, ALLOW_FFC_TEMP_HIGH_THR);
	mca_parse_dts_u32(np, "curr_terminate_compensation", &info->curr_term_compensation, 0);
	mca_parse_dts_u32(np, "allow_start_ffc_batt_soc_thr", &info->allow_start_ffc_batt_soc_thr, ALLOW_START_FFC_BATT_SOC_THR);
	mca_parse_dts_u32(np, "pmic_fv_compensation", &info->pmic_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_fv_compensation_cold", &info->pmic_fv_compensation_cold, 0);
	mca_parse_dts_u32(np, "bat_temp_fv_comp_cold_th", &info->bat_temp_fv_comp_cold_th, 0);
	mca_parse_dts_u32(np, "pmic_fv_compensation_hot", &info->pmic_fv_compensation_hot, 0);
	mca_parse_dts_u32(np, "bat_temp_fv_comp_hot_th", &info->bat_temp_fv_comp_hot_th, 50);
	mca_parse_dts_u32(np, "pmic_middle_fv_compensation", &info->pmic_middle_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_high_fv_compensation", &info->pmic_high_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_wls_fv_compensation", &info->pmic_wls_fv_compensation, 0);
	mca_parse_dts_u32(np, "pmic_iterm_compensation", &info->pmic_iterm_compensation, 30);
	mca_parse_dts_u32(np, "vusb_ovp_location", &info->vusb_ovp_location, VUSB_OVP_BEFORE_CP);
	mca_parse_dts_u32(np, "terminated_by_cp", &info->terminated_by_cp, 0);
	mca_parse_dts_u32(np, "ffc_terminated_by_cp", &info->ffc_terminated_by_cp, 0);
	mca_parse_dts_u32(np, "sw_cv_fcc_step", &info->sw_cv_fcc_step, 50);
	mca_parse_dts_u32(np, "sw_cv_fv_step", &info->sw_cv_fv_step, 5);
	mca_parse_dts_u32(np, "sw_cv_fcc_limit_buffer", &info->sw_cv_fcc_limit_buffer, 0);
	mca_parse_dts_u32(np, "support_pps_100w", &info->support_pps_100w, 0);
	mca_parse_dts_u32(np, "drop_fv_cycle", &info->drop_fv_cycle, 0);
	info->support_diff_temp_comp = of_property_read_bool(np, "support_diff_temp_comp");
	info->support_reverse_quick_charge = of_property_read_bool(np, "support_reverse_quick_charge");
	info->support_revchg_screenon = of_property_read_bool(np, "support_revchg_screenon");
	info->support_base_flip = of_property_read_bool(np, "support-base-flip");
	info->base_flip_same = of_property_read_bool(np, "base-flip-same");
	info->support_pmic_vterm_dynamics_adjust = of_property_read_bool(np, "support-pmic-vterm-dynamics-adjust");
	info->need_cp_to_pmic = of_property_read_bool(np, "need-cp-to-pmic");
	info->support_charge_more = of_property_read_bool(np, "support-charge-more");
	info->support_disable_otg_incold = of_property_read_bool(np, "support-disable-otg-incold");

	memset(otg_thr, 0, sizeof(otg_thr));
	ret = mca_parse_dts_string_array(np, "otg_incold_thresholds", (int *)otg_thr, OTG_INCOLD_THRESHOLD_MAX, 2);
	if (ret >= 1) {
		groups = ret / 2;
		info->otg_threshold_size = groups;
		for (i = 0; i < groups && i < OTG_INCOLD_THRESHOLD_MAX; i++) {
			info->otg_temp_threshold[i] = otg_thr[i].temp_threshold;
			info->otg_soc_threshold[i] = otg_thr[i].soc_threshold;
		}
		mca_log_info("OTG thresholds parsed: %d groups\n", info->otg_threshold_size);
	} else {
		/* defaults from the stock immediates: temp {0, -10, -20}, soc {10, 25, 35}, size 3 */
		info->otg_threshold_size = 3;
		info->otg_temp_threshold[0] = 0;
		info->otg_temp_threshold[1] = -10;
		info->otg_temp_threshold[2] = -20;
		info->otg_soc_threshold[0] = 10;
		info->otg_soc_threshold[1] = 25;
		info->otg_soc_threshold[2] = 35;
		mca_log_info("Use default OTG thresholds\n");
	}

	ret = mca_parse_dts_u32_array(np, "rev_req_vadp", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_req_vadp[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
		info->rev_req_vadp[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_DEFAULT;
	} else
		memcpy(info->rev_req_vadp, idata, sizeof(idata));
	mca_log_debug("rev_req_vadp %d %d\n", info->rev_req_vadp[REV_USBIN_TYPE_PPS],
		info->rev_req_vadp[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(np, "rev_vadp_valid_h", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
		info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_H;
	} else
		memcpy(info->rev_vadp_valid_h, idata, sizeof(idata));
	mca_log_debug("rev_vadp_valid_h %d %d\n", info->rev_vadp_valid_h[REV_USBIN_TYPE_PPS],
		info->rev_vadp_valid_h[REV_USBIN_TYPE_OTHER]);

	ret = mca_parse_dts_u32_array(np, "rev_vadp_valid_l", idata, REV_USBIN_TYPE_MAX);
	if (ret) {
		info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
		info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER] = STATEGY_USBIN_WLS_REV_CHG_VOLTAGE_BUF_L;
	} else
		memcpy(info->rev_vadp_valid_l, idata, sizeof(idata));
	mca_log_debug("rev_vadp_valid_l %d %d\n", info->rev_vadp_valid_l[REV_USBIN_TYPE_PPS],
		info->rev_vadp_valid_l[REV_USBIN_TYPE_OTHER]);
}

/* per-product override node (stock: only for product names "donghai" and "yanan"); every property keeps the current value as default */
static void strategy_buckchg_parse_dt_override(struct strategy_buckchg_dev *info, const struct mca_hwid *hwid)
{
	struct device_node *child;
	u32 idata[REV_USBIN_TYPE_MAX] = { 0 };
	struct otg_incold_threshold otg_thr[OTG_INCOLD_THRESHOLD_MAX];
	int ret = 0;
	int i, groups;

	if (!hwid)
		return;
	if (strcmp(hwid->product_name, "donghai") && strcmp(hwid->product_name, "yanan"))
		return;

	child = of_get_child_by_name(info->dev->of_node, "strategy_buckchg_class_override");
	if (!child) {
		mca_log_info("child node 'strategy_buckchg_class_override' not found, using parent config\n");
		return;
	}
	mca_log_info("applying strategy_buckchg override from child node\n");

	mca_parse_dts_u32(child, "batt_type", &info->batt_type, info->batt_type);
	mca_parse_dts_u32(child, "support_multi_buck", &info->support_multi_buck, info->support_multi_buck);
	mca_parse_dts_u32(child, "in_dcp", &info->in_dcp, info->in_dcp);
	mca_parse_dts_u32(child, "in_pd", &info->in_pd, info->in_pd);
	mca_parse_dts_u32(child, "in_pps", &info->in_pps, info->in_pps);
	mca_parse_dts_u32(child, "in_hvdcp", &info->in_hvdcp, info->in_hvdcp);
	mca_parse_dts_u32(child, "in_hvdcp3", &info->in_hvdcp3, info->in_hvdcp3);
	mca_parse_dts_u32(child, "in_hvdcp3p5", &info->in_hvdcp3p5, info->in_hvdcp3p5);
	mca_parse_dts_u32(child, "in_cdp", &info->in_cdp, info->in_cdp);
	mca_parse_dts_u32(child, "in_sdp", &info->in_sdp, info->in_sdp);
	mca_parse_dts_u32(child, "in_float", &info->in_float, info->in_float);
	mca_parse_dts_u32(child, "chg_dcp", &info->chg_dcp, info->chg_dcp);
	mca_parse_dts_u32(child, "chg_pd", &info->chg_pd, info->chg_pd);
	mca_parse_dts_u32(child, "chg_pps", &info->chg_pps, info->chg_pps);
	mca_parse_dts_u32(child, "chg_hvdcp", &info->chg_hvdcp, info->chg_hvdcp);
	mca_parse_dts_u32(child, "chg_hvdcp3", &info->chg_hvdcp3, info->chg_hvdcp3);
	mca_parse_dts_u32(child, "chg_hvdcp3p5", &info->chg_hvdcp3p5, info->chg_hvdcp3p5);
	mca_parse_dts_u32(child, "chg_cdp", &info->chg_cdp, info->chg_cdp);
	mca_parse_dts_u32(child, "chg_sdp", &info->chg_sdp, info->chg_sdp);
	mca_parse_dts_u32(child, "chg_float", &info->chg_float, info->chg_float);
	mca_parse_dts_u32(child, "chg_batt_auth_failed", &info->chg_batt_auth_failed, info->chg_batt_auth_failed);
	mca_parse_dts_u32(child, "ffc_temp_low", &info->ffc_temp_low, info->ffc_temp_low);
	mca_parse_dts_u32(child, "ffc_temp_high", &info->ffc_temp_high, info->ffc_temp_high);
	mca_parse_dts_u32(child, "curr_terminate_compensation", &info->curr_term_compensation, info->curr_term_compensation);
	mca_parse_dts_u32(child, "allow_start_ffc_batt_soc_thr", &info->allow_start_ffc_batt_soc_thr, info->allow_start_ffc_batt_soc_thr);
	mca_parse_dts_u32(child, "pmic_fv_compensation", &info->pmic_fv_compensation, info->pmic_fv_compensation);
	mca_parse_dts_u32(child, "pmic_fv_compensation_cold", &info->pmic_fv_compensation_cold, info->pmic_fv_compensation_cold);
	mca_parse_dts_u32(child, "bat_temp_fv_comp_cold_th", &info->bat_temp_fv_comp_cold_th, info->bat_temp_fv_comp_cold_th);
	mca_parse_dts_u32(child, "pmic_fv_compensation_hot", &info->pmic_fv_compensation_hot, info->pmic_fv_compensation_hot);
	mca_parse_dts_u32(child, "bat_temp_fv_comp_hot_th", &info->bat_temp_fv_comp_hot_th, info->bat_temp_fv_comp_hot_th);
	mca_parse_dts_u32(child, "pmic_middle_fv_compensation", &info->pmic_middle_fv_compensation, info->pmic_middle_fv_compensation);
	mca_parse_dts_u32(child, "pmic_high_fv_compensation", &info->pmic_high_fv_compensation, info->pmic_high_fv_compensation);
	mca_parse_dts_u32(child, "pmic_wls_fv_compensation", &info->pmic_wls_fv_compensation, info->pmic_wls_fv_compensation);
	mca_parse_dts_u32(child, "pmic_iterm_compensation", &info->pmic_iterm_compensation, info->pmic_iterm_compensation);
	mca_parse_dts_u32(child, "vusb_ovp_location", &info->vusb_ovp_location, info->vusb_ovp_location);
	mca_parse_dts_u32(child, "terminated_by_cp", &info->terminated_by_cp, info->terminated_by_cp);
	mca_parse_dts_u32(child, "ffc_terminated_by_cp", &info->ffc_terminated_by_cp, info->ffc_terminated_by_cp);
	mca_parse_dts_u32(child, "sw_cv_fcc_step", &info->sw_cv_fcc_step, info->sw_cv_fcc_step);
	mca_parse_dts_u32(child, "sw_cv_fv_step", &info->sw_cv_fv_step, info->sw_cv_fv_step);
	mca_parse_dts_u32(child, "sw_cv_fcc_limit_buffer", &info->sw_cv_fcc_limit_buffer, info->sw_cv_fcc_limit_buffer);
	mca_parse_dts_u32(child, "support_pps_100w", &info->support_pps_100w, info->support_pps_100w);
	if (of_property_read_bool(child, "support_diff_temp_comp"))
		info->support_diff_temp_comp = true;
	if (of_property_read_bool(child, "support_reverse_quick_charge"))
		info->support_reverse_quick_charge = 1;
	if (of_property_read_bool(child, "support_revchg_screenon"))
		info->support_revchg_screenon = 1;
	if (of_property_read_bool(child, "support-base-flip"))
		info->support_base_flip = 1;
	if (of_property_read_bool(child, "base-flip-same"))
		info->base_flip_same = 1;
	if (of_property_read_bool(child, "support-pmic-vterm-dynamics-adjust"))
		info->support_pmic_vterm_dynamics_adjust = 1;
	if (of_property_read_bool(child, "need-cp-to-pmic"))
		info->need_cp_to_pmic = 1;
	if (of_property_read_bool(child, "support-charge-more"))
		info->support_charge_more = 1;
	if (of_property_read_bool(child, "support-disable-otg-incold"))
		info->support_disable_otg_incold = true;

	memset(otg_thr, 0, sizeof(otg_thr));
	ret = mca_parse_dts_string_array(child, "otg_incold_thresholds", (int *)otg_thr, OTG_INCOLD_THRESHOLD_MAX, 2);
	if (ret >= 1) {
		groups = ret / 2;
		info->otg_threshold_size = groups;
		for (i = 0; i < groups && i < OTG_INCOLD_THRESHOLD_MAX; i++) {
			info->otg_temp_threshold[i] = otg_thr[i].temp_threshold;
			info->otg_soc_threshold[i] = otg_thr[i].soc_threshold;
		}
		mca_log_info("overridden OTG thresholds: %d groups\n", info->otg_threshold_size);
	}

	if (!mca_parse_dts_u32_array(child, "rev_req_vadp", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_req_vadp, idata, sizeof(idata));
	if (!mca_parse_dts_u32_array(child, "rev_vadp_valid_h", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_vadp_valid_h, idata, sizeof(idata));
	if (!mca_parse_dts_u32_array(child, "rev_vadp_valid_l", idata, REV_USBIN_TYPE_MAX))
		memcpy(info->rev_vadp_valid_l, idata, sizeof(idata));
	mca_log_info("strategy_buckchg override parse completed\n");
}

static int strategy_buckchg_enable(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
	mca_log_err("%d\n", effective_result);
	if (effective_result)
		return strategy_class_buckchg_ops_set_chg(info, true);
	else
		return strategy_class_buckchg_ops_set_chg(info, false);
}

static int strategy_buckchg_input_suspend(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
	mca_log_err("%d\n", effective_result);
	if (effective_result)
		mca_vote(info->input_limit_voter, "input_suspend", true, 0);
	else
		mca_vote(info->input_limit_voter, "input_suspend", false, 0);
	return 0;
}

static int strategy_buckchg_input_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	struct timespec64 ts;
	int is_zero_speed = 0;
	int result = effective_result;

	if (!data)
		return -1;

	ts = ns_to_timespec64(ktime_get_boottime());
	if ((u64)ts.tv_sec < 60) {
		get_smem_battery_info(&is_zero_speed);
		if (is_zero_speed && effective_result) {
			result = effective_result > 1500 ? effective_result : 1500;
			mca_log_err("is_zero_speed, result = %d, effective_result = %d\n", result, effective_result);
		}
	}

	mca_log_err("set input current %d\n", result);

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_input_curr_lmt(MAIN_BUCK_CHARGER, result);
}

static int strategy_buckchg_charge_limit(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	mca_log_err("set chg current %d\n", effective_result);

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_ichg(MAIN_BUCK_CHARGER, effective_result);
}

/*
 * iterm compensation (pmic_iterm_compensation) for the base-flip / base-flip-same models.
 * Thresholds (30..39 C, 18..29 C, parallel_iterm 1046/598/187, results 104/82/50/20/14, 350 -> 75/68) are the stock immediates.
 */
static int strategy_buckchg_set_iterm(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	int temp = 0;

	if (!data)
		return -1;

	if (info->support_base_flip) {
		if (strategy_class_fg_get_fastcharge()) {
			strategy_class_fg_ops_get_temperature(&temp);
			temp = temp / 10;
			if (temp >= 30 && temp <= 39 && info->parallel_iterm == 1046)
				info->pmic_iterm_compensation = 104;
			else if (temp >= 18 && temp <= 29 && info->parallel_iterm == 598)
				info->pmic_iterm_compensation = 82;
			else
				info->pmic_iterm_compensation = 50;
		} else if (info->parallel_iterm == 187) {
			info->pmic_iterm_compensation = 20;
		} else {
			info->pmic_iterm_compensation = 14;
		}
	}

	if (info->base_flip_same) {
		if (strategy_class_fg_get_fastcharge()) {
			strategy_class_fg_ops_get_temperature(&temp);
			temp = temp / 10;
			info->pmic_iterm_compensation = temp < 35 ? 75 : 68;
		} else {
			info->pmic_iterm_compensation = 20;
		}
	}

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_term_curr(MAIN_BUCK_CHARGER, info->pmic_iterm_compensation + effective_result);
}
