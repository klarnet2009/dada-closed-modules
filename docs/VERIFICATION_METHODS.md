# Verification methods

How a rebuilt module is compared with the stock one, what each method can and cannot see, and the tools in `tools/`. All methods are static. Nothing was run on a device.

The stock modules are built from a Qualcomm/Xiaomi tree on kernel 6.6.57 (HyperOS 4.0) or 6.6.118; ours are built in a GKI 6.6.144 tree. That alone produces differences that are not errors in the source (inlined `readl()`, header drift); each method below has to be read with that in mind.

## 1. `compare_ko.py` (first generation, with holes)

Compares per function the bytes and the resolved relocation targets, the global objects by name and size, exports and imports with CRCs, and the modinfo strings. Fast and useful as a first filter. Its blind spots are listed in `docs/STATUS_AND_LIMITS.md`: no data content, no kCFI hash, no bug table, no `depends=`, relocations only for functions with identical bytes. An "EQUIVALENT" from this tool alone is not enough to load a module.

## 2. `compare_ko_v2.py` (strict) and `preamble_cmp.py` (kCFI)

```
python3 -I tools/compare_ko_v2.py stock.ko built.ko [-v]
python3 -I tools/preamble_cmp.py  stock.ko built.ko
```

`compare_ko_v2.py` prints PASS/FAIL with counts for: function set; function bytes; function relocations (all functions, targets resolved to function plus offset, strings by content, absolute symbols that carry a kCFI type id); function binding (global/local); kCFI preamble; code that belongs to no function; objects (content of every named object, global and static, relocation-aware); anonymous data (by content, not as a C string); bug table, bug-table lines; jump table and other tables; exports and imports with CRC; modinfo; build metadata; `depends=`; vermagic (by flags); notes. The verdict is STRICT-EQUIVALENT only if everything passes. Typical accepted differences are listed in `docs/STATUS_AND_LIMITS.md`. It runs in a few seconds on a 20 MB module and uses only the standard library. `tools/review/v2_selftest.py` corrupts a module in 17 ways and checks that each is caught.

`preamble_cmp.py` compares the kCFI hash word in front of every global or address-taken function with the stock (promoted static functions are excluded because the stock has no preamble there and our "hidden global" workaround emits one, which is harmless). `verify_pkg.py` adds: CRC of every import against `Module.symvers` or another module's exports; no unsigned module that exports a protected symbol; modinfo `depends=` against the stock; vermagic flags; simulation of the load order from the modules.load lists; and a hook audit: each module that registers a tracepoint/vendor-hook probe is checked against the kCFI hash the `__traceiter_*` function in `vmlinux` expects, so that a wrong probe prototype is found before it traps.

### Why the kCFI hash matters

With `CONFIG_CFI_CLANG`, every address-taken function is preceded by a 32-bit hash of its type, and every indirect call checks it. The word sits 4 bytes before the symbol, outside the function body, so a per-function byte comparison never sees it. Example from this project: a `binder_gki` probe built with a 4-argument prototype where the kernel's hook has 5 arguments had hash `0x862c7abc`, the kernel expects `0x0c6377dc`; the function passed the old tool and would have trapped on the first binder buffer allocation.

## 3. Multiset diff (for modules that are not byte-identical)

Per function, compare the multiset of instructions with the registers masked, so that register allocation and basic-block order do not matter but loads, stores, calls, constants and trap instructions do. What it proves and what it does not is in `docs/STATUS_AND_LIMITS.md` section 3. Tools: `tools/multiset/` (`ob.sh`, `opb.py`, `om.sh`, `opm.py`, `opd2.py`, `unity.py`/`u.sh`: a "unity" build that makes the functions local in the stock static the same way), `tools/review/semfp.py` (call fingerprints with arguments, constants, field accesses and branch conditions), `tools/matt_review/` (register-agnostic checks, event comparison, call and atomic comparison per function, kCFI both ways against `vmlinux`). The results over the package and the PARTIAL modules are in `docs/reviews/multiset_audit.md` and `multiset_audit2.md` (English translations of the original working reports). `tools/review3/symcmp.py` is the symbolic comparison used by the third charging review and the Wi-Fi review: it compares call arguments, memory stores, canonical comparisons (signedness, operand order, constants) and the conditions of each call and store. `tools/review4/symcmp4.py` extends it for the hidden-behaviour audit of the charging family: it canonicalises sign- and zero-extension forms and `bool` tests, treats mutually exclusive switch cases as equal guards, and reports per function the effects (calls, stores, stack data, atomics, traps) that exist only in the stock or only in ours **together with the conditions that guard each of them**, so that equal facts under an inverted branch are no longer accepted. A function is called "form only" only when all effects and their guards are equal. `tools/rounds/ldst.py` lists functions where the stock reads more global scalars than ours (the place where a constant may have replaced a variable). Per-round helper scripts are in `tools/rounds/` and `tools/charging/`; most of them were written for our own build server and contain placeholders where a path or host was.

