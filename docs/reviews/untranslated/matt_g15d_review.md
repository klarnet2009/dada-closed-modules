# matt: независимое ревью диффа G15d (Opus, read-only), 19.10.2026

Объект: `work/re/src/matt/` (PARTIAL, DO NOT LOAD). Сток: GLOBAL OS4.0.0.7 `<work-dir>` (md5 4b0271c7...; = `<work-dir>`). Ничего не запускалось на телефоне, исходники не менялись, git/adb/fastboot не использовались. Рабочая папка на сервере: `<work-dir>` (private, 700): свой дизассемблер стока и нашей сборки (`llvm-objdump -dr --no-show-raw-insn`, clang-r510928), скрипты `nf.py` (нормализация функции), `md.py` (маскированный diff/мультимножество), `rs.py` (разрешение строк в релокациях), `tl2.py` (номера строк трасс), повторный прогон `calls.py`.

Пометки: **VERIFIED** = проверено мной по дизассемблеру/байтам/md5 (способ указан); **INFERRED** = вывод без полной проверки.

## 0. База сравнения и артефакты

- Состояние до G15d = `<work-dir>`. VERIFIED (md5 всех файлов): отличается от `<work-dir>` ровно в 7 файлах, названных в STATUS (matt_cc.c, matt_conn.c, matt_log.c, matt_q.c, matt_rcvd.c, matt_sack.h, matt_sock.h); локальные исходники `work/re/src/matt` == `g15d/src/matt` (md5 всех 38 файлов). INFERRED: g1ag/src = финальное состояние G1ag (mtime 18.10 10:41, числа G15d-1 "до правок" = числа G1ag).
- Дифф G15d: 162 строки `diff -u` (полностью прочитан).
- md5 текущих 7 файлов: matt_cc.c cbe1969d..., matt_conn.c 58bc2f64..., matt_log.c f258d4bb..., matt_q.c 210e0548..., matt_rcvd.c 4b37654e..., matt_sack.h 764c4fd4..., matt_sock.h 397010d3....
- **Собранного загружаемого .ko нет.** `<work-dir>` (md5 2f05abc2..., 8.5 MB) == `o/matt.lto.o` (тот же md5): это объект ThinLTO-эмуляции (ELF REL, нет полного набора `.gnu.linkonce.this_module`/`__versions`/`.modinfo`), не продукт Kleaf; Kleaf `bazel-out` удалён в G15d. Собран после правок исходников (VERIFIED: mtime src 20:49:31-40, matt.lto.o 20:49:59). Всё ниже сравнивалось на этом объекте.

## 1. Правки G15d против стока

| id | что | вердикт | как проверено |
|---|---|---|---|
| F6 | minmax: убран третий `if` | **MATCHES** | VERIFIED: `matt_minmax_running_max/min` побайтно равны стоку (69/69 строк, отличие только в слове-заполнителе после `ret`). Сток = стандартный двухуровневый `minmax_subwin_update`. |
| F10 | set_bind_port: беззнаковые сравнения, `(u64)rand*remaining>>32` | **MATCHES** (семантика; форма условий CODEGEN) | VERIFIED side-by-side: блок случайного порта (`sxtw x8,w24; mul x8,x9,x8; lsr #32; add`) идентичен; сток `cmp hi; ccmp lo,#0,ls; csinc lo` = наш `cmp lo; ccmp hi,#2,hs; csinc hi` (тот же предикат `snum+1 >u high || snum+1 <u low`); цикл `--remaining > 0` знаковый в обоих; сравнение порта ntohs/htons эквивалентно. |
| F11/N1 | data_bytes как u32 | **MATCHES** | VERIFIED: все сайты `sub w,w,#0x10; add x,x,w(uxtw)` в matt_rcv_data (5), matt_clean_rtx_queue (3), matt_rcv совпадают по ширине со стоком (32-битное вычитание, нулевое расширение). |
| N3 | тест push `(s32)(pushed-write+0x20000U)<0` | **MATCHES** | VERIFIED: сток `sub w9,[0xfe8],[0xfe4]; cmn w9,#0x20000; b.pl skip`, наш `... cmn; b.mi push` — та же инструкция и флаг N. |
| F7 | делитель `int`, баг "тера" | **MATCHES** (семантика), код не идентичен (CODEGEN) | VERIFIED: сток выбирает делитель csel-цепочкой, для ≥1e12 = `0xFFFFFFFFD4A51000` (= (int)1e12, знаково расширен) и делит `sdiv x`; наш — тот же 64-битный делитель, те же границы (≤999 / <1e6 / <1e9 / <1e12), тот же индекс единицы 1..4, те же три строки формата (разрешены по релокациям). Отличие только в trace-тексте при ≥1e12 бит/с, воспроизведено. |
| F9 | `READ_ONCE(sk_rcvbuf)` в трассе matt_rcv | **MATCHES** | VERIFIED: число и вид acquire-загрузок в matt_rcv теперь равны (4 ldar + 4 ldapr-альтернативы); аргументы трассы "drop: ..." совпадают. Остаток: `__LINE__` этой трассы 1711 вместо стоковых 1709 (см. D2, косметика). |

