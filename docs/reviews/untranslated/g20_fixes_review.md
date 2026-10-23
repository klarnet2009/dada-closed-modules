# Независимое ревью фиксов G20b / G20d / G20c (GLOBAL OS4.0.0.7, Xiaomi 15 dada)

Дата: 20.10.2026. Ревьюер: read-only (ничего не собиралось, не запускалось на телефоне, исходники не менялись).
Сток: `<work-dir>` (md5 совпадают с g20b/stock, g20c/stock, g20d/st).
Наши .ko: `<work-dir>` (= ours2.ko), `<work-dir>`,
`<work-dir>`.
Дизассемблер: `llvm-objdump -dr` (clang-r510928). Скретч: `<work-dir>` (8.9 MB: s/, o/ дизассемблы, fx.sh, fc.sh, nm.py).
`nm.py` = нормализованный поток инструкций (цели ветвлений как смещение от начала функции, секционные релокации без адреса);
«нормализованно идентично» = совпадают все инструкции, регистры и константы, отличаются только имена секций в релокациях.

## Итог

| Пункт | Вердикт |
|---|---|
| A1 mi_game init_module, все пути ошибок | MATCHES (VERIFIED) |
| A2 update_flt_load | MATCHES по двум фиксам (VERIFIED); остаток: окно читается один раз (DIFFERS, только при гонке, low) |
| A3 mist_frame_load_init | MATCHES (VERIFIED) |
| A4 mist_frame_load_reset | MATCHES (VERIFIED) |
| A5 game_add_task noinline | MATCHES (VERIFIED, тело совпадает инструкция в инструкцию) |
| B1 mca_cp_mode_in_range / select_cp_work_mode_by_current | MATCHES (VERIFIED) |
| B2 adjust_adapter_mode 21000/11000/5500 | MATCHES (VERIFIED) |
| B3 update_cp_fsw fsw_step=0 (в vfc_work) | MATCHES (VERIFIED, vfc_work нормализованно идентична) |
| B4 enable_buck_charging, use_buck_chg_once_flag=1 | MATCHES (VERIFIED, нормализованно идентична) |
| B5 open_path QC7-1/QC7-2 | MATCHES (VERIFIED) |
| B6 quick_wireless select_stage `if (k<0) stage=0` | MATCHES (VERIFIED, нормализованно идентична) |
| B7 basic_wireless строка bridge_state | MATCHES (VERIFIED, байты строки равны) |
| C1 xr_qs qievent_callback | **DIFFERS (medium)**: TASK_EXIT для game-VIP задач |
| C2 xr_qs set_rtg_grp (+ exchange/move_existed) | MATCHES (VERIFIED) |
| C3 xr_qi alter_rwsem_list_add, do/undo_rwsem_qos_inherit | MATCHES (VERIFIED) |

## Дефекты

### D1 (medium, VERIFIED) xr_qs qievent_callback: для QIEVENT_TASK_EXIT не вызывается plat_game_vip_check
Сток (`qievent_callback` 0x31e8..0x31fc):
```
cmp x1,#7 ; b.eq 0x31f8          ; TASK_EXIT пропускает ТОЛЬКО проверку PF_EXITING
ldrb w8,[x0,#0x44] ; tbnz w8,#2 -> return -1
0x31f8: bl plat_game_vip_check ; tbnz w0,#0 -> return 0   ; для ВСЕХ action, включая 7
```
Наш (qs.c:794-801) кладёт проверку VIP внутрь `if (action != QIEVENT_TASK_EXIT)`; в нашем .ko (0x54..0x7c) при action==7
сразу идёт переход в таблицу. Следствие: при выходе задачи, для которой plat_game_vip_check() истинна, сток событие
игнорирует (слот g_draw_grp остаётся), наш вызывает clear_draw_rtg и чистит слот. Комментарий в qs.c:793 и вывод G20c
«no proven BEHAVIOUR difference» по этой функции неверны.
Фикс (вернуть сток):
```c
if (action != QIEVENT_TASK_EXIT && (e->to->flags & PF_EXITING)) { ret = -1; goto out; }
if (plat_game_vip_check(e->to))
	goto out;
```
Проверить в .ko: `cmp x1,#7; b.eq <vip>`, `bl plat_game_vip_check` на пути action==7.
Замечание (INFERRED): поведение стока оставляет в g_draw_grp указатель на выходящую задачу; позже он разыменовывается
(set_rtg_grp: `g_draw_grp[0].render->tgid`, exchange_draw_rtg: `old_ui->exit_state`), то есть у стока здесь возможен
use-after-free. Повторяя сток, мы повторяем и этот риск; решение за владельцем, по правилу проекта (точность стоку) — фикс выше.

