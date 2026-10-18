/*
 * G13b: process_event rebuilt from the OS4 stock (jump table of 80 entries over enum mca_event_notify_list).  The online change code
 * (events 0/1, strategy_buckchg_process_online_change in the stock logs) is inlined as in the stock.
 * Every case body below was read from the stock disassembly; the voter value arguments (e.g. true/0 and false/1) are literal.
 */
static int strategy_buckchg_process_event(int event, int value, void *data)
{
	struct strategy_buckchg_dev *info = data;
	struct mca_event_notify_data n_data;
	char uevent[MCA_EVENT_NOTIFY_SIZE];
	bool otg_plug = false;
	bool e0 = false, e1 = false;
	bool cp_en = false;
	int soc_low, soc_high;

	if (!data)
		return -1;

	mca_log_info("receive event %d, value %d\n", event, value);

	switch (event) {
	case MCA_EVENT_USB_DISCONNECT:
	case MCA_EVENT_USB_CONNECT:
		if (info->proc_data.online == value)
			return 0;
		info->proc_data.online = value;
		if (value) {
			if (info->support_base_flip) {
				platform_fg_ops_get_error_state(FG_IC_MASTER, &e0);
				platform_fg_ops_get_error_state(FG_IC_SLAVE, &e1);
				if (!e0 || e1)
					platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER, false);
			}
			strategy_buckchg_start_charging(info);
		} else {
			platform_class_loadsw_set_lowpower_mode(LOADSW_ROLE_MASTER, true);
			info->pps_ptf = 0;
			strategy_buckchg_stop_charging(info);
			info->verify_process_end = 0;
			info->proc_data.charge_done_force_5v = false;
			info->buck_abnormal_cnt = 0;
			if (info->base_flip_same) {
				platform_class_cp_set_busovp(CP_ROLE_MASTER, 13000);
				platform_class_cp_get_charging_enabled(CP_ROLE_MASTER, &cp_en);
				mca_log_info("cp_enabled = %d in discharging\n", cp_en);
			}
		}
		return 0;
	case MCA_EVENT_WIRELESS_DISCONNECT:
	case MCA_EVENT_WIRELESS_CONNECT:
		info->wls_online = value != 0;
		goto otg_plug_check;
	case MCA_EVENT_WIRELESS_REVCHG:
		info->wls_revchg_online = value != 0;
otg_plug_check:
		protocol_class_pd_get_otg_plugin_status(0, &otg_plug);
		if (info->support_reverse_quick_charge && otg_plug && info->start_quick_revchg) {
			cancel_delayed_work_sync(&info->source_status_monitor_work);
			queue_delayed_work(system_wq, &info->source_status_monitor_work, 0);
		}
		return 0;
	case MCA_EVENT_CHARGE_TYPE_CHANGE:
		strategy_buckchg_process_type_change(value, info);
		break;
	case MCA_EVENT_CHARGE_CAP_CHANGE:
		strategy_buckchg_process_cap_change(info);
		break;
	case MCA_EVENT_CHARGE_VERIFY_PROCESS_END:
		mca_log_info("receive pd verify process end event, value %d\n", value);
		info->verify_process_end = value;
		if (value)
			mca_vote(info->input_limit_voter, "icl_limit", false, STATEGY_INPUT_DEFAULT_VALUE);
		break;
	case MCA_EVENT_WIRELESS_USB_REVCHG:
		mca_log_info("wireless revchg event %d\n", value);
		info->wls_revchg_en = value;
		if (value) {
			strategy_wls_revchg_monitor_workfunc(&info->wls_revchg_monitor_work.work);
		} else {
			cancel_delayed_work_sync(&info->wls_revchg_monitor_work);
			strategy_buckchg_exit_wireless_revchg(info);
		}
		break;
	case MCA_EVENT_BATTERY_TOTAL_ITERM:
		mca_log_info("parallel_iterm: %d\n", value);
		info->parallel_iterm = value;
		return 0;
	case MCA_EVENT_CP_CBOOT_FAIL:
		if (value)
			mca_vote(info->input_voltage_voter, "cp_cboot_short", true, 0);
		else
			mca_vote(info->input_voltage_voter, "cp_cboot_short", false, 1);
		break;
	case MCA_EVENT_CHARGE_RESTORE:
		platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &info->proc_data.online);
		if (info->proc_data.online)
			strategy_buckchg_start_charging(info);
		break;
	case MCA_EVENT_CONN_ANTIBURN_CHANGE:
		strategy_buckchg_process_antiburn_change(value, info);
		break;
	case MCA_EVENT_BATT_BTB_CHANGE:
		info->batt_missing = value;
		strategy_buckchg_process_batt_btb_change(value, info);
		break;
	case MCA_EVENT_BATT_AUTH_PASS:
		mca_log_err("receive batt_auth event, value %d", value);
		info->batt_auth = 1;
		mca_vote(info->input_voltage_voter, "batt_auth", false, 0);
		mca_vote(info->charge_limit_voter, "batt_auth", false, info->chg_batt_auth_failed);
		if (info->proc_data.online)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_LPD_STATUS_CHANGE:
		if (value)
			mca_vote(info->input_voltage_voter, "lpd", true, 0);
		else
			mca_vote(info->input_voltage_voter, "lpd", false, 0);
		break;
	case MCA_EVENT_PMIC_INIT_DONE:
		strategy_buckchg_process_pmic_init_done(info);
		break;
	case MCA_EVENT_CC_SHORT_VBUS:
		if (value)
			mca_vote(info->input_voltage_voter, "cc_short_vbus", true, 0);
		else
			mca_vote(info->input_voltage_voter, "cc_short_vbus", false, 1);
		break;
	case MCA_EVENT_VBAT_OVP_CHANGE:
		if (value)
			mca_vote(info->chg_enable_voter, "vbat_ovp", true, 0);
		else
			mca_vote(info->chg_enable_voter, "vbat_ovp", false, 1);
		break;
	case MCA_EVENT_QUICK_REVCHG_CHANGE:
		if (info->support_reverse_quick_charge)
			strategy_buckchg_cp_revert_handler(value, info);
		break;
	case MCA_EVENT_BOOST_STS:
		if (!info->support_disable_otg_incold)
			break;
		if (value == 1) {
			mca_log_info("otg inserted, start otg monitor work after 500ms delay\n");
			mca_queue_delayed_work(&info->otg_monitor_work, 125);
		} else {
			mutex_lock(&info->otg_incold_mutex);
			if (info->disable_otg_incold) {
				info->disable_otg_incold = false;
				mca_log_err("otg plug out, report now\n");
				memset(uevent, 0, sizeof(uevent));
				n_data.event = uevent;
				n_data.event_len = snprintf(uevent, MCA_EVENT_NOTIFY_SIZE,
					"POWER_SUPPLY_DISABLE_OTG_INCOLD=%d", info->disable_otg_incold);
				mca_event_report_uevent(&n_data);
			}
			mutex_unlock(&info->otg_incold_mutex);
			mca_log_info("otg removed, cancel otg monitor work\n");
			cancel_delayed_work_sync(&info->otg_monitor_work);
		}
		break;
	case MCA_EVENT_CHARGE_ABNORMAL:
		strategy_buckchg_stop_charging(info);
		break;
	case MCA_EVENT_CSD_SEND_PULSE:
		strategy_buckchg_process_csd_pulse(value, info);
		break;
	case MCA_EVENT_PPS_PTF:
		info->pps_ptf = value;
		mca_log_info("set PPS_PTF %d\n", value);
		if (info->pps_ptf == USBPD_BUCK_DPM_PORT_PPS_PTF_WARNING)
			mca_vote(info->input_voltage_voter, "ptf", true, 1);
		break;
	case MCA_EVENT_IS_EU_MODEL:
		info->is_eu_model = value != 0;
		mca_log_err("set buck is_eu_model %d\n", value);
		platform_class_buckchg_ops_set_eu_model(MAIN_BUCK_CHARGER, info->is_eu_model);
		break;
	case MCA_EVENT_PLATE_SHOCK:
		if (value)
			mca_vote(info->input_limit_voter, "plate_shock", true, STAEGY_CHARGE_PLATE_SHOCK);
		else
			mca_vote(info->input_limit_voter, "plate_shock", false, 0);
		break;
	case MCA_EVENT_START_QUICK_REVCHG:
		info->start_quick_revchg = true;
		if (info->support_reverse_quick_charge)
			queue_delayed_work(system_wq, &info->source_status_monitor_work, 0);
		break;
	case MCA_EVENT_REVCHG_BCL:
		info->revchg_bcl = value != 0;
		return 0;
	case MCA_EVENT_HANDLE_ALLOW_CHARGE:
		mca_log_info("handle_allow_charge: %d\n", value);
		if (value) {
			mca_vote(info->input_limit_voter, "handle", false, 0);
			mca_vote(info->chg_enable_voter, "handle", false, 0);
		} else {
			mca_vote(info->input_limit_voter, "handle", true, 0);
			mca_vote(info->chg_enable_voter, "handle", true, 0);
		}
		break;
	case MCA_EVENT_BUCKCHG_BATT_OV:
		if (!info->sw_cv_running)
			mod_delayed_work(system_wq, &info->monitor_work, 0);
		break;
	case MCA_EVENT_DEBUG_CTRL_MEMORY_TEST:
		mca_log_info("memory_test enable soc_limit: %d\n", value);
		if (value) {
			info->soc_limit_low = 1;
			info->soc_limit_high = 3;
		} else {
			info->soc_limit_low = 0;
			info->soc_limit_high = 0;
		}
		return 0;
	case MCA_EVENT_DEBUG_CTRL_SOC_LIMIT:
		soc_low = (value >> 8) & 0xff;	/* shift/mask of the high byte: not fully read, GUESS */
		soc_high = value & 0xff;
		info->soc_limit_low = soc_low;
		info->soc_limit_high = soc_high;
		mca_log_info("debug_ctrl set soc_limit: %d %d\n", soc_low, soc_high);
		break;
	default:
		return 0;
	}

	return 0;
}

