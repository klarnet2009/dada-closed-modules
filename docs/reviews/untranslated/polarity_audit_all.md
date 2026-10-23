# Аудит polarity по всем закрытым модулям (goal-G25a)

База: GLOBAL OS4.0.0.7 stock (`<work-dir>`). Только community-проверка, ничего не загружалось и не прошивалось, исходники модулей не менялись.
Инструмент: `tools5/symcmp5.py` (без правок) через `symcmp5b.py` + новые `s5digest.py`, `s5ms.py` (см. `tools5/README.md`).
Scratch (сервер): `<work-dir>` (`rep/all_<module>.txt` полный вывод, `stock/`, `ours/` копии .ko, `dd.sh`, `cs.sh`, `blk.py`, `rng.py`).
Режим: `all`, `ulimit -v 16G`, `nice -n 10`, `S5TMO=150`, по одному модулю; весь прогон (23 модуля) занял около 3 минут, cld3 (133 функции) 2 минуты.

Какие .ko сравнивались (последняя сборка модуля, новые не собирались):
mi_game `g23a/built`, xr_qs `g23a/built`, xr_qi `g20c/built`, mi_low_lat_mem `g14h/built`, mitee_dlkm `g15b/built` (**старше правок G23 в mitee, если они были**),
matt `work/a17/review/matt_g22a/matt.ko`, mi_reclaim `g22b/built`, lb/cameramsger/mibbr/minet/rsmc_driver/mca_connector_antiburn/mca_pwm_fan `g20a/built`,
miscore/mist/process_monitor/rss_monitor/mi_damon/cameralog/dump_binder/fault_report `g20a/built17` (других сборок нет), qca_cld3_peach_v2 `g15a/built` (**старше G22f/G23d**).
swinfo/plat_common: **не запущено**: своей сборки нет, в `g18a/old_kernel_cn` лежит 130-байтовая заглушка, не ELF (symcmp5 падал с traceback, теперь `symcmp5b` пишет понятную ошибку).

## 1. Таблица по модулям

"сравнено" = общие функции с отличающимся текстом (строго, цели ветвлений считаются). POL = POLARITY-DIFF, ATM = POLARITY-DIFF-ATOMS, FACT = FACT-DIFF, INC = INCONCLUSIVE/ERROR.

| модуль | общих | сравнено | CLEAN-POL | POL | ATM | FACT | INC | находки |
|---|---|---|---|---|---|---|---|---|
| mi_reclaim | 30 | 27 | 25 | 0 | 0 | 2 | 0 | нет (is_important_process, init_module: CODEGEN) |
| mi_damon | 188 | 84 | 83 | 0 | 0 | 0 | 1 (kdamond_fn) | нет; kdamond_fn не решён инструментом (G1aj читал вручную) |
| process_monitor | 42 | 26 | 25 | 1 | 0 | 0 | 0 | нет (detect_list_write: CODEGEN) |
| lb | 57 | 44 | 36 | 2 | 2 | 4 | 0 | нет по поведению; compiler_thread, init_module не решены |
| mi_game | 240 | 166 | 122 | 8 | 27 | 8 | 1 (flt_cal_freq) | B1, B2 (оба low, недостижимы); 12 больших функций не решены |
| xr_qi | 364 | 138 | 135 | 1 | 1 | 1 | 0 | нет (qi_acquire_gpu_blockeds: форма выхода из цикла) |
| xr_qs | 118 | 33 | 32 | 0 | 0 | 1 | 0 | qievent_callback: только слияние ret-листьев; D1 из g20_fixes_review остаётся |
| mi_low_lat_mem | 112 | 86 | 66 | 3 | 9 | 8 | 0 | нет по поведению (только лог-строки/алиасы dfx/mci) |
| mitee_dlkm (g15b) | 175 | 93 | 61 | 8 | 2 | 22 | 0 | нет доказанных; 6 функций error-path не решены (см. 3.3) |
| matt | 260 | 159 | 115 | 5 | 18 | 21 | 2 без вердикта | нет новых (инлайн-различия известны из G1ak) |
| cameramsger | 22 | 18 | 8 | 1 | 6 | 3 | 0 | нет (release: порядок блоков, не порядок исполнения) |
| cameralog | 11 | 1 | 0 | 1 | 0 | 0 | 0 | нет (ограничение инструмента ret(x)==0) |
| mibbr, minet, rss_monitor, dump_binder | 28/44/32/11 | 14/24/5/1 | все CLEAN | 0 | 0 | 0 | 0 | чисто |
| miscore | 27 | 17 | 14 | 0 | 3 | 0 | 0 | нет (hook4/hook6: udp_get_sock инлайн) |
| mist | 70 | 48 | 47 | 0 | 1 | 0 | 0 | нет (parse_cluster_node_map: ATOMS-шум, мультимножество вызовов равно) |
| rsmc_driver | 95 | 68 | 64 | 0 | 0 | 4 | 0 | нет (4 FACT-DIFF без разницы по типам эффектов) |
| fault_report | 8 | 1 | 0 | 0 | 1 | 0 | 0 | нет (get_stack_info: 163 ATM-атома, эффекты равны) |
| mca_connector_antiburn | 17 | 9 | 6 | 0 | 1 | 2 | 0 | нет (monitor_workfunc: эффекты равны, нужен ручной разбор по желанию) |
| mca_pwm_fan | 20 | 12 | 11 | 1 | 0 | 0 | 0 | B3 |
| swinfo | - | - | - | - | - | - | - | не запущено (нет сборки) |
| qca_cld3_peach_v2 (133 из списков G15a/G22f) | 133 | 133 | 10 | 15 | 50 | 47 | 10 + 1 MemoryError | B4, B5 (известные STRUCT cm_process_join_req, lim_fill_pe_session) |

