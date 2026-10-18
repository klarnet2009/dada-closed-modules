#!/usr/bin/env bash
# ms.sh <module>: one line per differing function (multiset diff of the last real build vs stock)
MOD=$1 MIN=1 V= bash "$(dirname "$0")/ob.sh" <work-dir> 2>&1 | grep -E "\(stock" | cut -c1-110
