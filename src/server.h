#ifndef IRC_SERVER_H_
#define IRC_SERVER_H_

#include "irc.h"

typedef struct _channel channel_t;
typedef struct _user user_t;

struct _channel {
    char name[CHANNEL_NAME_LEN];
    user_t* admin;
    char password[CHANNEL_PASS_LEN];
    pthread_t thread;
    int in_epoll;
    int out_epoll;
    int user_qty;
};

struct _user {
    char name[IRC_NAME_LEN];
    irc_sock_t connection;
    channel_t* channel;
    bool can_speak;
};

typedef struct _server {
    irc_sock_t listening;                   // socket that will accept new connections
    user_t clients[SERVER_CLIENT_QTY];      // users map
    int client_qty;
    channel_t channels[CHANNEL_QTY];        // channels map
    int channel_qty;
    pthread_mutex_t ch_mutex;               // channels map' mutex
} server_t;

void server_add_channel(server_t* server, char* name, user_t* user, char* password);

void channel_remove_user(channel_t* channel, user_t* user) {
    if(!user || !channel) return;

    epoll_ctl(channel->in_epoll, EPOLL_CTL_DEL, user->connection.sock, NULL);
    epoll_ctl(channel->out_epoll, EPOLL_CTL_DEL, user->connection.sock, NULL);
    channel->user_qty--;

    printf("%s left %s (now has %d members)\n", user->name, channel->name, channel->user_qty);
}

bool channel_add_user(channel_t* channel, user_t* user, char* password) {
    if(!user || !channel) return false;
    // printf("chanel_add_user::channel->password: %s\n", channel->password);
    if(channel->password && channel->password[0] != '\0') {
        if (!password) {
            printf("%s tried to join %s but submitted no password\n", user->name);
            return false;
        } else if (strcmp(password, channel->password)) {
            printf("%s tried to join %s but submitted wrong password\n", user->name, channel->name);
            return false;
        }
    }

    user->channel = channel;
    struct epoll_event in_event = {
        .events = EPOLLIN | EPOLLRDHUP,
        .data.ptr = user
    };
    epoll_ctl(channel->in_epoll, EPOLL_CTL_ADD, user->connection.sock, &in_event);

    struct epoll_event out_event = {
        .events = EPOLLOUT,
        .data.ptr = user
    };
    epoll_ctl(channel->out_epoll, EPOLL_CTL_ADD, user->connection.sock, &out_event);

    channel->user_qty++;
    printf("%s joined %s (now has %d members)\n", user->name, user->channel->name, user->channel->user_qty);
    return true;
}

server_t server_new(int addr_family, char* addr, in_port_t port) {
    irc_sock_t listening = irc_sock_new(addr_family, addr, port);
    // Assign name+address to socket
    if (bind(listening.sock, (const struct sockaddr*) &listening.addr, listening.addr_len) == -1) {
        perror("server_new::bind");
        exit(1);
    }

    // Put it on listening mode
    if (listen(listening.sock, SERVER_CLIENT_QTY) == -1) {
        perror("server_new::listen");
        exit(1);
    }

    server_t server = {
        .listening = listening,
        .clients = {0},
        .client_qty = 0,
        .channels = {0},
    };

    // Initialize all clients to NULL
    for (int i = 0; i < SERVER_CLIENT_QTY; i++) {
        server.clients[i].connection.sock = -1;
        server.clients[i].can_speak = true;
    }

    return server;
}

void server_move_user(server_t* server, user_t* user, char* ch_name, char* password) {
    channel_t* channel = NULL;
    for (int i = 0; i < CHANNEL_QTY; i++) {
        printf("\t[%d] channel name: %s\n", i, server->channels[i].name);
        if (!strcmp(server->channels[i].name, ch_name)) {
            channel = &server->channels[i];
            break;
        }
    }

    if(!channel) {
        printf("server_move_user: channel %s not found\n", ch_name);
        return;
    }

    pthread_mutex_lock(&server->ch_mutex);

    // Remove user from channel
    channel_t* old_channel = user->channel;
    if(channel_add_user(channel, user, password)) {
        channel_remove_user(old_channel, user);
    }

    pthread_mutex_unlock(&server->ch_mutex);
}

