# Независимое ревью G30e (quickchg regulation, фикс порядка в pmic_single_cp_charging, лифтер sx.py)

COMMUNITY-ONLY, DO NOT LOAD. Это не заявление о безопасности батареи: сравнивается только эквивалентность поведения со стоком, на железе ничего не проверялось.
База: GLOBAL OS4.0.0.7. Сток: `<work-dir>` (md5 7b06f5e9..., совпадает с vendor_ramdisk и с `g30e/st/gl_*`).
Наша сборка: `<work-dir>` (04:58). Сборка до фикса: `<work-dir>` (= g22c, 02:56).
Только чтение: ничего не собиралось, не загружалось и не прошивалось, исходники не менялись, git не трогался. Scratch: `<work-dir>` (листинги `r?.txt` = сырой `llvm-objdump -dr`, `L?.txt` = lst2, `X?.txt` = sx.py, `alpha.py` + `ad.txt` = блочный alpha-diff).
Перегенерированные lst2-листинги совпадают по md5 с `work/re/g30e_tools/data/L_S.txt` и `L_B2.txt`, `off_info.txt` совпадает с моим прогоном btfflat на стоковом .ko.

## 1. Фикс в инлайнутом pmic_single_cp_charging: VERIFIED

Сток: fn+0xcd4..0x10bc (абс. 0x8e64..0x924c), наша сборка: 0xc44..0xe54 + 0xfac..0x119c. Читал сырой листинг инструкция за инструкцией, без лифтера.

| место | сток | наша сборка | итог |
|---|---|---|---|
| вход | `ldr w8,[x19,#0xb8c]` (pmic_single_cp_chg, int) `cbz` -> выход | то же | EQUAL |
| ffc | `bl strategy_class_fg_get_fastcharge`, `cbz w0` | то же | EQUAL |
| ветка PPS | `cur_max(w23) < 9000` -> cancel; `adp_type[0x788] != 11` -> cancel | то же | EQUAL |
| 5 чтений | cp_get_bus_current(0,&ibus_cp), get_bus_curr(0,&ibus_buck), get_pack_vbat(0,&pvbat), get_chg_status(0,&st), fg_ops_get_voltage(&curr_vbat) | тот же порядок; слоты стека сопоставлены вызовам и аргументам лога вручную | EQUAL |
| лог 2928 | ffc: (ffc_flag), затем override(buck_vterm_voter[0x28], "single_cp_pmic", 1, 4650); PPS: лог с 0, без override | то же | EQUAL |
| cancel | vote(input_suspend[0x8],"wire_qc",1,1), override icl(0,0), fcc(0,0), vterm(0,0), лог 2918 | тот же порядок | EQUAL |
| лог 2934 | a3=ibus[0x864], a4=ibus_buck, a5=ibus_cp, a6=curr_vbat, a7=pvbat, [sp]=chg_status | то же | EQUAL |
| условие | `curr_vbat >= vbat_threshold[0xbb8]` (b.ge, signed) `\|\| u8 use_buck_flag[0xbbc]` | то же | EQUAL |
| Be14 / Bd14 | `strb #1 -> 0xbbc`, `str wzr -> 0xb0c`, затем `bl mca_vote(...,1,1)`, override icl(0,0), fcc(0,0) | тот же порядок (сторы до vote) | EQUAL (фикс работает) |
| ступени | thr[0xb90/94/98] < cur_max && ibus > ibus_thr[0xb9c/a0/a4] -> value[0xba8/ac/b0], иначе value[3][0xbb4]; `fcc == pmih_fcc` -> выход; icl = fcc/2 (cinc+asr), +500 если fcc != value[3]; `str fcc -> pmih_fcc` до vote(...,0,0); override icl(1,icl), fcc(1,fcc); set_restart_aicl(0,1); лог 2957 | идентичная последовательность | EQUAL |