## 2. Что считалось CODEGEN и на каком основании

Для каждой не-CLEAN функции дополнительно сняты (а) `s5digest`: эффекты, которые есть только на одной стороне, сведённые к виду (цель вызова, объект записи, atomic, trap);
(б) `s5ms`: мультимножество релокаций (вызовы/глобалы) и ключевых инструкций обеих сборок. Если и (а), и (б) пусты, а видимое различие это форма значения/адреса, порядок блоков, ldp/ldr,
ldapr/ldar (stock собран с RCpc, у нас ldar), lockdep-ключ `__mutex_init`, LTO-суффиксы `.llvm.<hash>`, то вердикт CODEGEN (это улика, не доказательство).
Прочитанные вручную (диффом нормализованного дизассемблера) как CODEGEN:
`mi_reclaim: is_important_process` (is_qos_task вызывается один раз в обеих сборках, ветка `prio<100` просто инвертирована; FACT-DIFF был из-за формы аргумента),
`mi_reclaim: init_module` (lockdep key), `process_monitor: detect_list_write` (444 "same-atoms": `cmp x23,#0x64; b.hi` против `cmp x23,#0x65; b.hs`, перестановка mov/sub),
`mi_game: game_choose_cpu_overutiled`, `game_lb_control` (стоковый `ret(0)` против нашего `ret(game_ip_task)`/`ret(migt_enable)`: на этом пути значение равно 0 из-за `cbz`, см. ограничение инструмента),
`xr_game_task_init/fork` (стоковый `str w [+0xa4]; strh [+0xa8]` против нашего `stur xzr,[+0xa2]`: покрывают тот же диапазон +0x9c..+0xa9),
`mi_game: mist_frame_load_history_update` (`current->thread_info.cpu` через ldar против `this_cpu_read(cpu_number)`; то же значение), `game_choose_cpu_fastpath` (`metis_lb_cpu_array`: те же загрузки, другое имя релокации),
`lb: file_sequential_get_fileinfo, mps_hot_pte_range` (хвостовое слияние kfree / один ldar вместо двух), `lb: real_timing_lookup_file_index` (стоковый fortify_panic; цепочка strlen/__kmalloc/memcpy/strstr/printk одинаковая),
`lb: mi_lb_compiler_thread` (у нас лишний `memset(stk,0,264)` перед `memcpy`: безопасное над-обнуление),
`cameramsger: cameramsger_release` (`__put_task_struct` лежит раньше принтов только по адресам; по исходнику put_task_struct после обоих printk), `cameralog_ioctl` (`ret(copy_to_user)` на пути с `cbz x0`: x0==0),
`xr_qi: qi_acquire_gpu_blockeds` (`i+1==a1` против `i==a1-1`, выход из цикла), `mitee: tui_shm_create_fd` (лишний mutex_unlock в нашем WARN-продолжении после `brk #0x800`; в stock это общий хвост),
`mitee: mitee_msgq_shm_register` (хвост ошибки `kmalloc(12)==NULL`: тот же код, `ghd_rm_mem_reclaim(handle)` берёт handle из слота стека `[x29-0x20]`, у stock `w21=0`; требуется только чтобы слот был обнулён: не проверено).

