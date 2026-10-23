# bq27z561 final audit (goal-G40c, 20.10.2026), GLOBAL OS4.0.0.7

SAFETY: community-only, PARTIAL, DO NOT LOAD, never flash, never in a ROM build or kernel package. Нет заявлений о безопасности. Every limit / register / command here was read from the stock disassembly (`llvm-objdump -dr` of `<work-dir>`); nothing was run on a gauge; NO new gauge WRITE path was added (the only source edit is a log-text helper); no firmware blob / key value in any file. База: stock GLOBAL OS4.0.0.7. Scratch: `<work-dir>` (stf/ = split stock listings, ourf/ = ours, rep/ = outputs).

Инструменты (копии в `work/re/g40c_tools`): `casesig.py` (по каждому case jump-таблицы собирает множество событий = вызовы, строки формата, imm, cmp-константы, смещения полей; stock против ours), `probe_seq.py` (упорядоченная последовательность вызовов bq_fg_probe, difflib), `symcmp5b all`, `s5ms`, `tbl_audit3 -v`, compare_ko_v2 + preamble_cmp (kb.sh / kbs.sh = g24a_tools, переименованы под g40c).

## 0. Итог
- Пересборка (real Kleaf, 1 сборка, nice 10, load 8, один Bazel): source `bq27z561.c` md5 **b4152e2922bb97a8f3ab7eb2f923b429** (G24a: 29060647...), `inc/bq27z561.h` без изменений bc371a0d...; `.ko` md5 **a99882fbd1c91669888019f561c328b1** (G24a: 29e01a58...). compare_ko_v2: 144 of 200 byte-identical (как G24a), 56 differ (тот же список), kCFI 83/83, preamble_cmp 0 mismatches, exports PASS; VERDICT NOT STRICT-EQUIVALENT (function-set 20 only-stock + 2 stubs, objects, anon-data, imports 7, buildmeta, depends).
- Единственная правка: sysfs `soh` store (id 12): лог "set fake soh:: %d" теперь с `__func__` = `fg_set_fake_soh` и line 2620 как у стока (inline helper, software only). 1 итерация; casesig после правки: id 12 равен. Других доказанных различий поведения на достижимых состояниях dada не найдено: остальное = CODEGEN / документированное отсутствие / мёртвый DT-гейт.
- Не доказано: поведение на реальном gauge; pv / vendor / версия data flash у dada; write paths fg_set_seal / fg_mac_write_block / __fg_write_block опираются на G17b / G22e / bq_dynfv_seal_review.md (здесь перепроверены только факты symcmp5).

## 1. DT dada (global dtbo + 8 DTB vendor_boot, `<work-dir>`, `dtb_gl/0*.dts`)
Единственный узел газометра: `bq27z561_standlone@55 { compatible = "ti,bq27z561_master"; ic_role = 0; reg = 0x55; unseal_key (u32); bq,charge-full-design = 0x14b4; soh_th = 0x5c; cycle_th = 0x190; cycle_step = 0x32; resistance-id = 0x186a0; support-voltage-record-level; version_number = [44]; start_byte_address = [0a]; byte_length = [06]; record_voltage (6 B); support-version-compatible; support_new_soc_smooth }` (fragment@47, dtbo.img: 1 запись). В 8 DTB узла газометра нет.
Гейты probe, число вхождений в dtbo / DTB: `support_nvt1000_ota` 0/0, `co_by_gpio` 0/0, `batt_ctl` 0/0, `base_termv` 0/0, `delta_soh*` 0/0, `support-dynamic-fv` 0/0, `support-fast-mode-when-charging` 0/0. Присутствуют `support-version-compatible` и `support-voltage-record-level` (оба порта следуют стоку; запись record-voltage = G24a, гейт platform/country/vendor).

