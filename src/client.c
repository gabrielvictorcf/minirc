#include "client.h"

bool client_connect(client_t* client, char* server_ip) {
    // stores the server's address in client.addr
    client->server = irc_sock_new(client->server.addr_family, server_ip, SERVER_PORT);

    // Connect client sock to server address, then send client's name to server
    int conn_res = connect(client->server.sock, (const struct sockaddr*) &client->server.addr, client->server.addr_len);
    if(conn_res == -1) {
        perror("client_connect");
        return false;
    }

    // Send the client's name to the server
    send(client->server.sock, client->name, IRC_NAME_LEN, 0);

    // Check if connection was sucessfull
    char handshake[9];
    recv(client->server.sock, handshake, 9, 0);
    printf("handshake: %s\n", handshake);
    if(!strcmp(handshake, "rejected")) {
        printf("Connection failed - handshake: %s\n", handshake);
        return false;
    }

    return true;
}

bool client_is_connected(client_t* client) {
    return client->server.sock != -1;
}

int client_disconnect(client_t* client) {
    if (!client_is_connected(client)) return -1;

    close(client->server.sock);
    client->server.sock = -1;
    return 0;
}

bool client_has_nickname(client_t* client) {
    return client->name[0] != '\0' && strcmp(client->name, "guest") != 0;
}

void* client_recv_msgs(void* args) {
    client_t* client = (client_t*) args;

    irc_packet_t pkt = {0};
    while (true) {
        int received = irc_recv(&client->server, &pkt, 0);
        if (received == 0) {
            printf("Server has disconnected\n");
            break;
        } else if (received <= -1) {
            perror("client_recv_msgs");
            continue;
        }

        if (client->changing_name) {
            char* res = strstr(pkt.data, "ok");

            if (res) client->name_changed = true;
            else client->name_changed = false;

            client->changing_name = false;
        }

        printf("\x1b[1K\r%s: %s", pkt.user, pkt.data);

        // Reprint prompt (old msg is still in stdin)
        printf("%s: ", client->name);
        fflush(stdout);

        memset(&pkt, 0, sizeof(pkt));
    }

    client_disconnect(client);
}

// Ignore SIGINT
void client_setup() {
    struct sigaction sa = { .sa_handler = SIG_IGN };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1);
}

int main(int argc, char const *argv[]) {
    client_setup();
    client_t client = { .name = "guest", .server = {.addr_family = AF_INET, .sock = -1}, .pkt = {.length = MSG_LEN}};
    pthread_t receive_thread = {0};

    bool is_active = true;
    while (is_active) {
        // Prompt and read input
        printf("%s: ", client.name);
        memset(client.pkt.data, '\0', client.pkt.length);
        char* read_msg = fgets(client.pkt.data, MSG_LEN, stdin);
        if(!read_msg) {
            client_disconnect(&client);
            break;
        }

        if(read_msg[0] == '\n') {
            continue;
        }

        client.pkt.length = strlen(client.pkt.data);
        client.pkt.data[client.pkt.length] = '\0';

        // Parse msg and error treatment
        irc_cmds_e cmd_type = parse_msg(client.pkt.data);

        bool needs_nickname = true;
        irc_cmds_e cmds_no_nickname[] = {cmd_quit, cmd_nickname};
        int cmds_no_nickname_len = sizeof(cmds_no_nickname)/sizeof(cmds_no_nickname[0]);
        for (int i = 0; i < cmds_no_nickname_len; i++) {
            if (cmd_type == cmds_no_nickname[i]) needs_nickname = false;
        }

        if(needs_nickname && !client_has_nickname(&client)) {
            printf("First you need to set a nickname with '/nickname <nick>'.\n");
            continue;
        }

        switch(cmd_type) {
            case cmd_join:
            case cmd_kick:
            case cmd_mute:
            case cmd_unmute:
            case cmd_whois:
            case cmd_msg:
            case cmd_ping: {
                if (!client_is_connected(&client)) {
                    printf("No server to send to, try '/connect <server_ip>' first.\n");
                    break;
                }

                int sent = irc_send(&client.server, &client.pkt, 0);
                if (sent == -1) printf("Failed to send message :/\n");
                break;
            }
            case cmd_connect: {
                char* server_ip = strchr(client.pkt.data, ' ');
                if(!server_ip) {
                    printf("Missing arg - correct usage: '/connect <server_ip>'.\n");
                    break;
                }

                client_disconnect(&client);

                bool did_connect = client_connect(&client, server_ip);
                if (!did_connect) break;

                printf("Connected sucessfully!\n");
                pthread_create(&receive_thread, NULL, client_recv_msgs, (void*) &client);

                break;
            }
            case cmd_quit:
                client_disconnect(&client);
                is_active = false;
                break;
            case cmd_nickname: {
                char* nick = strchr(client.pkt.data, ' ')+1;
                if (!nick) {
                    printf("Missing arg - correct usage: '/nickname <nick>'.\n");
                    break;
                }

                size_t nick_len = strlen(nick);
                nick[nick_len-1] = '\0';
                nick_len = nick_len > IRC_NAME_LEN? IRC_NAME_LEN : nick_len;

                if (strcmp(nick, "guest") == 0) {
                    printf("The nickname cannot be 'guest'.\n");
                    break;
                }

                char new_nick[IRC_NAME_LEN];
                strcpy(new_nick, nick);
                if (client_is_connected(&client)) {
                    client.changing_name = true;
                    irc_send(&client.server, &client.pkt, 0);

                    while (client.changing_name) {}
                    if(!client.name_changed) break;
                }

                strncpy(client.name, new_nick, nick_len);
                strcpy(client.pkt.user, client.name);
                break;
            }
        }
    }

    pthread_cancel(receive_thread);
    return 0;
}
