# g4q 33: force-included compat header emulating the 6.6.57 header vintage of the stock build (netif_tx_stop_queue family, readl/writel)
@@@ g4_modules.bzl
<<<<
    copts += ["-Wunused-but-set-parameter", "-include", "configs/grep_defines_g4.h"]
====
    copts += ["-include", "xiaomi_ext/xm_compat_6657.h"]
    copts += ["-Wunused-but-set-parameter", "-include", "configs/grep_defines_g4.h"]
>>>>