Итог п.1: все шесть правок соответствуют стоку; поведенческих отличий не внесено.

## 2. Сознательные отклонения S1/S2

### S2 (снимок `mh->flags` до возможного free) — **корректно, оставить**
- VERIFIED (сток): в matt_rcv_data путь перекрытия OFO делает `kfree_skb_reason(skb, 2)` (сток 0xb50) и `b 0x5f8`, где `ldrb w24,[x25,#0xc]` читает flags из головы освобождённого skb (x25 = старый `skb->data`) — 1-байтовое UAF-чтение стока.
- Поведение на валидном трафике не меняется (VERIFIED по C и стоку): между `matt_validate_incoming` и использованием никто не пишет `mh->flags`; in-order путь (`skb_pull` не трогает голову, сокет под bh-lock, ресивер не может съесть skb до unlock); OFO-вставка — skb жив в дереве. После free в стоке/у нас читаются только локальные `segs/start/old_rcv_nxt` и поля сокета. Других читателей mh после очереди/free не найдено (matt_rcv_established возвращается сразу; трасса add_backlog в matt_rcv выполняется до `bh_unlock_sock`; `matt_add_backlog` не коалесцирует).
- С настоящим пиром протокол сломать не может (меняется только источник одного байта, значение то же).

### S1 (длина SACK-списка против `skb_headlen`) — **оставить, но поправить условие (D1, LOW)**
- VERIFIED: на приёме все skb линейны — `matt_udp_rcv` (encap_rcv UDP-туннеля, сток идентичен) делает `__pskb_pull_tail(skb, data_len)`; `skb->data` в sacktag = matt-заголовок (matt_rcv делает `skb_pull` до transport header). Значит `headlen == len`.
- VERIFIED: валидные PACK/NACK всегда проходят: `matt_make_pack/nack` пишут `sacks_size = n<<3` и кладут ровно `0x34/0x18 + 8n` байт, n ≤ 128 (кольцо приёмника 128, `n >= 128 -> return`). Сток зажимает num до 128, наша проверка идёт уже после зажима. GRO не склеивает mgmt-пакеты (INFERRED по описанию matt_gro_receive в STATUS).
- С реальным пиром: для стокового пира отличий нет. Для чужой/будущей реализации пира с некорректным `sacks_size` S1 = "как будто SACK-блоков нет" (кумулятивный ACK работает, падает только эффективность) — это безопаснее стокового чтения мусора за концом пакета (INFERRED: серверная сторона Xiaomi неизвестна).
- **D1 (LOW, только malformed):** при отрицательном `sacks_size` (num = sz>>3 < 0) проверка из-за `(unsigned long)num*8` даёт большое число и делает `return` (для PACK при |num|>6, для NACK при |num|>3), тогда как сток идёт в ветку `sz<8` и записывает `highest_sackoffset=0, sack_used=0, first_sackoffset=sacks[0].start` (VERIFIED, сток 0x98-0xa4 и 0x5b4-0x5c0). Наш код оставляет устаревший `sack_used`. Исправление одним условием: `if (num > 0 && (unsigned long)((u8 *)wire - skb->data) + (unsigned long)num * 8 > skb_headlen(skb)) return;` — отрицательные размеры снова ведут себя как в стоке.
- Не покрыто S1 (INFO, сток так же): фиксированные поля mgmt-пакетов читаются без проверки длины (PACK до +0x34, PING/PONG/RESET/REPORT +16..+28 при проверке только 16 байт в matt_rcv). Для линейных UDP-skb чтение остаётся в пределах head-аллокации (tailroom/skb_shared_info) — мусорные значения, не fault (INFERRED). Захотите закрыть — отдельный выбор "эквивалентность против безопасности".

## 3. Пять формально не доказанных тел — построчное чтение стока против C

