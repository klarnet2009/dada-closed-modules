#!/bin/bash
# assemble the qcacld DDK package: CLO qcacld-3.0 @ WLAN.LA.1.0.r1-07500-pakala.0 (= 5.2.1.119O, public), fw-api from CLO (same tag),
# qca-wifi-host-cmn from the OnePlus 13 tree (the CLO cmn tag is mis-tagged: it points to the qcacld commit) with the oplus_project.h/AGING hooks removed
set -e
W=<work-dir> C=$W/clean; O=$W/op13m/vendor/qcom/opensource/wlan; P=$W/pkg/qcacld3
rm -rf $P; mkdir -p $P/cmn $P/api
(cd $C/qcacld-3.0 && tar c --exclude=.git --exclude=./cmn --exclude=./api .) | tar x -C $P
(cd $O/qca-wifi-host-cmn && tar c --exclude=.git .) | tar x -C $P/cmn
(cd $C/fw-api && tar c --exclude=.git .) | tar x -C $P/api
cd $P
find . -mindepth 2 \( -name BUILD.bazel -o -name BUILD -o -name WORKSPACE \) -delete
python3 - <<'PY'
import re
def sub(path, pairs):
    t = open(path).read()
    for a, b in pairs:
        assert a in t, (path, a[:40])
        t = t.replace(a, b)
    open(path, "w").write(t)
inc = "//#ifndef OPLUS_BUG_STABILITY\n#include <soc/oplus/system/oplus_project.h>\n//#endif /* OPLUS_BUG_STABILITY */\n"
sub("cmn/qdf/linux/src/i_qdf_trace.h", [(inc, ""), ("""//#ifndef OPLUS_BUG_STABILITY
//modify for: close bug in user build
//	BUG();
//#else /* OPLUS_BUG_STABILITY */
	if (AGING == get_eng_version()) {
		BUG();
	} else {
		WARN_ON(1);
	}
//#endif /* OPLUS_BUG_STABILITY */
""", "\tBUG();\n")])
sub("cmn/qdf/linux/src/qdf_trace.c", [("//#ifdef OPLUS_FEATURE_WIFI_MINIDUMP\n#include <soc/oplus/system/oplus_project.h>\n//#endif /* OPLUS_FEATURE_WIFI_MINIDUMP */\n", ""), ("""//#ifdef OPLUS_FEATURE_WIFI_MINIDUMP
//	BUG();
//#else /* OPLUS_FEATURE_WIFI_MINIDUMP */
	if (AGING == get_eng_version()) {
		BUG();
	} else {
		WARN_ON(1);
	}
//#endif /* OPLUS_FEATURE_WIFI_MINIDUMP */
""", "\tBUG();\n")])
sub("cmn/umac/scan/core/src/wlan_scan_cache_db.c", [("""	if (!wlan_action_oui_search(psoc, &attr,
				    ACTION_OUI_SKIP_BCN_CH_MISMATCH_CHK))
		return false;
""", "\treturn false; /* g4: newer OnePlus cmn feature, not in the Xiaomi release */\n")])
PY
C2=<build-dir>
for e in "nl80211_validate_key_link_id|net/wireless/nl80211.c|CFG80211_MLO_KEY_OPERATION_SUPPORT" "struct link_station_parameters|include/net/cfg80211.h|CFG80211_LINK_STA_PARAMS_PRESENT" "NL80211_EXT_FEATURE_PUNCT|include/uapi/linux/nl80211.h|NL80211_EXT_FEATURE_PUNCT_SUPPORT" "unsigned int link_id, u16 punct_bitmap|include/net/cfg80211.h|CFG80211_RU_PUNCT_NOTIFY" "NL80211_EXT_FEATURE_AUTH_AND_DEAUTH_RANDOM_TA|include/uapi/linux/nl80211.h|CFG80211_EXT_FEATURE_AUTH_AND_DEAUTH_RANDOM_TA" "NL80211_CMD_SET_TID_TO_LINK_MAPPING|include/uapi/linux/nl80211.h|WLAN_FEATURE_11BE_MLO_TTLM" "IEEE80211_CHANCTX_CHANGE_PUNCTURING|include/net/mac80211.h|CFG80211_RU_PUNC_CHANDEF"; do p=${e%%|*}; r=${e#*|}; f=${r%%|*}; fl=${r#*|}; if grep -qF "$p" $C2/$f; then echo "#define $fl (1)"; fi; done > configs/grep_defines_g4.h
bash <work-dir> >/dev/null
for c in kiwi-v2 peach peach-v2 qca6750 wcn7750; do echo "CONFIG_NL80211_TESTMODE=y" >> g4_${c}_defconfig; done
echo assembled; du -sh $P; grep -rIl "oplus" $P --include=*.c --include=*.h | head
