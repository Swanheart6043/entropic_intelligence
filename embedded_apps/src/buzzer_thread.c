#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <pthread.h>
#include "../include/common.h"

typedef struct beep_desc {
	int beep; // 2 3 4 5
	int beep_state; // 0 or 1
	int tcnt; // 占空比
	int tcmp; // 调节占空比
} beep_desc_t;

#define mytype 'f'
#define BEEP_ON _IOW(mytype,0,beep_desc_t)
#define BEEP_OFF _IOW(mytype,1,beep_desc_t)
#define BEEP_FREQ _IOW(mytype,2,beep_desc_t)

// 第2N个元素表示声调 第2N+1个元素表示该声调的时间
unsigned char MUSIC[500] ={ 0x26, 0x20, 0x26, 0x20 };

void* buzzer_thread(void* params) {
    printf("Buzzer thread preparation\n");
    pthread_t threadId = pthread_self();
    printf("当前线程id: %lu\n", threadId);

    int fd = 0;
	int is_on = 0;
	int i = 0;
	beep_desc_t beeper;
	MessageBody* msgBody = (MessageBody*)params;
	if(!msgBody->operate) {
		printf("The parameter is invalid\n");
		return NULL;
	}

	fd = open("/dev/fsbeeper0", O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		printf("open /dev/buzzer failed\n");
		return NULL;
	}
	
	ioctl(fd, BEEP_ON);
	for(i = 0; i < sizeof(MUSIC) / sizeof(MUSIC[0]); i += 2) {
		beeper.tcnt = MUSIC[i];
		beeper.tcmp = MUSIC[i] / 2;
		ioctl(fd, BEEP_FREQ, &beeper);
		usleep(MUSIC[i+1] * 20000);
	}
	printf("music over\n");

	close(fd);
	fd = -1;
	pthread_exit(NULL);
	return NULL;
}