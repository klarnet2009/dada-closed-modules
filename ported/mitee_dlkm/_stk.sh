#!/usr/bin/env bash
# _stk.sh <func> [emu]: stock (or emulation) function disassembly with relocations (server g12c)
B=<build-dir>
F=${2:+built/mitee_emu.ko}; F=${F:-stock/mitee_dlkm.ko}
ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "cd <work-dir> && $B -dr --no-show-raw-insn $F | awk '/^[0-9a-f]+ <$1(\.llvm\.[0-9]+)?>:/{p=1} p&&/^$/{exit} p' | cut -c1-140"
