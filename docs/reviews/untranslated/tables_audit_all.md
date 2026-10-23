# G33a: аудит СОДЕРЖИМОГО таблиц всех реконструированных модулей (20.10.2026, база GLOBAL OS4.0.0.7)

Эталон: сток `<work-dir>`. Наша сборка: самая свежая нестоковая `<m>.ko` из scratch (список в `work/re/src/g33a_tools/pairs.txt`, 93 модуля; charging: g30e/g31a/g25b/g24a, mi_game g27a, mi_low_lat_mem g27b/new_*, matt = review/matt_g22a/matt.ko, mitee g26a, xr_q*/lb/mibbr/... g25a/ours и g27b/ours, аудио g15c, публичные g7d/fixed). qca_cld3_* пропущен (G22f). Scratch `<work-dir>` (пересборок не было, bazel-out не создавался).
Инструменты (копии в `work/re/src/g33a_tools/`): `tbl_audit3.py` (новый аудит, надстройка над compare_ko_v2.Elf), `run_all.sh`, `g33_run1.sh`, `g33_jt.py` (`__jump_table` / `__bug_table` как мультимножества без смещений).

## 1. Почему G30e нашёл то, что не видели инструменты (дыра в compare_ko_v2)
`compare_ko_v2.Elf.strat()` считает анонимный чанк в НЕ-строковой секции C-строкой, если его начало похоже на строку с NUL. Таблица `{1000,900,500,600,300,350,100,150}` начинается `e8 03 00 00` = "\xe8\x03" + NUL, то есть «строка» из 2 байт: сравнивается только этот префикс, остальные 30 байт не видны никому (anon-data FAIL на таких модулях был всегда, на него не смотрели). Любая таблица int, у которой первый элемент состоит из байтов 1..6/32..255, попадает в эту дыру.
`tbl_audit3.py` берёт КАЖДУЮ релокацию в data/rodata (не str) вне именованного sized-объекта, режет чанк [target, следующая граница) независимо от «строковости» и сравнивает содержимое с разрешёнными релокациями (имя/addend/контент цели, не сырые байты):
1. именованные объекты (sysfs-атрибуты/режимы, fops, of_device_id, i2c_device_id, platform_driver, writable-статики с инициализаторами, значения module-param): мультимножество (class,size,content) по нормализованному имени; чужие имена спариваются по содержимому (rename-only); различия печатаются пословно;
2. анонимные чанки (константные пулы, lookup-таблицы, switch.table, jump-таблицы): чанк-«чистая строка» (только печатные байты, нет двух NUL подряд) и «таблица целиком найдена в другом модуле (>=12 байт)» считаются объяснёнными, остальные спариваются по общей функции-пользователю (из .text релокаций) и печатаются пословно; префиксные границы (BOUNDARY) проверяются по хвосту; пути `__FILE__` (`__bug_table`) отдельно;
3. jump-таблицы (`adr`-база + `ldrb/ldrh/ldrsw [tbl,idx]` + `add ..,lsl#2` + `br`): размер элемента, число входов и для каждого case первый вызов/`ret` по fall-through пути (разбор `b`-переходов): равенство = JT-EQUIV. Эвристика, не лифтер (см. лимиты);
4. `parm=`/modinfo, `__jump_table` (static keys), `__bug_table`: compare_ko_v2 + `g33_jt.py`.

## 2. Итог (REAL = содержимое отличается и это не размещение/кодген)
| # | модуль | таблица | сток | наше | функция | вердикт | действие |
|---|---|---|---|---|---|---|---|
| 1 | mca_strategy_quickchg | `.rodata+0x6348` (32 B) | `{1000,900},{500,600},{300,350},{100,150}` | `{300,300}` в g30e/built | mca_quick_charge_regulation (get_base_limit_curr) | **REAL** (слово +0x14: 350 против 300) | G31a уже применил `{300,350}`: `g31a/built/mca_strategy_quickchg.ko` перепроверен tbl_audit3 = 0 различий таблиц, 0 unexplained. Исходники quickchg НЕ тронуты |

