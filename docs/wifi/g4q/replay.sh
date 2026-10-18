#!/bin/bash
# replay.sh <pkg qcacld3 dir>: start from the g4p package (see work/a17/g4p/NOTES.md "Replay": g4o package + g4p replay.sh), then apply the g4q changes in order.
# Server working package: <work-dir> (== g4p package + these steps). ed.py = work/a17/g4o/tools/ed.py (idempotent).
P=$1; D=$(dirname "$0")
cp $D/xiaomi_ext/xm_compat_6657.h $P/xiaomi_ext/
python3 $D/../g4o/tools/ed.py $P $D/specs/3[2-9]_*.spec $D/specs/4[0-9]_*.spec $D/specs/5[0-5]_*.spec
# spec 33 adds `-include xiaomi_ext/xm_compat_6657.h` to g4_modules.bzl (kernel 6.6.57 header vintage of the stock: netif_tx_stop_queue family, readl/writel)
# line hack (optional, valid only for this exact source state): scripts/final_r.sh (linehack.py gen/apply dump/lh_b11.json)