bool is_admin(user_t* user) {
    printf("%s tried an admin only cmd\n", user->name);
    if (user->channel == NULL) return false;
    if(user != user->channel->admin) return false;
    printf("verification sucessfull\n");

    return true;
}

bool server_close_connection(server_t* server, channel_t* channel, user_t* user) {
    for (int i = 0; i < SERVER_CLIENT_QTY; i++) {
        if (&server->clients[i] != user) continue;
        printf("%s has left the server\n", user->name);

        pthread_mutex_lock(&server->ch_mutex);

        // Remove user from channel
        channel_remove_user(user->channel, user);

        close(user->connection.sock);

        // Swap removed user with last user added, then blank the last user slot
        user_t* swap_user = &server->clients[i];
        *swap_user = server->clients[server->client_qty-1];

        memset(&server->clients[server->client_qty-1], 0, sizeof(user_t));

        // Update the socket to return the correct user pointer
        struct epoll_event in_event = {
            .events = EPOLLIN | EPOLLRDHUP,
            .data.ptr = swap_user
        };
        epoll_ctl(channel->in_epoll, EPOLL_CTL_MOD, swap_user->connection.sock, &in_event);

        struct epoll_event out_event = {
            .events = EPOLLOUT,
            .data.ptr = swap_user
        };
        epoll_ctl(channel->out_epoll, EPOLL_CTL_MOD, swap_user->connection.sock, &out_event);

        pthread_mutex_unlock(&server->ch_mutex);

        server->client_qty--;
        return true;
    }

    return false;
}

void server_destroy_channel(server_t* server, channel_t* channel) {
    // Swap removed user with last user added, then blank the last user slot
    printf("channel qty = %d\n", server->channel_qty);
    printf("deleting channel %s with %d users\n", channel->name, channel->user_qty);

    memset(channel, 0, sizeof(channel_t));

    server->channel_qty--;
}

void ping_client(user_t* user) {
    irc_packet_t pkt = {
        .user = "server",
        .length = sizeof("pong\n"),
        .data = "pong\n"
    };

    ssize_t sent_bytes = irc_send(&user->connection, &pkt, 0);
    printf("[send %d bytes] pinging user (%s)\n", sent_bytes, user->name);
}

channel_t* server_search_channel_by_name(server_t* server, char* name) {
    for (int i = 0; i < CHANNEL_QTY; i++) {
        if (!server->channels[i].name) continue;

        if (!strcmp(server->channels[i].name, name)) {
            printf("found channel %s\n", &server->channels[i]);
            return &server->channels[i];
        }
    }

    return NULL;
}

user_t* server_search_client_by_name(server_t* server, char* name) {
    for (int i = 0; i < server->client_qty; i++) {
        if (!strcmp(server->clients[i].name, name)) {
            return &server->clients[i];
        }
    }

    return NULL;
}

// Channel só é usado aqui
void server_relay_msg(user_t* user, irc_packet_t* pkt) {
    channel_t* channel = user->channel;

    struct epoll_event events[channel->user_qty];
    printf("\tserver_relay_msg::channel->user_qty = %d\n", channel->user_qty);
    int ready_qty = epoll_wait(user->channel->out_epoll, events, channel->user_qty, -1);
    if (ready_qty == -1) {
        perror("server_relay_msg::epoll_wait");
        return;
    }

    printf("\tserver_relay_msg::ready_qty = %d\n", ready_qty);
    for (int n = 0; n < channel->user_qty; n++) {
        user_t* user_to = events[n].data.ptr;
        printf("\tserver_relay_msg::user_to = %s\n", user_to->name);
        if (user == user_to) continue;

        printf("\tUser %s is ready to recv (on %s)!\n", user_to->name, user_to->channel->name);

        ssize_t sent_bytes = irc_send(&user_to->connection, pkt, 0);
        printf("\tsend %d bytes to user %s\n", sent_bytes, user_to->name);
    }
}

