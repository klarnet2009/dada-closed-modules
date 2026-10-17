# mca_protocol_class - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 15 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_protocol_class.ko`: compare_ko.py EQUIVALENT (15 functions, 13 exports, 3 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_protocol_class - STATUS: EQUIVALENT


Template: XRING `mca/mca_protocol/mca_protocol_class.c`.  Changes vs the template (the stock module is newer):
- removed `protocol_class_get_bc12_adapter_detect_done` (not in the stock module), added `protocol_class_get_adapter_pwr_max_power`
  (new export in the stock, same shape as `get_adapter_max_power`, op slot +88); `get_adapter_type` casts `(int *)value` (stock ops prototype takes `int *`).
- added `#include <linux/platform_device.h>` (the XRING mca headers pulled it in, our reconstructed header does not).
- header `stubs_mcaB/include/mca/protocol/protocol_class.h` rewritten from the stock BTF (12-callback ops struct, 7-entry `adapter_power_cap_info`,
  `adapter_vendor_info` 16 bytes, `adapter_power_curve` 64 bytes, enum incl. QI2 = 4).

Header-only CRC forcing: the member names of `adapter_vendor_info` and `adapter_power_curve` are unresolved base-string offsets in the stock BTF.
A dictionary/meet-in-the-middle search (scripts_mcaB/mitm.c, 112k kernel identifiers, up to 3 unknown names) gives only random 32-bit collisions, so the names
in the header are guesses and a `#ifdef __GENKSYMS__` pad member (`int fcrcXXXX;` at the end of 3 structs) makes genksyms reproduce the three stock CRCs
(`protocol_class_get_adapter_info`, `..._get_adapter_power_curve`, `..._register_ops`).  The pads never reach the compiler (layout/code unaffected).
Tool: `scripts_mcaB/padcrc.py` (placeholder in header -> solved identifier).

Result: offline genksyms 13/13 export CRCs ok; real build + compare_ko (first real build after the header fix): 15 functions identical, relocations, objects,
13 exports with CRCs, 3 imports, modinfo -> EQUIVALENT.