| функция | результат | как |
|---|---|---|
| **matt_sacktag_write_queue** | **CODEGEN**, поведенческих отличий нет, кроме S1 | VERIFIED: прочитаны все 378 инструкций стока против matt_sack.h: диспетчер типов 4/3 (NACK 0x14/0x18, PACK 0x30/0x34), `asr`+знаковый зажим 128, `cbz num -> ret` без финальных записей, трасса до `sz>=8`, копирование + трасса с raw sz, bubble sort по `before()` с корректной перезагрузкой после swap, повторное использование `cur` при `before(start, cur.start)`, bsearch влево/вправо/найдено, условия walk (out-of-sack, in_sack, gso_segs ≥2 из cb+0x20, mss из cb+0x22, округление `ccmp` = +mss только при остатке и diff<0, `pkt_len >= skb->len`, `matt_fragment(...,1,..,0x820)<0`), sacktag_one (snd_una, ACKED, rtt через div 1000 и max(.,0), условие rack по 0x82 и srtt, rack.mstamp/end_seq, учёт lost/retrans/sacked, flag|=0x20, sack_delivered), rate_skb_delivered всегда, list_del_init при ACKED, финальные записи highest/used/first. |
| **matt_clean_rtx_queue** | **CODEGEN** | VERIFIED: прочитаны все 474 инструкции. Полный ACK, нет SACK (`diff>=0 -> break`), highest-тест, цикл по SACK (sbfiz), partack (SACKED-статистика 0x10c0/0x10c8), straddle (BUG_ON ×3 = `brk #0x800`, FACKED 0x10b0/0x10b8, `(u16)trimmed`, `max(segs,1)`), блок account (rack, перечитывание cb->sacked для RETRANS, sacked/lost/packets_out), free только при `acked && fully` (delivered, rate, FACKED, list_del с проверкой, rb_erase, wmem_free_skb), ack_update_rtt (min_rtt, srtt 7/8, rtt_var 3/4, rto, announce/10% порог, `udiv` на old_srtt, аргументы 4 трасс). Единственное отличие формы — сток проверяет `(s32)(sack.end-cb.end)>=0` и внутри ветки `before(sack.end, cb.start)`: различие возможно только при cb.end-cb.start ≥ 2^31 (нереально). |
| **mibbr_main** | **CODEGEN** | VERIFIED: прочитаны все 934 строки стока против matt_bbr_main.h + хелперов matt_bbr.c: update_bw (сброс round_start, `delivered<0 / interval_us<1`, round, `sdiv` bw, условие app_limited, minmax(…,10,…)), ack_aggregation (min(win_rtts+1,31), ≥5 -> переключение idx и обнуление, epoch через max(.,0), expected `>>24`, сброс эпохи, min 0xfffff, min с cwnd, u16 store), is_next_cycle_phase (все три ветки gain, packets_in_net_at_edt с tso_segs_goal = min(max(rate>>shift ∧ 65307 / mss, 1|2), 127)), advance_cycle_phase, check_full_bw_reached (×2, cnt==3), check_drain, reset_probe_bw_mode (`get_random_u32_below(7)` через u8-отбраковку <4, затем +1 фазы: (8-m)&7 — проверено численно, наш код идентичен), update_min_rtt (окно 2500·f/10, filter_expired, предикат обновления — наш порядок веток другой, предикат тот же, VERIFIED), PROBE_RTT (app_limited байтом, done_stamp+50, round_done, восстановление cwnd + WARN, reset_mode), update_gains (таблица переходов прочитана из .rodata стока: 0->739/739, 1->88/739, 2->таблица[320,192,256×6]/512 или 256/512 при lt_use_bw, 3->256/256, иначе WARN_ONCE), set_pacing_rate (gain·bw·mss>>8·990000>>24·pace_factor, min с max_pacing_rate, init-ветка srtt>>3), set_cwnd (losses, Recovery-переходы, packet_conservation, bdp со srtt при enable_opt+факторах, aggregation 100000, quantization, full_bw/`delivered<10`, max 4, clamp + WARN, PROBE_RTT-фактор /10 без WARN — компилятор доказывает >0). Остаток: перенос init-блока, база адресации, инвертированные ветки, `ldar` вместо `ldr` для sk_pacing_rate только в debug-трассе (TRACE). |
| **matt_inet_connect** | **CODEGEN** | VERIFIED (оба дизассемблера): единственное содержательное отличие — сток берёт `sock_intr_errno` от ИСХОДНОГО timeo (x25), наш — от остатка после `wait_woken`. Эквивалентно: `schedule_timeout(MAX_SCHEDULE_TIMEOUT)` возвращает MAX, конечный timeo никогда не становится MAX (INFERRED по семантике ядра 6.6). Неблокирующая ветка, ожидание, сигнальные биты TIF 0/6, ветка CLOSE + `sk_err` xchg / -ECONNABORTED + disconnect — одинаковы. |
| **matt_raw_skb_dump** | **TRACE-ONLY**, INFERRED | VERIFIED: те же вызовы (4×hex_dump_to_buffer с аргументами 16/1/буфер 131/false, 8×matt_trace_msg, preempt), единственные не-стековые записи — счётчики preempt/pagefault (kmap_atomic), буфер на стеке x29-0x98 одинаковый; остаток — слоты стека и режимы адресации. Арифметика длин сверена только на уровне C (skb_dump), не инструкция в инструкцию. |

