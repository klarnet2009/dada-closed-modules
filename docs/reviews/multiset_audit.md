# Аудит поведенческой достаточности по мультимножеству инструкций (goal-G10, 18.10.2026)

Второй, независимый слой после `compare_ko_v2`. Исходники и пакет не менялись, телефон не использовался, на сервере работал только `<work-dir>`. Таблица всех функций: `work/a17/review/multiset_audit.csv` (module, function, class, evidence, group, severity, note; 2325 строк, из них 100 строк MODULE с итогом по модулю (69 пакетных и 31 PARTIAL)).

## 1. ПАКЕТ `<work-dir>` (source = ours / public-built): расхождений нет

Проверен 69 уникальных `.ko` (99 файлов манифеста, дубли vendor_ramdisk/vendor_dlkm слиты по sha256), 1758 функций, сравнение с OS4-стоком из `<work-dir>`.

- 1756 функций побайтно идентичны стоку по тексту инструкций (регистры не маскировались, переходы символьно) и по релокациям.
- 2 функции отличаются только раскладкой: `millet_oem_cgroup` (`millet_can_attach`, `freezer_cancel_attach`): слово-замена `ldapr` (inline alternatives для `ldar`) лежит в другом месте .text. Мультимножество равно.
- 2 функции лежат в другой секции: `perf_actuator`: `xr_fbt_dev_init` и `xr_fbt_dev_exit` в `.text` вместо `.init.text`/`.exit.text` (память init не освобождается, поведение то же). Чинить не обязательно, можно при следующей пересборке пометить `__init`/`__exit`.
- Ни одного нового `brk`/`udf`/`ud2`, ни одного расхождения слова kCFI перед функцией (сравнивается слово перед каждой функцией, а не только global/address-taken как в `preamble_cmp.py`), ни одного расхождения набора хэшей, проверяемых перед косвенными вызовами.
- Размеры секций: расхождения только в `.modinfo`, `.rodata`/`.data`/`.bss` на единицы-десятки байт (выравнивание, слияние строк, `.data.once`) и 3 раза в `.init.eh_frame` (`debug_ext`, `fs19xx_dlkm`, `sia91xx_tuning_dlkm`): 28/128/10 FDE совпадают, отличается только число CIE (1 вместо 4/6/2). `__bug_table`, `.altinstructions`, `__jump_table`, `__ex_table`, `.text` (кроме `perf_actuator`) по размеру равны.

**Список «must go back to stock»: ПУСТ.** Ни один модуль пакета из наших/публичных исходников не имеет реального отличия.

## 2. Метод (что именно считалось и чего оно не видит)

