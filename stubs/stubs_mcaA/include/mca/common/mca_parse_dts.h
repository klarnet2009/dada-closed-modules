/* SPDX-License-Identifier: GPL-2.0 */
/* mca_parse_dts.h - dts helper API of mca_parse_dts.ko (prototypes from the XRING source, checked with the stock BTF/CRCs) */
#ifndef __MCA_PARSE_DTS_H__
#define __MCA_PARSE_DTS_H__

#include <linux/of.h>
#include <linux/types.h>

int mca_parse_dts_u8(const struct device_node *np, const char *prop, u8 *data, u8 default_value);
int mca_parse_dts_u32(const struct device_node *np, const char *prop, u32 *data, u32 default_value);
int mca_parse_dts_u8_array(const struct device_node *np, const char *prop, u8 *data, u16 len);
int mca_parse_dts_u32_array(const struct device_node *np, const char *prop, u32 *data, u32 len);
int mca_parse_dts_u8_count(const struct device_node *np, const char *prop, u32 row, u32 col);
int mca_parse_dts_u32_count(const struct device_node *np, const char *prop, u32 row, u32 col);
int mca_parse_dts_u32_index(const struct device_node *np, const char *prop, int index, u32 *data);
int mca_parse_dts_string(const struct device_node *np, const char *prop, const char **out);
int mca_parse_dts_string_index(const struct device_node *np, const char *prop, int index, const char **out);
int mca_parse_dts_count_strings(const struct device_node *np, const char *prop, u32 row, u32 col);
int mca_parse_dts_string_array(const struct device_node *np, const char *prop, int *data, u32 row, u32 col);

#endif /* __MCA_PARSE_DTS_H__ */
