# Ревью последних исправлений (G23a, G23d, G24a, G25b, G25c, G26a, G27a, G27b) против стока

База: GLOBAL OS4.0.0.7, сток = `<work-dir>` (md5 стоковых копий в g23a/g24a/g25b/g25c/g26a/g27a/g27b/g23d совпадают с mods: проверено).
Только чтение: ничего не собиралось, не загружалось, не прошивалось, исходники не менялись, git не трогался.
Инструменты (сервер, `<work-dir>`): `fn.sh` (одна функция из полного `llvm-objdump -dr`), `nz.sh` (нормализация листинга),
`rs.py` (строка по section+off / по релоку), `pk.py` (формат-строка каждого printk-вызова), `jt.py` (декодер jump-table), `fb.py`
(побайтное сравнение функции с маской релоцируемых слов + сравнение релоков), `strs.py` (все строки .rodata*). Кэш полных дампов удалён.

Проверенные .ko (md5 = из RESUME): mi_game `g27a/built` 3e8fed6a; xr_qs `g23a/built` 1d7f5cbd; bq27z561 `g24a/built` 29e01a58;
mitee_dlkm `g26a/built/g26a.ko` a453bb53; peach_v2 `g23d/built`; mca_pwm_fan `g27b/new_mca_pwm_fan.ko` 04122ac4;
mi_low_lat_mem `g27b/new_mi_low_lat_mem.ko` d45cb60f; fg_comp `g25c/built` 7e91023f; smart_charge/jeita `g25b/built`.
md5 локальных исходников совпадают с записанными в RESUME (migt_b..g, bq27z561.c, mitee_memlog.c/tee_core.c/tui_shm.c).

## (A) mi_game (G23a + G27a)

| Пункт | Вердикт | Как проверено |
|---|---|---|
| update_flt_load: 4 чтения GFI+0x50 (1x64 для scale, 3x32: printk, trace_printk, util) | MATCHES | VERIFIED: листинги прочитаны целиком; сток `ldr x11,[.bss+0xbd08]`, `ldr w8,[x13]` (f13c, f1c0), `ldr w8,[x4,#0x28]` (f258); наши 0x58/0x14c/0x1a8/0x1d8 то же. Порядок printk -> проверка 0x220 -> trace, max_load, quot/rem, min(capacity,util), запись cap_states[c].cpu_util, GFI+0x58 в tracepoint: одинаково |
| update_flt_load: printk уровень 3 | MATCHES | VERIFIED: `rs.py` по релокам: оба вызова дают `\x013flt: cpu %i ...\t` и `\x013flt: cluter ...\t`, trace-формат тот же |
| reset_render_info / update_render_info: `if (idx>=2) __builtin_trap()` | MATCHES по эффекту | VERIFIED: предикат тот же (`cmp w,#2; b.hs`, то же место: до пролога / после пролога); сток `brk #0x5512` -> `ubsan_handler` -> `die()`, наш `brk #0x1` -> хука нет -> `brk_handler` "Unexpected kernel BRK exception at EL1" -> `arm64_notify_die` -> `die()` (прочитано в `<build-dir>`, `mm/fault.c`). Оба = oops (panic при panic_on_oops); отличается только текст oops |
| ... достижимость | НЕДОСТИЖИМО | VERIFIED: в стоке и у нас только `CALL26` с константами 0/1 (сток 4+2 вызова), функции не экспортированы, других ссылок кроме .eh_frame нет |
| reset_render_info остальное | MATCHES | VERIFIED: нормализованный diff = только порядок двух store внутри spinlock между dmb |
| update_render_info остальное | DIFFERS (LOW, старое, не правка G27a) | VERIFIED: сток после strnlen делает `cmp x0,#0x10; b.eq fortify_panic("__fortify_strlen")` = в стоке `strlen(p->comm)`, у нас `strnlen(p->comm,16)` (migt_b.h:416). Сток паникует, если comm без NUL в 16 байтах; INFERRED недостижимо (comm всегда терминирован, set_task_comm = strscpy_pad). Фикс по желанию: `strncpy(ri->name, p->comm, strlen(p->comm))` |
| clean_task_window `if (row>=10) __builtin_trap()` | MATCHES по эффекту | VERIFIED: сток `ldar; cmp #9; b.hi brk #0x5512`, наш `cmp #0xa; b.hs brk #1` = тот же предикат. Недостижимо: у функции НЕТ ни одного вызова ни в стоке, ни у нас (только .eh_frame). (Справка: migt_ioctl и в стоке пишет индекс как `ldaddal` +1, потом `str %10`, т.е. транзиентное 10 видно; при наличии вызова оба упали бы одинаково.) Остальное: CODEGEN (дублирование хвостов, лишняя проверка `x25>0x7cf` на тот же brk, недостижима) |
| set_migt_freq: одна запись `en` | MATCHES | VERIFIED: сток `strb` в `.bss+0xc4` (= migt_enabled по nm) один раз, -EINVAL-пути запись пропускают, у нас то же. Остальной diff: регистры + сток `cmp w22,#1; b.lt` перед циклом (while vs do-while; при нечётном nsep>=1 эквивалентно) |
| три голых printk | MATCHES | VERIFIED: `pk.py` по migt_ioctl: тот же мультисет форматов (`--------ceiling_user list--------\n`, `\nadd uid %d to ceiling users\n`, 10x `%d `) без level-префикса; `strs.py`: множества строк .rodata равны (отличия только в бинарных данных jump-table) |
| migt_prefer_cpu: текст trace_printk + READ_ONCE | MATCHES | VERIFIED: формат по `__trace_printk_fmt` тот же; в trace-блоке оба перечитывают top_task_util[0/1] через `ldar`. Остальное CODEGEN: у нас `p==NULL` проверен раньше второго вызова migt_enable (результат -1 в обоих) |

