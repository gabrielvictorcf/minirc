#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <netinet/ip.h>
#include <arpa/inet.h>

#include <sys/epoll.h>
#include <pthread.h>
#include <signal.h>

// Server-related defines
#define SERVER_PORT 9090
#define SERVER_CLIENT_QTY 4
#define CHANNEL_CLIENT_QTY 4
#define CHANNEL_QTY 4
#define CHANNEL_NAME_LEN 200
#define CHANNEL_PASS_LEN 20

// This enum and the cmd_types array must follow the same order
typedef enum _irc_commands {
    cmd_connect,
    cmd_quit,
    cmd_ping,
    cmd_join,
    cmd_nickname,
    cmd_kick,
    cmd_mute,
    cmd_unmute,
    cmd_whois,
    cmd_msg,
    _len
} irc_cmds_e;

const char* all_irc_cmd_types[] = {
    "/connect",
    "/quit",
    "/ping",
    "/join",
    "/nickname",
    "/kick",
    "/mute",
    "/unmute",
    "/whois",
    "message"
};

irc_cmds_e parse_msg(char* msg) {
    msg += strspn(msg, " ");
    if (msg[0] != '/') return cmd_msg;

    int cmd_len = strcspn(msg, " \n\0")-1;
    for (int i = 0; i < _len-1; i++) {
        const char* cmd = all_irc_cmd_types[i];
        if(!strncmp(msg, cmd, cmd_len)) {
            return i;
        }
    }

    return cmd_msg; // default case: could not recognize command, so it must be a message
}

#define IRC_NAME_LEN 50
#define MSG_LEN 4096

typedef struct _irc_sock {
    int addr_family;
    int sock;
    struct sockaddr_in addr;
    socklen_t addr_len;
} irc_sock_t;

irc_sock_t irc_sock_new(int addr_family, char* addr, in_port_t port) {
    int sock = socket(addr_family, SOCK_STREAM, 0);
    struct sockaddr_in sock_addr = {
        .sin_family = addr_family,
        .sin_port = htons(port)
    };
    inet_pton(addr_family, addr, (void*) &sock_addr.sin_addr);
    socklen_t addr_len = sizeof(sock_addr);

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    return (irc_sock_t) {
        .addr_family = addr_family,
        .sock = sock,
        .addr = sock_addr,
        .addr_len = addr_len
    };
}

typedef struct _irc_packet {
    short length;
    char user[IRC_NAME_LEN];
    char data[MSG_LEN];
} irc_packet_t;

int irc_send(irc_sock_t* user, irc_packet_t* pkt, int flags) {
    if(user->sock == -1){
        return 0;
    }
    if(send(user->sock, &pkt->length, sizeof(pkt->length), flags) == -1) { return -1; }; // send message length
    if(send(user->sock, pkt->user, IRC_NAME_LEN, flags) == -1) { return -1; }; // send message's user

    int sent = 0, len_left = pkt->length;
    while(sent < len_left) { // how many we have left to send
        int sent_now = send(user->sock, &pkt->data[sent], len_left, flags);
        if (sent_now == -1) { return -1; } // return -1 on failure
        sent += sent_now;
        len_left -= sent_now;
    }

    return sent; // return quantity of bytes sent on success
}

int irc_recv(irc_sock_t* user, irc_packet_t* pkt, int flags) {
    ssize_t bytes_received = recv(user->sock, &pkt->length, sizeof(pkt->length), flags);
    if (bytes_received == -1) {
        perror("irc_recv");
        return -2;
    }

    bytes_received |= recv(user->sock, pkt->user, IRC_NAME_LEN, 0);
    bytes_received |= recv(user->sock, pkt->data, pkt->length, 0);

    return bytes_received;
}