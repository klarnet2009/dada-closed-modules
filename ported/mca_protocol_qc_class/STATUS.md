# mca_protocol_qc_class - STATUS: EQUIVALENT

Template: XRING `mca/mca_protocol/protocol_qc/mca_protocol_qc_class.c`; the logic is identical to the stock (12 functions incl. init/exit identical), only
includes added (`platform_device.h`, `of.h`, `of_device.h`: the XRING headers pulled them in, ours do not).
Header `stubs_mcaB/include/mca/protocol/protocol_qc_class.h` (new): `struct protocol_class_qc_ops` (4 callbacks from the stock BTF, note the swapped argument
order `(int *type, void *data)` of the two type getters, as in the XRING code) and the 5 prototypes; `enum typec_port_num` {TYPEC_PORT_0, TYPEC_PORT_1, TYPEC_PORT_MAX}
added to `protocol_pd_class.h` (stock arrays are sized for 2 ports: `cmp w0,#1; b.ls`).
Depends on the REAL `mca_protocol_class` (`protocol_class_register_ops`): BUILD.bazel deps `//re/mca_protocol_class:mca_protocol_class`; for that every real package got
`package(default_visibility = ["//re:__subpackages__"])` and `push.sh` takes `EXTRA="<real dep packages>"`.

Result: offline genksyms 5/5 export CRCs ok; real build + compare_ko: 12 functions identical, relocations, objects, 5 exports with CRCs, 4 imports (incl. the CRC of
`protocol_class_register_ops` from our real provider), modinfo -> EQUIVALENT (first build once the dependency visibility was fixed).
