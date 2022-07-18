#ifndef IRC_CLIENT_H_
#define IRC_CLIENT_H_

#include "irc.h"

typedef struct _client {
    char name[IRC_NAME_LEN];    // Client nickname, both locally and in the server
    irc_packet_t pkt;           // Client outgoing packet
    irc_sock_t server;          // Server client is connected to
    bool changing_name;
    bool name_changed;
} client_t;

bool client_connect(client_t* client, char* server_ip);
bool client_is_connected(client_t* client);
int client_disconnect(client_t* client);
bool client_has_nickname(client_t* client);

void* client_recv_msgs(void* args);

void client_setup();

#endif