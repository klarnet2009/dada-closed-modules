#!/bin/bash
# cmpbuilt.sh <old.ko> <new.ko>  (server, in <work-dir>): code+reloc+section-content diff, ignoring the build-id note
cd <work-dir> B=<build-dir> mkdir -p tmp
$B/llvm-objdump -d -r built/$1.ko | sed 1,3d > tmp/a.dis; $B/llvm-objdump -d -r built/$2.ko | sed 1,3d > tmp/b.dis
$B/llvm-objdump -s built/$1.ko | sed 1,2d > tmp/a.sec; $B/llvm-objdump -s built/$2.ko | sed 1,2d > tmp/b.sec
echo "dis diff lines: $(diff tmp/a.dis tmp/b.dis | wc -l)  sec diff lines: $(diff tmp/a.sec tmp/b.sec | wc -l)"; diff tmp/a.sec tmp/b.sec | head -${3:-8}
