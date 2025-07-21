#include <stdio.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include "../include/common.h"

int msgid;
int shmid;

static void daemonize() {
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

static int create_ipc_file(const char* filepath) {
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

static int check_string(struct mg_str str1, const char* str2) {
    return str1.len == strlen(str2) && memcmp(str1.buf, str2, str1.len) == 0;
}

static void match_route(struct mg_connection *c, int ev, void *request_info) {
    char* cors_headers =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    if (ev != MG_EV_HTTP_MSG) {
        return;
    }
    
    struct mg_http_message* hm = (struct mg_http_message *) request_info;    
    
    // 添加这里：处理所有 OPTIONS 预检请求
    if (check_string(hm->method, "OPTIONS")) {
        mg_http_reply(c, 200, cors_headers, "");
        return;
    }

    if (check_string(hm->uri, "/api/control")) {
        if (!check_string(hm->method, "GET")) {
            mg_http_reply(c, 405, cors_headers, "请求方式错误");
            return;
        }
        control(c);
        return;
    }

    if (check_string(hm->uri, "/api/lamp")) {
        if (!check_string(hm->method, "POST")) {
            mg_http_reply(c, 405, cors_headers, "请求方式错误");
            return;
        }
        if ((int)hm->body.len <= 0) {
            mg_http_reply(c, 200, cors_headers, "参数不能为空");
            return;
        }
        lamp(c, hm);
        return;
    }
    
    if (check_string(hm->uri, "/api/speakers")) {
        if (!check_string(hm->method, "POST")) {
            mg_http_reply(c, 405, cors_headers, "请求方式错误");
            return;
        }
        if ((int)hm->body.len <= 0) {
            mg_http_reply(c, 405, cors_headers, "参数不能为空");
            return;
        }
        speakers(c, hm);
        return;
    }

    if (check_string(hm->uri, "/api/fan")) {
        if (!check_string(hm->method, "POST")) {
            mg_http_reply(c, 405, cors_headers, "请求方式错误");
            return;
        }
        if ((int)hm->body.len <= 0) {
            mg_http_reply(c, 405, cors_headers, "参数不能为空");
            return;
        }
        fan(c, hm);
        return;
    }

    if (check_string(hm->uri, "/api/env")) {
        if (!check_string(hm->method, "GET")) {
            mg_http_reply(c, 405, cors_headers, "请求方式错误");
            return;
        }
        env(c);
        return;
    }

    mg_http_reply(c, 404, cors_headers, "Not Found");
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
    msgid = msgget(msg_key, IPC_CREAT|0666);
    shmid = shmget(shm_key, 512, IPC_CREAT|0666);
    if (msgid == -1 || shmid == -1) {
        printf("消息队列 共享内存id == -1\n");
        return -1;
    }

    // 启服务
    struct mg_mgr mgr; // 事件管理器
    char* url = "http://192.168.1.100:8080";
    mg_mgr_init(&mgr);
    mg_http_listen(&mgr, url, match_route, NULL);
    printf("Server is running on http://192.168.1.100:8080\n");
    
    // 监听轮询事件，超时1000ms
    while (1) {
        mg_mgr_poll(&mgr, 1000);
    }

    mg_mgr_free(&mgr);
    return 0;
}