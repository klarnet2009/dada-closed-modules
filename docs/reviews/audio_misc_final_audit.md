# G40g: audio and misc leftovers on the GLOBAL OS4.0.0.7 stock (20.10.2026)

Reference: `<work-dir>`. Method: raw `llvm-objdump -dr` pairing (dfn/dfc), multiset diff (opb.py via kob.sh), message strings (strdiff2.sh), real Kleaf builds (kb.sh = Bazel + compare_ko_v2 + preamble_cmp), tbl_audit3.py. Tools: `work/re/src/g40g_tools` (copies of g15c_tools with scratch <work-dir>), `work/re/src/stubs_g40g`. Nothing flashed or loaded, no git; bazel-out deleted after each compare (kbs.sh), scratch <work-dir> only.

## 1. Builds (real Kleaf, GKI 6.6.144)
| module | md5 of built .ko | function set | byte-identical | kCFI | other |
|---|---|---|---|---|---|
| sia91xx_dlkm | e2f71dbc7520b6f2953809488ac34221 | 104 = 104 | 70/104 (was 66) | 92/92 | strings equal to stock; imports/exports/modinfo PASS; objects (+g3m_rst_off_level), anon-data, altinstr, depends order FAIL |
| tfa98xx_dlkm | acd6c5dc5f9174e03b20973ddc3a33d1 | 389 = 389 | 289/389 (was 281) | 375/375 | objects/imports/exports/modinfo PASS; anon-data, bug-table (__LINE__), altinstr FAIL |
| aw882xx_dlkm | abb4dda60a5c3cb3731cce8139d5f008 | 267 = 267 | 251/267 | 235/235 | all tables, objects, imports PASS; only function bytes/relocs differ |
| mi_memory | (no source change; g7d/fixed/mi_memory.ko cc6a3c3d9b3d1e8de9b0ddc6edd4c58b) | - | - | - | see 3 |
First lines of the three STATUS.md files unchanged (COMPILES-only, DO NOT LOAD); a G40g section was added to each. None of these three modules binds on dada (global DTB: only cirrus,cs35l43 + qcom wsa883x/wsa884x/wcd939x).

## 2. BEHAVIOUR differences found and fixed (all from the raw stock disassembly)
1. **aw882xx awrw_show**: passed `struct aw882xx *` as the `struct i2c_client *` of aw882xx_i2c_reads (wrong i2c addr/adapter, memory fault on reading the sysfs node); stock passes aw882xx->i2c; kmalloc(GFP_KERNEL) not kzalloc. (The G15c verdict "no BEHAVIOUR difference" for aw882xx was wrong.)
2. **sia91xx SIPA_REG_PROC layout**: stock reads `delay` at +0x10 (all four actions of sipa_regmap_proc_1_reg), the template had it at +0xc: every register-list delay of sipa.bin was read from the wrong word.
3. **sia91xx SIPA_PARAM_WRITEABLE size**: stock 0x88 (fw.ch_en at +0x90), ours 0x98 (cal_spk entry had a 5th word). sipa_param_read_chip_cfg / sipa_param_read_extra_cfg are byte-identical now.
4. **sia91xx component probe**: stock NULL checks ("component is NULL", "si_pa is NULL", -EINVAL); scene_set `%u ... channel` message with a u32 value; LOG_FLAG of sipa_parameter.c = "sipa_param ".
5. **tfa98xx dbgfs MTPEX**: the stock accepts only 0 (writes tfa_dev_mtp_set(EX,0)), ours accepted 1 (set the MTP-exec bit); otc/mtpex get/set print the stock errors and return -EIO/-EINVAL like the stock; the four functions are byte-identical.
6. **tfa98xx start_set** starts with the calibration profile (tfaContGetCalProfile + warning), not tfa98xx->profile; dsp_init "[goodix] init_count" print; set_vstep "no_start" print; 11 info messages that the stock does not contain removed.

## 3. Resolved as CODEGEN / equal (no source change)
- sia91xx sipa_compatible_chips_adapt jump table (stock bytes `05 00 04 61x7 08`: type 14->rec0, 15->rec1, 16->rec2, 24->rec3, default distinguish_chip_type): same type->record mapping as ours. sipa_regmap_proc_1_reg table `00 20 2c 58` (READ, WRITE, CHECK, PAD) equals ours (write-vs-read was only the heuristic).
- sipa_suspend: stock second 80-byte table `[6,7,8,6,6,11,12,...]` is the identity for the mask types (0x3b919c0 = 0xee467 over type-6), used only for `== 0x14`; the first table equals our Lswitch.table.
- sipa_i2c_probe (17): stock inlines mievent_report. sipa_probe (34): inlined distinguish_chip_type, 0x3a919c0 irq mask + type-20 boost path == our 0x3b919c0 + `20 == type`, dead `== 0xc8` test, devm_pinctrl_put tail.
- tfa98xx i2c_probe (6): ordering/inlining only; `tfa_cf_enabled==0` string is common.
- **mi_memory cst16**: the 48-byte block `3cc10000 00000002 00000200 ...` of ours is byte-identical to stock .rodata+0x1008..0x1038 (hr_show and scsi_xbstor_hr load the 8-byte words at +0x1008 / +0x1028 in the stock too); the G33a NO-COUNTERPART was a chunk-boundary artifact (the stock block starts inside the string "INTEL\0"). No change.
- aw882xx: 4 stock-only strings are __func__ names of inlined functions (log text only); remaining differing functions are local-symbol call naming (ThinLTO), ldrsw/ldr, loop shape.

## 4. Other modules of work/re/src (not covered in the last days)
tables (tbl_audit3.py, GLOBAL/OS4 stock vs our latest build): dcvs_main (g1al), sched_entity_freq (g1al), nxp-nci (a17C): named objects 0 differing, anon chunks all explained (only the string pools), nothing unexplained. Modules with no non-stock build left on the server (binder_gki, hl7603, mii, nuvolta_1652, sc8581_sy6541, sched-penalty: STATUS EQUIVALENT, our build equals the stock file) were not table-audited here. cameralog, dump_binder, fault_report, mi_damon, miscore, mist, rss_monitor, process_monitor, minet, mibbr, rsmc_driver are in tables_audit_all.md (G33a, 0 DIFF, unexplained 0-1) and were not re-checked.

## 5. What is left (not done within the budget)
tfa98xx: tfa_dev_set_state (10 words: stock 0xcb28..0xcb6c prints and returns 7 on a non-zero result of [tfa+0xa0](tfa,&val), ours returns err without the per-iteration print), tfa9894_specific (26), tfaContWriteFilesProf (23, create_dsp_buffer_msg inlined), tfa98xx_container_loaded (23); extra error/debug messages that the stock lacks (tfa_dsp.c / tfa_container.c / tfa98xx.c: "Calibration failed!", "invalid version string.", kmem alloc, "Invalid Codec master mode", dsp_msg(_read) error, "Writing cmd", "no secondary Re25"). sia91xx: sipa_cmd_show (51), sipa_resume, sipa_pending_actions, sipa_regmap_check_trimming and 14 other 8-12-word functions only read from the multiset lists. aw882xx: awrw_store (10 words), the 16 codegen functions not compared instruction by instruction. All three stay COMPILES-only / DO NOT LOAD.
