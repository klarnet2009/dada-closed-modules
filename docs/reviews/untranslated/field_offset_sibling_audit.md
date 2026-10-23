# Сверка смещений полей task_struct и соседних структур: наши реконструкции против stock OS4.0.0.7 (G42a, 20.10.2026)

## 1. Повод и итог
Аудит struct_layout_audit.md (раздел 4) показал, что mm = 0x5a0 и active_mm = 0x5a8 одинаковы в stock и в нашем ядре. Значит,
разница в `optee_open_session` модуля mitee_dlkm (stock читает `[sp_el0,#0x5a8]`, мы `[...,#0x5a0]`) была не дрейфом ядра, а
реальной разницей источника: `call.c` брал `current->mm`, stock берёт `current->active_mm`.

**Итог: найдена и исправлена одна BEHAVIOUR-разница (mitee_dlkm). В остальных 113 парах модулей доказанных разниц по смещениям
task_struct нет** (все найденные расхождения разобраны по листингам: CODEGEN или не task_struct).

## 2. mitee_dlkm: что было и что сделано
- Stock `optee_open_session` (0x450): `mrs x24,SP_EL0; ldr x8,[x24,#0x5a8]; ldr x8,[x8,#0x418]` (exe_file), `add x0,x8,#0xa8` (f_path), `bl d_path`.
  Без проверки NULL, без mmgrab/get_task_mm. Дальше инлайн `auth_ca_name`.
- Исправлено `work/re/src/mitee_dlkm/call.c`: `current->mm` -> `current->active_mm` (md5 call.c d292e5c134d9fe1c7de3de2f0f541691).
  Остальные использования: `get_ca_path` уже на `active_mm`; `optee_check_mem_type` читает `[sp_el0,#0x5a0]` (mm) и в stock
  (листинг совпадает, symcmp5b CLEAN-POL). get_task_mm / mmgrab в модуле нет.
- Пересборка реальным Kleaf (`_kb42.sh`, scratch <work-dir> 1 итерация, bazel-out удалён): .ko md5 73f771910b3d141e9edf40b23f6c30c6.
  compare_ko_v2: 156 из 182 функций идентичны (как было), function-set, binding, kCFI 160/160 (preamble_cmp: 0 расхождений), imports 187, exports,
  objects, vermagic PASS; bug-table/anon-data/altinstructions/buildmeta/depends FAIL, как раньше (NOT STRICT, DO NOT LOAD).
- Множество смещений от `current` у `optee_open_session` теперь равно stock (раньше 0x5a0 вместо 0x5a8). symcmp5b: остались только атомы
  стековых слотов `stkarg(tee_session_calc_client_uuid)` (буфер: stock sp+0x18, ours sp+0x8; uuid: sp+8 против x29-0x18) и перестановка двух
  регистров при заполнении заголовка msg_arg: CODEGEN.
- STATUS.md модуля: пункт «KERNEL DRIFT task_struct.mm» вычеркнут, добавлен раздел G42a, первая строка по-прежнему PARTIAL / DO NOT LOAD.

## 3. Сверка всех модулей
Метод (`work/re/g42a_tools/fo_scan.py`, на сервере <work-dir>): для каждой функции stock и нашей сборки через llvm-objdump
берутся все `[xN,#imm]` загрузок и записей. Три множества: (а) базы, пришедшие от `mrs xN,SP_EL0` (current), с отслеживанием mov и сбросом
при записи регистра; (б) то же без сброса («loose»: любой регистр, в который хоть раз клали SP_EL0); (в) мультимножество всех imm >= 0x100.
Сравнение stock и ours по функциям и по модулю. Имена нормализованы (`.llvm.<hash>`, `.text.<fn>`).
Пары: 114 модулей, 77 030 общих функций (новейшие сборки из <work-dir> stock из <work-dir>).
Не вошло: модули без stock-двойника по имени (dcvs_main, sched_entity_freq, mitee_emu, служебные before_*/base_*/b_*).
Ограничение: линейный разбор без CFG, после `ldp`/`bl` отслеживание current теряется (даёт ложные расхождения в обе стороны),
поэтому расхождения считались подозрением, а решение принято по сырым листингам.