Других REAL-различий содержимого таблиц ни в одном из остальных 92 модулей не найдено. Всё остальное, что аудит показал и что я разобрал:
- Размещение/нумерация (содержимое равно): все `.rodata.<fn>` против слитого `.rodata`; границы чанков; имена: mi_game (`migt_debug_l`, `migt_hook_thresh.old_migt_thresh`, `param_ops_flt_target_check` = переименования локальных статиков, содержимое равно, `migt_table+0x48` указывает на тот же по смыслу локальный `migt_debug`), mitee (`g_read_b_buf`), process_monitor (`proc_debug_into_D_ops`), fault_report (`err_code_to_miev`), cameramsger (`cameramsger_init.__key`), synaptics_tcm2, sia91xx (`sia91xx_mute.g3m_rst_off_level` = анонимные 20 B стока, байты равны).
- Jump-таблицы (смещения блоков отличаются из-за раскладки кода, число входов и case->callee равны): quickchg process_event (86x2B) и parse_batt_info (11x1B), buckchg process_event, business_charger_comp event_thread (86x4B) и usb_psy_get_prop, mi_game migt_ioctl, tfa98xx (4 таблицы), cameramsger_ioctl, mitee recv_task (8 против 6 входов: у стока паддинг, у обоих `cmp w1,#5`) и др.
- mca_strategy_buckchg strategy_buckchg_monitor_workfunc 16x1B: эвристика показала разные первые вызовы для case 10..12 (PD/PPS/PD_VERIFY). Дизассемблер: сток `ptf==2 -> 1000/1100; иначе type==PPS && is_support_pps_100w()`, у нас те же проверки в другом порядке. Исходник совпадает со стоком по смыслу: КОДГЕН, не таблица.
- `__jump_table` (static keys), `parm=`/modinfo, число `__bug_table`: равны (мультимножество функция+ключ без смещений; matt bug_table 36 = 36, различаются только имена inline-функций и номера строк).
- Строки, которых нет у нас (диагностика, не данные): mi_low_lat_mem `"low_lat_mem: cma pool register failed! ret=%d\n"` (у нас `cma_pool_init_shrinker` всегда возвращает 0, ветка ошибки вычеркнута; в стоке функция тоже всегда возвращает 0 (`mov w0,wzr`), поведение равно); lb `"memcpy"` (fortify-имя в стоке); matt `matt_sacktag_walk/_one`, `matt_match_skb_to_sack`, `matt_msol_sacked`, lockdep `&h->hash[i].lock` (`__func__` inline-функций); minet `tc_high`+`wlan0` 46-байтный блоб (stock разворачивает strncpy в memcpy, значения те же); аудио (aw882xx, sia91xx, tfa98xx, cs35l43, tas25xx, si_core): printk-строки `\x01N...` и `__func__`.
- fs19xx `fsm_get_index_by_position`: таблица стока `{3,2,0,1,0,0,0,0}` = `switch` в нашем исходнике (case 1->3, 2->2, 4->1, 8->0, иначе 0): равна по смыслу.
- bq27z561: 311 чанков и 9 объектов только в стоке принадлежат НЕпортированным по замыслу функциям (nfg1000_* OTA, fg_fw_clound_update, fg_term_voltage_update_work, check_fg_cloud_string, fg_write_first_usage_date, `POWER_SUPPLY_VENDOR_TEXT`, data_flash_*); `fg_sysfs_field_tbl` 4920 против 4880 B (нет id 35 first_usage_date store и id 123 upgrade_fw), `g_bq_fg_ops` без 3 слотов (nfg1000_ota_update_check, fg_set_one_first_usage_date, fg_set_one_fast_mode_times), jump-таблицы fg_sysfs_show (124 против 123) / fg_sysfs_store (116 против 104, case 27 = first_usage_date): всё задокументировано в STATUS.md (G14g/G22e/G24a), новых различий нет. Две jump-таблицы bq_fg_probe (12 B и 32 B) имеют ту же форму (нули, шаг 3), значения = смещения блоков.
- ufs_ffu: `need_update_k` (3936 B): у нас указатели на прошивку = zero (заглушка, 25 MB прошивки намеренно не воспроизводятся, `.ko` не публикуется): 31 объект различается, известный дизайн.
- sia91xx: две jump-таблицы не разрешены эвристикой (`sipa_compatible_chips_adapt` 11x1B: первые вызовы `check_chip_id` против `gpio_to_desc` в case 0..2; `sipa_regmap_proc_1_reg` 4x1B case 1: write_reg против read_reg) и 80-байтная lookup-таблица `sipa_suspend` ({6,7,8,6,6,11,12,...}), которой у нас нет (код). НЕ ДОКАЗАНО (аудио, не charging): нужен разбор дизассемблера, перенесено в «осталось». mi_memory: константа cst16 только у нас (кодген векторных констант `hr_show`).
- qca_cld3_peach_v2: в таблице для полноты, разобран в G22f (placement).

