# mca_qcom_smem
STATUS: EQUIVALENT (compare_ko.py: functions, relocations with strings, exports+CRC, imports+CRC, modinfo).
Key facts: smem item 81 (size 40); verify_result takes bool*, bits 5/4 of byte 0x24 (read once into a local); __LINE__ pinned by gen_mca_qcom_smem.py (work/re/poc).
Build needs local_defines CONFIG_QCOM_SMEM=1 and stubs smem_stub + mca_log_stub (prototype __mca_log_err(const char *, ...) confirmed by import CRC).