## (B) xr_qs qievent_callback (G23a)

MATCHES. VERIFIED по листингу и декодированной jump-table (`jt.py`):
- голова одинакова: `!e` -> -22; in_interrupt -> 0; `cmp x1,#7; b.eq` пропускает только PF_EXITING (`ldrb [to,#0x44]; tbnz #2` -> -1); `plat_game_vip_check` вызывается для ВСЕХ action, бит0 -> return 0.
- `sub x8,x1,#1; cmp x8,#6; b.hi -> return 0`: action 0 и >=8 -> 0 (после тех же двух проверок) в обоих.
- таблица (сток / наш): 1 qos_set (g_qs_enable, `(qos&0xe)>=6`), 2 g_qs_enable + `(qos&0xe)<=5` -> restore(e,2) иначе qos_reset, 3 и 5 -> restore_qos_sched(e, action), 4 qos_inherit (inline), 6 correct (inline), 7 g_qs_enable + to!=NULL + spinlock + clear_draw_rtg. Совпадает 1:1.
- блок TASK_EXIT (inline clear_draw_rtg) прочитан: 4 группы, slot0/slot1, сброс count/flag только если второй слот пуст: одинаково (у нас хвосты продублированы = CODEGEN). Все пути возвращают 0, кроме -22/-1.

## (C) bq27z561 (G24a)

MATCHES. VERIFIED: нормализованный diff всей `fg_update_data_work` (сток .text+0x18260, инлайн record-voltage) против нашей: различия ТОЛЬКО в раскладке циклов `mutex_trylock`/`usleep_range(40,50)`/проверка ota_updating (ротация цикла, та же логика); все смещения полей, константы, MAC-команды, номера строк, вызовы и их порядок совпадают.
Цепочка записи data flash (обе стороны, VERIFIED по листингу стока):
1. `[bq+0x428]` (update_fw_status) != 0 -> выход (после gauge-mode; при ошибке gauge-mode лог 595 и та же проверка);
2. `mca_get_hwid_info()`: `hwid && [hwid]==3 && [hwid+4]!=0` -> `strb wzr,[bq+0x198]` (навсегда);
3. `[0x198]==0 || [bq+0x3e0]!=3` -> лог 5478, state 3, выход;
4. MAC 0x4B 6 B (ret игнорируется в обоих; при ota буфер 0) `t_buf[3] >= [0x19b]` -> лог 5486, state 2;
5. `fg_set_seal(1)`, MAC 0x54 32 B: ошибка/ota -> лог 5494, state 1, без re-seal; `(b1&3)` в {1,2} иначе так же;
6. до 3 попыток 0x448A (чтение, патч байтов DT с теми же UBSAN-ловушками `brk #0x5512`/`brk #1` при start+i>=36, запись, 100 ms, read-back); успех на 3-й попытке = ошибка (`cmp w21,#2; b.ls`);
7. до 3 попыток 0x440C (byte3 = version); state = (cnt<=2) ? 2 : 1; затем `fg_set_seal(0)`.
Других вызовов `fg_update_record_voltage_level` нет (только из fg_update_data_work при INIT).
- `len>32` в `fg_mac_write_block`: в .ko проверки НЕТ (VERIFIED: компилятор выкинул её, все вызовы с константами 2/4/32); копирующий цикл с той же ловушкой `cmp #0x26` что и сток; остальная функция = сток (ota -> 0 без записи, secure_delay 100 ms для cmd>0x4000/0x7b/0x1054, checksum, длина). Значит ни один путь не пишет больше стока; у нас ещё и 20 стоковых функций записи отсутствуют.
- lifetime: 2 попытки MAC 0x60 и 0x62, лог 3195/3217 retry 1/2, после 2-й ошибки переход к gauge-mode = сток (VERIFIED тем же diff).
- fg_set_one_co (fg_set_co): -1 после второй ошибки: MATCHES (VERIFIED: diff = только планирование инструкций, все `mov w20,#-1/wzr` одинаковы).