Скрипты: `work/a17/review/g10/audit2.py` (основной), `ctx.py`, `seq.py`, `trapreach.py`, `kattr.py`, `build_report.py`; сервер `<work-dir>`. Для каждой функции:
1. `identical`: тот же текст инструкций и релокации.
2. Иначе мультимножество инструкций с маскированием регистров. Нормализация: выброшены обращения к стеку через sp, кадр, `mov R,R`, безусловный `b`, `ret`, `paciasp/autiasp`, `ldapr`; `ldp/stp` разложены в два `ldr/str`; `ldur`=`ldr`; `cmp #N`+`hi/ls/gt/le` приведены к `#N+1`+`hs/lo/ge/lt`; полярность условий (cbz/cbnz, b.eq/b.ne ...) не учитывается; локальные `bl` без релокации считаются как CALL.
3. Вторая попытка с «выпрямлением инлайна»: вызов функции того же модуля заменяется её мультимножеством.
Классы: `LAYOUT` (мультимножество равно), `INLINE` (равно после выпрямления), `SELECT` (отличаются только ALU/ветвления), `CONST` (отличается ещё и immediate: цикловые счётчики, сравнения), `REAL` (отличаются обращения к памяти не через sp, вызовы, барьеры, атомики, трапы), `STUB`/`MISSING`/`ADDED`, `TRAP` (лишние brk/udf, показано, какие именно добавлены), `KCFI` (слово перед функцией), `KCFICALL` (набор хэшей, проверяемых перед косвенными вызовами), `SECTION`.
Не видит: инверсию полярности ветки с зеркальной раскладкой, неэквивалентность при совпадающем мультимножестве (перестановка операций), значения данных в `.data/.rodata` (это зона `compare_ko_v2`). Класс `REAL` не доказывает ошибку: многие записи объясняются слиянием хвостов (tail merge), раскруткой цикла, перетасовкой `ldr/str`.
Собранные `.ko` для не-пакетных модулей: самый свежий `<work-dir>` по времени (g1h dump_binder/miscore/rss_monitor, g1g fault_report, g1o mi_reclaim/sched_entity_freq, g1l xr_qi/xr_qs, g1ab mi_game/mi_low_lat_mem, g1ad matt, g3i synaptics_tcm2, g3n aw882xx/sia91xx/tfa98xx, g4h binder_gki/rsmc_driver, g4i mibbr/ufs_ffu, g4j cameralog/cameramsger/dcvs_main/lb/mi_damon/mist/process_monitor, g4p qca_cld3_*). Это состояние на момент аудита: часть работает другими агентами.

## 3. PARTIAL-модули (сток в пакете, наши сборки только проверены)