## 2. bq_fg_probe: остаток stock-only (symcmp5: only-stock 182 / only-ours 58; 1867 vs 1597 insns; упорядоченные вызовы: stock 230 / ours 177)
| # | stock-only факт (stock .text+) | гейт | на dada |
|---|---|---|---|
| P1 | `gpio_to_desc` + `gpiod_direction_output_raw(ctl_co gpio, 0)` + log "bq_parse_dt" (afb4..afe0) | DT `co_by_gpio` | в DT нет: мёртвое |
| P2 | NFG1000 boot-app judge: 3 raw `__fg_read_word` (blocking mutex_lock, msleep), raw `__fg_write_block {0x00,0x02}`, `__fg_read_byte`, `nfg1000_update_APP`, `nfg1000_ota_program_step7_ExitBoot`, 9x mutex_lock/unlock, 8x msleep, логи "nfg1000_boot_app_judge" (b2ec..b620, 51 вызов) | DT `support_nvt1000_ota` != 0 | в DT нет: мёртвое (presence-check word-read вариант уже в нашем D2 G22e) |
| P3 | `fg_set_force_sim_soh` (MAC 0x1056 без данных; c7b8 -> .text+0x10928) после MAC 0x4A и `strcmp("3XM31")` | hwid `platform_version == 3 && country == 0` + имя "3XM31" | мёртвое при pv 2 (pv INFERRED: берётся из hwid, статически не видно) |
| P4 | `INIT_DELAYED_WORK` + `queue_delayed_work_on(system_wq, term_voltage work, 2500 jiffies)` (c674); INIT `fg_clound_update_workfunc` | БЕЗ DT-гейта, при каждом probe (в т.ч. "probe cnt max") | term_voltage ДОСТИЖИМО в стоке (10 s после probe), не портировано (data-flash пути, BQ7-5); cloud INIT = software, действует только через upgrade_fw |
| P5 | `of_get_property` stkarg x5, lockdep-key аргумент `__mutex_init` (obj `fg_sysfs_attrs`) | нет | CODEGEN (CONFIG_LOCKDEP off): эффекта нет |
| P6 | 9 логов "val:%d, '0':%d, 'A':%d, 'a':%d" и лог eeprom_version: иной порядок / `__func__` (stock inline в probe, у нас fg_get_manufacture_data) | нет | КОСМЕТИКА: строки формата равны |
| ours-only | presence-check в ином порядке (usleep/trylock/mac_read/word read) + 1 log_err | D2 G22e | равно по эффекту |
Вывод: достижимое на dada различие probe одно (P4, документировано, не портируется).

## 3. Per-function (все 220 stock-функций)
Verdict: IDENT = compare_ko_v2 byte-identical; CODEGEN = symcmp5 CLEAN-POL или факты равны + равный мультимножество вызовов (s5ms); FORM = различие формы / лог-текста на достижимых путях; DEAD-DIFF = различие только в состояниях, недостижимых на dada; ABSENT-REACH / ABSENT-DEAD = функции нет у нас.
### 3.1 144 IDENT (evidence: compare_ko_v2 function-bytes, тот же список что G24a)
__fg_read_byte, __fg_read_word, bq27z561_dump_log_head, bq_fg_remove, bq_fg_resume, bq_fg_shutdown, bq_fg_suspend, cleanup_module, fg_fl4p0_enable_check, fg_get_aged_flag, fg_get_alarm_gas_final, fg_get_authentic, fg_get_average_temp, fg_get_batt_abnormal_info, fg_get_batt_sn, fg_get_charge_absoc, fg_get_chip_ok, fg_get_csd_flag, fg_get_csd_r1, fg_get_csd_r2, fg_get_cuv_count, fg_get_cuv_last_cycle, fg_get_dcr, fg_get_dcr_inc_ratio, fg_get_dcr_ratio, fg_get_dcr_refresh_time, fg_get_dcr_slope1, fg_get_dcr_slope2, fg_get_dcr_slope3, fg_get_dcr_slope_ge, fg_get_device_name, fg_get_dod_count, fg_get_eis_soh, fg_get_eis_soh_cyclecount, fg_get_error_state, fg_get_fast_mode_times, fg_get_fc_damage, fg_get_fc_hours_real, fg_get_hcuv_count, fg_get_hcuv_last_cycle, fg_get_hocd_count, fg_get_hocd_last_cycle, fg_get_hscd_count, fg_get_hscd_last_cycle, fg_get_iaver, fg_get_isc_alert_level, fg_get_max_dsg_current, fg_get_max_life_temp, fg_get_max_life_vol, fg_get_min_life_temp, fg_get_min_life_vol, fg_get_min_temp, fg_get_min_voltage, fg_get_non_fc_hours_real, fg_get_ocd_count, fg_get_ocd_last_cycle, fg_get_one_adapt_power, fg_get_one_average_current, fg_get_one_batt_cell_info, fg_get_one_batt_qmax, fg_get_one_batt_sn, fg_get_one_calc_rvalue, fg_get_one_calibration_charge_energy, fg_get_one_calibration_ffc_iterm, fg_get_one_chip_ok, fg_get_one_count_level1, fg_get_one_count_level2, fg_get_one_count_level3, fg_get_one_count_lowtemp, fg_get_one_cutoff_voltage, fg_get_one_device_name, fg_get_one_dod_count, fg_get_one_fast_mode_times, fg_get_one_fastcharge_mode, fg_get_one_fcc_default, fg_get_one_fg_vendor, fg_get_one_first_usage_date, fg_get_one_iaver, fg_get_one_isc_alert_level, fg_get_one_manufacturing_date, fg_get_one_raw_soc, fg_get_one_real_supplement_energy, fg_get_one_soa_alert_level, fg_get_one_soc_decimal, fg_get_one_soc_decimal_rate, fg_get_one_temp_min, fg_get_one_time_ot, fg_get_one_vsiloss, fg_get_ota_update_flag, fg_get_pack_vendor, fg_get_qmax_cyclecount, fg_get_rel_soh, fg_get_rel_soh_cyclecount, fg_get_remaining_time, fg_get_seal, fg_get_soa_alert_level, fg_get_ucl_ge, fg_get_ui_soh, fg_get_verify_digest, fg_read_avercurrent, fg_read_co_auto_open, fg_read_current, fg_read_fcc_soh, fg_read_manufacturing_date, fg_read_max_temp_occur_time, fg_read_max_temp_time, fg_read_one_charging_voltage, fg_read_one_co, fg_read_one_current, fg_read_one_cyclecount, fg_read_one_dc, fg_read_one_fc, fg_read_one_fcc, fg_read_one_original_temperature, fg_read_one_probe_ok, fg_read_one_rm, fg_read_one_rsoc, fg_read_one_soh, fg_read_one_status, fg_read_one_temperature, fg_read_one_tte, fg_read_one_ttf, fg_read_one_volt, fg_read_original_temperature, fg_read_qmax, fg_read_run_time, fg_read_status, fg_read_tambient, fg_read_temperature, fg_read_tfullchgq, fg_read_total_fw_runtime, fg_read_tremq, fg_read_tsim, fg_read_volt, fg_set_authentic, fg_set_force_report_full, fg_set_one_clear_count_data, fg_set_one_cutoff_voltage, fg_set_one_fastcharge_mode, fg_set_temperature, fg_set_verify_digest, fg_write_byte, fg_write_word, init_module