Пары с отличием по current-множеству или по модульным подсчётам: bq27z561, mi_game, mi_reclaim, xr_qi, xr_qs, mitee_dlkm (до правки).

| модуль / функция | отличие | вердикт и доказательство |
|---|---|---|
| mitee_dlkm / optee_open_session | 0x5a8 против 0x5a0 | **BEHAVIOUR, исправлено** (раздел 2) |
| mi_reclaim / is_important_process | у ours «лишние» 0x46, 0x878 (signal) | CODEGEN: в stock те же `[x20,#0x46]`, `[x20,#0x878]`, `[x20,#0x61c]` (потеря отслеживания после `ldp x20`); различается планировка и адресация .bss |
| mi_game / migt_hardwork (0x30), mist_frame_load_history_update (0x10) | «лишние» смещения у ours | CODEGEN, **только отчёт, файл не правился**: в обоих `str w8,[x23,#0x30]` (`__state`) и `str w8,[x2x,#0x10]` (preempt_count), у stock отслеживание потеряно из-за `ldp x23` |
| mi_game / migt_ioctl | stock читает `current->pid` (0x618) 3 раза, ours 2 | вероятно CODEGEN (в ours значение `w2` с первого чтения используется выше по пути, сравнения с render_pid те же), **только отчёт**, уверенность средняя, владельцу mi_game стоит перепроверить |
| mi_game / migt_hardwork 0x61c (tgid) | «лишний» у ours | CODEGEN: stock `ldr w1,[x24,#0x61c]`, ours `[x23,#0x61c]` |
| xr_qi / android_vh_rwsem_opt_rspin | 0x10 (preempt_count) 54 против 52 | CODEGEN (число инлайновых preempt_disable/enable), **только отчёт** |
| xr_qs / set_rtg_grp | 0x5b0 в одном из множеств | не расхождение: `ldr w,[x,#0x5b0]` есть в обеих сборках (2 в stock, 1 в ours, разница по CFG), **только отчёт** |
| bq27z561 / fg_set_one_dynamic_fv | у ours нет `[sp_el0,#0x620]` (stack canary) | CODEGEN: stack-protector не вставлен, поле task_struct по смыслу не меняется |
| matt / matt_make_pack, matt_make_nack, matt_rcv_data | у ours «лишние» 0x618/0x620/0x624 | CODEGEN: поля собственной структуры matt, другая форма цикла (`ldr [x10,#0x620]` против `add x12,x20,#0x620`), те же смещения |
| mibbr / mibbr_main | у ours 0x5a0, 0x540 | не task_struct: база x19 это структура mibbr |
| mi_low_lat_mem / *_end_handler | у ours 0x7c0/0x7c4 | CODEGEN: глобальный массив статистики; проверено, что `tbnz w,#15` равно `cmp -0xffff, w<<16` |
| qca_cld3_*, mca_*, aw882xx/tfa98xx/sia91xx, cameramsger | 0x100..0x1000: стековые слоты и поля своих структур | CODEGEN или не task_struct; **Wi-Fi не правился** |

Для остальных модулей (pub_*, millet_*, mist, miscore, minet, mi_wq_dynamic_priority, mi_power и т.д.) множества от current и мультимножество
>= 0x100 совпадают в каждой функции. Пар pid/tgid (0x618/0x61c), parent/real_parent, group_leader, cred/real_cred, comm с перестановкой не найдено.

## 4. Что осталось и риски
- Проверка шире, чем пара 0x5a0/0x5a8, но эвристическая: поля структур, пришедших аргументом, ловятся только мультимножеством >= 0x100.
- mi_game, xr_*, Wi-Fi не правились (только отчёт).
- mitee_dlkm по-прежнему НЕ загружать; os4_* стабы не поставлять.