## 3. Находки

### 3.1 BEHAVIOUR (все low: пути недостижимы при текущих вызовах или касаются только ошибок/логов)

| № | модуль / функция | stock (листинг) | наша сборка | предложение |
|---|---|---|---|---|
| B1 | mi_game `reset_render_info`, `update_render_info` (g23a) | stock: `cmp w0,#2; b.hs <ret>` ДО prologue (reset_render_info `+0x0`, update_render_info `+0x1c`: `cmp w1,#2; b.hs`): индекс >= 2 это no-op | у нас `cmp w0,#3; b.hs`, затем в reset_render_info `bl spin_lock(&render_info[idx])`, `cmp w19,#2; b.eq` (idx==2 берёт spin_lock за пределами `render_info[2]`, затем пропуск) + bounds-`brk #1`, `fortify_panic` | в начале обеих функций `if (idx >= MI_RENDER_TYPES) return;` (в `migt_b.h:356/389`). Недостижимо: все вызовы `0/1` (migt_e.h:377,380; migt_f.h:35,36,44,156) |
| B2 | mi_game `clean_task_window` (g23a) | stock: `ldar w19,[migt_item_index]; cmp w19,#9; b.hi <skip>` (row > 9: ничего не делать) | `cmp w9,#0xb; b.hs`, row==10 доходит до цикла и ловит array-bounds `brk` (`cmp x25,#0x7cf; b.hi`) | в `migt_g.h` в начале `if (row >= 10) return;`. Недостижимо: `migt_item_index = inc_return % 10` (migt_f.h:103) |
| B3 | mca_pwm_fan `set_pwm` (g20a/built) | stock: 10 вызовов `__mca_log_err`, в том числе `pwm_fan_power_on failed` (`line 207`, ветка выключения) и возврат результата последнего `pwm_reg_config` (>=0) | в .ko только 9 вызовов (лога line 207 нет); возврат константа 0. Текущий `g20a/src/mca_pwm_fan/mca_pwm_fan.c` этот лог УЖЕ содержит (строки 288-293), то есть .ko старше исходника | пересобрать и перезапустить `symcmp5b ... set_pwm`; проверить что `pwm_reg_config` не возвращает >0 (иначе `return ret;` вместо `return 0;` на строке 224) |
| B4 | qca_cld3 `cm_process_join_req` (g15a/built) | stock вызывает `wlan_crypto_get_param`, `wlan_crypto_build_rsnie_with_pmksa`, `qdf_trace_msg` | в нашей функции этих вызовов нет (`s5ms`: S-only x3). Это известный пункт STRUCT из G22f (`dis_cm_process_join_req_*`) | не новая: перепроверить на актуальной wifi-сборке (в списке INCONCLUSIVE tool-у `wlan_crypto_build_rsnie_with_pmksa` падает с MemoryError, смотреть вручную) |
| B5 | qca_cld3 `lim_fill_pe_session` (g15a/built; tool: INCONCLUSIVE) | stock вызывает `wlan_cm_set_disable_hi_rssi` | у нас вызова нет (`s5ms`: S-only x1). Тоже из списка STRUCT G22f | проверить исходник функции (ожидается вызов `wlan_cm_set_disable_hi_rssi(psoc, vdev_id, ...)` при заполнении pe_session), перепроверить на актуальной сборке |
| (известные) | mi_game `update_flt_load` D2, xr_qs `qievent_callback` D1 | см. `g20_fixes_review.md` | | не повторяю |

### 3.2 Ограничение инструмента, давшее ложные POLARITY-DIFF [reach] ret(...)

`ret(x)` против `ret(0)` на пути, где `cbz/cmp #0` уже доказал x==0 (game_choose_cpu_overutiled, game_lb_control, cameralog_ioctl, mca_pwm_fan set_pwm частично),
и выход из цикла в другой форме (`i+1==n` против `i==n-1`: qi_acquire_gpu_blockeds). В `symcmp5b.py` не исправлено (нужна подстановка значения по равенству в пути); записано в README.

