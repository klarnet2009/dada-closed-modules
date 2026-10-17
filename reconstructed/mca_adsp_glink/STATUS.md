# mca_adsp_glink - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `mca_adsp_glink.ko` (md5 450e83ff..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/mca_adsp_glink`) + compare_ko.py (EQUIVALENT).

unchanged since HyperOS 3: no source change; real Kleaf build + compare_ko.py vs the OS4 stock: function set (13), bytes, relocations, global objects, exports+CRC (6), imports+CRC (21), modinfo all OK.

SAFETY: charging module, community-only source: never flash it and never put it into a ROM build.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# mca_adsp_glink - EQUIVALENT

`compare_ko.py`: function set (13), bytes, resolved relocations, global objects, exports+CRC (6), imports+CRC (21), modinfo: all OK.
Data objects (`mca_adsp_driver`, `mca_adsp_match_table`, both ops list heads, `g_mca_adsp_glink`): identical. Only vermagic/intree/scmversion/built_with differ.

Build requirements (all verified):
- `defconfig = "mca_adsp_glink_defconfig"` (CONFIG_LTO_CLANG_THIN=y): the stock is a ThinLTO build; without LTO the inlined helper (`mca_adsp_glink_write`) and the
  stack-initialisation of `probe` differ by a few instructions (second optimisation round: GEP CSE and dead-store clean-up).
- the `lock_class_key` section hack at the top of the .c (all keys of mutex_init()/init_completion() into one .bss section, like the stock link).
- every log `__LINE__` is pinned with `#line N` (106, 117, 287, 302 (multi-line call: the closing line 303 counts), 310, 417, 430, 437).
- stub providers: `//re/stubs_mcaA:{mca_log_stub,qti_pmic_glink_stub}`; `qti_pmic_glink_stub.c` needs the provider's full include list AND the private
  `struct pmic_glink_client/_dev` bodies, otherwise the CRC of `pmic_glink_register_client` (0x1ae7d248) differs (genksyms expands struct device only if its body is visible).

Reconstructed protocol (see the comments in the .c and `mca_adsp_glink_msg.h`): request 276 bytes {owner,type=1,opcode(1 read/2 write),prop_id,seq,data[256]},
response 280 bytes (+ret_code), notifications (type 2) are fanned out to the registered `mca_adsp_call_back` of the owner (0x800a BATTMGR / 0x8009 QBG).
All four property accessors use one inlined helper `mca_adsp_glink_write(owner, opcode, ...)` (the owner id selects the QBG flavour, both use the same pmic_glink client);
`struct mca_adsp_glink` has an unused 0x30 byte gap at +0xa0 that nothing in this module touches (named `unused`).
The accessor prototypes use `unsigned int prop_id` (the export CRCs prove it, `u32` gives a different CRC).
