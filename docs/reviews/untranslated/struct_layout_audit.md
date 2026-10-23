# G40a: аудит раскладки структур (stock-модули OS4.0.0.7 на нашем ядре 6.6.144)

Дата: 20.10.2026. Только чтение: телефон не трогали, ничего не прошивали, git не трогали, дерево ядра не меняли.
Скретч на сервере: `<work-dir>` (94 МБ). Инструменты: `work/re/src/g40a_tools/` (только python).
Копии ключевых выводов: `work/a17/review/g40a/`.

## 1. Вердикт

**SAFE: для stock-модулей раскладка структур нашего ядра безопасна.** Не найдено ни одного поля, которое stock-модуль
читает или пишет по смещению, означающему в нашем ядре другое поле.

- Каждая KMI-структура, которую затрагивает вендорский код (task_struct, mm_struct, sched_entity, rq, cfs_rq, page, folio,
  vm_area_struct, cgroup, css, device, net_device, sk_buff, sock, file, inode, super_block, regulator, power_supply,
  input_dev, i2c/spi/platform, mutex/rwsem/spinlock, kobject, work/timer/hrtimer, module и ещё ~70), побайтно совпадает
  со stock. Исключения только в слотах `ANDROID_KABI_RESERVE`: task_struct, sched_entity, usb_request и fscrypt_operations.
  Там поля положены через `ANDROID_KABI_USE`, поэтому размер и смещения всех остальных полей не сдвинулись.
  Проверено (VERIFIED) двумя способами: моим BTF-сравнением и Google `stgdiff` по списку символов KMI.
- Поля со сдвигом есть только в 49 структурах, и все они внутренние. Либо они определены в .c-файле или приватном
  заголовке (pool_workqueue, usb_udc, fscrypt_master_key, f2fs/erofs/ext4 *_sb_info, security_hook_heads и т.д.),
  либо ими пользуется только встроенный код. Ни один stock-модуль их не импортирует и не трогает (VERIFIED, раздел 5).
- Утверждение G26a о сдвиге `task_struct.mm` (stock +0x5a8, у нас +0x5a0) **НЕВЕРНО**. В обоих ядрах `mm` = 0x5a0,
  `active_mm` = 0x5a8. Stock `mitee_dlkm` в `optee_open_session` читает `current->active_mm` (0x5a8), затем
  `exe_file` (+0x418) и `f_path` (+0xa8), а в `optee_check_mem_type` читает `current->mm` (0x5a0).
  Расхождение пришло из реконструкции исходника: в `work/re/src/mitee_dlkm/call.c:368` стоит `current->mm`, должно быть
  `current->active_mm`. Это не дрейф ядра. Проверено (VERIFIED): BTF обоих ядер плюс дизассемблер stock .ko.
- Пересобирать ядро **не нужно**. Обязательных исправлений в ядре нет. Ниже есть одно исправление в исходнике
  mitee (наш модуль) и пункты на будущее.

## 2. Метод и инструменты

| шаг | что сделано | инструмент |
|---|---|---|
| BTF stock | Из `os4g/an/boot/kernel` (stock Image 6.6.118-...-ab15863337) по магии 0xEB9F вырезан ровно один блоб `.BTF` (6 118 877 B; 141 469 типов, строки 2 487 057 B) | `btfparse.py extract` |
| BTF наш | Из `<build-dir>` (6.6.144-...-APEX-Foundry-v1.2): блоб побайтно равен `.BTF` из `dist/vmlinux` (6 202 560 B; 143 589 типов) | то же, `cmp` |
| структуры | Все именованные struct/union. Анонимные члены раскрыты, и члены с анонимным типом тоже (проход 2). Сравнивались размер, имя, битовое смещение, ширина битового поля и тип каждого члена. Дубликаты dedup распределены по парам. Сюда же входят «глубокие» различия: структура вложена по значению | `layoutdiff.py` -> `diff2.txt/json` |
| прочий ABI | enum (значения), typedef анонимных struct, прототипы tracepoint (`btf_trace_*`), прототипы FUNC (по экспортам из `vmlinux.symvers`) | `abidiff_extra.py` -> `extra.txt` |
| KMI | Google `stg`/`stgdiff` (prebuilts kernel-build-tools) по BTF обоих ядер, фильтр `-S :dist/abi_symbollist` (8079/8124 символов). TYPE_TAG переписан в const, потому что старый stg его не читает | `btf_untag.py`, `kmi_diff_small.txt` |
| конфиг | stock ikconfig `os4g/an/ikconfig_global.txt` против нашего `pos17_inputs/.../verify/ikconfig.txt`, 41 значимое различие | `cfgdiff.py` -> `cfg_diff.txt` |
| BTF модулей | 636 уникальных .ko (vendor_dlkm, vendor_ramdisk, system_dlkm). Определено, на какой базе построен split-BTF каждого; для модулей на базе stock собраны ссылки на типы ядра и локальные копии структур | `btfbase.py`, `modbtf.py`, `modredef.py` |
| инструкции | Скан всех 636 .ko: регистр от `mrs xN, SP_EL0` (current) отслеживается через add/mov, каждое `[xN,#imm]` переводится в член task_struct в stock BTF и в нашем. Плюс точечный разбор 5 модулей и grep смещений слотов KABI | `curscan.py`, `at.py`, `llvm-objdump` clang-r510928 |
| импорты | `llvm-nm -u` всех модулей (36 060 импортов) против символов, связанных с изменёнными внутренними структурами | `imports.tsv` (сервер) |

