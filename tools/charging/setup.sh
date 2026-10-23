set -u
cd <work-dir>
G=<work-dir> mkdir -p $G/st $G/built $G/rep $G/tmp $G/bin
cp g14k/bin/{compare_ko_v2.py,lst2.py,preamble_cmp.py} $G/bin/ 2>/dev/null
declare -A SRC=( [mca_strategy_quickchg]=g31a [mca_basic_wireless]=g31a [mca_quick_wireless]=g30e [mca_business_charger_comp]=g25b [mca_strategy_buckchg]=g30e [mca_strategy_fg_comp]=g31a [mca_smart_charge]=g25b [mca_buckchg_jeita]=g25b [mca_wireless_revchg]=g25b [mca_connector_antiburn]=g20a [mca_pwm_fan]=g20a [charger_partition]=g14b [mca_platform_fg_ic_ops]=g14b [mca_strategy_fg_class]=g14b [mca_charge_mievent]=g14b )
for m in "${!SRC[@]}"; do
  cp ${SRC[$m]}/built/$m.ko $G/built/$m.ko
  s=$(find os4g/mods/vendor_dlkm os4g/mods/vendor_ramdisk -name $m.ko | head -1)
  cp $s $G/st/gl_$m.ko
  echo "== $m"
  nice -n 10 python3 -I $G/bin/compare_ko_v2.py $G/st/gl_$m.ko $G/built/$m.ko -v > $G/rep/cmp_$m.txt 2>&1
  grep -E "^(function|kCFI|objects|imports)" $G/rep/cmp_$m.txt | head -6
done
