// SPDX-License-Identifier: GPL-2.0
/* Stub provider of msm_gpio_mpm_wake_set (real: pinctrl-msm.ko). Link/CRC checking only. */
#include <linux/module.h>
#include <linux/pinctrl/qcom-pinctrl.h>

int msm_gpio_mpm_wake_set(unsigned int gpio, bool enable) { return 0; }
EXPORT_SYMBOL(msm_gpio_mpm_wake_set);
MODULE_LICENSE("GPL");
