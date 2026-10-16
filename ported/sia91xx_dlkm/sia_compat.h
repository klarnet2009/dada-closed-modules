/* a17C: pre-5.10 uaccess compat for the old yunluo-s-oss template (the set_fs() calls are no-ops around commented-out kernel_write()) */
#ifndef __SIA_COMPAT_H__
#define __SIA_COMPAT_H__
typedef int mm_segment_t;
#define get_fs() 0
#define set_fs(x) do { (void)(x); } while (0)
#define KERNEL_DS 0

#include <linux/fs.h>
#include <linux/err.h>
#define filp_open(path, flags, mode) ((struct file *)ERR_PTR(-ENOSYS)) /* kernel-module file I/O is not available (GKI); stock uses request_firmware */
#endif
