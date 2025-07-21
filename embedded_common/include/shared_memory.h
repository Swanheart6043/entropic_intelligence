typedef struct Mpu6050Data {
    float CYROX;
    float CYROY;
    float CYROZ;
    float AACX;
    float AACY;
    float AACZ;
} Mpu6050Data;

typedef struct {
    float A9_RESERVED_0;
    float A9_RESERVED_1;
} ReservedData;

typedef struct {
    float temperature;
    float humidity;
} ZeeBigData;

typedef struct {
    float adc;
    Mpu6050Data base1;
    ReservedData base2;
    ZeeBigData base3;
} RequestData;