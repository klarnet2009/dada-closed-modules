/*
 * vterm voter callback.  Compensation tables (pmic_fv_compensation + cold/hot/middle/high/wls) are applied depending on the
 * battery temperature (fg temperature / 10, 0.1 C units from the gauge); every threshold below is a stock immediate:
 * 180..299 / 300..399 / >= 400 (0.1 C), base-flip temperatures 33..34 / 48..54 C, compensations 10/20/30/40/45 mV.
 */
static int strategy_buckchg_set_vterm(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;
	int offset_flag = 0;
	int temp = 0;
	int *comp;
	int vterm;
	int v;

	if (!data)
		return -1;

	if (info->support_base_flip) {
		if (!strategy_class_fg_get_fastcharge())
			info->pmic_fv_compensation = 10;
		else if (info->wls_online)
			info->pmic_fv_compensation = info->pmic_wls_fv_compensation;
		else
			info->pmic_fv_compensation = 20;
	}

	strategy_class_fg_ops_get_temperature(&temp);
	temp = temp / 10;
	if (temp < info->bat_temp_fv_comp_cold_th && info->support_diff_temp_comp)
		comp = &info->pmic_fv_compensation_cold;
	else if (temp > info->bat_temp_fv_comp_hot_th && info->support_diff_temp_comp)
		comp = &info->pmic_fv_compensation_hot;
	else
		comp = &info->pmic_fv_compensation;
	v = *comp + effective_result;
	vterm = v;

	if (info->support_pmic_vterm_dynamics_adjust) {
		if (!strategy_class_fg_get_fastcharge()) {
			vterm = effective_result;
		} else {
			int raw;

			strategy_class_fg_ops_get_temperature(&temp);
			raw = temp;
			temp = temp / 10;
			if (raw >= 180 && raw <= 299)
				v = info->pmic_fv_compensation + effective_result;
			if (temp >= 30 && temp <= 39)
				vterm = info->pmic_middle_fv_compensation + effective_result;
			else if (raw >= 400)
				vterm = info->pmic_high_fv_compensation + effective_result;
			else
				vterm = v;
		}
	}

	if (info->base_flip_same) {
		bool fast = strategy_class_fg_get_fastcharge();
		int c;

		strategy_class_fg_get_temp_offset_flag(&offset_flag);
		if (fast) {
			if (temp > 34 || (temp >= 33 && offset_flag))
				c = 45;
			else
				c = 40;
		} else if (temp >= 48 && temp <= 54) {
			c = 30;
		} else {
			c = 0;
		}
		info->pmic_fv_compensation = c;
		vterm = c + effective_result;
	}

	if (info->support_multi_buck)
		return 0;

	return platform_class_buckchg_ops_set_term_volt(MAIN_BUCK_CHARGER, vterm);
}

static int strategy_buckchg_input_voltage(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->hvdcp_allow_flag = effective_result;
	info->proc_data.voltage = effective_result ? STATEGY_CHARGE_VBUS_9V : STATEGY_CHARGE_VBUS_5V;

	return 0;
}

static int strategy_buckchg_set_5v_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_5v_in = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_5V)
		return 0;

	if (info->buck_5v_in)
		mca_vote(info->input_limit_voter, "volt_limit", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "volt_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_5v_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_5v_ich = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_5V)
		return 0;

	if (info->buck_5v_ich)
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", true, effective_result);
	else
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_9v_input(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_9v_in = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V)
		return 0;

	if (info->buck_9v_in)
		mca_vote(info->input_limit_voter, "volt_limit", true, effective_result);
	else
		mca_vote(info->input_limit_voter, "volt_limit", false, effective_result);

	return 0;
}

static int strategy_buckchg_set_9v_ichg(struct mca_votable *votable,
	void *data, int effective_result, const char *effective_client)
{
	struct strategy_buckchg_dev *info = data;

	if (!data)
		return -1;

	info->buck_9v_ich = effective_result;

	if (info->proc_data.voltage != STATEGY_CHARGE_VBUS_9V)
		return 0;

	if (info->buck_9v_ich)
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", true, effective_result);
	else
		mca_vote(info->charge_limit_voter, "volt_thermal_limit", false, effective_result);

	return 0;
}

