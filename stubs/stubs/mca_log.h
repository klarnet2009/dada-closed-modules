/* Guessed from the call sites in the stock binary: __mca_log_err(fmt, func, line, ...) */
#ifndef _MCA_LOG_H
#define _MCA_LOG_H
void __mca_log_err(const char *fmt, ...);
#define mca_log_err(fmt, ...) __mca_log_err("[mca_smem]%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#endif
