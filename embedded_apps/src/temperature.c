#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include "../include/common.h"
#include "../include/zigbee.h"

ZeeBigData temperature() {
    ZeeBigData zigbee_data;
    int fd = open_port("/dev/ttyUSB0");
	if(fd < 0){
        printf("Open /dev/ttyUSB0 failed\n");
        zigbee_data.temperature = 00.00;
        zigbee_data.humidity = 00.00;
		return zigbee_data;
	}
	
    set_com_config(fd, 115200, 8, 'N', 1);
    int writeResult = write(fd, "22", 2);
	if (writeResult == -1) {
		printf("Write /dev/ttyUSB0 failed\n");
		zigbee_data.temperature = 00.00;
        zigbee_data.humidity = 00.00;
		return zigbee_data;
	}

    sleep(2);
	char buf[32];
    int readResult = read(fd, &buf, sizeof(buf));
    if (readResult == -1) {
        printf("Read /dev/ttyUSB0 failed\n");
		zigbee_data.temperature = 00.00;
        zigbee_data.humidity = 00.00;
		return zigbee_data;
    }
	printf("%s\n", buf);
    
    zigbee_data.temperature = 10.00;
    zigbee_data.humidity = 20.00;
    close(fd);
	fd = -1;
    return zigbee_data;
}