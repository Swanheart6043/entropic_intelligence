#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include "../../embedded_common/include/message.h"
#include "../include/zigbee.h"

void fan(MessageBody msg_body) {
	printf("\n");
	printf("fan\n");
	if(!msg_body.operate) {
		printf("msg_body cannot be empty\n");
		return;
	}

	int fd = open_port("/dev/ttyUSB0");
	if(fd == -1){
		printf("open /dev/ttyUSB0 error\n");
		return;
	}

	set_com_config(fd, 115200, 8, 'N', 1);
	int result = write(fd, strcmp(msg_body.operate, "on") == 0 ? "00" : "11", 2);
	if (result == -1) {
		printf("write /dev/ttyUSB0 error\n");
		return;
	}

	close(fd);
	fd = -1;
	return;
}
