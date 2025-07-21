#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include "../include/cJSON.h"
#include "../include/format_response.h"
#include "../../embedded_common/include/message.h"
#include "../../embedded_common/include/led.h"
#include "../include/common.h"

extern int msgid;
int lamp(struct mg_connection *c, struct mg_http_message* hm) {
    printf("lamp\n");
    char* headers =
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Content-Type: application/json\r\n";

    cJSON* json = cJSON_Parse(hm->body.buf);
    if (json == NULL) {
        mg_http_reply(c, 405, headers, "参数不正确");
        cJSON_Delete(json);
        return -1;
    }
    cJSON* isOpen = cJSON_GetObjectItemCaseSensitive(json, "isOpen");
    cJSON* which = cJSON_GetObjectItemCaseSensitive(json, "which");
    if (!cJSON_IsBool(isOpen)) {
        mg_http_reply(c, 405, headers, "参数不正确");
        cJSON_Delete(json);
        return -1;
    }
    if (!cJSON_IsNumber(which)) {
        mg_http_reply(c, 405, headers, "参数不正确");
        cJSON_Delete(json);
        return -1;
    }
    printf("isOpen: %s\n", isOpen->valuestring);
    printf("which: %d\n", which->valueint);

    Message msg;
    char* operate = cJSON_IsTrue(isOpen) ? "on" : "off";
    msg.type = 1;
    strncpy(msg.body.operate, operate, sizeof(msg.body.operate) - 1);
    msg.body.operate[sizeof(msg.body.operate) - 1] = '\0';
    msg.body.which = (unsigned short)which->valueint;
    int result = msgsnd(msgid, &msg, sizeof(msg.body), 0);
    if (result == -1) {
        mg_http_reply(c, 505, headers, "服务器异常");
        cJSON_Delete(json);
        return -1;
    }
    
    char* response = format_response(0, cJSON_CreateString("操作成功"), true);
    mg_http_reply(c, 200, headers, response);
    free(response);
    cJSON_Delete(json);
    return 0;
}