### 3.3 Не решено инструментом (по риску; нужно чтение исходника)

* mi_game (большие, структура цикла/инлайна отличается): `update_cpu_frame_load` (stock на один `get_scale_exec_time` больше, `adjust_frame_start_slowpath` вызывается в другом месте; G1aj читал `update_frame_load`/`adjust_frame_start_slowpath`, эту функцию нет),
  `uclamp_frame_load` (203 против 283 insns, stock читает `cluster_cpumask`), `update_flt_load` (194 против 138 insns, у stock 5 лишних `trace_printk(flt_trigger_source)` и 10 обходов `runqueues`: хвост задвоен у stock по G1ak, но 5 принтов не объяснены),
  `cpu_load_show` (у stock на 4 `seq_printf` больше), `migt_hardwork` (у stock `__list_add_valid_or_report`, у нас нет), `set_migt_freq`, `migt_ioctl`, `xr_set_next_freq`, `update_pkg_load`, `update_frame_load`, `game_load_history_update/reset`, `flt_cal_freq` (INCONCLUSIVE).
* mitee_dlkm (g15b): `mitee_msgq_secmem_lend`, `mitee_msgq_shm_register` (у нас +2 вызова `kfree`: хвостовое дублирование или лишний free на ошибочном пути, нужна проверка отсутствия двойного free), `mitee_msgq_shm_unregister(_supp)` (gating стековых аргументов `hyp_assign_table`),
  `optee_open_session` (у нас 8 stkarg `tee_session_calc_client_uuid`, у stock лог `fail to auth`), `mitee_recv_task_fn`, `tui_shm_alloc/free`, `mitee_memlog_read`. Сборка g15b, перепроверить на новой.
* lb: `init_module` (stock 3 `ret`-листа class_create/device_create, у нас нет: пути ошибок), `mi_lb_ioctl` (лог `LB_GET_VERSION: copy to user fail` только у stock; по поведению LOW).
* mi_damon `kdamond_fn` (INCONCLUSIVE), cld3: 10 INCONCLUSIVE (`lim_fill_pe_session`, `lim_send_assoc_req_mgmt_frame`, `scm_add_update_entry`, `scm_filter_match`, `util_scan_gen_scan_entry`, `util_scan_unpack_beacon_frame`, `wlan_cm_calculate_bss_score`, `wlan_hdd_cfg80211_vendor_scan`, `wlan_hdd_cm_connect`, `wlan_hdd_update_wiphy`);
  по мультимножеству вызовов (`s5ms`) отличается только `lim_fill_pe_session` (B5), остальные девять равны.
* cld3 остальное: 131 из 133 функций не отличаются мультимножеством вызовов (`s5ms` по всем 133 именам: разница только у `cm_process_join_req` (B4) и `lim_fill_pe_session` (B5)), значит FACT-DIFF/ATM там по форме значений; hdd_update_config (gating 10), lim_process_sme_req_messages (gating 7), hdd_cm_set_hlp_data (gating 1) не читались.
* matt (три ревью уже были): `matt_rcv_established` (gating 4, инлайн `matt_rate_gen`), `matt_inet_connect`, `matt_ping_timer_handler` (stock инлайнит `judgement_for_rst`), `matt_rcv_data` (у нас 2 лишних `matt_trace_msg`).
* mi_low_lat_mem: в `alloc_pages_slowpath_end_handler`/`dma_heap_buffer_alloc_end_handler` stock считает в объект `dfx`, у нас `mci`: если это разные объекты, счётчики дампятся не туда (стат., не поведение); `stats_history_show`, `dfx_info_reset`, `predo_store_call`, `scene_alloc_info_show_common` ATM-шум.

## 4. Выводы

* Новых BEHAVIOUR уровня buckchg `monitor_workfunc` (пропущенное условие) на не-charging модулях НЕ найдено; найдено 2 недостижимых расхождения охраны индекса (B1, B2) и одна устаревшая сборка (B3).
* Главный риск на будущее: сборки mitee (g15b) и cld3 (g15a) старше правок, повторить `symcmp5b all` на новых .ko; mi_game big-функции требуют чтения исходника, а не инструмента.
* Рабочий порядок для ревьюера: `symcmp5b all` -> `s5digest` (кандидаты с S-only/O-only эффектами) -> `s5ms` (потеряна ли вызов) -> только потом дизассемблер.