### 3.2 56 differing
| функция | verdict | evidence |
|---|---|---|
| fg_sysfs_show | CASE-EQUIV (id 0..122); id 123 upgrade_fw ABSENT | casesig: 123 case-сигнатур стока и ours равны (calls, форматы, imm, cmp, смещения полей); артефакт id 32 (константный клон fg_mac_read_block); stock id 123 = флаг из `spec_result` печатается "%d", у нас узла нет. symcmp5 POLARITY-DIFF-ATOMS = размер таблицы 124 против 123 |
| fg_sysfs_store | CASE-EQUIV (id 8..111) + FIXED id 12; id 35 и 123 ABSENT | casesig; id 12 исправлен (G40c); id 35 stock `fg_write_first_usage_date`; id 112..122 у стока jump на общий эпилог 0x1088 (= default, проверено), у нас за bound = default; id 123 (check_fg_cloud_string, 26 log_err, sscanf x11, strncpy x3, queue_delayed_work_on) ABSENT |
| fg_mac_read_block, __fg_mac_read_block | CODEGEN | наш константный клон (cmd 123, len 11 = ui_soh); CLEAN-POL |
| __fg_write_block | FORM (safe side) | stock `kmalloc(len+1)`, len u32; ours len -> u8 + FORTIFY `fortify_panic` ветка; те же kmalloc / memcpy / i2c_transfer / kfree; callers len <= 0x22 |
| fg_read_packvolt | DEAD-DIFF | int return стока = результат лога / stale reg; единственный caller (sysfs id 2) игнорирует; значение, лог, шина равны |
| fg_read_rsoc, fg_read_cyclecount, fg_read_rm, fg_read_fcc, fg_read_soh, fg_read_df_check, fg_read_dc, fg_read_block, bq27z561_dump_log_context | CODEGEN | CLEAN-POL, вызовы равны (fg_read_fcc: 1 count-diff = лишний store) |
| fg_read_chem_df_sign, fg_get_app_checksum | CODEGEN | FACT-DIFF 0/4 = 4 stack-slot labels; вызовы равны |
| fg_get_calc_rvalue, fg_get_cutoff_voltage, fg_get_over_vol_duration, fg_get_batt_use_environment | CODEGEN | CLEAN-POL; stock дублирует trylock / usleep в двух vendor-ветках (phi), у нас одна (ite); команды 0x66 / 0x1030, 0x4F / 0x50, 0x67 / 0x1031 |
| fg_read_first_usage_date | CODEGEN | symcmp5 atoms: A1 = (c-48) <u 10, A2 = (c-58) <u -10 = та же проверка цифры (A2 = !A1), "reach"-разница артефакт; "99999999" как 64-bit imm (G22e) |
| fg_get_one_temp_max | CODEGEN (path-paired здесь) | stock: до 3 чтений MAC 0x60, байт 6 в 20..60 (cmn / b.hi), 3-е чтение при выходе из диапазона или после 2 отказов; при отказе 3-го значение = последнее (0 после 2-го отказа, иначе вне-диапазонный байт): у нас то же; логи 2961 / 2971 / 2977 равны |
| fg_read_time_ht, fg_read_time_ot | DEAD-DIFF | stock читает MAC 0x62 один раз ДО switch (также vendor >= 5; при ota_updating не читает, value = 0), ours читает только внутри case. `fg_read_vendor` даёт 0..4 (MPC7021 = 5 не присваивается), ota_updating только в OTA-пути (не портирован): на достижимых состояниях значения равны (vendor 0: (b19<<9 + b18<<1) в ht; 1..4: u16 b12 / b14) |
| fg_get_over_peak_flag, fg_get_current_dev, fg_get_power_dev, fg_get_average_current, fg_get_start_learning, fg_get_stop_learning, fg_get_actual_power, fg_get_learning_power, fg_get_learning_power_dev, fg_get_learning_time_dev, fg_get_constant_power, fg_get_referance_power, fg_get_nvt_referance_power, fg_get_referance_current, fg_get_start_learning_b, fg_get_stop_learning_b, fg_get_learning_power_b, fg_get_actual_power_b, fg_get_learning_power_dev_b, fg_get_vsiloss, fg_get_one_csd_flag | CODEGEN | CLEAN-POL, вызовы равны |
| fg_set_seal | FORM (write path, не менялась) | symcmp5 77 vs 75 фактов = объединённые log-сайты 5023 / 5038 / 5055 и unused return (callers игнорируют, sysfs id 21); 3 guard-разницы разобраны в bq_dynfv_seal_review.md |
| fg_mac_write_block | FORM (write path, не менялась) | symcmp5 34 / 35 фактов; checksum-цикл; ours отказывает при len > 32 (stock без проверки; callers <= 32); последовательность 0x3E затем 0x60 равна |
| bq_fg_probe | см. §2 | P1..P6 |
| fg_set_fastcharge_mode, fg_read_batt_info (80/80), fg_update_data_work (167/167, inline record-voltage), fg_set_one_co (23/23) | CODEGEN | CLEAN-POL |
| fg_get_one_cc_coulomb_integral | CODEGEN (path-paired здесь) | команда: vendor 0 -> 0x74, иначе 0x71, vendor 4 и [0x73d] != 0 -> 0x3004; результат: vendor 0 -> u16 buf[+0x14], vendor 4 и [0x73d] -> u16 buf[+0x20], иначе word buf[+0x1c]: ours идентично, блоки в другом порядке; symcmp5 POLARITY-DIFF (1 same-atom) = хэш phi, A5 (vendor == 0) подразумевает !A1 |
| fg_set_one_dynamic_fv | STUB (ABSENT-DEAD) | stock: MAC 0x3002 / 0x3003 write (fg_mac_write_block, -10 / -5), вызывается jeita только при DT `support-dynamic-fv` (в DT dada нет); ours -EOPNOTSUPP |
| fg_toggle_co | FORM | return sbfx(eor(co, want)): ours ret(0) на ветке co == want + слитый хвост (charging_g22_review C1); на dada fg_type 0 / нет co_by_gpio |

