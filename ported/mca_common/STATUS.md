# mca_common - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 23 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_common.ko`: compare_ko.py EQUIVALENT (23 functions, 18 exports, 31 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_common - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_voter.c (mca_common.ko = mca_voter.o)
Result: EQUIVALENT: compiles on 6.6.144; function set (23), bytes, relocs, exports + CRC (18), imports + CRC (31), modinfo, 6 data objects identical

- added kernel includes (module, slab, mutex, spinlock, list, debugfs, seq_file) before the mca headers, followed by '#line 21' so the original __LINE__ numbering is kept
- stock is a newer version: mca_vote logs 'callback failed with rc=%d' (mca_log_err) when the callback returns an error (braces added around the callback call, +3 lines); mca_vote_override copies the client with strncpy(.., CLIENT_MAX_LEN) + override_client[CLIENT_MAX_LEN - 1] = 0 and logs single-argument messages 'ovrried vote %d' / 'cleared ovrried vote %d'
- header: NUM_VOTABLE_TYPES added to enum mca_votable_type in src/stubs_mcaA/include/mca/common/mca_voter.h