### D2 (low, только при гонке, VERIFIED по коду / влияние INFERRED) mi_game update_flt_load: окно читается один раз
Сток читает GFI+0x50 трижды: 64 бита на входе для scale (f008), 32 бита заново для каждого printk в цикле CPU (f13c, f1c0)
и заново перед `util = max_load / window` (f258 `ldr w8,[x4,#0x28]`). Наш (flt.c:1196, 1211, 1214, 1216) берёт `window`
один раз на входе (x24 = (u32)window). Разница видна только если mist_frame_load_history_update/mist_frame_load_reset
на другом CPU перезапишет GFI+0x50 во время работы flt_irq_work. Фикс: в двух `div_u64(..., (u32)window)` читать
`(u32)GFI_U64(0x50)` в месте использования (scale оставить на значении со входа).

## Подробности по пунктам

### A1 init_module (VERIFIED, построчно сток 0x6e8..0xd98 против нашего .ko)
Порядок захвата в обоих: xr_sched_init, migt_sched_init, game_load_init, mist_frame_load_init (результаты не проверяются),
cpus_visit, цикл freq_qos по present CPU, [при ошибке: откат qos, printk 1876, cpufreq_register_notifier], hrtimer x2,
alloc_workqueue, proc_mkdir, proc_create, create_frame_load_proc, misc_register, alloc_chrdev_region, cdev_init/cdev_add,
class_create, device_create, INIT_*WORK x5, spin_lock_init, migt_info, register_plat_func 13/14/15/20/39/30.
Выходы с ошибкой (сток = наш):
1. cpufreq_cpu_get NULL (без put) / add min / add max (с put) -> откат qos -> регистрация notifier; ошибка -> хвост
   [cpufreq_unregister_notifier, game_load_exit, migt_sched_exit] -> ret; успех -> продолжение (first_ok).
2. alloc_workqueue NULL -> -14 -> откат qos -> хвост.
3. proc_mkdir NULL -> -14 -> flush+destroy wq -> откат qos -> хвост.
4. misc_register != 0 -> ret -> proc_remove(exec_buckets если есть), proc_remove(migt_dir) -> wq -> qos -> хвост
   (misc_deregister НЕ вызывается — верно, регистрация не удалась).
5. alloc_chrdev_region -> printk -> misc_deregister -> proc -> wq -> qos -> хвост.
6. cdev_add -> printk -> unregister_chrdev_region -> misc_deregister -> ... (cdev_del не вызывается в обоих).
7. class_create NULL/ERR -> printk -> cdev_del -> unregister_chrdev_region -> если (int)ptr==0 продолжить как успех,
   иначе misc_deregister -> ... ; 8. device_create — то же плюс class_destroy.
Наш хвост 0x1d4 (unregister_notifier, game_load_exit, migt_sched_exit) достигается из выхода цикла отката qos (0x338 b.hi 0x1d4)
и из ошибки notifier — как сток 0x8c8. Коды возврата совпадают. Двойных освобождений нет (unregister неизвестного
notifier безвреден и делается стоком так же).
Особенности стока, повторённые 1:1 (не регресс, INFERRED риск): после неудачного init остаются зарегистрированы
plat_func 18/19/21 (xr_sched_init) и 35..38 + sysctl (mist_frame_load_init), rvh try_to_wake_up_success и
cpufreq_transition (rvh снять нельзя), kmem-кэши — указатели в освобождённый модуль. delete_frame_load_proc в пути ошибки
не зовётся в обоих (proc_remove(migt_dir) удаляет поддерево). Возврат migt_sched_init игнорируется в обоих.

