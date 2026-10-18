#ifndef __SIA91XX_EXT_INC__
#define __SIA91XX_EXT_INC__
#include <linux/i2c.h>

extern int sipa_i2c_probe(struct i2c_client *i2c);
extern void sipa_i2c_remove(struct i2c_client *i2c);
extern void sipa_i2c_shutdown(struct i2c_client *i2c);

#endif /* __SIA91XX_EXT_INC__ */