| модуль | функций | идентично | вердикт |
|---|---|---|---|
| binder_gki | 10 | 10 | чисто, ни трапов, ни расхождений kCFI |
| synaptics_tcm2 | 233 | 233 | чисто (функциональный уровень, нет расхождений kCFI/трапов): кандидат на перенос в пакет после полного compare_ko_v2 |
| cameralog | 11 | 10 | чисто (`cameralog_ioctl` только раскладка) |
| mist | 70 | 67 | чисто (2 LAYOUT, 1 SELECT `parse_cluster_node_map`) |
| rsmc_driver | 93 | 89 | чисто (4 LAYOUT); 2 слова kCFI есть только у нашей сборки (`rsmc_tcxo_power_free`, `smc_free_gpio`), безвредно |
| rss_monitor | 32 | 31 | чисто (`init_module` LAYOUT) |
| ufs_ffu | 8 | 5 | низкая: `start_ffu` CONST (7 инструкций, сравнение/csel), 2 LAYOUT |
| mibbr | 28 | 26 | низкая: `mibbr_main` -1 `ldr [x,#-8]` (кадр), `update_bbr_uid_list` LAYOUT |
| process_monitor | 42 | 38 | низкая: `detect_list_write` (mov #512), `init_D_task` (mov #-12) мелочь; `timeout_check` другой хэш kCFI (global, указателей в модуле нет) |
| fault_report | 8 | 7 | низкая: `get_stack_info` (cmp #17 против #16 плюс tbz): остаток 8, границу цикла перепроверить |
| dump_binder | 11 | 10 | низкая: `extend_android_vh_binder_proc_transaction_finish` на один `_raw_spin_unlock` меньше: по порядку вызовов это общий эпилог (STATUS.md описывает), CFG не доказан |
| sched_entity_freq | 36 | 33 | низкая, ПРОВЕРЕНО РУКАМИ: `cluster_freq_work_handler` ветка прыгает на общий `spin_unlock_irqrestore` (слияние хвостов, утечки лока нет); `init_module` `cmp #8` против `#0x400` это 8x128-байтный шаг индекса; `sched_tick_hook` перенос `strb/ldr` |
| dcvs_main | 18 | 15 | средняя: `mi_recalculate_freq_mhz` (EXPORT_SYMBOL, `__ksymtab`) хэш kCFI в нашей сборке другой: при косвенном вызове снаружи ловушка; `mi_calculate_mon_sampling_freq` CONST (`cmp lsl #1`) |
| cameramsger | 22 | 14 | средняя: хэш kCFI другой у `cam_sched_ioctl`, `cam_sched_w_pids`, `get_tgid_by_cmdline` (адреса лежат в `.rodata/.text`, прототип отличается от стока: ловушка при косвенном вызове); `cameramsger_read/write/release/snd_message` REAL по перетасовке `ldr/str` и стековым слотам (STATUS: «register allocation») |
| mi_damon | 188 | 187 | средняя: `kdamond_fn` остаток 11 (`mov #4096`, `cmp #2,lsl #12`); `damon_vma_need_sample` другой хэш kCFI (global) |
| mi_reclaim | 30 | 26 | средняя: `is_important_process` остаток 13 (`cmp #128` против `#32`, `mov` и `lsl #2`), `init_module` SELECT (`asr #37`), `event_store` LAYOUT |
| miscore | 27 | 22 | средняя: `miscore_handle_link_change`: проверка `strnlen>=17` в стоке ведёт на `fortify_panic`, у нас на `brk #0x1` (то же условие, тот же эффект); `miscore_input_hook4/6` `cset` против `mov #1` (безвредно) |
| lb | 57 | 38 | средняя: 7 REAL: `file_sequential_get_fileinfo` (kfree слиты/переставлены, набор тот же), `mi_lb_compiler_thread` (+1 `memset`, остаток 135), `mi_lb_dealwith_collect_cmd` (инлайн refcount), `mi_lb_ioctl` (санитайзерные вызовы), `real_timing_lookup_file_index` (нет `fortify_panic`), `init_module`, `is_real_timing_package` |
| xr_qs | 120 | 114 | средняя: `qievent_callback` (+1 вызов `restore_qos_sched`, остаток очень большой), `qos_set_callback`, `qos_reset_callback`, `restore_qos_sched`, `set_rtg_grp` REAL; зарядная семья, не загружать |
| mi_low_lat_mem | 110 | 82 | КРАШ-КАНДИДАТ: 25 REAL (`alloc_pages_slowpath_end_handler`, `cma_alloc_end_handler`, `dma_heap_buffer_alloc_end_handler`, `mm_direct_reclaim_end_handler`, `init_module` +`memset`, `mi_dfx_init` +`memset`), 6 слов kCFI другие; у `mi_smart_refill_alloc` и `mi_smart_refill_extend_alloc` адрес в .text, хэш другой |
| mi_game | 234 | 156 | КРАШ-КАНДИДАТ: см. ниже |
| matt | 261 | 167 | известно (G1ad): `matt_clean_rtx_queue` +`brk #0x5512` (то же, что нашли в sack-цикле), слово kCFI `matt_write_xmit`, остальное REAL 33; свой аудит, не загружать |
| aw882xx_dlkm | 267 | 99 | средняя: 159 REAL, 9 KCFICALL (stock проверяет хэши, которых у нас нет: девиртуализация), 3 TRAP только kCFI-проверки `brk #0x822x` (больше косвенных вызовов); работает G3n, не загружать |
| sia91xx_dlkm | 104 | 31 | КРАШ-КАНДИДАТ/функционал: `sipa_fw_load_work_delay` добавлен `brk #0x800` (BUG); `sipa_pm_suspend` и `sipa_pm_resume` у нас 2 инструкции против 14 стока (заглушка); 65 REAL |
| tfa98xx_dlkm | 389 | 202 | ФУНКЦИОНАЛ: STUB (после выпрямления инлайна меньше в разы) `tfa986x_specific` 769 против 1762, `tfa98xx_hw_params` 88 против 617, `tfa98xx_set_profile` 81 против 85689; 156 REAL, 39 KCFICALL, 28 слов kCFI; не загружать |
| xr_qi | 371 (370) | 339 | ФУНКЦИОНАЛ: STUB `android_vh_binder_proc_transaction` (1 инструкция против 228), `android_vh_binder_set_priority` (1 против 253), `android_vh_percpu_rwsem_wq_add` (1 против 475), `qos_event_handler` (2 против 345); нет функции `trace_qi_percpu_rwsem_uninherit`; слова kCFI `do_qos_inherit`, `undo_qos_inherit` другие. Состояние g1l, G1q в очереди |
| qca_cld3_peach / peach_v2 / kiwi_v2 / qca6750 / wcn7750 | 18324 / 18446 / 18271 / 15880 / 17981 | 17840 / 18249 / 18074 / 15701 / 17786 | средняя (97,4-99,0 процента функций идентичны): REAL 86-134 и CONST 80-335 на модуль (не разбирались, в основном инлайн/раскладка); `epping_stop_adapter` ложная тревога (в стоке инлайнится `netif_tx_disable`, у нас вне строки); у qca6750 и wcn7750 `wlan_hdd_state_ctrl_param_write` проверяет хэши kCFI, которых нет в стоке (проверить типы указателей); стоковые проверки `0x1581ffc7` в `wlan_hdd_cfg80211_scan/vendor_scan` у нас отсутствуют (девиртуализация) |

### mi_game (G1ab, сборка 08:51), точные функции
- `flt_irq_work`, `mi_force_update_freq`: перед косвенным вызовом проверяется хэш `0xc026e2eb`, в стоке `0x4ede13bb` (неверный тип указателя в исходнике), плюс у `flt_irq_work` один вызов `walt_sched_clock` вместо двух. Ловушка kCFI при вызове: самое тяжёлое в модуле.
- 31 слово kCFI перед функциями другое (из них с адресом в .text: `do_boost_work`, `game_choose_cpu_fastpath`, `mi_force_update_freq`, `update_cpu_frame_load`, `update_frame_load`; остальные global): список в CSV, класс KCFI.
- `clean_task_window`, `reset_render_info`, `update_frame_load`, `update_render_info`: добавлен `brk #0x1` (fortify как трап вместо `fortify_panic`, то же условие).
- `init_module` лежит в `.init.text`, в стоке в `.text` (безвредно).
- `cpu_load_show`: в стоке цикл раскручен (8 `seq_printf`), у нас цикл (проверено, безвредно).

## 4. Что можно/нужно сделать

1. Пакет: ничего возвращать в сток не нужно. `perf_actuator`: пометить `xr_fbt_dev_init/exit` как `__init/__exit` при следующей пересборке (по желанию).
2. В пакет и дальше только стоком остаются все PARTIAL-модули выше; кандидаты на перенос после полного strict-сравнения и проверки preamble: `synaptics_tcm2` (233/233), `binder_gki` (10/10), `cameralog`, `mist`, `rsmc_driver`, `rss_monitor`.
3. Исправить в исходниках (приоритет): `mi_game` (типы указателей в `flt_irq_work`/`mi_force_update_freq`, прототипы 31 функции с другим kCFI-хэшем), `cameramsger` и `dcvs_main` (прототипы `cam_sched_ioctl`, `cam_sched_w_pids`, `get_tgid_by_cmdline`, `mi_recalculate_freq_mhz` для kCFI), `xr_qi` (заглушки), `sia91xx` (`sipa_fw_load_work_delay`, suspend/resume), `matt_clean_rtx_queue` (brk 0x5512).
4. Не доверять «PARTIAL, раскладка»: после исправлений повторить `audit2.py` (1-2 минуты на модуль).

## 5. Не сделано / риски
- Для `REAL`-строк построчный разбор CFG не делался, проверены руками только перечисленные выше функции; остальные REAL помечены `low/medium` по эвристике (число остатка, наличие вызовов).
- Берётся последний собранный `.ko` каждого модуля, а не обязательно «финальный».
- qca_cld3_*: 202-489 неидентичных функций на модуль (из 16-18 тысяч) не разбирались.