Самопроверки: парсер проверяет, что секция типов прочитана точно до конца (assert). Смещения из BTF совпали с
дизассемблером во всех проверенных местах: mitee 0x5a0/0x5a8/0x418/0xa8; mi_reclaim `android_oem_data1[3]` = 0xdd8,
это совпадает с прошлым ревью; WALT `rq->idle_state` = 0xe10; ufs_qcom `android_quirks` = 0xd0.

## 3. Различия раскладки: stock 6.6.118 против нашего 6.6.144

### 3а. Структуры, видимые через KMI (stgdiff, символы `abi_symbollist`)

| структура | stock | наш | что изменилось | можно ли сломать модуль |
|---|---|---|---|---|
| task_struct (4800 B) | `android_kabi_reserved2` @0xe00 | union { `user_dumpable:1` } @0xe00 | KABI_USE(2), исправление ptrace | нет: размер и смещения те же; ни один модуль не обращается к 0xe00 (VERIFIED) |
| task_struct | `reserved6..8` @0xe20..0xe37 | `sysvsem` @0xe20, `sysvshm` @0xe28 (16 B) | KABI_USE(6) и KABI_REPLACE(7+8), CONFIG_SYSVIPC=y | нет: к 0xe20..0xe37 обращений 0 (VERIFIED) |
| sched_entity (320 B) | `android_kabi_reserved1` @0x100 (task+0x1c0) | `bore_stats*` @0x100 | KABI_USE(1), CONFIG_SCHED_BORE | нет: обращений к task+0x1c0 через current 0 (VERIFIED); прочие пути: INFERRED |
| usb_request (104 B) | `android_kabi_reserved1` @0x60 | `ep*` @0x60 | KABI_USE(1). Пишет `usb_ep_alloc_request`, читает только встроенный `free_usb_request()` в f_ncm/f_acm/f_ecm/f_rndis | нет: и запись, и чтение во встроенном коде (VERIFIED по исходнику) |
| fscrypt_operations (128 B) | `reserved1` @0x40 | union { `get_devices_new` } @0x40 | KABI_USE(1) | нет: fscrypt реализуют только встроенные ФС |
| pool_workqueue (512 B) | stats @0x98 | `mayday_cursor` @0x98, stats @0xc8, release_work 0xd8->0x108, rcu 0x100->0x130 | бэкпорт из stable | нет: определение в `kernel/workqueue.c`, модулю видно только как непрозрачный указатель; единственный, кто ставит workqueue-хуки (`mi_wq_dynamic_priority`), читает лишь `workqueue_struct.flags` (0x101), а эта структура не менялась (VERIFIED) |
| usb_udc (1072->1080) | dev @0x10 | `gadget_release` @0x10, dev @0x18, ... | бэкпорт из stable | нет: приватна для `udc/core.c` |
| fscrypt_master_key (872->368) | mk_direct_keys[11]... | mk_mode_keys, mk_users стал list_head | переделка fscrypt | нет: приватна (`fs/crypto/fscrypt_private.h`) |

Проверено (VERIFIED): stgdiff. Все четыре KABI_USE и fscrypt_master_key записаны в `common/android/abi_gki_aarch64.stg.allowed_breaks`,
то есть разрывы ABI в истории ACK нашего дерева одобрены (ACK_SHA: `android15-6.6-2026-01_r19`). Других изменений типов,
достижимых из KMI, stgdiff не нашёл. Добавлены 45 символов `__traceiter_android_{vh,rvh}_*`: это только добавления, они безвредны.