## 3. Что покрыто и лимиты
- Покрыто для 93 модулей: named data (включая writable init, sysfs/fops/OF/i2c/platform-таблицы, mode bits), анонимные пулы, switch/jump-таблицы, parm=/modinfo, static-key/bug-таблицы.
- Не покрыто: 733 стоковых файла пакета (в них нет нашего кода); ссылки без релокаций; лифтер-уровень jump-таблиц (case -> первый вызов по fall-through, не полная семантика: ложные JT-DIFF при смене порядка проверок, как в buckchg; ложное равенство, если различие позже первого вызова); строки определяются как «печатные байты без двух NUL подряд»: таблица из одних печатных байт, совпавшая с чужой строкой, спрятала бы различие; таблицы >= 12 байт «найдены целиком» считаются равными без привязки к функции.
- Пересборок не было: правок исходников этим заданием нет (STATUS.md не менялись). Безопасность charging: community-only, не прошивать, никаких заявлений о безопасности.

## 4. Таблица по модулям
Колонки: модуль | наша сборка | различающиеся имена / только-сток / только-наше (+ rename с равным содержимым) | анон-чанки сток / наше | jump-таблиц равны | JT-DIFF (эвристика) | unexplained после классификации (в bq27z561/qca/аудио/matt это строки-логи и отсутствующие функции, см. выше). quickchg здесь по g30e/built (до фикса); по g31a/built: 0 различий.

