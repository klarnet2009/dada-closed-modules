# mca_business_misc_comp - EQUIVALENT

`compare_ko.py`: function set (8), bytes, resolved relocations, global objects, exports (0), imports+CRC (10), modinfo (both
description/author/license pairs): all OK.  Data objects (`business_misc_driver`, `match_table`): identical.
Only vermagic/intree/scmversion/built_with differ (build environment); `depends` identical (mca_log,mca_common).

Source = the XRING sources `mca_business_misc.c` + `mca_business_votable.c` + `inc/mca_business_misc.h` verbatim (the stock module is built from
both objects, see the XRING Kbuild); even every `__LINE__` of the log calls is identical, so the Qualcomm and the XRING MCA business_misc code are the same file.
Only the missing headers had to be supplied: `mca/common/mca_voter.h` (votable API, in `work/re/src/stubs_mcaA/include`) and the stub provider
`//re/stubs_mcaA:mca_common_stub`, which must contain the private `struct mca_votable` (copied from XRING `mca_voter.c`) so that the CRC of
`mca_create_votable` (0xe7845e6f) matches the stock provider.  MCA_VOTE_MIN=0, MCA_VOTE_OR=2 (proven by the stock code; MAX=1, AND=3 assumed).
