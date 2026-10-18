/*
 * G13d: process_soc_limit_change_more, base_flip_sw_cv_workfunc, otg_monitor_workfunc and check_reverse_quick_charge are rebuilt from the OS4 stock
 * listing (constants, tables and call arguments are the stock ones; see STATUS.md for what is still a guess).
 */
/* stock .rodata object soc_limit_stepper_table [88 bytes]: {charge_limit, input_limit} per step 0..10 (read from the stock .ko) */
struct soc_limit_step {
	int charge_limit;
	int input_limit;
};

static const struct soc_limit_step soc_limit_stepper_table[] = {
	{ 0, 0 },
	{ 1990, 850 },
	{ 1400, 750 },
	{ 1100, 650 },
	{ 900, 550 },
	{ 700, 450 },
	{ 500, 350 },
	{ 300, 350 },
	{ 200, 350 },
	{ 100, 250 },
	{ 0, 250 },
};

static void strategy_buckchg_process_soc_limit_change_more(int value, struct strategy_buckchg_dev *info)
{
	static u8 cur_step;

	if (value) {
		mca_log_info("SOC limit triggered\n");
		if (cur_step < 10)
			cur_step++;
	} else {
		cur_step = 0;
		mca_log_info("SOC limit released\n");
	}

	if (cur_step == 0) {
		mca_vote(info->charge_limit_voter, "soc_limit", false, 0);
		mca_vote(info->input_limit_voter, "soc_limit", false, 0);
		mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
	} else {
		mca_vote(info->charge_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].charge_limit);
		mca_vote(info->input_limit_voter, "soc_limit", true, soc_limit_stepper_table[cur_step].input_limit);
		if (cur_step == 10) {
			mca_vote(info->chg_enable_voter, "soc_limit", true, 0);
		} else {
			mca_vote(info->chg_enable_voter, "soc_limit", false, 0);
			queue_delayed_work(system_wq, &info->soc_limit_stepper_work, 1000);
		}
	}
	mca_log_info("cur_step = %d\n", cur_step);
}

/* stock rodata object strategy_buckchg_base_flip_sw_cv_workfunc.sw_cv_volt_delta_map [16 bytes]: {1000, 7}, {0, 2} */
static void strategy_buckchg_base_flip_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, base_flip_sw_cv_work.work);
	static const int sw_cv_volt_delta_map[][2] = {
		{ 1000, 7 },
		{ 0, 2 }
	};
	int interval = 5000;
	int vterm_jeita = mca_get_client_vote(info->vterm_voter, "jeita");
	int iterm = info->parallel_iterm;	/* stock reads dev+2040 here; the name is the BTF member name guess of that slot */
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int first_termination = 0;
	int vbat, ibat, fastcharge, vterm, actual_vterm, volt_delta, vadj, fv_step, i;

	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat);
	fastcharge = strategy_class_fg_get_fastcharge();
	vbat = info->proc_data.vbat;
	ibat = -info->proc_data.ibat / 1000;
	strategy_class_fg_get_first_termination(&first_termination);
	mca_log_info("first_termination_flag:%d", first_termination);
	vterm = vterm_jeita;
	if (first_termination)
		vterm = mca_get_effective_result(info->vterm_voter);
	actual_vterm = info->pmic_fv_compensation + vterm;
	vadj = (fastcharge && !info->base_flip_same) ? -5 : -3;
	fv_step = (fastcharge && !info->base_flip_same) ? 10 : 5;
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, actual_vterm: %d, iterm: %d, fcc: %d, fast_charge: %d, fv_step: %d\n",
		vbat, ibat, vterm_jeita, actual_vterm, iterm, fcc, !!fastcharge, fv_step);

	volt_delta = sw_cv_volt_delta_map[0][1];
	for (i = 0; i < ARRAY_SIZE(sw_cv_volt_delta_map); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (fastcharge && vbat >= vterm_jeita - volt_delta) {
		interval = 2000;
		if (ibat - 50 > iterm) {
			if (fcc - ibat >= 100)
				mca_vote(info->charge_limit_voter, "sw_cv", true, ibat / 50 * 50);
			else if (info->base_flip_same && !first_termination && ibat - iterm >= 300)
				mca_vote(info->charge_limit_voter, "sw_cv", true, fcc - 50);
		}
	}

	if (vbat >= vterm_jeita + vadj) {
		int cnt = info->vbat_ov_count;

		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n", info->vbat_ov_count);
		info->vbat_ov_count = cnt + 1;
		platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, actual_vterm - min(cnt + 1, 5) * fv_step);
	}

	mca_queue_delayed_work(&info->base_flip_sw_cv_work, msecs_to_jiffies(interval));
}

