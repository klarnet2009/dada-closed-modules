#!/bin/bash
# cnt.sh <module> <fn-regex>: only the diff-count line(s) of matching functions
cd "$(dirname "$0")/.." || exit 1
bash g14h_tools/qq.sh $1 2>&1 | grep -E "error| stock +[0-9]+ built" | grep -E "error|$2"