void handle_cmds(irc_cmds_e cmd_type, user_t* user, irc_packet_t* pkt, server_t* server) {
    switch (cmd_type) {
        case cmd_msg:
            if (!user->can_speak || !user->channel ) break;

            server_relay_msg(user, pkt);
            break;

        case cmd_connect:
            printf("server cannot connect\n");
            break;
        case cmd_join: {
            // /join <channel_name> password
            strtok(pkt->data, " \n");

            char* ch_name = strtok(NULL, " \n");
            size_t ch_name_len = strlen(ch_name);
            printf("ch_name: %s\n", ch_name);

            char* password = strtok(NULL, " \n");
            if (password) {
                size_t password_len = strlen(password);
                password[password_len] = '\0';
                printf("password: %s\n", password);
            }

            printf("user %s is joining channel %s\n", user->name, ch_name);

            // Verificação se o canal existe
            channel_t* channel = server_search_channel_by_name(server, ch_name);
            if (channel) {
                server_move_user(server, user, ch_name, password);
            } else {
                // Validate channel name
                bool valid_name = ch_name[0] == '#';
                valid_name &= strchr(ch_name, ',') == NULL;

                if (!valid_name) {
                    printf("Attempted to create channel with invalid name (%s)\n", ch_name);
                    irc_packet_t out_pkt = {
                        .user = "server",
                        .data = "Attempted to create channel with invalid name\n",
                        .length = sizeof("Attempted to create channel with invalid name\n")
                    };
                    ssize_t sent_bytes = irc_send(&user->connection, &out_pkt, 0);
                    break;
                }

                channel_remove_user(user->channel, user);
                server_add_channel(server, ch_name, user, password);
            }
            user->can_speak = true;
            break;
        }
        case cmd_quit:
            server_close_connection(server, user->channel, user);
            break;
        case cmd_ping:
            ping_client(user);
            break;
        case cmd_kick:
            if(!is_admin(user)) break;

            char* kicked_name = strchr(pkt->data, ' ')+1;
            size_t kicked_name_len = strlen(kicked_name)-1;
            kicked_name[kicked_name_len] = '\0';

            user_t* kicked_user = server_search_client_by_name(server, kicked_name);
            if(!kicked_user) break;
            server_close_connection(server, kicked_user->channel, kicked_user);
            break;
        case cmd_mute:
            if(!is_admin(user)) break;

            char* muted_name = strchr(pkt->data, ' ')+1;
            size_t muted_name_len = strlen(muted_name)-1;
            muted_name[muted_name_len] = '\0';

            user_t* muted_user = server_search_client_by_name(server, muted_name);
            if(!muted_user) break;
            muted_user->can_speak = false;
            break;
        case cmd_unmute:
            if(!is_admin(user)) break;

            char* unmuted_name = strchr(pkt->data, ' ')+1;
            size_t unmuted_name_len = strlen(unmuted_name)-1;
            unmuted_name[unmuted_name_len] = '\0';

            user_t* unmuted_user = server_search_client_by_name(server, unmuted_name);
            if(!unmuted_user) break;
            unmuted_user->can_speak = true;
            break;
        case cmd_whois:
            if(!is_admin(user)) break;

            char* username = strchr(pkt->data, ' ')+1;
            size_t username_len = strlen(username)-1;
            username[username_len] = '\0';

            user_t* srch_user = server_search_client_by_name(server, username);
            if(!srch_user || srch_user->channel != user->channel) break;

            char* ascii_address = inet_ntoa(srch_user->connection.addr.sin_addr);

            strcpy(pkt->data, ascii_address);
            strcat(pkt->data, "\n");
            irc_send(&srch_user->channel->admin->connection, pkt, 0);
            break;
        case cmd_nickname:
            strtok(pkt->data, " \n");

            char* new_nick = strtok(NULL, " \n");
            size_t new_nick_len = strlen(new_nick);
            new_nick[new_nick_len] = '\0';
            printf("new_nick: %s\n", new_nick);

            user_t* existing_user = server_search_client_by_name(server, new_nick);
            if(existing_user) {
                printf("Attempted to change nick to existing name (%s)\n", new_nick);
                irc_packet_t out_pkt = {
                    .user = "server",
                    .data = "Attempted to change nick to existing name\n",
                    .length = sizeof("Attempted to change nick to existing name\n")
                };
                ssize_t sent_bytes = irc_send(&user->connection, &out_pkt, 0);
                break;
            } else {
                printf("name change to (%s) sucessfull :)\n", new_nick);
                irc_packet_t out_pkt = {
                    .user = "server",
                    .data = "nick ok :)\n",
                    .length = sizeof("nick ok :)\n")
                };
                ssize_t sent_bytes = irc_send(&user->connection, &out_pkt, 0);

                strcpy(user->name, new_nick);
                break;
            }

    }
}


