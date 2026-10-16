// SPDX-License-Identifier: GPL-2.0
/*
 * Stub providers for minidump.ko imports (real: gh_rm_drv [gh_rm_minidump_*], debug_symbol, smem [qcom_smem_get], qcom_dma_heaps [qcom_is_dma_buf_file]).
 * Link/CRC checking only; prototypes come from the kernel headers.
 */
#include <linux/module.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/soc/qcom/smem.h>
#include <linux/qcom_dma_heap.h>

bool debug_symbol_available(void) { return true; }
EXPORT_SYMBOL(debug_symbol_available);
unsigned long debug_symbol_lookup_name(const char *name) { return 0; }
EXPORT_SYMBOL(debug_symbol_lookup_name);
int gh_rm_minidump_get_info(void) { return 0; }
EXPORT_SYMBOL(gh_rm_minidump_get_info);
int gh_rm_minidump_register_range(phys_addr_t base_ipa, size_t region_size, const char *name, size_t name_size) { return 0; }
EXPORT_SYMBOL(gh_rm_minidump_register_range);
int gh_rm_minidump_deregister_slot(uint16_t slot_num) { return 0; }
EXPORT_SYMBOL(gh_rm_minidump_deregister_slot);
int gh_rm_minidump_get_slot_from_name(uint16_t starting_slot, const char *name, size_t name_size) { return 0; }
EXPORT_SYMBOL(gh_rm_minidump_get_slot_from_name);
void *qcom_smem_get(unsigned int host, unsigned int item, size_t *size) { return NULL; }
EXPORT_SYMBOL(qcom_smem_get);
bool qcom_is_dma_buf_file(struct file *file) { return false; }
EXPORT_SYMBOL(qcom_is_dma_buf_file);
MODULE_LICENSE("GPL");