### 3.3 20 only-stock (ABSENT) и 2 stubs
| функция | гейт | на dada |
|---|---|---|
| fg_term_voltage_update_work | queue при КАЖДОМ probe (10 s), без DT-гейта; внутри pv / версия data flash / имя пака | ABSENT-REACH (единственная достижимая); data flash MAC 0x440C 0x4412 0x4414 0x4417 0x448A 0x448B; не портируется |
| check_fg_cloud_string, fg_clound_update_workfunc, fg_fw_clound_update, fg_fw_modify_ivterm_fun_P11, fg_ver43_update_ver_P11, GetCRC8 | только запись userspace в sysfs `upgrade_fw` (id 123) | ABSENT-REACH-IF-WRITTEN: узла у нас нет (запись = ошибка, не data flash); писатель на dada не найден |
| fg_set_one_first_usage_date, fg_write_first_usage_date | запись sysfs id 35 / fg_comp store (MAC 0x7B) | как выше (у нас 0444, ops slot NULL) |
| fg_set_force_sim_soh | pv == 3, country == 0, "3XM31" | ABSENT-DEAD (pv 2 INFERRED) |
| fg_set_one_fast_mode_times | fg_comp `support-fast-mode-when-charging` | ABSENT-DEAD (DT 0 вхождений); sysfs `fast_mode_times` store (MAC 0xE5) у нас есть |
| nfg1000_erase_flash_step, nfg1000_ota_program_step5_update_gauge, nfg1000_ota_program_step6_CheckCrc, nfg1000_ota_program_step7_ExitBoot, nfg1000_ota_update_check, nfg1000_read_flash_step, nfg1000_update_APP, nfg1000_update_flash_step, nfg1000_update_judge | DT `support_nvt1000_ota` | ABSENT-DEAD (0 вхождений в dtbo и 8 DTB) |
| fg_clound_update_workfunc_stub, fg_term_voltage_update_work_stub (ours) | никогда не ставятся в очередь | stubs; stub .ko не поставляется |

