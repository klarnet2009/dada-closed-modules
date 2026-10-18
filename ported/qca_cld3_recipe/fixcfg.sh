#!/bin/bash
# regenerate the 5 defconfigs (gki config + ThinLTO lines + NL80211_TESTMODE) and drop options the DDK reports as undeclared
P=<work-dir>
cd $P
for c in kiwi-v2 peach peach-v2 qca6750 wcn7750; do
  (grep -v "^CONFIG_NL80211_TESTMODE\|^CONFIG_ARM_SMMU=" configs/sun_gki_${c}_defconfig; echo "CONFIG_NL80211_TESTMODE=y"; echo "CONFIG_ARM_SMMU=y"; echo "# CONFIG_LTO_NONE is not set"; echo "CONFIG_LTO_CLANG_THIN=y") > g4_${c}_defconfig
done
cd <build-dir>
for c in kiwi_v2 peach peach_v2 qca6750 wcn7750; do
  cf=g4_$(echo $c | sed 's/_v2/-v2/')_defconfig
  for i in 1 2 3 4 5 6; do
    out=$(nice -n 10 tools/bazel --output_user_root=<build-dir> build --lto=none --config=stamp //re/qcawlan/qcacld3:qca_cld3_${c}_config 2>&1 | grep -E "^\s+CONFIG_.*: actual" )
    [ -z "$out" ] && { echo "$c config OK"; break; }
    echo "$out" | cut -c1-160
    for sym in $(echo "$out" | sed -E 's/^\s+(CONFIG_[A-Za-z0-9_]+):.*/\1/'); do sed -i "/^$sym=/d" $P/$cf; done
  done
done