## (D) mitee_dlkm (G26a)

MATCHES. VERIFIED:
- mitee_memlog_read, обе ветки: стоковый блок `subs w8=put-get; b.hs ok; mvn w9=~get; cmp w8,w9; b.hs ok; get=put; -EFAULT` мёртв алгебраически (при put<get: w8 = w9 + put + 1 без переполнения => всегда `hs`), у нас блока нет. Остальное (overflow -> printk + get=alloc-sz, empty-проверка по сохранённому s->get, min(int), kzalloc 0xdc0, ловушка line_buffer >255, fortify memcpy, copy_to_user/-EFAULT, запись s->get / s->b_get, g_read_b_buf=1 только в boot-ветке) одинаково. Мелочь CODEGEN: в boot-ветке сток перечитывает log->sz в каждой итерации, мы один раз (общая память TEE, INFERRED неизменна).
- add_log_memory_to_dump_region inline: md_entry байт-в-байт тот же ("MITEELOG", id 0, va/pa/size), у нас 2 лишних нулевых store в уже обнулённые байты; аргументы printk = `add_log_memory_to_dump_region`, `MITEELOG`, формат тот же; нормализованный diff probe = только эти 2 store.
- mitee_tee_init и tui_shm_free: нормализованный diff пустой; "mitee tee init" первым printk.

## (E) Wi-Fi peach_v2 (G23d)

- dp_rx_process_be: MATCHES. VERIFIED `fb.py`: 6304 = 6304 байт, 0 различающихся нерелоцируемых слов, 177 = 177 релоков на тех же смещениях, все именованные символы совпадают (75 отличий только в именах секций .rodata/.bss/.text). Байтовое равенство => все RX-пути, включая NULL/ошибки, как в стоке. dp_rx_cksum_offload вызывается в be/li/rh, в peach_v2 есть только dp_rx_process_be (nm) => другие функции правкой не затронуты.
- policy_mgr_allow_4th_new_freq: MATCHES. Исходное выражение (g23d.patch) `sta == 1 || (sta == 2 && p2p == 2) || p2p == 3`, новое `sta == 1 || p2p == 3 || (p2p == 2 && sta == 2)`: sta/p2p локальные счётчики, сравнения без побочных эффектов => равенство для всех значений (перебор -300..300 x -300..300: True). В .ko: 3968 = 3968, 3 слова: сток и наш считают одинаковый `w8=(sta==2&&p2p==2)` (ccmp/cset), true при sta==1 / w8 / p2p==3, иначе тот же переход +0x25c (VERIFIED по листингу).

## (F) быстрые проверки

- mca_pwm_fan set_pwm: возвраты MATCHES (VERIFIED: same-duty 0; !fan_support -22 до/после lock; enable-ветка = результат pwm_reg_config; disable-ветка 0; else-ветка = результат pwm_reg_config; ошибки = ret). DIFFERS LOW (старое, не правка G27b): в стоке `ctx->pwm_duty = duty` сохраняется ДО лога строки 222 и логируется u32-значение поля (`mov w3,w22`), у нас лог `duty` (64 бит) и запись после лога. Эффект только при duty >= 2^32 (sysfs обрезает 0..100) и гонка лога; фикс: `ctx->pwm_duty = duty; mca_log_err(..., (unsigned long)ctx->pwm_duty);`.
- mi_low_lat_mem: MATCHES все три (VERIFIED): `strh wzr,[mci]` в обоих; `str x0, try_to_free_pages_symbol` после `cbz x0` в обоих; do_compact_mem_collect: t9 (mci+8) кэширован, порог mci+4 перечитан в финальной проверке, scene_code>0 -> только *bypass=1, в обоих.
- fg_comp li_plating (G25c): MATCHES. strategy_li_plating_work: diff = перенос одного `stp xzr,xzr`; isc_do_point_sample_2: те же суммы полей (+0/+8 со страйдом 0x20) идут в те же аргументы лога (w6/w4), `ldrsw` vs `ldr` поля 0xf58 используется только как 32-бит: CODEGEN; ловушки/сравнения границ по количеству равны.
- smart_charge update_smart_bypass_temp_section: MATCHES (VERIFIED `fb.py`: 248 = 248 байт, 0 нерелоцируемых отличий).
- jeita get_status: MATCHES (VERIFIED: cur_jeita_index читается один раз в обоих; у нас лишний мёртвый `str w21,[sp]` из-за повторного использования адресуемой `temp`: CODEGEN, опционально отдельная локальная).

## Итог
Все запрошенные правки поведенчески равны стоку. Новых находок две, обе LOW и старые (не внесены этими правками):
update_render_info strnlen vs strlen (fortify-паника стока при comm без NUL, недостижимо) и порядок/значение лога в __set_pwm.
Не проверялось: поведение на железе, остальные отличающиеся функции модулей, другие пути записи в bq27z561 (cutoff/feature/ui_soh).
