﻿#ifndef  TC_MSG_INCLUDED
#define  TC_MSG_INCLUDED

#include <xcopy.h>

typedef struct msg_client_s msg_client_t;
typedef struct msg_server_s msg_server_t;

#pragma pack(push,1)

struct msg_client_s {
    uint32_t  client_ip;
    uint16_t  client_port;
    uint16_t  type;
    uint32_t  target_ip;
    uint16_t  target_port;
};

struct msg_server_s {
    tc_iph_t  ip;
    tc_tcph_t tcp;
    unsigned char extension[MAX_OPTION_LEN];
};
#pragma pack(pop)

#define MSG_CLIENT_SIZE sizeof(msg_client_t)
#define MSG_SERVER_SIZE sizeof(msg_server_t)

#endif /*  TC_MSG_INCLUDED */