* Сборка до фикса (g25b): `bl mca_vote`, потом `strb use_buck_flag`, `str pmih_fcc`, потом override. Сторы стояли после vote, но до override. Аудит пишет «после всего», это неточно, но разница реальная. Между g25b и g30e alpha-diff даёт ровно одно изменение, этот блок. VERIFIED.
* Остальные отличия в регионе только регистровые и слотовые: перезагрузка w25 из -0x3c против -0x34, обнуление chg_status до или после вызова get_fastcharge (адрес ещё не передан), раскладка блоков. Поведения они не меняют. VERIFIED.
* Смещения подтверждены стоковым BTF: 0xb8c pmic_single_cp_chg, 0xbb8 vbat_threshold, 0xbbc use_buck_flag (1 байт, bool), 0xb0c pmih_fcc, 0xb90 cur_max_threshold[3], 0xb9c ibus_threshold[3], 0xba8 pmih_fcc_value[4], 0x864 proc_data.ibus, 0x788 adp_type, voters 0x8/0x10/0x20/0x28. Источник: btfflat, перепрогнан. VERIFIED.
* Потребители в стоке (поиск по всему .ko): use_buck_flag читается только в regulation (0x8f9c, это и есть условие выше). Пишется в Be14 (1) и в stop_charging (0x6274, 0). pmih_fcc читается только в regulation (0x91a8 в ступенях, 0x94c8 в open_buck). Пишется в regulation (0x8e28, 0x8fc0, 0x91dc, 0x94f0) и в stop_charging (0x5f5c). VERIFIED.
* Голоса 0x8/0x10/0x20/0x28 найдены через mca_find_votable, то есть принадлежат чужим модулям. Свои votable quickchg создаёт по адресам 0x2f8..0x350 (callbacks chg_disable/chg_en), и в этом блоке за них не голосуют. Поэтому синхронный callback этих голосов прочитать флаги quickchg не может. Перестановка значима только при гонке с конкурентным stop_charging (process_event, ovp_monitor_work, sysfs_store, voter_cb; mutex берётся только в части веток process_event). В старом порядке stop_charging, попавший между vote и сторами, оставлял use_buck_flag=1 до следующего stop. INFERRED (buckchg-callbacks не читались).

## 2. Перепроверка 6 самых рискованных секций (сток против исходника, без лифтера)

| # | секция | вердикт |
|---|---|---|
| 5 | channel, thermal, max_adap_ibat, pps100w, temp_max, ratio*max_adp_curr, sw_ocp, bypass, cutoff_priority, third-party PPS | EQUAL. Все min/max знаковые (csel lt/gt). Порядок вызова pps_100w и проверки adp_type совпадает. thermal_cur[cur_work_cp==2] через указатель; channel 0x80c/0x808; ccmp для 11000/pwr_boost; w25 = volt_para[size-1].current_min. VERIFIED |
| 6 | base/flip: get_base_curr, set_flip_cur, get_base_limit_curr | **DIFFERS**, подробности ниже. Всё остальное EQUAL: (stage+1)/2 через csinc, `stage > 2*(size-1)`, -30 мВ, last_flip до loadsw, аргументы лога 1081, счётчик и runSwOcp, `ibat < base-150`, max(result,0), `result < 1 ? limit : min`. Статики .data+0x350/0x354 = -1 и в стоке, и у нас. VERIFIED |
| 10 | comp_extra, CC/CV final_vstep, vstep_ex | EQUAL. `>`, `<`, `>=` совпадают; `ratio*abs < 20` -> -20; max(.., -100); `<< (anti != 0)`; 8 групп с шагом 0x18; stage записывается до проверки abs; `dv > 0`, `stage >= 0`; boost_time_thr сравнивается беззнаково (b.ls); min(final, vstep_ex). VERIFIED |
| 11 | cyclecount>799 clamp, boost_done (2000/1000 беззнаково), hwid(+4==1, +0==7, final>=80, boost<=1999) | EQUAL, VERIFIED |
| 13 | en_buck_parallel_chg, inline is_en_buck_hwid | EQUAL. Множество {1, 0x10000, 0x10002, 0x10003, 0x100000, 0x100001, 0x110000} при +0==3 восстановлено из сравнений. NULL -> лог 2979 и false. `pv-11 <=u 1` (b.hi) берётся по hwid уровня regulation. Проверка `final>200` есть только в ветке полных значений. VERIFIED |
| 14 | open_buck_charging | EQUAL. 6 чтений в порядке исходника; `pvbat >= vterm-30` -> stop_pmic; аргументы лога 2874; PD_VERIFY(12) && DUAL(2) && !stop_pmic; ступени 11000/5200 -> 3000, 8000/4200 -> 2000, 5000/2700 -> 1000, иначе 400 (у нас ccmp вместо 5001, эквивалентно); `cur_max > ibat && fcc != pmih_fcc`; стор pmih_fcc до vote; icl = fcc/2+300; в else-ветке pmih_fcc=0 до vote. VERIFIED |