static void strategy_buckchg_otg_monitor_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work, struct strategy_buckchg_dev, otg_monitor_work.work);
	struct mca_event_notify_data n_data;
	int temp = 0, soc, i;
	bool otg_present = false;
	char uevent[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	bool need_disable = false;

	strategy_class_fg_ops_get_temperature(&temp);
	soc = strategy_class_fg_ops_get_soc();
	temp /= 10;
	protocol_class_pd_get_otg_plugin_status(0, &otg_present);
	mca_log_err("otg_present = %d, batt_temp = %d, system_soc = %d\n", otg_present, temp, soc);

	for (i = 0; i < info->otg_threshold_size; i++) {
		if (temp < info->otg_temp_threshold[i] && soc <= info->otg_soc_threshold[i]) {
			need_disable = true;
			mca_log_info("OTG disable condition met: temp<%d && soc<=%d\n",
				info->otg_temp_threshold[i], info->otg_soc_threshold[i]);
			break;
		}
	}

	mutex_lock(&info->otg_incold_mutex);
	mca_log_err("bat temp = %d, soc = %d, disable_otg_incold = %d, need_disable = %d\n",
		temp, soc, info->disable_otg_incold, need_disable);
	if (info->disable_otg_incold) {
		if (info->support_disable_otg_incold && !(otg_present && need_disable)) {
			info->disable_otg_incold = false;
			mca_log_err("temp is above threshold or OTG removed, report now\n");
			goto report;
		}
	} else if (info->support_disable_otg_incold && otg_present && need_disable) {
		mca_log_err("temp is cold, disable otg now\n");
		info->disable_otg_incold = true;
		platform_class_buckchg_ops_set_boost_enable(MAIN_BUCK_CHARGER, (0 << 16) | (info->otg_boost_src << 8) | 0);
report:
		n_data.event_len = snprintf(uevent, MCA_EVENT_NOTIFY_SIZE, "POWER_SUPPLY_DISABLE_OTG_INCOLD=%d", info->disable_otg_incold);
		n_data.event = uevent;
		mca_event_report_uevent(&n_data);
	}
	mutex_unlock(&info->otg_incold_mutex);

	if (otg_present && info->support_disable_otg_incold)
		mca_queue_delayed_work(&info->otg_monitor_work, 1250);
}

/*
 * stock: standalone (not referenced from .text/.data in the OS4 module) function with the reverse-quick-charge eligibility rule:
 * result = otg_present && !wls_online && !wls_revchg_online && batt_temp >= 0 && thermal_board_temp <= 400 && soc > (reverse_auth_sts ? 29 : 69).
 * The stock stores the result through the 2nd argument as a 32-bit value; the prototype (info, int *result) is inferred from the listing.
 */
static void __used strategy_buckchg_check_reverse_quick_charge(struct strategy_buckchg_dev *info, int *result)
{
	int temp = 0, soc;
	bool otg_present = false;
	int ok = 0;

	strategy_class_fg_ops_get_temperature(&temp);
	soc = strategy_class_fg_ops_get_soc();
	temp /= 10;
	protocol_class_pd_get_otg_plugin_status(0, &otg_present);
	mca_log_info("otg_present = %d, batt_temp = %d, thermal_board_temp =%d\n", otg_present, temp, info->thermal_board_temp);
	if (otg_present && !info->wls_online && !info->wls_revchg_online && temp >= 0 && info->thermal_board_temp <= 400)
		ok = soc > (info->reverse_auth_sts ? 29 : 69);
	*result = ok;
}
