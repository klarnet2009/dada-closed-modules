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

Per function, compare the multiset of instructions with the registers masked, so that register allocation and basic-block order do not matter but loads, stores, calls, constants and trap instructions do. What it proves and what it does not is in `docs/STATUS_AND_LIMITS.md` section 3. Tools: `tools/multiset/` (`ob.sh`, `opb.py`, `om.sh`, `opm.py`, `opd2.py`, `unity.py`/`u.sh`: a "unity" build that makes the functions local in the stock static the same way), `tools/review/semfp.py` (call fingerprints with arguments, constants, field accesses and branch conditions), `tools/matt_review/` (register-agnostic checks, event comparison, call and atomic comparison per function, kCFI both ways against `vmlinux`). The results over the package and the PARTIAL modules are in `docs/reviews/multiset_audit.md`.

Lesson learned: code written "by meaning" from a disassembly hides real runtime bugs that a byte-level comparison with masked registers shows at once (a double lock, wrong prototypes that trap, a loop the sanitizer replaced by `brk`). Run the multiset diff on every module before trusting it.

## 4. From a kCFI hash to the prototype

The kCFI type id is the low 32 bits of the xxHash64 (seed 0) of the Itanium-mangled name of the function type, `_ZTS` followed by the mangling of the function type. For example `void (struct page*, int)` is `_ZTSFvP4pageiE`. Two things matter when searching a prototype from a hash that was read out of the stock binary (the immediate of the `movk w17, ...` pair before an indirect call, or the word in front of the function):

- **Substitutions.** The Itanium mangler replaces a repeated type by `S_`, `S0_`, ... , so `int (task_struct*, task_struct*, void*, u8)` is `_ZTSFiP11task_structS0_PvhE`, not `..._P11task_struct`. Our first search script (`tools/kcfi/kcq.py`, `kcb.py`) did not apply substitutions, and every prototype with a repeated type was "not found". `tools/kcfi/kcs.c` does, and finds prototypes of up to four arguments in seconds.
- **Struct names count, struct bodies do not.** The hash depends on the struct *name* (a callback typed `struct waltgov_callback` and one typed `struct update_util_data` have different hashes), not on the members and not on the typedef name (the type is the canonical one; integer types are not normalised in these builds).

`tools/kcfi/xxh.py` is the hash in Python, `kb.sh`/`kbs.sh` are the build-compare-check loops. This method fixed wrong prototypes in `mi_game`, `mi_low_lat_mem`, `cameramsger`, `dcvs_main` (an `s64` instead of `u64`: with the right prototype the export CRC then matched by itself and a forged CRC could be removed) and `xr_qi`.

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

## 6. Other methods used

- **Stub providers with exact export CRCs.** `tools/gen_stubs.py` generates one stub module per stock module whose exports have the right CRCs without knowing the prototypes: genksyms hashes the declaration with crc32, and a short struct tag is searched until the CRC matches. The module under test then links against the stubs.
- **GENKSYMS-only padding.** `tools/padcrc.py`/`crcpad.py` search a padding that exists only under `#ifdef __GENKSYMS__` (an enum value, attribute or struct member with a fitted identifier) to hit a given export CRC without changing the code or the ABI.
- **Package-level checks.** `docs/KERNEL_PACKAGE.md`.