**DIFFERS (секция 6, get_base_limit_curr, таблица SW-OCP).** Сток берёт уменьшение из `.rodata+0x6348` (`adrp/add .rodata+0x6348; add x11,x12,x11,lsl #3; ldr w11,[x11,#4]`). Байты стока: `{1000,900},{500,600},{300,**350**},{100,150}` (0x635c = `5e010000`). Наш исходник (mca_strategy_quickchg.c:2506..2508) и `.rodata.cst32` нашей сборки: `{300,**300**}`. Порог 300 у нас и в стоке совпадает (`cmp w11,#0x12c`). Эффект: при support_base_flip и `300 < ibat_base - base_curr_max <= 500` два цикла подряд сток снижает cur_max на 350 мА, наш код на 300 мА. Наш запрос выходит на 50 мА выше стока. VERIFIED (дамп `.rodata` стока и `.rodata.cst32` нашей сборки).
Нужная правка: `{ 300, 350 }` плюс пересборка. Комментарий G13u («.rodata+0x6280») и строка 67 STATUS.md (`{300,300}`) взяты не из GLOBAL OS4. Активна ли ветка на dada, зависит от DT `support-base-flip`; DT не проверялся, INFERRED: на не-раскладушке скорее всего выключено.

Мелочи, которые аудит не назвал. Все CODEGEN, наблюдаемы только при гонке, INFERRED:
* секция 9: сток пишет `ibus_compensation=0` до лога 3253, у нас после (callee лога данные не читает);
* vstep_ex: сток пишет last_vbat (приватный static) до лога 3181, у нас после;
* ratio [0x7fc] у нас читается 1 раз, в стоке 4 раза, в разное время относительно вызовов логов.

## 3. Ревью лифтера `work/re/g30e_tools/sx.py` (и atoms.py, effects.py)

Проверка на трёх известных различиях:
1. **Порядок в Be14** (g25b против g30e и стока). sx.py показывает его: в XP блок Bd14 = `CALL mca_vote`, потом `ST use_buck_flag`, `ST pmih_fcc`. atoms.py (147=147) и effects.py (0 различающихся ключей) его **не видят**: они работают с мультимножествами и не учитывают порядок. VERIFIED.
2. **quick_wireless select_max_ibat до G22c** (g20d против стока, cinc вместо csinc). В тексте разница есть, но едва заметна: `t18 = (parall_cur_stage+5 < 0 ? parall_cur_stage+6 : parall_cur_stage+5)` против `t24 = (...+4 < 0 ? ...+5 : ...+4)`. Номера временных переменных сдвинуты, идиома asr после выноса во временную переменную не распознаётся. atoms.py ловит только побочный атом (csel против IF). Найти можно, если читать построчно. VERIFIED.
3. **Таблица OCP 350 против 300** (наша сборка против стока). sx.py её **не показывает**: в обоих листингах `*(((&.rodata+0x6348)+(φ<<3))+0x4)` против `.rodata.cst32`, значения не разыменовываются. Аудит опирался на комментарий исходника. VERIFIED.

