static int strategy_buckchg_class_probe(struct platform_device *pdev)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	struct strategy_buckchg_dev *info;
	int online = 0;

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	strategy_buckchg_parse_dt(info);
	strategy_buckchg_parse_dt_override(info, hwid);

	/* init voters (inlined in the stock probe, same order and defaults) */
	info->chg_enable_voter = mca_create_votable("chg_enable", MCA_VOTE_AND,
		strategy_buckchg_enable, STATEGY_CHARGE_DISENABLE, info);
	if (IS_ERR(info->chg_enable_voter))
		goto voter_err;
	info->input_suppend_voter = mca_create_votable("input_suspend", MCA_VOTE_OR,
		strategy_buckchg_input_suspend, STATEGY_CHARGE_INPUT_SUSPEND, info);
	if (IS_ERR(info->input_suppend_voter))
		goto voter_err;
	info->input_limit_voter = mca_create_votable("buck_input", MCA_VOTE_MIN,
		strategy_buckchg_input_limit, STATEGY_INPUT_DEFAULT_VALUE, info);
	if (IS_ERR(info->input_limit_voter))
		goto voter_err;
	info->charge_limit_voter = mca_create_votable("buck_charge_curr", MCA_VOTE_MIN,
		strategy_buckchg_charge_limit, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->charge_limit_voter))
		goto voter_err;
	hwid = mca_get_hwid_info();
	info->iterm_voter = mca_create_votable("term_curr",
		(hwid && (hwid->platform_version & ~1U) == 4) ? MCA_VOTE_MIN : MCA_VOTE_MAX,
		strategy_buckchg_set_iterm, STATEGY_ITERM_DEFAULT_VALUE, info);
	if (IS_ERR(info->iterm_voter))
		goto voter_err;
	info->vterm_voter = mca_create_votable("term_volt", MCA_VOTE_MIN,
		strategy_buckchg_set_vterm, STATEGY_VTERM_DEFAULT_VALUE, info);
	if (IS_ERR(info->vterm_voter))
		goto voter_err;
	info->input_voltage_voter = mca_create_votable("input_voltage", MCA_VOTE_AND,
		strategy_buckchg_input_voltage, 1, info);
	if (IS_ERR(info->input_voltage_voter))
		goto voter_err;
	info->buck_5v_in_voter = mca_create_votable("buck_5v_in", MCA_VOTE_MIN,
		strategy_buckchg_set_5v_input, STATEGY_INPUT_5V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_in_voter))
		goto voter_err;
	info->buck_5v_ich_voter = mca_create_votable("buck_5v_ich", MCA_VOTE_MIN,
		strategy_buckchg_set_5v_ichg, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_5v_ich_voter))
		goto voter_err;
	info->buck_9v_in_voter = mca_create_votable("buck_9v_in", MCA_VOTE_MIN,
		strategy_buckchg_set_9v_input, STATEGY_INPUT_9V_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_in_voter))
		goto voter_err;
	info->buck_9v_ich_voter = mca_create_votable("buck_9v_ich", MCA_VOTE_MIN,
		strategy_buckchg_set_9v_ichg, STATEGY_CHARGE_CURRENT_DEFAULT_VALUE, info);
	if (IS_ERR(info->buck_9v_ich_voter))
		goto voter_err;

	mca_vote(info->input_voltage_voter, "batt_auth", true, 0);
	mca_vote(info->charge_limit_voter, "batt_auth", true, info->chg_batt_auth_failed);
	mca_vote(info->vterm_voter, "batt_miss", true, STAEGY_BATT_MISS_FV);
	info->proc_data.voltage = STATEGY_CHARGE_VBUS_5V;
	info->proc_data.pre_volt = STATEGY_CHARGE_VBUS_5V;
	info->batt_missing = 1;
	info->hvdcp_allow_flag = 0;
	info->vbat_ov_count = 0;
	info->buck_abnormal_cnt = 0;
	info->cp_vendor = -1;
	info->otg_boost_src = -1;
	info->disable_otg_incold = false;
	mutex_init(&info->otg_incold_mutex);

	INIT_DELAYED_WORK(&info->monitor_work, strategy_buckchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->sw_cv_work, strategy_buckchg_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->base_flip_sw_cv_work, strategy_buckchg_base_flip_sw_cv_workfunc);
	INIT_DELAYED_WORK(&info->wls_revchg_monitor_work, strategy_wls_revchg_monitor_workfunc);
	INIT_DELAYED_WORK(&info->csd_pulse_process_work, strategy_csd_pulse_process_workfunc);
	INIT_DELAYED_WORK(&info->source_status_monitor_work, strategy_source_status_monitor_workfunc);
	INIT_DELAYED_WORK(&info->check_pd_secret_work, strategy_buckchg_check_pdsecret_workfunc);
	INIT_DELAYED_WORK(&info->rerun_handle_pd_auth_work, strategy_rerun_handle_pd_auth_workfunc);
	INIT_DELAYED_WORK(&info->soc_limit_stepper_work, strategy_buckchg_soc_limit_stepper_workfunc);
	INIT_DELAYED_WORK(&info->otg_monitor_work, strategy_buckchg_otg_monitor_workfunc);

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
		strategy_buckchg_process_event, strategy_buckchg_get_status, strategy_buckchg_set_config, info);
	g_strategy_buckchg_if_ops.data = info;
	(void)mca_charge_if_ops_register(&g_strategy_buckchg_if_ops);
	g_buck_smartchg_if_ops.data = info;
	(void)mca_smartchg_if_ops_register(&g_buck_smartchg_if_ops);
	info->thermal_board_nb.notifier_call = strategy_buckchg_thermal_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP, &info->thermal_board_nb);
	info->panel_nb.notifier_call = strategy_buckchg_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &info->panel_nb);

	platform_class_buckchg_ops_get_online(MAIN_BUCK_CHARGER, &online);
	if (online) {
		mca_log_info("avoid missing first usb connect event\n");
		mca_event_block_notify(MCA_EVENT_TYPE_CHARGER_CONNECT, MCA_EVENT_USB_CONNECT, NULL);
	}

	g_buckchg_info = info;
	mca_log_err("androidboot.mode=%d\n", mca_log_get_charge_boot_mode());
	info->init_ok = 1;
	mca_log_err("probe success\n");
	return 0;

voter_err:
	mca_log_err("init voter err\n");
	return -1;
}

static int strategy_buckchg_class_remove(struct platform_device *pdev)
{
	return 0;
}

static void strategy_buckchg_class_shutdown(struct platform_device *pdev)
{
}

static const struct of_device_id match_table[] = {
	{.compatible = "mca,strategy_buckchg"},
	{},
};

static struct platform_driver strategy_buckchg_class_driver = {
	.driver	= {
		.name = "strategy_buckchg_class",
		.owner = THIS_MODULE,
		.of_match_table = match_table,
	},
	.probe = strategy_buckchg_class_probe,
	.remove = strategy_buckchg_class_remove,
	.shutdown = strategy_buckchg_class_shutdown,
};

static int __init strategy_buckchg_class_init(void)
{
	return platform_driver_register(&strategy_buckchg_class_driver);
}
module_init(strategy_buckchg_class_init);

static void __exit strategy_buckchg_class_exit(void)
{
	platform_driver_unregister(&strategy_buckchg_class_driver);
}
module_exit(strategy_buckchg_class_exit);


MODULE_DESCRIPTION("strategy buckchg class");
MODULE_AUTHOR("liyuze1@xiaomi.com");
MODULE_LICENSE("GPL v2");
