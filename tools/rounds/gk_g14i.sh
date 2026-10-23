#!/bin/bash
# gk.sh <type> : genksyms crc of "<type> is_doing_gc; EXPORT_SYMBOL_GPL"
B=<build-dir> C=<build-dir> O=<build-dir>
cd <work-dir>
printf '#include <linux/module.h>\n%s is_doing_gc;\nEXPORT_SYMBOL_GPL(is_doing_gc);\n' "$1" > gk.c
$B/clang -E -D__GENKSYMS__ -nostdinc -I$C/arch/arm64/include -I$O/arch/arm64/include/generated -I$C/include -I$O/include -I$C/arch/arm64/include/uapi -I$O/arch/arm64/include/generated/uapi -I$C/include/uapi -I$O/include/generated/uapi -include $C/include/linux/compiler-version.h -include $C/include/linux/kconfig.h -include $C/include/linux/compiler_types.h -D__KERNEL__ --target=aarch64-linux-gnu -DMODULE -DKBUILD_MODNAME='"mi_game"' gk.c 2>/dev/null | $O/scripts/genksyms/genksyms 2>&1 | head -3
