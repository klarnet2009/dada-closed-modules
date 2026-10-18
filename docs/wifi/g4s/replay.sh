#!/bin/bash
# replay.sh <pkg qcacld3 dir>: start from the g4r package (work/a17/g4r/replay.sh), then apply the g4s specs in numeric order (ed.py = g4o ed.py, idempotent). 74/75 were trial specs and were reverted (do not exist).
P=$1; D=$(dirname "$0")
python3 $D/../g4o/tools/ed.py $P $D/specs/6[2-9]_*.spec $D/specs/7[0-3]_*.spec $D/specs/7[6-9]_*.spec $D/specs/8[0-9]_*.spec
# Server working package: <work-dir> (== g4r package + these specs); the final chain is tools/final_s.sh (line hack json: logs/lh_sf.json, regenerated for this source state, NOT restored code).
