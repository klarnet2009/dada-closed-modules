/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Google, Inc.
 * Copyright (C) 2024 XiaoMi, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DYNAMIC_MEM_H
#define __DYNAMIC_MEM_H

#include <linux/types.h>
#include <linux/scatterlist.h>


/* mem_size: [in] target memory size going to free
 * sgt: target memory assigned in sg_table to free
 * free memory that assigned in a sg_table with size of mem_size
 */
void mitee_free_memory_sgt(uint32_t mem_size, struct sg_table *sgt);

/* mem_size: [in] targe memory size want to allocate
 * out_sgt: [out] target memory sg_table will assigned in
 * allocate a large memory as size of mem_size and assigned to out_sgt
 */
int mitee_alloc_memory_sgt(uint32_t mem_size, struct sg_table **out_sgt);


#endif
