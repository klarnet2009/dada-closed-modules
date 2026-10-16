// SPDX-License-Identifier: GPL-2.0
/* Stub provider (not the real driver) so that modpost resolves symbols imported by reconstructed modules. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/qcom_dma_heap.h>

bool qcom_is_dma_buf_file(struct file *file) { return false; }
EXPORT_SYMBOL(qcom_is_dma_buf_file);
MODULE_LICENSE("GPL");
