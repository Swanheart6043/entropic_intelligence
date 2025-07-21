#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/shm.h>
#include "../include/common.h"

float get_adc();
ReservedData get_reserved();

void* collection_thread(void* params) {
    printf("Start collection thread...\n");
    pthread_t threadId = pthread_self();

    thread_data_t* params_copy = (thread_data_t*)params;
    RequestData requestParams;

    RequestData* content = (RequestData*)shmat(params_copy->shm_id, NULL, 0);
    // bzero(content,512);
    strcpy((char*)content, (char*)&requestParams);
    
    // while (1) {

    // }
    
    float adc_data = get_adc();
    Mpu6050Data mpu6050_data = mpu6050();
    ZeeBigData zeebig_data = temperature();
    ReservedData reserved_data = get_reserved();
    
    content->adc = adc_data / 1000.00;
    content->base1.CYROX = mpu6050_data.CYROX;
    content->base1.CYROY = mpu6050_data.CYROY;
    content->base1.CYROZ = mpu6050_data.CYROZ;
    content->base1.AACX = mpu6050_data.AACX;
    content->base1.AACY = mpu6050_data.AACY;
    content->base1.AACZ = mpu6050_data.AACZ;
    content->base2.A9_RESERVED_0 = reserved_data.A9_RESERVED_0;
    content->base2.A9_RESERVED_1 = reserved_data.A9_RESERVED_1;
    content->base3.temperature = zeebig_data.temperature;
    content->base3.humidity = zeebig_data.humidity;

    printf("\n");
	printf("adc: %f\n", content->adc);
	printf("CYROX: %f\n", content->base1.CYROX);
	printf("CYROY: %f\n", content->base1.CYROY);
	printf("CYROZ: %f\n", content->base1.CYROZ);
	printf("AACX: %f\n", content->base1.AACX);
	printf("AACY: %f\n", content->base1.AACY);
    printf("AACZ: %f\n", content->base1.AACZ);
    printf("temperature: %f\n", content->base3.temperature);
    printf("humidity: %f\n", content->base3.humidity);
}

float get_adc() {
    int data = 0;
    int fd = open("/dev/adc0", O_RDWR);
    if (fd == -1) {
        printf("Open /dev/adc0 failed\n");
        return data;
    }
    int readResult = read(fd, &data, sizeof(data));
    /*将结果转换成实际的电压值mv*/
    data *= 0.44;
    close(fd);
	fd = -1;
    return data;
}

ReservedData get_reserved() {
    ReservedData reserved_data;
    reserved_data.A9_RESERVED_0 = 0;
    reserved_data.A9_RESERVED_1 = 0;
    return reserved_data;
}