Блокировки/рефкаунты/free: повторный прогон `calls.py` на моих копиях (VERIFIED): отличия только инлайнинг (bbr_init, init_module, inputq/outq_init, inq_free, ping_timer), +2 call-site трасс в rcv_data и ±acquire-загрузки (inet_poll, sendmsg_locked, write_space, mibbr_main) — совпадает со STATUS. Ни одной разницы sock_hold/put/sk_free/lock/atomic/brk.

## 4. Кто создаёт сокеты IPPROTO_MATT (168)

- VERIFIED (сток): matt.ko экспортирует только `matt_trace_msg`, `matt_register_ulp`, `matt_unregister_ulp`; `.modinfo` без `parm=` (тюнинг только через sysctl `net/matt/*`), `depends=` пусто.
- VERIFIED: ни один из остальных 925 `.ko` в `<work-dir>` (system_dlkm, vendor_dlkm, vendor_ramdisk) не импортирует символы `matt_*` (`llvm-nm -u`; контроль: 478 модулей импортируют kfree) и не содержит строк `matt_register_ulp`/`matt_trace_msg`; в `vendor_dlkm.modules.dep` у matt нет зависимостей и зависимых; matt в `vendor_dlkm.modules.load` строка 284.
- VERIFIED (импорты/код): сам модуль создаёт только UDP-туннельные сокеты (`udp_sock_create4/6`, при bind/connect matt-сокета) и регистрирует `inet_protosw` SOCK_STREAM/168 для v4/v6, `proto_register` MATT/MATTv6, inet/inet6 offload 168 (только gso_segment). Своих сокетов 168 он не открывает.
- Следовательно сокеты 168 создаются только через `socket(AF_INET[6], SOCK_STREAM, 168)` из userspace (или из модуля вне образа). **Userspace не проверялся** (по условию задачи). Подсказка для поиска (INFERRED): в SELinux такой сокет — класс `rawip_socket` (SOCK_STREAM с не-TCP протоколом), искать `allow … rawip_socket create` в vendor/system sepolicy и бинарники/rc со строками `net/matt`, `matt/enable`. Кто ставит `enable=1` — неизвестно; дефолт 0 (VERIFIED .bss стока).

## 5. Поведение при enable=0 и чек-лист

### Воспроизведено ли поведение стока при enable=0
Да, на уровне последовательности вызовов и ветвлений (VERIFIED), но ни разу не исполнялось:
- **init_module**: тот же порядок — kprobe на `kallsyms_lookup_name` -> таблица из 40 kallsyms-символов (имена сверены с `.rela.data.symbol_table` стока, включая баг S3: слот `xm_inet_unregister_protosw` = "inet6_unregister_protosw"), hash-таблицы, `alloc_workqueue` (при ошибке return 0 — S4, VERIFIED), percpu_counter, proto_register ×2, protosw ×2, offload ×2 с откатами, sysctl (21 запись), `matt_tasklet_init`.
- **Параметры**: VERIFIED значения в .data/.bss стока = matt_params.c (debug=0x1e, enable=0, inet_enable=0, limit_output=0xffff0, defer_max=64, bbr_enable_opt=1, srtt_factor=5, mrtt_factor=10, pace_factor=0, no_pacing=1).
- **enable=0**: единственный шлюз — первая проверка в `matt_init_sock`; сток и наш: трасса уровня 2 (line 963, при debug=0x1e печатается pr_err) и `return -1` (-EPERM), затем ядро зовёт `sk_common_release` -> destroy/unhash на неинициализированном сокете (S5: безопасно только при init_on_alloc). Других путей, управляемых enable, нет; без сокетов 168 data-path недостижим (offload 168 и GRO срабатывают только для skb/сокетов matt — INFERRED).
- **rmmod**: тот же порядок, что в стоке, включая S3 (IPv4 protosw снимается `inet6_unregister_protosw` под inetsw6_lock) и отсутствие `tasklet_kill` (сток так же). Никогда не выгружать.
- Не бит-в-бит: binding/kCFI-преамбула write_xmit, релокации, номера строк трасс (lto=none-артефакты и косметика).