## 4. Таблицы (tbl_audit3 -v, содержимое, не размещение)
- Именованные объекты: 25 stock / 16 ours; только stock 9 (POWER_SUPPLY_VENDOR_TEXT, code_version_crc16, data_flash_erase_order, nfg1000_Dataflash_updata_CMD, nfg1000_code_data 28672 B, sha_256_pass_word, sha_256_ramdom_data, spec_result, static_df_sig) = данные непортированных функций (OTA / firmware). Различаются по содержимому 3: `fg_sysfs_attrs` 992 vs 984 B (123 vs 122 указателей); `fg_sysfs_field_tbl` 4920 vs 4880 B: ровно 3 hunks = запись `first_usage_date` (mode 0664 у стока, 0444 у нас; store ptr) и запись `upgrade_fw` (id 123, 0664) только у стока; `g_bq_fg_ops` 632 B: 3 слота NULL у нас (ota_update_check 65, set_first_usage_date 69, set_fast_mode_times 72). Остальные записи таблицы (имя, mode, id, show / store) равны.
- Jump-таблицы: fg_sysfs_show 124 vs 123 (первые 123 равны; последняя = upgrade_fw); fg_sysfs_store 116 x2B vs 104 x2B (case id 35: call fg_write_first_usage_date против ret; id 112..122 стока = default; id 123 = upgrade_fw); probe .rodata+0x1ec (12 x1B) и +0x20c (16 x2B) JT-EQUIV.
- Анонимные пулы: stock-only unexplained 28 = строки непортированных функций (check_fg_cloud_string, cloud-ветка store, NFG-ветка probe, dynamic_fv, VENDOR_TEXT) + 3 безвредных ("RemainingCapacity command not supported!" есть в обоих, tool видит хвост chunk; "99999999" у нас imm; лог probe). ours-only 4 = размещение.

