#include <sys/types.h>
#include "../../embedded_common/include/message.h"
#include "../../embedded_common/include/shared_memory.h"

typedef struct {
    int shm_id;
    key_t shm_key;
} thread_data_t;

void led(MessageBody);
void* buzzer_thread(void*);
void fan(MessageBody);
void* collection_thread(void*);
ZeeBigData temperature();
Mpu6050Data mpu6050();