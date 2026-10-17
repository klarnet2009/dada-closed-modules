# mca_qcom_smem - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `mca_qcom_smem.ko` (md5 a430ed1a..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/mca_qcom_smem`) + compare_ko.py (EQUIVALENT).

CHANGED in OS4 and fixed: new 4th export `get_smem_battery_is_new_batt_sn(bool *)` (smem item 81, byte +0x26 of the 40-byte entry, log 'uefi_is_new_batt_sn: %d'); the four functions are now in the OS4 order with their __LINE__ values (53/59/65, 80/86/95, 109/115/122, 136/142/149) pinned by #line (the old blank-line generator work/re/poc/gen_mca_qcom_smem.py is obsolete). compare_ko vs OS4: function set (4), bytes, relocations, objects, exports+CRC (4), imports+CRC (4), modinfo all OK. No hacks.

SAFETY: charging module, community-only source: never flash it and never put it into a ROM build.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# mca_qcom_smem
STATUS: EQUIVALENT (compare_ko.py: functions, relocations with strings, exports+CRC, imports+CRC, modinfo).
Key facts: smem item 81 (size 40); verify_result takes bool*, bits 5/4 of byte 0x24 (read once into a local); __LINE__ pinned by gen_mca_qcom_smem.py (work/re/poc).
Build needs local_defines CONFIG_QCOM_SMEM=1 and stubs smem_stub + mca_log_stub (prototype __mca_log_err(const char *, ...) confirmed by import CRC).