Lesson learned: code written "by meaning" from a disassembly hides real runtime bugs that a byte-level comparison with masked registers shows at once (a double lock, wrong prototypes that trap, a loop the sanitizer replaced by `brk`). Run the multiset diff on every module before trusting it.

## 4. From a kCFI hash to the prototype

The kCFI type id is the low 32 bits of the xxHash64 (seed 0) of the Itanium-mangled name of the function type, `_ZTS` followed by the mangling of the function type. For example `void (struct page*, int)` is `_ZTSFvP4pageiE`. Two things matter when searching a prototype from a hash that was read out of the stock binary (the immediate of the `movk w17, ...` pair before an indirect call, or the word in front of the function):

- **Substitutions.** The Itanium mangler replaces a repeated type by `S_`, `S0_`, ... , so `int (task_struct*, task_struct*, void*, u8)` is `_ZTSFiP11task_structS0_PvhE`, not `..._P11task_struct`. Our first search script (`tools/kcfi/kcq.py`, `kcb.py`) did not apply substitutions, and every prototype with a repeated type was "not found". `tools/kcfi/kcs.c` does, and finds prototypes of up to four arguments in seconds.
- **Struct names count, struct bodies do not.** The hash depends on the struct *name* (a callback typed `struct waltgov_callback` and one typed `struct update_util_data` have different hashes), not on the members and not on the typedef name (the type is the canonical one; integer types are not normalised in these builds).

`tools/kcfi/xxh.py` is the hash in Python, `kb.sh`/`kbs.sh` are the build-compare-check loops. The `kcs2*.c` variants in `tools/rounds/` take the candidate name lists from a file (struct names read from the module BTF, up to four arguments, enum names guessed), which is how `clear_vip_task_type` (four arguments, `char *` third) and `get_vm_state` (`enum tee_state (*)(void)`, hash of `_ZTSF9tee_statevE`) were found; a sweep of `preamble_cmp` over fresh builds of all modules found the remaining kCFI mismatches (most of them harmless: the stock has no preamble where we emit one). **The earlier search without substitutions missed prototypes, so "prototype not found" in an older STATUS.md only means "not found with the old script".** Known open case: `process_monitor` `timeout_check` (hash `0x6a311a23`, struct tag unknown, no BTF in the module). This method fixed wrong prototypes in `mi_game`, `mi_low_lat_mem`, `cameramsger`, `dcvs_main` (an `s64` instead of `u64`: with the right prototype the export CRC then matched by itself and a forged CRC could be removed) and `xr_qi`.

It does **not** reproduce a genksyms export CRC: the kCFI hash ignores struct bodies and typedef names, genksyms does not. Forged CRCs that come from a struct layout of another tree stay forged (`docs/HACKS_AND_FORGED_CRCS.md`).

## 5. BTF: where struct layouts come from, and the limits

The stock modules carry a split `.BTF` section. It is the best source of struct layouts and real enum names we have:

