typedef struct {
    char operate[5];
    unsigned short which;
} MessageBody;

typedef struct { 
    long type; 
    MessageBody body;
} Message;