### Дефекты
| id | важность | суть | предложение |
|---|---|---|---|
| D1 | LOW | S1 при отрицательном sacks_size уходит в `return` вместо стоковой записи `sack_used=0, highest=0, first` | условие `num > 0 && …` |
| D2 | COSMETIC | `__LINE__` трасс расходится в 8 функциях (matt_rcv 1693/1709 -> 1695/1711, write_space 925->927, inet_poll 453->454, reo_timer 119->120, stats_timer 389/399->396/408, inet6_udp_conf_init 101->102, dump_skb 175/191->176/193; rcv_data +333/348, ping_timer -53 — инлайнинг) | `#line N-(k-1)` для k-строчных вызовов; не влияет на поведение |
| D3 | INFO | set_bind_port, вторая трасса 1253: сток передаёт неинициализированный x5 (%p), наш — udp-сокет | оставить (только текст трассы) |
| D4 | INFO (стоковый баг, воспроизведён) | `matt_udp_rcv`: при неудаче `__pskb_pull_tail` skb не освобождается (утечка при OOM) | оставить как в стоке или `kfree_skb` (ещё одно отклонение) |
| D5 | MEDIUM (риск окружения) | 40 функций ядра берутся через kallsyms без CRC; при другом прототипе в ядре 6.6.144 — kCFI-паника на первом косвенном вызове (`brk #0x8228`), при отсутствии символа — init -ENOENT (безопасно) | прогнать kcfi_xm.py против vmlinux именно того ядра, которое будет грузиться (пункт 3 ниже) |
| D6 | PROCESS | загружаемого Kleaf .ko сейчас нет; проверенный объект — ThinLTO-эмуляция | пункт 1 ниже |

### Упорядоченный чек-лист до "можно считать загружаемым"
1. Собрать Kleaf `.ko` из текущих исходников (md5 7 файлов выше), записать md5 .ko и всех исходников; на ЭТОМ файле повторить compare_ko_v2, preamble_cmp, calls.py (сейчас .ko нет — D6).
2. Решения по отклонениям: S1 оставить с правкой D1; S2 оставить; S3–S6 принять как стоковые (рекомендация: S3 не трогать, но никогда не делать rmmod), D4/D5-хардненинг mgmt-полей — по желанию.
3. Проверка против целевого ядра 6.6.144 (тот Image, что будет загружен): все 40 kallsyms-имён существуют; kCFI typeid всех xm_*-целей совпадают с call-site модуля (D5); imports+CRC против нашего Module.symvers; `CONFIG_KPROBES=y` (иначе insmod откажет — безопасно); `CONFIG_INIT_ON_ALLOC_DEFAULT_ON=y` и нет `init_on_alloc=0` в /proc/cmdline (S5).
4. Независимое ревью — это (G15d-дифф + 5 тел): поведенческих расхождений не найдено; письменно принять остаток: raw_skb_dump (INFERRED, только трасса), inet_connect-эквивалентность через семантику schedule_timeout, косметика D2.
5. Найти пользователя сокетов 168 / того, кто пишет `net.matt.enable` (userspace стока, sepolicy rawip_socket). Если в нашем ROM его нет — модуль при enable=0 инертен, кроме init/exit.
6. Первая загрузка только на тестовом телефоне, глобальная OS4.0.0.7, наше ядро, enable=0: insmod, dmesg чист, 21 запись в /proc/sys/net/matt, lsmod стабилен сутки; затем один контрольный `socket(AF_INET, SOCK_STREAM, 168)` -> ожидать -EPERM и одну строку "under management control" (проверяет S5-путь); лучше на KASAN-сборке. Без rmmod.
7. Только после 6: enable=1, debug=0, одно соединение с известным пиром, KASAN.
8. Пакет/ROM: matt остаётся EXCLUDED до подписания 1–7.

## Что не проверено
- Userspace (по условию). Сервер-пир Xiaomi и его формат PACK/NACK.
- matt_raw_skb_dump инструкция в инструкцию (только вызовы/записи/аргументы/C-уровень).
- Эквивалентность нашего скомпилированного C нашему же C (доверие компилятору); пять тел сверялись "сток против C" + маскированный diff "сток против нашей сборки".
- kCFI/прототипы на ядре 6.6.144 и его конфиг (D5, пункт 3).
- Ничего не исполнялось.
