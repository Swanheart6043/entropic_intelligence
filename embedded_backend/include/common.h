#ifndef COMMON_H
#define COMMON_H

#include "mongoose.h"

int control(struct mg_connection *c);
int lamp(struct mg_connection *c, struct mg_http_message* hm);
int speakers(struct mg_connection *c, struct mg_http_message* hm);
int fan(struct mg_connection *c, struct mg_http_message* hm);
int env(struct mg_connection *c);

#endif