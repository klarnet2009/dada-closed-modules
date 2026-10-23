#!/usr/bin/env bash
# _fd12.sh <func...>: disassembly diff stock vs ThinLTO emulation (server g15b); env OB=1 -> multiset diff (opb.py, V=1 shows lines, MIN=n), HEADN=n lines
S="ssh -i <ssh-key-file> -o BatchMode=yes <user>@<build-server>"
if [ -n "$OB" ]; then
 ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "cd <work-dir> && MOD=mitee_dlkm MIN='$MIN' V='$V' python3 tools2/opb.py $*"
else
 ssh -i "<ssh-key-file>" -o BatchMode=yes <user>@<build-server> "cd <work-dir> && cp -f o/mitee_dlkm.lto.o built/mitee_emu.ko && python3 tools/pb_fd.py mitee_emu $* 2>&1" | head -${HEADN:-150}
fi
