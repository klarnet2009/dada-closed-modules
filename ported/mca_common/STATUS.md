# mca_common - EQUIVALENT (port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_voter.c (mca_common.ko = mca_voter.o)
Result: EQUIVALENT: compiles on 6.6.144; function set (23), bytes, relocs, exports + CRC (18), imports + CRC (31), modinfo, 6 data objects identical

- added kernel includes (module, slab, mutex, spinlock, list, debugfs, seq_file) before the mca headers, followed by '#line 21' so the original __LINE__ numbering is kept
- stock is a newer version: mca_vote logs 'callback failed with rc=%d' (mca_log_err) when the callback returns an error (braces added around the callback call, +3 lines); mca_vote_override copies the client with strncpy(.., CLIENT_MAX_LEN) + override_client[CLIENT_MAX_LEN - 1] = 0 and logs single-argument messages 'ovrried vote %d' / 'cleared ovrried vote %d'
- header: NUM_VOTABLE_TYPES added to enum mca_votable_type in src/stubs_mcaA/include/mca/common/mca_voter.h
