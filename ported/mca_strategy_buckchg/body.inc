/* ===== G13b: functions rebuilt from the OS4 stock disassembly (see STATUS.md for the list) ===== */
static void strategy_buckchg_stop_charging(struct strategy_buckchg_dev *info);
static void strategy_buckchg_start_charging(struct strategy_buckchg_dev *info);
static void strategy_buckchg_process_soc_limit_change_more(int value, struct strategy_buckchg_dev *info);

static void strategy_buck_update_req_volt(struct strategy_buckchg_dev *info)
{
	bool v9 = false;

	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		mca_vote(info->input_voltage_voter, "real_type", true, 1);
		break;
	case XM_CHARGER_TYPE_PPS:
		if (mca_strategy_is_support_pps_100w()) {
			mca_vote(info->input_suppend_voter, "real_type", true, 0);
			break;
		}
		/* fall through */
	case XM_CHARGER_TYPE_PD:
	case XM_CHARGER_TYPE_PD_VERIFY:
		v9 = info->proc_data.is_pd_9v;
		mca_vote(info->input_voltage_voter, "real_type", true, v9 ? 1 : 0);
		break;
	default:
		mca_vote(info->input_voltage_voter, "real_type", true, 0);
		break;
	}

	mca_log_info("real_type = %d, input_voltage_voter effective_result: %d\n",
		info->proc_data.real_type, mca_get_effective_result(info->input_voltage_voter));
}

static void stategy_buckchg_is_pdo_9v(struct strategy_buckchg_dev *info)
{
	int i;

	if (info->proc_data.real_type < XM_CHARGER_TYPE_PD || info->proc_data.real_type > XM_CHARGER_TYPE_PD_VERIFY)
		return;

	if (info->pdo_nums == 1 && info->pwr_cap.nums >= 2)
		queue_delayed_work(system_wq, &info->rerun_handle_pd_auth_work, msecs_to_jiffies(1000));

	info->pdo_nums = info->pwr_cap.nums;
	if (!info->pwr_cap.nums) {
		mca_log_info("pwr_cap.nums is null\n");
		return;
	}

	for (i = 0; i < info->pwr_cap.nums && i < ADAPTER_POWER_CAP_MAX; i++) {
		if (info->pwr_cap.cap[i].max_voltage == STATEGY_CHARGE_VBUS_9V &&
			info->pwr_cap.cap[i].min_voltage == STATEGY_CHARGE_VBUS_9V) {
			info->proc_data.is_pd_9v = true;
			info->proc_data.curr_pd_pos = i;
			mca_log_info("pdo[%d] can support 9v: %d\n", i, true);
			return;
		}
	}
	info->proc_data.is_pd_9v = false;
	info->proc_data.curr_pd_pos = 0;
	mca_log_info("pdo[%d] can support 9v: %d\n", 0, false);
}

static void strategy_buckchg_set_charge_volt(struct strategy_buckchg_dev *info, int target_volt)
{
	if (!target_volt) {
		mca_log_info("target_volt = 0v is invalid\n");
		return;
	}

	switch (info->proc_data.real_type) {
	case XM_CHARGER_TYPE_HVDCP2:
	case XM_CHARGER_TYPE_HVDCP3:
	case XM_CHARGER_TYPE_HVDCP3_B:
	case XM_CHARGER_TYPE_HVDCP3P5:
		platform_class_buckchg_ops_set_qc_volt(MAIN_BUCK_CHARGER, target_volt);
		break;
	case XM_CHARGER_TYPE_PPS:
	case XM_CHARGER_TYPE_PD_VERIFY:
		if (!info->pwr_cap.nums) {
			mca_log_info("pwr_cap nums is null\n");
			break;
		}
		if (target_volt == STATEGY_CHARGE_VBUS_5V || target_volt == STATEGY_CHARGE_VBUS_12V ||
			target_volt == STATEGY_CHARGE_VBUS_9V) {
			protocol_class_pd_set_fixed_volt(0, target_volt);
		} else {
			mca_log_info("set no fix pdo\n");
			protocol_class_set_adapter_volt_and_curr(ADAPTER_PROTOCOL_PPS, target_volt, 3000);
		}
		break;
	case XM_CHARGER_TYPE_PD:
		if (!info->pwr_cap.nums) {
			mca_log_info("pwr_cap nums is null\n");
			break;
		}
		protocol_class_pd_set_fixed_volt(0, target_volt);
		break;
	default:
		break;
	}
}

static void strategy_buckchg_start_charging(struct strategy_buckchg_dev *info)
{
	mca_log_info("start charging\n");
	strategy_buck_update_req_volt(info);
	cancel_delayed_work_sync(&info->monitor_work);
	strategy_class_buckchg_ops_adc_enable(info, true);
	queue_delayed_work(system_wq, &info->monitor_work, 0);
}

