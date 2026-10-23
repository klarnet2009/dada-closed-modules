#!/usr/bin/env bash
# dd.sh <sym>... : dump stock data objects with relocation targets

./sv.sh "cd <work-dir> && python3 tools2/ddump.py stock/matt.ko $*"