### A2 update_flt_load (VERIFIED)
printk (GFI+0x28 != 0) и trace_printk (flt_ftrace_debug == 0x220) независимы: сток f100 -> f13c printk -> f104 проверка 0x220;
наш 0x12c -> 0x198 printk -> 0x130. capacity (x21) обнуляется один раз до цикла кластеров в обоих. scale = mult_frac
(udiv/msub/udiv 64), quot/rem знаковые (sbfx #10,#22), `(util*rem)>>10` логический — одинаково. Аргументы и формат
trace_printk/printk/trace_update_flt_load_mark совпадают. Ловушка `c == 2 -> brk #1` есть в обоих. Остаток — D2.

### A3 mist_frame_load_init (VERIFIED)
Записи GFI+4/5/6, gpu_time, fence_pending, hispeed/lowspeed 90/10, цикл кластеров (f32 = n, обнуление rq, ловушки
c>2 -> brk 0x5512, c==2 -> brk 1), cluster_number, GFI_U32(0)=0, WARN_ON(header) (brk 0x800), init_irq_work в
GFI+0xa58..0xa70, register_sysctl_sz("migt", table, 8), plat_func 35/36/38/37, rvh cpufreq_transition, GFI_U32(0)=1, printk.
Записи dir=NULL нет ни в стоке, ни в нашем.

### A4 mist_frame_load_reset (VERIFIED)
GFI+0x58 = (u32)frame_tperiod, GFI+0x50 = `ubfx x,#10,#22` в обоих. Индекс таблицы tperiod: сток `sxtw`, наш `uxtw`,
но индекс уже проверен беззнаково <= 28 — эквивалентно.

### A5 game_add_task (VERIFIED)
Тело совпадает инструкция в инструкцию (лимиты 0x31/0x63, kmalloc 0x38, lock, hlist_add_head, счётчики). game_setup_wrender_list
вызывает её через bl в обоих; набор вызовов совпадает.

### B1 select_cp_work_mode_by_current (VERIFIED)
Сток a28c..a2b4: форма 1 `h<=cur && l>=cur && l>=h`, форма 2 `h>=cur && l<=cur && l<=h`; поиск вниз при `h>=cur`
иначе вверх; оба цикла проверяют обе формы. Наш .ko: те же условия; третье сравнение (l vs h) местами выброшено
компилятором, т.к. следует из двух первых (l<=cur<=h => l<=h) — эквивалентно для всех int. Форма 2 для текущего
режима у нас слита с веткой «вниз» (она требует h>=cur) — эквивалентно. Границы циклов: вверх i=mode+1..2, вниз mode-1..0.

### B2 adjust_adapter_mode (VERIFIED)
Потолки 0x5208/0x2af8/0x157c без ratio, пороги 18000/12000/9000/6000/4500/3600 с `lsl` по batt_type==2, /1000 знаковое,
max через csel gt, adp_mode = 4 / |=2 / |=1, PPS(11) перечитывает тип, DIV1 только PD_VERIFY(12). Отличия только в
порядке планирования (batt_type читается до/после вызова get_adapter_type; stur adp_mode=0 до/после fortify-проверки).

### B3/B4 (VERIFIED)
vfc_work (с инлайном update_cp_fsw) и strategy_quickchg_enable_buck_charging нормализованно идентичны стоку
(`strb w22(=1),[x19,#0xaf4]`).

### B5 open_path (VERIFIED)
Нормализованно идентична, кроме порядка хвостовых блоков. PPS (adp_type 11): mca_vote([+0x10]="wire_qc",1,100), иначе
mca_vote([+0x8],"wire_qc",1,1); голоса buck_charge_curr нет; хвост PPS: [+0x8]=1, [+0x10] снят; cp_vendor==5 -> msleep(20)
с выходом -1; строки "wire_qc" и "cp SY6541, wait 20ms..." совпадают (.rodata стока 0x105b/0x3746).

### B6/B7 (VERIFIED)
mca_wireless_quick_charge_select_stage нормализованно идентична (mov w0,wzr при k<0 в обоих циклах parallel).
Особенность стока (повторена): после обработки cur_volt_para[1] безусловный `brk #0x5512` (выход за массив при i=2).
Строка "... bridge_state %d 【0】is FULL Bridge" байт-в-байт равна стоку.

### C2 set_rtg_grp (VERIFIED: сток прочитан блоками, сверено с qs.c:276-510)
orig 2..10 -> skip-trace; from-путь (grp from 2..10: set, затем clear_draw_rtg под g_qs_lock если orig==12 или
orig в id[1..3] и p != NULL); switch lvl 6..10 (таблица: 6 цикл, 7->14, 8->13, 9->12, 10->11; иначе 0; сверено с
нашей таблицей); qos&0xc0 -> 13; skip при orig==new или new==0 (unlock + trace); !set: get_static_qos_lvl > 5
беззнаково (b.ls в обоих) -> -1; idx-путь (orig==12: valid==1&&tgid -> перенос, иначе exchange; иначе set + соседний
слот NULL при другом tgid); new!=12; new==12 + orig в idx (move_existed/exchange); поиск свободной группы и перенос
ui/render с get/put_task_struct; итог g0.valid=1/tgid; trace + `qs |= 0x8000` при ret==0. Наборы вызовов равны
(9 set_group_id, 2 get_group_id, 2 strcmp, 2 lock/unlock, 2 __put_task_struct). Ловушка стока `brk #1`
(`(idx*32|0x18) > 0x78`) недостижима (idx 1..3). exchange_draw_rtg и move_existed_draw_rtg нормализованно идентичны.

### C1 qievent_callback, остальное (VERIFIED)
Таблица 7 элементов, отображение action -> блок совпадает: 1 SET, 2 UNSET, 3/5 restore_qos_sched(e, action),
4 INHERIT (инлайн), 6 CORRECT (инлайн), 7 TASK_EXIT (инлайн clear_draw_rtg). Неизвестные action (0, >=8): в обоих
проверка PF_EXITING (-1), VIP (0), затем `action-1 > 6` -> 0. in_interrupt -> 0, data==NULL -> -22. asm("" : "+r")
поведения не меняет. Единственное отличие — D1.

### C3 xr_qi rwsem (VERIFIED)
do_rwsem_qos_inherit и undo_rwsem_qos_inherit нормализованно идентичны; android_vh_alter_rwsem_list_add отличается только
перестановкой x24/x25 и местом блока __list_add_valid_or_report (условие инвертировано вместе с целями), ldrb/strb [#0x28]
через x27 вместо x22 стоят под `cmp x22,x27; b.ne` — тот же адрес. Релокации совпадают (iter_ptrs_start/end — rwsem-копии
с orr #0x20, find_next_reader_owner, trace_qi_reader_owner_rwsem_inherit, do/undo). Блокировки (info->lock irqsave, v->lock),
get/put_task_struct, порядок вставки в список — как в стоке.
Риски стока, повторённые 1:1 (INFERRED): owner из sem->owner и reader из таблицы используются без RCU до get_task_struct
внутри do_rwsem_qos_inherit; в последнем цикле по writer'ам w->task без ссылки; проверка IS_ERR_OR_NULL(waiter) после
разыменования waiter->task.

## Оговорки
- Проверены собранные .ko из g20b/g20c/g20d. После этих сборок изменены `mi_game/flt.c` (02:04, правка G22b в строке 962,
  вне ревью) и `mca_strategy_quickchg.c` (02:06, автор неизвестен, вероятно G21a). Строки фиксов G20 в текущих исходниках
  на месте (проверено grep), но перед тестом нужна пересборка и повторное сравнение.
- Не проверялось: cleanup_module mi_game, функции вне перечисленных, динамика на устройстве.
