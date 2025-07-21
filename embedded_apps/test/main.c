#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include "../../embedded_common/include/message.h"

int main() {
    long type;
    char operate[2] = {'\0'};
    unsigned short which;

    key_t key = ftok("/tmp/control.txt", 'g');
    if (key == -1) {
        printf("key cannot be == -1\n");
        return -1;
    }
    int msgid = msgget(key, IPC_CREAT|0666);
    if (msgid == -1) {
        printf("msgid cannot be == -1\n");
        return -1;
    }
    
    scanf("%ld %s %hu", &type, operate, &which);
    printf("type:%ld\n", type);
    printf("operate:%s\n", operate);
    printf("which:%hu\n", which);

    MessageBody body;
    strncpy(body.operate, operate, sizeof(body.operate) - 1);
    body.operate[sizeof(body.operate) - 1] = '\0';
    body.which = which;
    Message msg;
    msg.type = type;
    msg.body = body;

    int result = msgsnd(msgid, &msg, sizeof(msg.body), 0);
    if (result == -1) {
        printf("msgsnd cannot be == -1\n");
        return -1;
    }

    return 0;
}