### 3б. Внутренние структуры со сдвигом полей (не KMI или приватные)

| структура | размер stock -> наш | первый сдвиг (stock -> наш) | причина |
|---|---|---|---|
| f2fs_sb_info | 4592 -> 5072 | gc_lock 0x9e0 -> 0xa70 (113 полей) | CONFIG_F2FS_VIP_FILE, F2FS_FS_HYBRID_UFS, XIAOMI_ENHANCED_IOSTAT (патчи Xiaomi в дереве; коммит 56daf21c0) |
| f2fs_inode_info / f2fs_nm_info / f2fs_attr | 1464 -> 1488 / 376 -> 424 / = | atomic_write_cnt 0x5ac -> 0x5b0 / +vip / id 0x28 -> 0x2c | то же |
| erofs_sb_info | 448 -> 1656 | volume 0x198 -> 0x650 | CONFIG_XIAOMI_EROFS_IOSTAT |
| ext4_sb_info / ext4_buddy | 2368 -> 2432 / = | s_sb 0x238 -> 0x268 (+s_error_notify_mutex) / page -> folio | дрейф stable до 6.6.144 |
| security_hook_heads, lsm_blob_sizes, union security_list_options | 1776 -> 1800, 32 -> 36 | file_ioctl 0x270 -> 0x280 | хуки LSM backing_file (stable) |
| getdents_callback(64), compat_getdents/readdir_callback | 40 -> 56 | prev_reclen 0x18 -> 0x28 | SUSFS (sb, idmap в readdir) |
| nf_queue_entry / nf_ct_timeout / nf_conncount_list | 88 -> 96 / 16 -> 32 / = | id 0x18 -> 0x20 / data 0x10 -> 0x20 / в дыру | stable |
| io_cancel_data, request_key_auth, jitterentropy, crypto_authenc(_esn)_ctx, tcf_pedit_parms, proc_maps_private, backing_file, dm_exception*, mt_device, uinput_device, uvc_device, f_ncm_opts, nvme_ctrl_ops, tegra_pmc, xfrm_mgr (прототип migrate), trace_event_raw_hrtimer_start | разные | разные | stable 6.6.118 -> 6.6.144 |
| kvm_host_data / pkvm_hyp_vm | 2176 -> 2192 / = | +pmblimitr/trblimitr в конце / refcount 0x1138 -> 0x113a | stable (pKVM, EL2) |
| mmu_gather, iio_dev_opaque, journal_s, common_audit_data | = | новые биты в бывших пустых битах / lock_class_key нулевого размера / только const | без влияния на смещения |

Прочие ABI-находки (`extra.txt`):
- enum `lockdown_reason`: в позицию 20 вставлен `LOCKDOWN_XEN_USER_ACTIONS`, значения KCORE..CONFIDENTIALITY_MAX сдвинулись на +1.
  Ни один модуль не импортирует `security_locked_down` (VERIFIED). Lockdown LSM выключен.
- enum `ufshcd_android_quirks`: у нас нет `UFSHCD_ANDROID_QUIRK_NO_IS_READ_ON_H8` (1<<31). Stock `ufs_qcom` выставляет
  в `hba->android_quirks` (0xd0) только бит 0 (VERIFIED, дизассемблер), так что ни на что не влияет.
- Остальные enum (FI_VIP, STOP_CP_REASON, RESCUER_BATCH, TP_ERR, erofs attr) внутренние.
- Прототип tracepoint `hrtimer_start` получил аргумент `bool was_armed` в конце. На него никто не подписан
  (импортов `__tracepoint_hrtimer_start` 0), а добавление в конец на arm64 и так безвредно.
- Прототипы FUNC: отличаются 38, из них экспортных 0. Прототипы всех `__traceiter_android_*`, общих для двух ядер,
  совпадают. 52 функции есть только в stock; ни одну из них модули не импортируют.
- Typedef'ы анонимных struct (spinlock/atomic/seqlock и т.д.): отличий 0.

## 4. Модули по группам

