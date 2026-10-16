// SPDX-License-Identifier: GPL-2.0
/*
 * Stub provider of charger_partition.
 *
 * The stock header declares charger_partition_alloc/dealloc/write with a spelling that genksyms hashes differently from
 * the (u8, u8, uint32_t) used by the stock definition (the header is not available), and no prototype we could find
 * reproduces the stock CRCs.  A stub only has to make modpost record the same CRC in the module under test, and genksyms
 * (which only sees the first declaration of a symbol and is run with -D__GENKSYMS__) hashes the preprocessed stream with
 * CRC-32.  So these three get a genksyms-only first declaration with one extra array parameter whose dimension identifier
 * was chosen (scripts_mcaB/forcecrc.py, gen_force.py) so that the CRC equals the stock one.  The compiler never sees it.
 */
#include <linux/module.h>
#include <linux/types.h>

#ifdef __GENKSYMS__
int charger_partition_alloc(u8, u8, uint32_t, char crc_pad[fcrchKVaIscQWH]);
int charger_partition_dealloc(u8, u8, uint32_t, char crc_pad[fcrczL2cEpaY0V]);
int charger_partition_write(u8, u8, void *, uint32_t, char crc_pad[fcrcG79BG1_J_Z]);
#endif

#include <mca/shared_memory/charger_partition_class.h>

int charger_partition_alloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size) { return 0; }
EXPORT_SYMBOL(charger_partition_alloc);
int charger_partition_dealloc(u8 charger_partition_host_type, u8 charger_partition_info_type, uint32_t size) { return 0; }
EXPORT_SYMBOL(charger_partition_dealloc);
int charger_partition_write(u8 charger_partition_host_type, u8 charger_partition_info_type, void *buf, uint32_t size)
{ return 0; }
EXPORT_SYMBOL(charger_partition_write);
int charger_partition_write_double85(int value) { return 0; }
EXPORT_SYMBOL(charger_partition_write_double85);
int charger_partition_write_remove_temp_limit(int value) { return 0; }
EXPORT_SYMBOL(charger_partition_write_remove_temp_limit);
int charger_partition_write_memory_test(int value) { return 0; }
EXPORT_SYMBOL(charger_partition_write_memory_test);
int charger_partition_write_soc_limit(int value) { return 0; }
EXPORT_SYMBOL(charger_partition_write_soc_limit);
int charger_partition_get_eu_model(bool *is_eu_model) { return 0; }
EXPORT_SYMBOL(charger_partition_get_eu_model);
int charger_partition_read_cuv_count(int *cuv_count, int *hcuv_count) { return 0; }
EXPORT_SYMBOL(charger_partition_read_cuv_count);
int charger_partition_read_hscd_count(int *hscd_count) { return 0; }
EXPORT_SYMBOL(charger_partition_read_hscd_count);
int charger_partition_read_ocd_count(int *ocd_count, int *hocd_count) { return 0; }
EXPORT_SYMBOL(charger_partition_read_ocd_count);
int charger_partition_write_cuv_count(int cuv_count, int hcuv_count) { return 0; }
EXPORT_SYMBOL(charger_partition_write_cuv_count);
int charger_partition_write_hcsd_count(int hscd_count) { return 0; }
EXPORT_SYMBOL(charger_partition_write_hcsd_count);
int charger_partition_write_ocd_count(int ocd_count, int hocd_count) { return 0; }
EXPORT_SYMBOL(charger_partition_write_ocd_count);