## 5. Sysfs: каждый id 0..122 (+123): режим, формат, запись
mode = наш (стоковый такой же, кроме id 35); `=` значит: множество событий case (вызовы, форматы, imm, cmp, поля) равно стоку; далее сами события. Единицы: sysfs единиц не печатает; значение = результат названного read-helper (сырое значение регистра / MAC; физические единицы в модуле не видны и не заявляются). `fld:str0x..` = store только в поле драйвера (software); gauge-записи через sysfs (все есть и в стоке, права 0664): seal (id 21, fg_set_seal: unseal / re-seal DT-ключом), start / stop_learning (59, 60, 70, 71: fg_write_byte, bound cmp 255), learning_power, constant_power, referance_power, learning_power_b (62, 65, 67, 72: fg_write_word, bounds cmp 255 и 32767), ui_soh (32: MAC 0x7B, 11 токенов), fast_mode_times (111: MAC 0xE5, 2 B). 0664-узлы без события store (dod_count 38 ...) = no-op в обоих.
| id | name | mode ours (stock) | show (stock-equal? ) | store |
|---|---|---|---|---|
| 0 | chip_ok | 0440 | = fg_get_chip_ok | n/a  |
| 1 | vbatt | 0440 | = fg_read_volt | n/a  |
| 2 | pack_vbat | 0440 | = fg_read_packvolt | n/a  |
| 3 | co_status | 0440 | = fg_read_one_co | n/a  |
| 4 | ibatt | 0440 | = fg_read_current | n/a  |
| 5 | rsoc | 0440 | = fg_read_rsoc | n/a  |
| 6 | temp | 0440 | = fg_read_temperature | n/a  |
| 7 | original_temp | 0440 | = fg_read_original_temperature | n/a  |
| 8 | cyclecount | 0664 | = fg_read_cyclecount | = __mca_log_info, fld:str0x3e8, imm:2613, log |
| 9 | qmax | 0440 | = fg_read_qmax | =  |
| 10 | rm | 0664 | = fg_read_rm | = fld:str0x3f4 |
| 11 | charger_full | 0664 | = fg_read_fcc | = fld:str0x3f8 |
| 12 | soh | 0664 | = fg_read_soh | = __mca_log_info, fld:str0x42c, imm:2620, log |
| 13 | fcc_soh | 0440 | = fg_read_fcc_soh | =  |
| 14 | fast_charge | 0440 | = fld:ldr0x3ac | =  |
| 15 | resistance_id | 0440 | =  | =  |
| 16 | tsim | 0440 | = fg_read_tsim | =  |
| 17 | tambinet | 0440 | = fg_read_tambient | =  |
| 18 | tremq | 0440 | = fg_read_tremq | =  |
| 19 | tfullchgq | 0440 | = fg_read_tfullchgq | =  |
| 20 | - | - | = fg_read_avercurrent | =  |
| 21 | seal | 0664 | = fg_get_seal | = fg_set_seal |
| 22 | df_check | 0440 | = fg_read_df_check, fmt %x | =  |
| 23 | chem_df_sign | 0440 | = fg_read_chem_df_sign, fmt %x | =  |
| 24 | if_sig | 0440 | = fg_get_app_checksum, fmt %x | =  |
| 25 | vendor | 0440 | = __mca_log_info, fld:ldr0x3e0, imm:8122, fmt %d , log | =  |
| 26 | pack_vendor | 0440 | = fld:ldr0x3e4 | =  |
| 27 | cell_vendor | 0440 | = fld:ldr0x3d8, fmt %s | =  |
| 28 | eeprom_version | 0440 | = fld:ldr0x18f, fmt %c | =  |
| 29 | design_capacity | 0440 | = fg_read_dc | =  |
| 30 | calc_rvalue | 0440 | = fg_get_calc_rvalue, fmt %lu | =  |
| 31 | aged_flag | 0440 | = fld:ldr0x400 | =  |
| 32 | ui_soh | 0664 | = (tool artifact: ours calls the const clone with 123 / 11) fg_mac_read_block, imm:11, imm:123, fmt %u %u %u %u %u %u %u %u %u %u %u | = (only the string-pool reference form differs) __fg_mac_read_block, __kmalloc, __mca_log_err, __mca_log_info, fg_mac_write_block, kfree, kstrtouint, mutex_trylock, mutex_unlock, strnlen, strscpy, strsep, usleep_range_state, cmp:1, cmp:11, fld:ldr0x484, fld:str0x404, imm:123, imm:2, imm:32, imm:3520, imm:40, imm:50, imm:7783, imm:7786, imm:7790, imm:7809, obj:.rodata+a20, log |
| 33 | batt_sn | 0440 | = fg_get_batt_sn | =  |
| 34 | manufacturing_date | 0440 | = fg_read_manufacturing_date, fmt %s | =  |
| 35 | first_usage_date | 0444 (stock 0664) | = fg_read_first_usage_date, fmt %s | ABSENT in ours: stock calls fg_write_first_usage_date (MAC 0x7B write) |
| 36 | fake_first_usage_date | 0664 | = fld:ldr0x213 | = fld:str0x213 |
| 37 | soh_new | 0440 | = fld:ldr0x404 | =  |
| 38 | dod_count | 0664 | = fg_get_dod_count | =  |
| 39 | count_level1 | 0664 | = fld:ldr0x408, fld:ldr0x418 | = fld:str0x418 |
| 40 | count_level2 | 0664 | = fld:ldr0x40c, fld:ldr0x41c | = fld:str0x41c |
| 41 | count_level3 | 0664 | = fld:ldr0x410, fld:ldr0x420 | = fld:str0x420 |
| 42 | count_lt | 0664 | = fld:ldr0x414, fld:ldr0x424 | = fld:str0x414 |
| 43 | cutoff_vol | 0440 | = fg_get_cutoff_voltage | =  |
| 44 | device_name | 0664 | = fg_get_device_name, fmt %s | = strnlen, strscpy, cmp:1, cmp:9 |
| 45 | max_temp_occur_time | 0440 | = fg_read_max_temp_occur_time | =  |
| 46 | run_time | 0440 | = fg_read_run_time | =  |
| 47 | max_temp_time | 0440 | = fg_read_max_temp_time | =  |
| 48 | total_fw_runtime | 0440 | = fg_read_total_fw_runtime | =  |
| 49 | temp_max | 0440 | = fg_get_one_temp_max | =  |
| 50 | time_ht | 0440 | = fg_read_time_ht | =  |
| 51 | time_ot | 0440 | = fg_read_time_ot | =  |
| 52 | peak_flag | 0440 | = fg_get_over_peak_flag | =  |
| 53 | isc | 0664 | = fg_get_isc_alert_level | = __mca_log_info, fld:str0x3f0, imm:1104, log |
| 54 | soa | 0440 | = fg_get_soa_alert_level | =  |
| 55 | current_deviation | 0440 | = fg_get_current_dev | =  |
| 56 | power_deviation | 0440 | = fg_get_power_dev | =  |
| 57 | average_current | 0440 | = fg_get_average_current | =  |
| 58 | average_temperature | 0440 | = fg_get_average_temp | =  |
| 59 | start_learning | 0664 | = fg_get_start_learning | = __mca_log_info, fg_write_byte, cmp:255, fld:ldr0x16f, imm:1308, imm:1315, log |
| 60 | stop_learning | 0664 | = fg_get_stop_learning | = __mca_log_info, fg_write_byte, cmp:255, fld:ldr0x170, imm:1343, imm:1350, log |
| 61 | action_power | 0440 | = fg_get_actual_power | =  |
| 62 | learning_power | 0664 | = fg_get_learning_power | = __mca_log_info, fg_write_word, cmp:255, fld:ldr0x171, imm:1414, imm:1421, imm:32767, log |
| 63 | learning_power_dev | 0440 | = fg_get_learning_power_dev | =  |
| 64 | learning_time_dev | 0440 | = fg_get_learning_time_dev | =  |
| 65 | constant_power | 0664 | = fg_get_constant_power | = __mca_log_info, fg_write_word, cmp:255, fld:ldr0x175, imm:1508, imm:1515, imm:32767, log |
| 66 | remaining_time | 0440 | = fg_get_remaining_time | =  |
| 67 | referance_power | 0664 | = fg_get_referance_power | = __mca_log_info, fg_write_word, cmp:255, fld:ldr0x177, imm:2372, imm:2379, imm:32767, log |
| 68 | nvt_referance_power | 0440 | = fg_get_nvt_referance_power | =  |
| 69 | referance_current | 0440 | = fg_get_referance_current | =  |
| 70 | start_learning_b | 0664 | = fg_get_start_learning_b | = __mca_log_info, fg_write_byte, cmp:255, fld:ldr0x17a, imm:2389, imm:2396, log |
| 71 | stop_learning_b | 0664 | = fg_get_stop_learning_b | = __mca_log_info, fg_write_byte, cmp:255, fld:ldr0x17b, imm:2406, imm:2413, log |
| 72 | learning_power_b | 0664 | = fg_get_learning_power_b | = __mca_log_info, fg_write_word, cmp:255, fld:ldr0x17c, imm:2431, imm:2438, imm:32767, log |
| 73 | action_power_b | 0440 | = fg_get_actual_power_b | =  |
| 74 | learning_power_dev_b | 0440 | = fg_get_learning_power_dev_b | =  |
| 75 | rel_soh | 0440 | = fg_get_rel_soh | =  |
| 76 | rel_soh_cyclecount | 0440 | = fg_get_rel_soh_cyclecount | =  |
| 77 | eis_soh | 0440 | = fg_get_eis_soh | =  |
| 78 | eis_soh_cyclecount | 0440 | = fg_get_eis_soh_cyclecount | =  |
| 79 | qmax_cyclecount | 0440 | = fg_get_qmax_cyclecount | =  |
| 80 | batt_use_environment | 0440 | = fg_get_batt_use_environment, fmt %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %u | =  |
| 81 | max_life_vol | 0440 | = fg_get_max_life_vol | =  |
| 82 | min_life_vol | 0440 | = fg_get_min_life_vol | =  |
| 83 | max_life_temp | 0440 | = fg_get_max_life_temp | =  |
| 84 | min_life_temp | 0440 | = fg_get_min_life_temp | =  |
| 85 | over_vol_duration | 0440 | = fg_get_over_vol_duration | =  |
| 86 | fc | 0440 | = fg_read_status, fld:ldr0x190 | =  |
| 87 | csd_flag | 0440 | = fg_get_csd_flag | =  |
| 88 | csd_r1 | 0440 | = fg_get_csd_r1 | =  |
| 89 | csd_r2 | 0440 | = fg_get_csd_r2 | =  |
| 90 | dcr_slope1 | 0440 | = fg_get_dcr_slope1 | =  |
| 91 | dcr_slope2 | 0440 | = fg_get_dcr_slope2 | =  |
| 92 | dcr_slope3 | 0440 | = fg_get_dcr_slope3 | =  |
| 93 | charge_absoc | 0440 | = fg_get_charge_absoc | =  |
| 94 | max_dsg_current | 0444 | = fg_get_max_dsg_current, fld:ldr0x430, fmt %hd | =  |
| 95 | min_temp | 0444 | = fg_get_min_temp, fld:ldr0x434 | =  |
| 96 | min_voltage | 0444 | = fg_get_min_voltage, fld:ldr0x438 | =  |
| 97 | cuv_count | 0444 | = fg_get_cuv_count, fld:ldr0x43c | =  |
| 98 | cuv_last_cycle | 0444 | = fg_get_cuv_last_cycle, fld:ldr0x440 | =  |
| 99 | hcuv_count | 0444 | = fg_get_hcuv_count, fld:ldr0x444 | =  |
| 100 | hcuv_last_cycle | 0444 | = fg_get_hcuv_last_cycle, fld:ldr0x448 | =  |
| 101 | ocd_count | 0444 | = fg_get_ocd_count, fld:ldr0x44c | =  |
| 102 | ocd_last_cycle | 0444 | = fg_get_ocd_last_cycle, fld:ldr0x450 | =  |
| 103 | hocd_count | 0444 | = fg_get_hocd_count, fld:ldr0x454 | =  |
| 104 | hocd_last_cycle | 0444 | = fg_get_hocd_last_cycle, fld:ldr0x458 | =  |
| 105 | hscd_count | 0444 | = fg_get_hscd_count, fld:ldr0x45c | =  |
| 106 | hscd_last_cycle | 0444 | = fg_get_hscd_last_cycle, fld:ldr0x460 | =  |
| 107 | co_auto_open | 0444 | = fg_read_co_auto_open | =  |
| 108 | v_siloss | 0664 | = fg_get_vsiloss | = fld:str0x69c |
| 109 | i_aver | 0664 | = fg_get_iaver | = fld:str0x6a0 |
| 110 | - | - | =  | =  |
| 111 | fast_mode_times | 0664 | = fg_get_fast_mode_times | = __mca_log_err, fg_mac_write_block, imm:2, imm:229, imm:5932, log |
| 112 | support_batt_manager_4p0 | 0444 | = cmp:4, fld:ldr0x199, fld:ldr0x3e0 | =  |
| 113 | fc_hours_real | 0440 | = fg_get_fc_hours_real, fmt %u | =  |
| 114 | non_fc_hours_real | 0440 | = fg_get_non_fc_hours_real, fmt %u | =  |
| 115 | dcr | 0440 | = fg_get_dcr, fmt %u | =  |
| 116 | dcr_inc_ratio | 0440 | = fg_get_dcr_inc_ratio | =  |
| 117 | dcr_refresh_time | 0440 | = fg_get_dcr_refresh_time, fmt %u | =  |
| 118 | fc_damage | 0440 | = fg_get_fc_damage | =  |
| 119 | alarm_gas_final | 0440 | = fg_get_alarm_gas_final, fmt %u | =  |
| 120 | dcr_slope_ge | 0440 | = fg_get_dcr_slope_ge | =  |
| 121 | ucl_ge | 0440 | = fg_get_ucl_ge | =  |
| 122 | dcr_ratio | 0440 | = fg_get_dcr_ratio | =  |
| 123 | upgrade_fw(ABSENT) | - | DIFF obj:spec_result+4c | DIFF __mca_log_err, check_fg_cloud_string, memset, queue_delayed_work_on, sscanf, strlen, strncpy, strsep, cmp:1, cmp:10, cmp:12, cmp:128, cmp:2, cmp:3, cmp:4, cmp:5, cmp:511, cmp:6, cmp:7, cmp:8, cmp:9, imm:1, imm:1024, imm:15, imm:15000, imm:2, imm:3, imm:32, imm:4, imm:4691, imm:4711, imm:4719, imm:4725, imm:4736, imm:4754, imm:4759, imm:4769, imm:5, imm:511, imm:6, imm:7, imm:8, imm:9, obj:.rodata+3e68, obj:spec_result, obj:spec_result+24, obj:spec_result+2c, obj:spec_result+30, obj:spec_result+34, obj:spec_result+38, obj:spec_result+3c, obj:spec_result+4, obj:spec_result+40, obj:spec_result+44, obj:spec_result+48, obj:spec_result+4c, fmt %d, log, fmt parse_clound_data |

## 6. Что осталось / риски
- Нет проверки на реальном gauge; pv / vendor / версия data flash у dada неизвестны. Сигнатуры case = эвристика (множества событий без порядка и без арифметики), поэтому для show / store равенство "по событиям", а не побитовое.
- fg_term_voltage_update_work (достижимо в стоке) не портирован. Write paths fg_set_seal / fg_mac_write_block / __fg_write_block не менялись.
- Косметика (`__func__` / порядок логов probe P6, fg_read_packvolt) не правилась: на поведение не влияет.