static void strategy_buckchg_process_cap_change(struct strategy_buckchg_dev *info)
{
	if (info->proc_data.real_type >= XM_CHARGER_TYPE_PD && info->proc_data.real_type <= XM_CHARGER_TYPE_PD_VERIFY) {
		protocol_class_get_adapter_power_cap(ADAPTER_PROTOCOL_PD, &info->pwr_cap);
		protocol_class_pd_get_suspend_support_status(0, &info->proc_data.pdsuspendsupported);
	}
	stategy_buckchg_is_pdo_9v(info);
	strategy_buck_update_req_volt(info);
	mod_delayed_work(system_wq, &info->monitor_work, 0);
}

static void strategy_buckchg_process_pmic_init_done(struct strategy_buckchg_dev *info)
{
	mca_log_info("deal with pmic_init_done\n");
	mca_rerun_election(info->chg_enable_voter);
	mca_rerun_election(info->input_limit_voter);
	mca_rerun_election(info->charge_limit_voter);
	mca_rerun_election(info->iterm_voter);
	mca_rerun_election(info->vterm_voter);
}

static void strategy_buckchg_exit_wireless_revchg(struct strategy_buckchg_dev *info)
{
	strategy_buckchg_set_charge_volt(info, info->proc_data.voltage);
	mca_vote(info->input_limit_voter, "wireless_revchg", false, 0);
	info->proc_data.wls_revchg_init_done = false;
	info->rev_icl_for_qc2 = false;
}

static void strategy_buckchg_process_antiburn_change(int value, struct strategy_buckchg_dev *info)
{
	mca_log_info("value: %d\n", value);
	strategy_buckchg_set_charge_volt(info, STATEGY_CHARGE_VBUS_5V);
	if (value)
		strategy_buckchg_stop_charging(info);
	else if (info->proc_data.online)
		strategy_buckchg_start_charging(info);
}

static void strategy_buckchg_process_batt_btb_change(int value, struct strategy_buckchg_dev *info)
{
	if (value) {
		mca_vote(info->input_voltage_voter, "batt_miss", true, 0);
		mca_vote(info->input_limit_voter, "batt_miss", true, info->batt_type ? 1000 : 500);
		mca_vote(info->vterm_voter, "batt_miss", true, STAEGY_BATT_MISS_FV);
	} else {
		mca_vote(info->input_voltage_voter, "batt_miss", false, 0);
		mca_vote(info->input_limit_voter, "batt_miss", false, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE);
		mca_vote(info->vterm_voter, "batt_miss", false, STATEGY_VTERM_DEFAULT_VALUE);
	}
	mca_vote_override(info->input_limit_voter, "master_batt_missing", false, 0);
}

static void strategy_buckchg_process_csd_pulse(int value, struct strategy_buckchg_dev *info)
{
	int ffc = strategy_class_fg_get_fastcharge();

	mca_log_info("value: %d, ffc_sts: %d\n", value, ffc);
	if (value >= 1 && ffc == 1)
		strategy_class_fg_set_fastcharge(0);

	if (value == 1 || value == 2) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
		info->csd_flag = true;
		queue_delayed_work(system_wq, &info->csd_pulse_process_work, msecs_to_jiffies(75000));
	} else if (value == 0) {
		cancel_delayed_work_sync(&info->csd_pulse_process_work);
		mca_vote(info->charge_limit_voter, "csd_pulse", false, 0);
		mca_vote(info->chg_enable_voter, "csd_pulse", false, 0);
		info->csd_flag = false;
	} else if (value == 3) {
		mca_vote(info->charge_limit_voter, "csd_pulse", true, 1500);
	}
}

static int strategy_buckchg_thermal_notifier_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct strategy_buckchg_dev *info = container_of(nb, struct strategy_buckchg_dev, thermal_board_nb);

	if (event == MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE)
		info->thermal_board_temp = *(int *)data / 100;
	return 0;
}

static int strategy_buckchg_panel_notifier_cb(struct notifier_block *nb, unsigned long event, void *data)
{
	struct strategy_buckchg_dev *info = container_of(nb, struct strategy_buckchg_dev, panel_nb);
	int screen;

	if (event == MCA_EVENT_PANEL_SCREEN_STATE_CHANGE) {
		screen = *(int *)data;
		mca_log_info("update screen_state: %d => %d\n", info->screen_status, screen);
		info->screen_status = screen != 0;
	}
	return 0;
}

static void strategy_buckchg_soc_limit_stepper_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, soc_limit_stepper_work.work);

	strategy_buckchg_process_soc_limit_change_more(info->soc_limit, info);
}

static void strategy_rerun_handle_pd_auth_workfunc(struct work_struct *work)
{
	mca_event_block_notify(MCA_EVENT_CHARGE_STATUS, MCA_EVENT_USB_STS_CHANGE, NULL);
	mca_log_info("send usb power_supply trigger handle pd auth\n");
}

static void strategy_buckchg_check_pdsecret_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, check_pd_secret_work.work);

	mca_vote(info->input_limit_voter, "icl_limit", false, STATEGY_INPUT_DEFAULT_VALUE);
	mca_log_info("cancel icl_limit vote when using pd charger\n");
}

static int strategy_buckchg_smartchg_set_fcc_callback(void *data, int effective_result)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;
	mca_vote(info->charge_limit_voter, "smartchg", effective_result != 0, effective_result);
	return 0;
}