| группа | сколько | метод | результат |
|---|---|---|---|
| BTF на базе stock vmlinux (N=141469/L=2487057): 96 GKI в system_dlkm + matt, minet, miscore, mibbr, miicmpfilter, sla, bootinfo, hardwareinfo, msm_video, spcom, spss_utils, synaptics_tcm2, xiaomi_touch | 109 | split-BTF разобран точно | Ссылки на изменённые структуры только у task_struct/sched_entity/fscrypt_operations (части KABI_USE). Локальные копии task_struct/mm_struct/cgroup/dma_buf/ipv6_devconf/prefix_info отличаются от stock **только** тем, что в них нет новых членов-union в слотах KABI: модули собраны на чуть более старых заголовках ACK, и для нашего ядра действует то же самое. channel/raw_sock/console/clock_info: совпадение имён с другими структурами. VERIFIED |
| BTF на другой базе (N=141744/L=2547933, сборка Xiaomi/Qualcomm; такого vmlinux на сервере нет): платформа QCOM, sched-walt, mca_*, mi_*, millet_*, qcom_* и т.д. | 421 | BTF не разрешить. Использованы скан current, импорты и транзитивность | По скану current расхождений 0. Импортов, связанных с внутренними структурами из 3б, нет. Транзитивность: эти модули работают на stock ядре (base A), а наше ядро равно A по всем KMI-полям. INFERRED, частично VERIFIED |
| без .BTF (techpack: audio, camera, display, kgsl, wlan qca_cld3, rmnet, mitee) | 105 | скан current и импорты | то же. mitee проверен вручную |
| база не определилась (qti-fixed-regulator) | 1 | скан current и импорты | расхождений нет |

Скан `current->` (VERIFIED, `cur.txt`): у 534 модулей 102 978 обращений по 252 различным смещениям.
**Ни одно смещение в stock и в нашем BTF не раскрывается в разные члены** (DIFF = 0). Ни одного обращения к 0xdf8..0xe38
(слоты KABI task_struct) и к 0x1c0 (`se.bore_stats`). Пометки «RSV/hole/outside» (msm_drm, cnss2, msm-eva, единичные)
выглядят ложными срабатываниями линейного скана; у stock и у нас они раскрываются одинаково.
Grep `#0xe00..#0xe30` по всем модулям дал 16 модулей. Разобран sched-walt: 7 обращений = `rq->idle_state` (0xe10),
rq совпадает. Остальные (qca_cld3_*, msm_drm, lt9611uxc, camera, ...) по базе не разобраны, см. раздел 7.

Точечная проверка на инструкциях (VERIFIED):
- **mitee_dlkm**: current+0x5a0 `mm` и +0x5a8 `active_mm`, mm+0x418 `exe_file`, file+0xa8 `f_path`. Одинаково в обоих ядрах.
- **sched-walt**: task 0x0/0x10/0x28/0x30/0x84/0x618/0x658 и `android_vendor_data1[0..54]` (0xbc0..0xd70); rq 0xe10 = idle_state.
- **mi_reclaim**: `android_oem_data1[3]` = 0xdd8 (32 раза), tgid 0x61c, signal 0x878.
- **mi_wq_dynamic_priority** (хуки `android_rvh_create_worker` и `alloc_and_link_pwqs`): `workqueue_struct.flags` (+0x101),
  `blk_mq_hw_ctx.run_work` (+0x40), `request_queue.requeue_work` (+0x298), `task.prio`. pool_workqueue не трогает.
- **ufs_qcom**: `ufs_hba.android_quirks` (0xd0) |= 1 (CUSTOM_CRYPTO_PROFILE).
- **matt** (BTF): локальные копии task_struct/mm_struct/cgroup совпадают со stock до бита, кроме union в слотах KABI.

Загрузка модулей: в обоих конфигах `CONFIG_MODULE_ALLOW_BTF_MISMATCH=y`, а `btf_module_notify` при несовпадении базы
только предупреждает (`pr_warn_once`) и модуль грузится. Расхождение BTF-базы модулей (A на нашем ядре, B на любом) к ошибке
загрузки не ведёт (VERIFIED: конфиг и `kernel/bpf/btf.c:7388-7398`).

## 5. KMI или не KMI (п. 3 задания)

- (a) Изменения в KMI: только task_struct, sched_entity, usb_request и fscrypt_operations, все через ANDROID_KABI_USE
  в зарезервированных слотах. KMI-совместимы по построению и внесены в allowed_breaks. Ошибки сборки нашего ядра нет.
  pool_workqueue, usb_udc и fscrypt_master_key stgdiff видит только потому, что в BTF есть приватные определения
  (а pool_workqueue нет даже в базовом STG Google). Модулю они непрозрачны.
- (b) Остальные 42 структуры из 3б внутренние (определены в .c или приватном заголовке либо используются только встроенным
  кодом). Импортов, через которые до них можно добраться, нет (f2fs/erofs/ext4/fscrypt/jbd2, nf_queue/conncount/ct_timeout,
  security_*, pkvm, xfrm_register_km, `__tracepoint_hrtimer_start`: 0 совпадений). Безвредны.
