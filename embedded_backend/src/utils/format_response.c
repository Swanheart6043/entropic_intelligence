#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "../../include/cJSON.h"

char* format_response(int code, cJSON *data_obj, bool success);

char* format_response(int code, cJSON *data_obj, bool success) {
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        printf("Failed to create JSON root\n");
        return NULL;
    }

    cJSON_AddNumberToObject(root, "code", code);
    if (data_obj) {
        cJSON* data_copy = cJSON_Duplicate(data_obj, 1);
        cJSON_AddItemToObject(root, "data", data_copy);
    } else {
        cJSON_AddNullToObject(root, "data");
    }
    cJSON_AddBoolToObject(root, "success", success);

    char* json_for_char = cJSON_PrintUnformatted(root);
    if (!json_for_char) {
        printf("json_for_char empty\n");
        cJSON_Delete(root);
        return NULL;
    }
    char* result = malloc(1000);
    if (!result) {
        printf("Failed to allocate memory\n");
        free(json_for_char);
        cJSON_Delete(root);
        return NULL;
    }
    strcpy(result, json_for_char);

    free(json_for_char);
    cJSON_Delete(root);
    return result;
}
