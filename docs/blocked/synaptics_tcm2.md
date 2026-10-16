# synaptics_tcm2 - BLOCKED (not built): the template is the OnePlus touchpanel framework, the stock uses the Xiaomi framework

Reference: OS4 `<work-dir>` (Synaptics TouchCom TCM2 SPI touch, 240 functions; analysed only, did not iterate).

Template: OnePlus13 `op13_modules: vendor/oplus/kernel/touchpanel/synaptics_hbp/` (copied unmodified into this directory). 143 of the 240 stock function names exist in it (the generic Synaptics `tcm/` TouchCom core, `syna_tcm2*.c`), but:
- The OnePlus glue (`touchpanel_proc.c`, `touchpanel_healthinfo`, `touchpanel_autotest`, `touch_comon_api`, `soc/oplus/...touchpanel_event_notify.h`, boot_mode, MTK/Qualcomm panel notifier ifdefs) is absent in the stock. The stock instead imports the Xiaomi touch framework (module `xiaomi_touch`): `register_xiaomi_input_dev`, `register_touch_panel`, `xiaomi_register_panel_notifier`, `xiaomi_touch_mievent_report_{str,int}`, `update_fod_press_status`, `xiaomi_get_gesture_type`, `add_input_event_timeline_before_event_time`, `notify_raw_data_update`, `get_raw_data_base`, `schedule_resume_suspend_work`, plus the Gunyah TUI hooks (`gh_mem_notifier_*`, `gh_irq_lend_*`, `ghd_rm_mem_*`, depends gh_mem_notifier, gh_rm_drv, gh_irq_lend, pinctrl-msm).
- 97 stock functions are not in the template (Xiaomi self-test `testing_xiaomi_self_test`, `syna_testing_pt*`, `syna_pm_suspend/resume`, `syna_spi_config_gpios`, `testing_load_testlimits`, ...).
- `xiaomi_touch` is also not available as a header set here (the a17-group `xiaomi_touch` port is a different agent's module).
The Synaptics `tcm/` core files can be taken from this template, but the platform layer has to be rewritten from the stock disassembly against the Xiaomi touch framework; not feasible inside this task's budget.
