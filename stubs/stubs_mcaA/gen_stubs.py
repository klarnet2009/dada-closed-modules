#!/usr/bin/env python3
"""Generates the provider stub modules (stub .c + BUILD entries) and the matching reconstructed headers for the MCA class APIs used by
the mcaA modules.  Prototypes come from the XRING sources / stock BTF; every CRC is verified against the stock exporter with
<build-dir> (genksyms).  Run from this directory: python3 gen_stubs.py"""
import os

# provider module name -> (header path, [prototypes])   (only exports that some reconstructed module imports)
PROV = {
 "mca_protocol_class": ("mca/protocol/protocol_class.h", [
   "int protocol_class_get_adapter_type(unsigned int protocol, unsigned int *value)",
   "int protocol_class_get_adapter_verified(unsigned int protocol, int *verified)",
   "int protocol_class_set_adapter_verified(unsigned int protocol, int verified)"]),
 "mca_protocol_pd_class": ("mca/protocol/protocol_pd_class.h", [
   "int protocol_class_pd_get_cid_status(unsigned int port_num, bool *status)",
   "int protocol_class_pd_get_cc_toggle(unsigned int port_num, bool *en)",
   "int protocol_class_pd_set_cc_toggle(unsigned int port_num, bool en)",
   "int protocol_class_pd_get_has_dp(unsigned int port_num, bool *has_dp)",
   "int protocol_class_pd_get_pd_active(unsigned int port_num, int *pd_active)",
   "int protocol_class_pd_set_pd_active(unsigned int port_num, int pd_active)"]),
 "mca_platform_cp_class": ("mca/platform/platform_cp_class.h", [
   "int platform_class_cp_get_ovpgate_status(unsigned int role, bool *en)",
   "int platform_class_cp_enable_ovpgate(unsigned int role, bool en)",
   "int platform_class_cp_get_present(unsigned int role, bool *present)",
   "int platform_class_cp_enable_ovpgate_with_check(unsigned int role, int type_temp, bool en)",
   "int platform_class_cp_get_chip_vendor(unsigned int role, int *chip_vendor)",
   "int platform_class_cp_set_revchg(unsigned int role, bool en)"]),
 "mca_platform_wireless_class": ("mca/platform/platform_wireless_class.h", [
   "int platform_class_wireless_get_fw_version(unsigned int role, char *buf)",
   "int platform_class_wireless_get_hall_gpio_status(unsigned int role, bool *status)",
   "int platform_class_wireless_get_tx_adapter(unsigned int role, int *adapter)",
   "int platform_class_wireless_get_vout(unsigned int role, int *vout)",
   "int platform_class_wireless_get_vrect(unsigned int role, int *vrect)",
   "int platform_class_wireless_get_iout(unsigned int role, int *iout)",
   "int platform_class_wireless_get_ss_voltage(unsigned int role, int *ss_voltage)",
   "int platform_class_wireless_get_phone_case_category(unsigned int role, int *category)",
   "int platform_class_wireless_set_phone_case_category(unsigned int role, int category)"]),
 "mca_strategy_class": ("mca/strategy/strategy_class.h", [
   "int mca_strategy_func_get_status(int type, int status, void *value)",
   "int mca_strategy_func_process(unsigned int type, int event, int value)"]),
 "mca_basic_wireless": ("mca/strategy/strategy_wireless_class.h", [
   "int strategy_class_wireless_ops_get_wls_type(int *wls_type)"]),
 "mca_wireless_revchg": ("mca/strategy/mca_wireless_revchg.h", [
   "int mca_wireless_rev_update_fw_version(int cmd)",
   "int mca_wireless_rev_get_reverse_chg(bool *reverse_chg_en)",
   "int mca_wireless_rev_enable_reverse_charge(bool enable)",
   "int mca_wireless_rev_set_user_reverse_chg(bool user_reverse_chg)",
   "int mca_wireless_rev_get_reverse_chg_state(int *state)"]),
 "mca_charger_thermal": ("mca/strategy/mca_charger_thermal.h", [
   "int mca_get_wls_charger_thermal_remove(bool *wls_thermal_remove)",
   "int mca_set_wls_charger_thermal_remove(bool wls_thermal_remove)"]),
}
PRE = {  # extra text for the headers (enums proven by stock code)
 "mca/protocol/protocol_class.h": "enum adapter_protocol_type {\n\tADAPTER_PROTOCOL_PD = 0,\n\tADAPTER_PROTOCOL_BC12 = 5,\n};\n\n",
 "mca/protocol/protocol_pd_class.h": "enum typec_port_num {\n\tTYPEC_PORT_0 = 0,\n};\n\n",
 "mca/platform/platform_cp_class.h": "enum cp_role {\n\tCP_ROLE_MASTER = 0,\n};\n\n",
 "mca/platform/platform_wireless_class.h": "enum wireless_role {\n\tWIRELESS_ROLE_MASTER = 0,\n};\n\n",
 "mca/strategy/strategy_class.h": ("enum strategy_func_type {\n\tSTRATEGY_FUNC_TYPE_BUCK_CHARGE = 0,\n\tSTRATEGY_FUNC_TYPE_QUICK_CHARGE = 1,\n"
    "\tSTRATEGY_FUNC_TYPE_BASIC_WIRELESS = 2,\n};\n\nenum strategy_status_type {\n\tSTRATEGY_STATUS_TYPE_ONLINE = 0,\n\tSTRATEGY_STATUS_TYPE_QC_TYPE = 4,\n"
    "\tSTRATEGY_STATUS_TYPE_POWER_MAX = 9,\n};\n\n"),
}
def guard(p): return "__" + p.replace("/", "_").replace(".", "_").upper() + "__"
def write(path, text):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    open(path, "w", newline="\n").write(text)
build = ""
for prov, (hdr, protos) in PROV.items():
    hp = "include/" + hdr
    g = guard(hdr)
    h = ("/* SPDX-License-Identifier: GPL-2.0 */\n/* %s - reconstructed API of %s.ko (prototypes from the XRING sources, CRC-verified against the stock exporter) */\n"
         "#ifndef %s\n#define %s\n\n#include <linux/types.h>\n\n" % (os.path.basename(hdr), prov, g, g))
    h += PRE.get(hdr, "")
    h += "".join(p + ";\n" for p in protos)
    h += "\n#endif /* %s */\n" % g
    # keep hand-written additions (buckchg/fg/pmic headers are not generated here)
    write(hp, h)
    s = "// SPDX-License-Identifier: GPL-2.0\n/* Stub provider of (a subset of) the %s.ko exports. */\n#include <linux/module.h>\n#include <%s>\n\n" % (prov, hdr)
    for p in protos:
        name = p.split("(")[0].split()[-1].lstrip("*")
        s += "%s { return 0; }\nEXPORT_SYMBOL(%s);\n" % (p, name)
    s += 'MODULE_LICENSE("GPL");\n'
    write("%s_stub.c" % prov, s)
    build += ('ddk_module(name = "%s_stub", srcs = ["%s_stub.c"], out = "%s.ko", kernel_build = _K, deps = [":mca_headers", _G])\n' % (prov, prov, prov))
open("BUILD.generated", "w", newline="\n").write(build)
print(build)
