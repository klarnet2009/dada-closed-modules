#include <linux/module.h>
#include <linux/soc/qcom/smem.h>
int qcom_smem_alloc(unsigned host, unsigned item, size_t size) { return 0; }
EXPORT_SYMBOL(qcom_smem_alloc);
void *qcom_smem_get(unsigned host, unsigned item, size_t *size) { return NULL; }
EXPORT_SYMBOL(qcom_smem_get);
MODULE_LICENSE("GPL");