Найденные дефекты и слепые зоны:
* **Ошибка именования статиков:** `nm = '@' + (SYMS.get(0) or cmt)` (sx.py:448). В сборке с function-sections по адресу 0 лежит много символов, побеждает последний, update_batt_para. Поэтому все загрузки .bss/.data с reloc-комментарием в нашем листинге называются `@update_batt_para`: 90 мест в X_B2.txt regulation, 10 в monitor_work. Какой static читается, на нашей стороне не видно. effects.py ещё и нормализует это имя в `@static`. VERIFIED.
* **Выходные параметры через стек:** после вызова слот сохраняет значение, записанное до вызова. Ключевое условие фикса у нас выглядит как `IF (0 >= info.vbat_threshold)`, в стоке `φ270 >= ...`. Аргументы лога 2934 превращаются в 0 или φ в разных позициях. Перестановку pvbat и curr_vbat (оба обнулены) лифтер не покажет. VERIFIED.
* **Суффикс смещения и арифметика неразличимы:** `field+4` означает и загрузку элемента [1], и `field[0]+4`. Пример: `parall_cur_stage+5` = parall_cur_stage[1]+1. Ошибки индекса, как в истории с stage/2, так прячутся. VERIFIED.
* Не показываются ширина и знак загрузок: ldr w/x/ldrsw без тега, sxtw/uxtw выброшены, ширина cmp w против x не видна. В циклах нет φ, обратные дуги игнорируются. Для blr печатается имя регистра, а не указатель на функцию. При сторе в глобал через adrp-базу теряется смещение (sx.py:466). У umulh и при исключениях в step (csel или ccmp без флагов) регистр назначения остаётся устаревшим. В atoms.py временные переменные и φ схлопываются в `T`/`PHI`, а effects.py выкидывает неконстантные сторы и аргументы. INFERRED (из кода).
* Вывод недетерминирован между версиями: мой прогон на том же L_S.txt дал другой порядок φ, чем сохранённый X_S.txt. Косметика.
* lst2: свёртка adrp+add не проверяет регистры. В regulation (сток и наша сборка) случаев с разным dst 0. VERIFIED.

Как пользоваться: только как подсказкой при чтении и никогда как доказательством. atoms.py и effects.py годятся лишь для поиска мест, «равно» на них ничего не значит. Обязательный порядок:
(а) блочный alpha-diff сырых листингов (`alpha.py`: регистры переименовываются по первому появлению в блоке). Для regulation он даёт 809 из 1234 строк равными, 150 ханков;
(б) каждый ханк и каждый блок с CALL/ST сверять с сырым листингом, вручную сопоставляя слоты стека и вызовы;
(в) отдельным шагом дампить все не строковые `.rodata`/`.rodata.cst*` и начальные значения `.data`-статиков в стоке и у нас и сравнивать значения;
(г) починить sx.py: убрать SYMS.get(0), после вызова портить слоты, адрес которых передан в вызов (`out:<callee>`), разыменовывать `.rodata`, тегировать ширину и знак, писать `field[+4]` отдельно от `+4`, ставить метки LOOP и φ на заголовки циклов.

## 4. Вывод

* Фикс порядка в pmic_single_cp_charging корректен. Регион инструкция в инструкцию эквивалентен стоку с точностью до регистров, спиллов и раскладки. VERIFIED.
* Утверждение G30e «все 16 секций EQUAL, кроме одного порядка» **неверно**: в секции 6 значение таблицы SW-OCP `{300,350}` в стоке против `{300,300}` у нас, то есть DIFFERS. Нужна правка одной константы и пересборка. VERIFIED.
* Остальные 5 перепроверенных секций (5, 10, 11, 13, 14) EQUAL. VERIFIED.
* Утверждения «EQUAL» по секциям, которые я не перечитывал (1-4, 7-9, 12, 16), и по экрану других функций опираются на тот же лифтер. Из-за его слепых зон (константы в rodata, выходные параметры через стек, имена статиков в нашей сборке) они не доказаны. INFERRED.
