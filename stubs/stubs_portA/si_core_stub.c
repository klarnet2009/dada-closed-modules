// SPDX-License-Identifier: GPL-2.0
/* Stub providers for si_core_module imports (real: qcom-scm [qcom_scm_invoke_*, qtee_shmbridge_*] and qseecom_proxy). Link/CRC checking only. */
#include <linux/module.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/qtee_shmbridge.h>
#include <linux/qseecom_kernel.h>

int qcom_scm_invoke_smc(phys_addr_t in_buf, size_t in_buf_size, phys_addr_t out_buf, size_t out_buf_size,
		int32_t *result, u64 *response_type, unsigned int *data) { return 0; }
EXPORT_SYMBOL_GPL(qcom_scm_invoke_smc);
int qcom_scm_invoke_callback_response(phys_addr_t out_buf, size_t out_buf_size, int32_t *result,
		u64 *response_type, unsigned int *data) { return 0; }
EXPORT_SYMBOL_GPL(qcom_scm_invoke_callback_response);
int32_t qtee_shmbridge_allocate_shm(size_t size, struct qtee_shm *shm) { return 0; }
EXPORT_SYMBOL_GPL(qtee_shmbridge_allocate_shm);
void qtee_shmbridge_free_shm(struct qtee_shm *shm) { }
EXPORT_SYMBOL_GPL(qtee_shmbridge_free_shm);
int qseecom_process_listener_from_smcinvoke(uint32_t *result, u64 *response_type, unsigned int *data) { return 0; }
EXPORT_SYMBOL_GPL(qseecom_process_listener_from_smcinvoke);
MODULE_LICENSE("GPL");