- `tools/btf_dump.py <base_image> <module.ko> [--all]` prints C declarations of the module's functions and variables and the structs they reach. The module BTF refers to the base BTF embedded in the kernel image by type id, so a base image of the matching kernel is needed. `tools/patch_btf.py` patches the dumper to print every module-local struct, union and enum with `--all`.
- For OS4 modules the base type ids could not always be resolved locally; `tools/btf_dump_startid.py` takes the first module-local type id as an argument (141904 for the `mca_*` modules, 141631 for `minet`) so that local struct and enum member names are still available.
- The layouts gave exact offsets for `xiaomi_touch`, `mi_fp`, `nuvolta_1652`, `mca_strategy_fg_comp` and others and replaced guessing. For `matt`, 257 `_Static_assert` on field offsets were generated from the BTF (`tools/multiset/chk_off.py`).
- A trace header can be generated from one stock module (`tools/gen_trace.py`): fields from the `trace_event_fields_*` tables, `TP_printk` from `print_fmt_*`, the prototype from the `__traceiter_*` BTF.

Limits:

- BTF has the types, not the code: no initial values of variables, no inlined functions, no function attributes (`noinline`, `always_inline`), no macros, no unused enum values. Function prototypes exist only for functions that survived as symbols.
- Struct layouts are those of the **stock tree** (6.6.57). Our build uses 6.6.144 headers; kernel structs that differ between the two (and a few vendor structs) make export CRCs and some offsets disagree. This is the source of most remaining forged CRCs.
- BTF does not tell the meaning of a field. Names of vendor structs are sometimes missing or generic; field names for audio-amplifier and `matt` structures were often guessed from use.
- With the base type ids unresolved, base types (for example `u64` vs `unsigned long`) can only be guessed from the instructions.
- The `.BTF` section itself is not compared by `compare_ko_v2.py`.

## 5b. Kernel drift between the two trees

The stock modules were built against 6.6.57 headers, ours against 6.6.144 (the package kernel). Differences that follow from this and are not source errors: inlined versus out-of-line `readl`/`writel` (`log_*_mmio`), `netif_tx_stop_queue` (6.6.144 writes `trans_start` and a barrier), `poll_wait` (extra `dmb ish`), line numbers in the bug table for functions that come from kernel headers, and export CRCs of structs that changed. When a difference can be explained this way, a force-included header can restore the old body (`ported/qca_cld3_xiaomi_ext/xm_compat_6657.h` for Wi-Fi). Such a header is a **build-environment emulation, not Xiaomi code**; it changes what the module does only the way the 6.6.57 kernel would.

## 5c. Enum names and values from BTF

Stub headers written from memory carried wrong enumerator names (and in an old case a wrong order). The module BTF has the real enums; a systematic audit (`docs/reviews/charging_enum_audit.md`) dumped all enums of all stock charging modules and compared names and values with every stub tree and every finished module. A wrong name with a right value does not show in any binary comparison.

## 5d. Switches, jump tables and the store emulator

Two lessons from the last rounds, with their tools:

- **Case values are not in the compare instructions.** LLVM lowers a sparse `switch` to a binary search, and the constants that appear in the compare instructions are the pivots of that search, not case values. In `aw882xx_dlkm` the chip ids `0x2031`, `0x2070` and `0x2115` were taken as supported chips for several rounds; they are pivots between the real cases `1852/2013/2032/2055/2071/2113/2116/2308`. Case values must be read from the jump table in `.rodata` (the index is `value - minimum`; byte tables map to a second level), from the dense compare chain, or from the string the case prints. The same applies to `process_event` switches of the charging modules, where the jump tables were decoded (`tools/charging/jt32.py`) and an event (`FG_OTA_PROCESS`) outside the compiled-in range of our switch was found that way. A kCFI hash is the other anchor: the prototype `int (*)(void)` of `is_support_pps_100w` was proved by the hash of the stock function, not guessed from the call site.
- **Compare the final state of the stores, not the order.** `tools/rounds/gx.py` is a small aarch64 emulator that runs over `objdump` text, follows the stock function for one input (for example a chip id), records every store into a structure and names the offsets from the compiler's record layout; `gd.sh` compares the final store state of the stock and of our build field by field, `--genc` writes the C stores of the stock block, and `mkinit.py` assembles the init function from them. It made `aw882xx_init` equal in store state for all eight chip ids and found real behaviour errors (a block written twice around a version read, a non-existent error message, fields written through the wrong pointer) that an instruction multiset could not.

## 6. Other methods used

