#define LED_IOC_MAGIC 'L'
#define LED_ON         _IO(LED_IOC_MAGIC, 1)
#define LED_OFF        _IO(LED_IOC_MAGIC, 0)
#define LED_SET_BRIGHT _IOW(LED_IOC_MAGIC, 3, int)
#define LED_GET_STATUS _IOR(LED_IOC_MAGIC, 4, int)
