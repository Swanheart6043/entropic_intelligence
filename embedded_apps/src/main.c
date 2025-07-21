#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include "../include/common.h"

pthread_t buzzer_tid;
pthread_t fan_tid;
pthread_t collection_tid;
int buzzer_thread_running = 0;
int fan_thread_running = 0;

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        exit(1);
    }
    // 父进程直接退出
    if (pid > 0) {
        exit(0);
    }
    // 子进程
    if (setsid() < 0) {
        perror("setsid failed");
        exit(1);
    }
    // 避免 reacquire tty
    signal(SIGHUP, SIG_IGN);

    // 再 fork 一次，确保不可再打开控制终端
    pid = fork();
    if (pid < 0) {
        perror("fork #2 failed");
        exit(1);
    }
    if (pid > 0) {
        exit(0);
    }

    // 切换到根目录，并重设 umask
    chdir("/");
    umask(0);

    // 重定向标准文件描述符
    // int fd = open("/dev/null", O_RDWR, 0);
    // if (fd != -1) {
    //     dup2(fd, STDIN_FILENO);
    //     dup2(fd, STDOUT_FILENO);
    //     dup2(fd, STDERR_FILENO);
    //     if (fd > 2) close(fd);
    // }
}

int create_ipc_file(const char* filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        return 0;  // 文件已存在
    }    
    FILE* fp = fopen(filepath, "w");
    if (fp == NULL) return -1;
    
    fclose(fp);
    chmod(filepath, 0644); // 设置合适的权限
    return 0;
}

void match_msg(long type, MessageBody body) {
    if (!type) {
        return;
    }
    printf("\n", type);
    printf("type: %ld\n", type);
    printf("operate: %s\n", body.operate);
    printf("which: %d\n", body.which);
    if (type == 1) {
        led(body);
        return;
    }
    if (type == 2) {
        if (buzzer_thread_running) {
            printf("Stopping buzzer thread\n");
            pthread_join(buzzer_tid, NULL);
            buzzer_thread_running = 0;
        }
        int buzzer_thread_result = pthread_create(&buzzer_tid, NULL, buzzer_thread, &body);
        if (buzzer_thread_result == -1) {
            perror("Failed to create buzzer thread");
            return;
        }
        printf ("pthread buzzer end\n");
        buzzer_thread_running = 1;
        return;
    }
    if (type == 3) {
        fan(body);
        return;
    }
    return;
}

int main() {
    // 先做守护化
    daemonize();
    // 确保IPC文件存在
    const char* ipc_file_path = "/tmp/ipc_file";
    if (create_ipc_file(ipc_file_path) < 0) {
        printf("创建ipc文件失败\n");
        return -1;
    }
    // 初始化消息队列 共享内存
    key_t msg_key = ftok(ipc_file_path, 'M');
    key_t shm_key = ftok(ipc_file_path, 'S');
    if (shm_key == -1 || msg_key == -1) {
        printf("消息队列 共享内存key == -1\n");
        return -1;
    }
    int msgid = msgget(msg_key, IPC_CREAT|0666);
    int shmid = shmget(shm_key, 512, IPC_CREAT|0666);
    if (msgid == -1 || shmid == -1) {
        printf("消息队列 共享内存id == -1\n");
        return -1;
    }
    printf("Start app...\n");
    
    // 创建常驻线程
    thread_data_t collection_thread_params = { 
        .shm_id = shmid, 
        .shm_key = shm_key, 
        1024 
    };
    int collection_thread_result = pthread_create(&collection_tid, NULL, collection_thread, &collection_thread_params);
    if (collection_thread_result == -1) {
        perror("Failed to create collection thread");
        return -1;
    }

    // 监听消息
    while(1) {
        Message msg;
        msgrcv(msgid, &msg, sizeof(msg.body), 0, 0);
        match_msg(msg.type, msg.body);
        usleep(10000);
    }
    
    return 0;
}