struct channel_args {
    server_t* server;
    channel_t* channel;
};

void* channel_chat(void* args) {
    struct channel_args* ch_args = (struct channel_args*) args;
    server_t* server = ch_args->server;
    channel_t* channel = ch_args->channel;
    free(ch_args);

    irc_packet_t pkt;

    struct epoll_event in_events[20];
    while(channel->user_qty) {
        int ready_qty = epoll_wait(channel->in_epoll, in_events, channel->user_qty, -1);
        if (ready_qty == -1) {
            perror("channel_chat::epoll_wait");
            exit(1);
            continue;
        }

        for (int n = 0; n < ready_qty; n++) {
            user_t* user = in_events[n].data.ptr;
            printf("Got event %u from user %s (chanel %s)!\n",
                in_events[n].events,
                user->name,
                user->channel->name);

            if (in_events[n].events & EPOLLRDHUP) {
                printf("EPOLLRDHUP Client has disconnected\n");
                server_close_connection(server, user->channel, user);
                continue;
            }

            int received = irc_recv(&user->connection, &pkt, 0);
            if (received == 0) {
                printf("Client has disconnected\n");
                server_close_connection(server, user->channel, user);
                continue;
            } else if (received == -1) {
                // perror("channel_chat::irc_recv");
                continue;
            } else if (received == -2) {
                continue;
            }

            irc_cmds_e cmd_type = parse_msg(pkt.data);
            printf("(%s) %s: %s", all_irc_cmd_types[cmd_type], user->name, pkt.data);
            handle_cmds(cmd_type, user, &pkt, server);
        }

        memset(pkt.data, '\0', pkt.length);
    }

    printf("chanel_chat::channel %s shutting down... (destroying thread)\n", channel->name);
    server_destroy_channel(server, channel);
    return NULL;
}

void server_add_channel(server_t* server, char* name, user_t* user, char* password) {
    pthread_mutex_lock(&server->ch_mutex);

    if (server->channel_qty == CHANNEL_QTY) {
        printf("server_add_channel::channel-search found no available slots :/\n");
        exit(1);
        return;
    }

    channel_t* new_channel = NULL;
    for (int i = 0; i < CHANNEL_QTY; i++) {
        if(server->channels[i].name[0] == '\0') {
            new_channel = &server->channels[i];
            break;
        }
    }

    strncpy(new_channel->name, name, CHANNEL_NAME_LEN);
    if (password) {
        strncpy(new_channel->password, password, CHANNEL_PASS_LEN);
    }
    printf("new channel name is %s\n", new_channel->name);

    int clients_in = epoll_create1(0);
    if (clients_in == -1) {
        perror("channel_chat::epoll_create1");
        exit(1);
    }
    new_channel->in_epoll = clients_in;

    int clients_out = epoll_create1(0);
    if (clients_out == -1) {
        perror("channel_chat::epoll_create1");
        exit(1);
    }
    new_channel->out_epoll = clients_out;

    new_channel->admin = user;
    channel_add_user(new_channel, user, password);

    struct channel_args* ch_args = malloc(sizeof(struct channel_args));
    ch_args->server = server;
    ch_args->channel = new_channel;
    server->channel_qty++;
    pthread_create(&new_channel->thread, NULL, channel_chat, ch_args);

    pthread_mutex_unlock(&server->ch_mutex);
}


#endif