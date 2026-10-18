#!/bin/bash
# replay.sh <pkg qcacld3 dir>: start from the g4q package (work/a17/g4q/replay.sh), then apply the g4r specs in order (ed.py = g4o ed.py, idempotent).
P=$1; D=$(dirname "$0")
python3 $D/../g4o/tools/ed.py $P $D/specs/5[6-9]_*.spec $D/specs/6[0-9]_*.spec
# 59 also edits BUILD.bazel (CONFIG_CNSS_HW_SECURE_DISABLE removed for qca6750 / wcn7750) and adds the unused global nss_mode_type.
# Server working package: <work-dir> (== g4q package + these specs); scripts = g4q scripts with paths renamed (+ offscan.py, cmpf.sh).