- **Stub providers with exact export CRCs.** `tools/gen_stubs.py` generates one stub module per stock module whose exports have the right CRCs without knowing the prototypes: genksyms hashes the declaration with crc32, and a short struct tag is searched until the CRC matches. The module under test then links against the stubs.
- **GENKSYMS-only padding.** `tools/padcrc.py`/`crcpad.py` search a padding that exists only under `#ifdef __GENKSYMS__` (an enum value, attribute or struct member with a fitted identifier) to hit a given export CRC without changing the code or the ABI.
- **Package-level checks.** `docs/KERNEL_PACKAGE.md`.

## 7. Polarity-aware symbolic comparison (`tools/review5/`)

`symcmp4.py` compares calls, stores and the conditions that guard them, but keeps only the class of each compare, so an inverted or missing range check can still end as "same facts". `symcmp5.py` keeps the exact boolean path condition of every fact and of every merged value (a BDD over canonical compare atoms) and compares them between the stock and our function. Verdicts per function: `POLARITY-DIFF` (same compares, different path condition), `POLARITY-DIFF-ATOMS` (some compares exist on one side only: needs a read), `FACT-DIFF` (a call, store or return exists on one side only), `CLEAN-POL` (none of these), `INCONCLUSIVE` (BDD cap or timeout). `symcmp5b.py` is a wrapper with an ELF check and function lists, `s5digest.py` reduces the one-sided effects to kinds, `s5ms.py` compares relocation-target multisets and key instructions. A planted-bug self-test is in `tools/review5/selftest/` (8 condition bugs, all found; equivalent rewrites stay clean).

What it does **not** prove: `CLEAN-POL` is not equality (effect values other than the boolean algebra are compared as text, loops are approximated). Known false positives and limits are listed in `tools/review5/README.md`: `ret(x)` versus `ret(0)` after a zero test, loop-exit written two ways, `brk` taken as terminal, huge functions with different inlining giving hundreds of one-sided atoms, `ldapr` versus `ldar`. Several large functions of the charging family (for example the quick-charge `regulation`) hit the BDD cap and are **inconclusive**; they are listed as unproven in the module `STATUS.md`, not as equal.

Results: `docs/reviews/polarity_audit_charging.md` and `polarity_audit_all.md` (the latter is partly in Russian: `docs/reviews/untranslated/`).

## 8. What the tools missed (lessons of the last round, 20.10.2026)

- **`CLEAN-POL` and `CODEGEN` are not proof.** After the symbolic tools had called functions clean, a reader of the raw `llvm-objdump -dr` listings found real differences in them: an inlined branch whose stores were in another order (`symcmp5` is order-blind; a lifter shows it), a `batt_emerg` term of a condition, a caller task versus the current task in a hook (`symcmp5` blind spot), a quick-charge taper condition `a && (b || c)` written as `(a && b) || c`.
- **Constant tables.** `compare_ko_v2` takes an integer table for a C string when it starts with bytes that look like a string (`{1000, 900, ...}` starts with `e8 03 00 00`), and then only that prefix is compared: one row of a table of the quick-charge module differed unseen. `tools/rounds/tbl_audit3.py` compares named objects (sysfs and ops tables, device-id tables, writable initialisers, modes), every relocated anonymous pool word by word, jump tables (element size, number of entries, case to first call), `modinfo`/`parm`, jump and bug tables. It was run over 93 modules (`docs/reviews/tables_audit_all.md`).
- **Lifter tools.** `tools/charging/sx.py` lifts a listing to block pseudo-C. It does not dereference `.rodata` values, labels loads of statics wrongly, does not show a swapped output parameter and cannot distinguish `field+4` of two different bases: use it as a hint while reading, never as proof (`docs/reviews/g30e_review.md` section 3).
- **What found the differences in the end:** raw listings of stock and ours, a block alpha-diff (registers renamed inside a block, relocations normalised), reading only the differing hunks with the context of both sides, string tables compared including level prefixes, and call sequences with immediates. Tools only choose where to read.
- **Struct layout.** `tools/rounds/btfparse.py`, `layoutdiff.py`, `modbtf.py`, `curscan.py` compare the BTF of the two kernels and of the modules, and count offsets accessed from `current` in every module (`docs/reviews/struct_layout_audit.md`, `field_offset_sibling_audit.md`).
