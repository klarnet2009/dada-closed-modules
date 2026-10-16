// SPDX-License-Identifier: GPL-2.0
/* Stub provider of smcinvoke_dlkm exports (resolves modpost imports with the stock CRCs). */
#include <linux/module.h>
#include "smcinvoke_object.h"

int32_t get_client_env_object(struct Object *clientEnvObj) { return 0; }
EXPORT_SYMBOL(get_client_env_object);
MODULE_LICENSE("GPL");
