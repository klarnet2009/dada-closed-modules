/* G13d: rebuilt from the OS4 stock listing (the XRING template had FCC_STEP/FV_STEP constants, a {1000,5} delta map and no dynamic-adjust block) */
static void strategy_buckchg_sw_cv_workfunc(struct work_struct *work)
{
	struct strategy_buckchg_dev *info = container_of(work,
		struct strategy_buckchg_dev, sw_cv_work.work);
	/* stock rodata object strategy_buckchg_sw_cv_workfunc.sw_cv_volt_delta_map [16 bytes]: {1000, 4}, {0, 2} */
	static const int sw_cv_volt_delta_map[][2] = {
		{ 1000, 4 },
		{ 0, 2 }
	};
	int interval;
	int vbat, ibat, i;
	int vterm = mca_get_effective_result(info->vterm_voter);
	int iterm = mca_get_effective_result(info->iterm_voter);
	int fcc = mca_get_effective_result(info->charge_limit_voter);
	int pmic_comp = info->pmic_fv_compensation;
	int temp = 0;
	int volt_delta = sw_cv_volt_delta_map[0][1];

	strategy_class_fg_ops_get_voltage(&info->proc_data.vbat);
	strategy_class_fg_ops_get_current(&info->proc_data.ibat);
	vbat = info->proc_data.vbat;
	ibat = -info->proc_data.ibat / 1000;
	mca_log_info("vbat: %d, ibat: %d, vterm: %d, iterm: %d, fcc: %d\n", vbat, ibat, vterm, iterm, fcc);

	for (i = 0; i < ARRAY_SIZE(sw_cv_volt_delta_map); i++) {
		if (ibat > sw_cv_volt_delta_map[i][0]) {
			volt_delta = sw_cv_volt_delta_map[i][1];
			break;
		}
	}

	if (vbat >= vterm - volt_delta) {
		int fcc_step = info->sw_cv_fcc_step;

		if (ibat - fcc_step > iterm) {
			int v;

			if (fcc - ibat >= 2 * fcc_step)
				v = ibat / fcc_step * fcc_step;
			else
				v = fcc - fcc_step;
			if (v - iterm < info->sw_cv_fcc_limit_buffer)
				v = info->sw_cv_fcc_limit_buffer + iterm;
			mca_vote(info->charge_limit_voter, "sw_cv", true, v);
		}
		interval = 2000;
	} else {
		interval = 5000;
	}

	if (info->support_pmic_vterm_dynamics_adjust) {
		int fast = strategy_class_fg_get_fastcharge();
		int raw;

		strategy_class_fg_ops_get_temperature(&temp);
		raw = temp;
		temp = temp / 10;
		if (fast && raw >= 180 && raw <= 299)
			pmic_comp = info->pmic_fv_compensation;
		else if (fast && raw >= 300 && raw <= 399)
			pmic_comp = info->pmic_middle_fv_compensation;
		else {
			pmic_comp = 0;
			if (fast && raw >= 400 && raw <= 479)
				pmic_comp = info->pmic_high_fv_compensation;
		}
	}

	if (vbat >= vterm - 1) {
		int old;

		mca_log_err("WARNING: batt ov, reduce fv, count: %d\n", info->vbat_ov_count);
		old = info->vbat_ov_count;
		info->vbat_ov_count = old + 1;
		if (old == 0)
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, pmic_comp + vterm - info->sw_cv_fv_step);
		else if (old + 1 >= 2)
			platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, pmic_comp + vterm - 2 * info->sw_cv_fv_step);
	}

	mca_queue_delayed_work(&info->sw_cv_work, msecs_to_jiffies(interval));
}