| модуль | наша сборка | DIFF-имён / сток-only / наше-only (rename) | анон. сток / наше | JT равны | JT-DIFF | unexpl |
|---|---|---|---|---|---|---|
| aw882xx_dlkm | g15c/built/aw882xx_dlkm.ko | 0/0/0 (+0 renamed) | 890 / 13 | 0 | 0 | 4 |
| bootinfo | g7d/fixed/bootinfo.ko | 0/0/0 (+0 renamed) | 39 / 0 | 0 | 0 | 0 |
| bq27z561 | g24a/built/bq27z561.ko | 3/9/0 (+0 renamed) | 863 / 6 | 1 | 1 | 30 |
| cameralog | g25a/ours/cameralog.ko | 0/0/0 (+0 renamed) | 35 / 1 | 1 | 0 | 0 |
| cameramsger | g25a/ours/cameramsger.ko | 0/1/1 (+1 renamed) | 99 / 1 | 1 | 0 | 0 |
| charger_partition | g14b/built/charger_partition.ko | 0/0/0 (+0 renamed) | 64 / 0 | 0 | 0 | 0 |
| cs35l43_dlkm | g15c/built/cs35l43_dlkm.ko | 0/0/0 (+0 renamed) | 493 / 16 | 0 | 0 | 1 |
| debug_ext | g7d/fixed/debug_ext.ko | 0/0/0 (+0 renamed) | 66 / 0 | 0 | 0 | 0 |
| dump_binder | g25a/ours/dump_binder.ko | 0/0/0 (+0 renamed) | 24 / 0 | 0 | 0 | 0 |
| dynamic_readahead | g7d/fixed/dynamic_readahead.ko | 0/0/0 (+0 renamed) | 7 / 0 | 0 | 0 | 0 |
| fault_report | g25a/ours/fault_report.ko | 0/1/1 (+1 renamed) | 17 / 0 | 0 | 0 | 0 |
| fs19xx_dlkm | g15c/built/fs19xx_dlkm.ko | 0/0/0 (+0 renamed) | 259 / 3 | 0 | 0 | 1 |
| gsim | g7d/fixed/gsim.ko | 0/0/0 (+0 renamed) | 618 / 14 | 0 | 0 | 0 |
| hardwareinfo | g7d/fixed/hardwareinfo.ko | 0/0/0 (+0 renamed) | 13 / 0 | 0 | 0 | 0 |
| hwid | g7d/fixed/hwid.ko | 0/0/0 (+0 renamed) | 55 / 5 | 0 | 0 | 0 |
| lb | g27b/ours/lb.ko | 0/0/0 (+0 renamed) | 184 / 4 | 1 | 0 | 1 |
| matt | g33a/matt_g22a.ko | 0/0/0 (+0 renamed) | 516 / 25 | 2 | 0 | 5 |
| mca_basic_wireless | g31a/built/mca_basic_wireless.ko | 0/0/0 (+0 renamed) | 435 / 15 | 3 | 0 | 0 |
| mca_buckchg_jeita | g25b/built/mca_buckchg_jeita.ko | 0/0/0 (+0 renamed) | 136 / 5 | 1 | 0 | 0 |
| mca_business_charger_comp | g25b/built/mca_business_charger_comp.ko | 0/0/0 (+0 renamed) | 218 / 6 | 2 | 0 | 0 |
| mca_charge_mievent | g14b/built/mca_charge_mievent.ko | 0/0/0 (+0 renamed) | 16 / 0 | 0 | 0 | 0 |
| mca_connector_antiburn | g27b/ours/mca_connector_antiburn.ko | 0/0/0 (+0 renamed) | 104 / 1 | 1 | 0 | 0 |
| mca_platform_fg_ic_ops | g14b/built/mca_platform_fg_ic_ops.ko | 0/0/0 (+0 renamed) | 1 / 0 | 0 | 0 | 0 |
| mca_pwm_fan | g27b/new_mca_pwm_fan.ko | 0/0/0 (+0 renamed) | 117 / 1 | 0 | 0 | 0 |
| mca_quick_wireless | g30e/built/mca_quick_wireless.ko | 0/0/0 (+0 renamed) | 265 / 4 | 1 | 0 | 0 |
| mca_smart_charge | g25b/built/mca_smart_charge.ko | 0/0/0 (+0 renamed) | 188 / 6 | 1 | 0 | 0 |
| mca_strategy_buckchg | g30e/built/mca_strategy_buckchg.ko | 0/0/0 (+0 renamed) | 312 / 8 | 1 | 1 | 1 |
| mca_strategy_fg_class | g14b/built/mca_strategy_fg_class.ko | 0/0/0 (+0 renamed) | 1 / 0 | 0 | 0 | 0 |
| mca_strategy_fg_comp | g31a/built/mca_strategy_fg_comp.ko | 0/0/0 (+0 renamed) | 522 / 8 | 0 | 0 | 0 |
| mca_strategy_quickchg | g30e/built/mca_strategy_quickchg.ko | 0/0/0 (+0 renamed) | 583 / 7 | 2 | 0 | 1 |
| mca_wireless_revchg | g25b/built/mca_wireless_revchg.ko | 0/0/0 (+0 renamed) | 210 / 5 | 0 | 0 | 0 |
| mi_damon | g27b/ours/mi_damon.ko | 0/0/0 (+0 renamed) | 120 / 2 | 0 | 0 | 0 |
| mi_fp | g7d/fixed/mi_fp.ko | 0/0/0 (+0 renamed) | 296 / 3 | 0 | 0 | 0 |
| mi_game | g27a/built/mi_game.ko | 2/2/5 (+2 renamed) | 263 / 16 | 1 | 0 | 0 |
| mi_low_lat_mem | g27b/new_mi_low_lat_mem.ko | 0/0/0 (+0 renamed) | 237 / 3 | 0 | 0 | 1 |
| mi_memory | g7d/fixed/mi_memory.ko | 0/0/0 (+0 renamed) | 174 / 7 | 0 | 0 | 1 |
| mi_mempool | g7d/fixed/mi_mempool.ko | 0/0/0 (+0 renamed) | 64 / 1 | 0 | 0 | 0 |
| mi_power | g7d/fixed/mi_power.ko | 0/0/0 (+0 renamed) | 109 / 0 | 0 | 0 | 0 |
| mi_reclaim | g25a/ours/mi_reclaim.ko | 0/0/0 (+0 renamed) | 217 / 0 | 0 | 0 | 0 |
| mi_stack | g7d/fixed/mi_stack.ko | 0/0/0 (+0 renamed) | 15 / 0 | 0 | 0 | 0 |
| mi_thermal_interface | g7d/built/mi_thermal_interface.ko | 0/0/0 (+0 renamed) | 85 / 1 | 0 | 0 | 0 |
| mi_trace | g7d/fixed/mi_trace.ko | 0/0/0 (+0 renamed) | 29 / 0 | 0 | 0 | 0 |
| mi_ubt | g7d/fixed/mi_ubt.ko | 0/0/0 (+0 renamed) | 8 / 0 | 0 | 0 | 0 |
| mi_ubt_test | g7d/fixed/mi_ubt_test.ko | 0/0/0 (+0 renamed) | 4 / 0 | 0 | 0 | 0 |
| mi_wmark | g7d/fixed/mi_wmark.ko | 0/0/0 (+0 renamed) | 9 / 0 | 0 | 0 | 0 |
| mi_wq_dynamic_priority | g7d/fixed/mi_wq_dynamic_priority.ko | 0/0/0 (+0 renamed) | 19 / 0 | 0 | 0 | 0 |
| mibbr | g25a/ours/mibbr.ko | 0/0/0 (+0 renamed) | 64 / 3 | 0 | 0 | 0 |
| miev | g7d/fixed/miev.ko | 0/0/0 (+0 renamed) | 45 / 0 | 0 | 0 | 0 |
| miicmpfilter | g7d/fixed/miicmpfilter.ko | 0/0/0 (+0 renamed) | 13 / 1 | 0 | 0 | 0 |
| millet_binder | g7d/fixed/millet_binder.ko | 0/0/0 (+0 renamed) | 7 / 0 | 0 | 0 | 0 |
| millet_core | g7d/fixed/millet_core.ko | 0/0/0 (+0 renamed) | 79 / 1 | 0 | 0 | 0 |
| millet_hs | g7d/fixed/millet_hs.ko | 0/0/0 (+0 renamed) | 3 / 0 | 0 | 0 | 0 |
| millet_oem_cgroup | g7d/fixed/millet_oem_cgroup.ko | 0/0/0 (+0 renamed) | 2 / 0 | 0 | 0 | 0 |
| millet_pkg | g7d/fixed/millet_pkg.ko | 0/0/0 (+0 renamed) | 19 / 1 | 0 | 0 | 0 |
| millet_sig | g7d/fixed/millet_sig.ko | 0/0/0 (+0 renamed) | 3 / 0 | 0 | 0 | 0 |
| minet | g25a/ours/minet.ko | 0/0/0 (+0 renamed) | 79 / 3 | 0 | 0 | 1 |
| minidump | g7d/fixed/minidump.ko | 0/0/0 (+0 renamed) | 280 / 1 | 0 | 0 | 0 |
| miscore | g25a/ours/miscore.ko | 0/0/0 (+0 renamed) | 54 / 2 | 0 | 0 | 0 |
| mist | g25a/ours/mist.ko | 0/0/0 (+0 renamed) | 116 / 0 | 0 | 0 | 0 |
| mitee_dlkm | g26a/built/mitee_dlkm.ko | 0/1/1 (+1 renamed) | 307 / 15 | 1 | 0 | 0 |
| perf_actuator | g7d/fixed/perf_actuator.ko | 0/0/0 (+0 renamed) | 18 / 0 | 0 | 0 | 0 |
| perfmgr | g7d/fixed/perfmgr.ko | 0/0/0 (+0 renamed) | 105 / 3 | 0 | 0 | 0 |
| plat_common | g7d/fixed/plat_common.ko | 0/0/0 (+0 renamed) | 8 / 0 | 0 | 0 | 0 |
| process_monitor | g27b/ours/process_monitor.ko | 0/1/1 (+1 renamed) | 140 / 2 | 0 | 0 | 0 |
| qca_cld3_peach_v2 | g23d/built/qca_cld3_peach_v2.ko | 8/0/0 (+0 renamed) | 45586 / 1170 | 1 | 0 | 38 |
| qcom_adsp_pd_protocol | g7d/fixed/qcom_adsp_pd_protocol.ko | 0/0/0 (+0 renamed) | 38 / 2 | 0 | 0 | 0 |
| rsmc_driver | g25a/ours/rsmc_driver.ko | 0/0/0 (+0 renamed) | 430 / 1 | 0 | 0 | 0 |
| rss_monitor | g25a/ours/rss_monitor.ko | 0/0/0 (+0 renamed) | 41 / 1 | 0 | 0 | 0 |
| rtg | g7d/fixed/rtg.ko | 0/0/0 (+0 renamed) | 8 / 0 | 0 | 0 | 0 |
| scene_swappiness | g7d/fixed/scene_swappiness.ko | 0/0/0 (+0 renamed) | 21 / 1 | 0 | 0 | 0 |
| si_core_module | g7d/fixed/si_core_module.ko | 0/0/0 (+0 renamed) | 40 / 3 | 0 | 0 | 1 |
| sia91xx_dlkm | g15c/built/sia91xx_dlkm.ko | 0/0/1 (+0 renamed) | 419 / 6 | 1 | 2 | 10 |
| sia91xx_tuning_dlkm | g15c/built/sia91xx_tuning_dlkm.ko | 0/0/0 (+0 renamed) | 37 / 1 | 0 | 0 | 0 |
| sla | g7d/fixed/sla.ko | 0/0/0 (+0 renamed) | 69 / 0 | 0 | 0 | 0 |
| stm_console | g7d/fixed/stm_console.ko | 0/0/0 (+0 renamed) | 2 / 0 | 0 | 0 | 0 |
| stm_core | g7d/fixed/stm_core.ko | 0/0/0 (+0 renamed) | 24 / 4 | 0 | 0 | 0 |
| stm_ftrace | g7d/fixed/stm_ftrace.ko | 0/0/0 (+0 renamed) | 2 / 0 | 0 | 0 | 0 |
| stm_heartbeat | g7d/fixed/stm_heartbeat.ko | 0/0/0 (+0 renamed) | 1 / 0 | 0 | 0 | 0 |
| stm_nfc_i2c | g7d/fixed/stm_nfc_i2c.ko | 0/0/0 (+0 renamed) | 91 / 2 | 0 | 0 | 0 |
| stm_p_ost | g7d/fixed/stm_p_ost.ko | 0/0/0 (+0 renamed) | 7 / 0 | 0 | 0 | 0 |
| stm_st54se_gpio | g7d/fixed/stm_st54se_gpio.ko | 0/0/0 (+0 renamed) | 31 / 0 | 0 | 0 | 0 |
| swinfo | g7d/fixed/swinfo.ko | 0/0/0 (+0 renamed) | 11 / 2 | 0 | 0 | 0 |
| synaptics_tcm2 | g7d/fixed/synaptics_tcm2.ko | 0/1/1 (+1 renamed) | 1493 / 18 | 0 | 0 | 0 |
| tas25xx_dlkm | g15c/built/tas25xx_dlkm.ko | 0/0/0 (+0 renamed) | 497 / 13 | 0 | 0 | 1 |
| tfa98xx_dlkm | g15c/built/tfa98xx_dlkm.ko | 0/0/0 (+0 renamed) | 506 / 29 | 3 | 0 | 26 |
| typec_analog_acc_dlkm | g7d/fixed/typec_analog_acc_dlkm.ko | 0/0/0 (+0 renamed) | 12 / 0 | 0 | 0 | 0 |
| ufs_ffu | g14a/built/ufs_ffu.ko | 31/0/0 (+0 renamed) | 67 / 0 | 0 | 0 | 1 |
| vip_sched | g7d/fixed/vip_sched.ko | 0/0/0 (+0 renamed) | 51 / 2 | 0 | 0 | 0 |
| xiaomi_touch | g7d/fixed/xiaomi_touch.ko | 0/0/0 (+0 renamed) | 442 / 8 | 0 | 0 | 0 |
| xiaomi_wifi_gpio | g7d/fixed/xiaomi_wifi_gpio.ko | 0/0/0 (+0 renamed) | 24 / 3 | 0 | 0 | 0 |
| xm_power | g7d/fixed/xm_power.ko | 0/0/0 (+0 renamed) | 17 / 0 | 0 | 0 | 0 |
| xr_qi | g25a/ours/xr_qi.ko | 0/0/0 (+0 renamed) | 124 / 1 | 1 | 0 | 0 |
| xr_qs | g25a/ours/xr_qs.ko | 0/0/0 (+0 renamed) | 59 / 2 | 2 | 0 | 0 |