- Конфиги, влияющие на inline-код модулей, одинаковы: HZ=250, NR_CPUS=32, VA_BITS=39, 4K, PREEMPT, KASAN_HW_TAGS, CFI,
  SCS, STACKPROTECTOR_PER_TASK, SLUB, без LOCKDEP. Enum, задающие индексы счётчиков (vm_event_item, node_stat_item,
  zone_stat_item, pageflags и т.д.), не изменились.

## 6. Первопричины и предложения (по порядку)

Причины (VERIFIED по конфигу, дереву и BTF):
1. CONFIG_SYSVIPC=y, CONFIG_SCHED_BORE=y. Дерево уже кладёт их в слоты KABI: `include/linux/sched.h:577` (bore_stats)
   и `:1533-1548` (user_dumpable, sysvsem, sysvshm; обычные поля закомментированы в `:1087-1088`).
2. Upstream stable 6.6.118 -> 6.6.144 смержен поверх ACK `android15-6.6-2026-01_r19` (дерево одним коммитом
   `2fe507dca Rebase onto the LineageOS QCOM sm8750 kernel (6.6.144)`, история патчей недоступна): сдвиги во внутренних
   структурах, вставка в enum lockdown_reason, `was_armed` в hrtimer_start.
3. Функции Xiaomi в ФС (F2FS_VIP_FILE, F2FS_FS_HYBRID_UFS, XIAOMI_ENHANCED_IOSTAT/EROFS_IOSTAT): f2fs/erofs.
4. SUSFS: readdir callbacks. KernelSU, ntsync, ADIOS, connector/proc_events, wrapfd добавляют только новые структуры
   (188 only_ours), существующих не меняют.

Предложения:
1. **Ядро: менять ничего не нужно, пересборка не требуется.**
2. Исправить реконструкцию mitee: `work/re/src/mitee_dlkm/call.c:368` `current->mm` -> `current->active_mm`
   (так в stock). Пересобирается только модуль mitee, не ядро. В `STATUS.md` вычеркнуть «KERNEL DRIFT task_struct.mm
   +0x5a8 vs +0x5a0» и пометку Kernel-ABI в строке 111.
3. Правило на будущее: новые поля в KMI-структурах (task_struct, sched_entity, rq, mm_struct, page, cgroup, ...) только
   через `ANDROID_KABI_USE` в свободные слоты (у task_struct свободны reserved3..5, у sched_entity reserved2..4) или в
   `android_vendor_data`. После каждой пересборки прогонять:
   `btfparse.py extract dist/Image ours.btf && layoutdiff.py stock.btf ours.btf d` и
   `stgdiff --stg stock_kmi.stg --stg ours_kmi.stg --format small` (команды в разделе 2). Ожидается ровно 3а и 3б.
4. Необязательно: если когда-нибудь появится модуль, который вызывает `security_locked_down`, вернуть порядок enum
   `lockdown_reason` как в stock или перенести `LOCKDOWN_XEN_USER_ACTIONS` в конец. Сейчас не нужно.

## 7. Что проверить не удалось

- База BTF для 421 модуля платформы QCOM/Xiaomi (vmlinux сборки Xiaomi, N=141744, L=2547933) недоступна. Раскладку,
  с которой собраны именно эти модули, напрямую не видно. Их совместимость выведена (INFERRED): stock на этих модулях
  работает, а наше ядро совпадает со stock по всем KMI-полям. Для этой группы и для 105 модулей без BTF проверены только
  обращения через `current` и импорты.
- Обращения к task_struct, sched_entity и usb_request через указатели, а не через `current` (аргументы хуков, списки),
  систематически не проверены. Только grep смещений 0xe00..0xe30 (16 модулей, разобран один sched-walt) и разбор 5 модулей.
  Линейный скан без потока управления даёт ложные срабатывания; ложные пропуски в редких случаях тоже возможны.
- Константы `#define` и семантика inline-кода сравнивались только через конфиг. Поведение функций (не раскладка) не
  проверялось: например, в stock обработка квирка NO_IS_READ_ON_H8 есть, у нас нет, но ufs_qcom его не включает.
- Коммиты-источники каждого бэкпорта атрибутировать нельзя: дерево сведено в один коммит.
- Ничего не запускалось на устройстве. Вывод «SAFE» относится только к раскладке структур, не к работоспособности ROM.
