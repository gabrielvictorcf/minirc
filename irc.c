#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>

void exit_error(const char* errmsg) {
    perror(errmsg);
    exit(1);
}

// IRC defines and structs
#define MSG_LEN 4096

typedef struct _irc_socket {
    int sock;
    struct sockaddr_in addr;
    int addr_len;
} irc_socket_t;

typedef struct _irc_packet {
    short length;
    char data[MSG_LEN];
} irc_packet_t;

int irc_send(irc_socket_t* user, irc_packet_t* pkt, int flags) {
    if (send(user->sock, &pkt->length, sizeof(pkt->length), flags) == -1) { return -1; }; // send message length

    int sent = 0, len_left = pkt->length;
    while(sent < len_left) { // how many we have left to send
        int sent_now = send(user->sock, &pkt->data[sent], len_left, flags);
        if (sent_now == -1) { return -1; } // return -1 on failure
        sent += sent_now;
        len_left -= sent_now;
    }

    return sent; // return quantity of bytes sent on success
}

void* chat_send(void *args) {
    irc_socket_t* user = (irc_socket_t*) args;

    irc_packet_t pkt = {0};
    while (1) {
        char* read_string = fgets(pkt.data, MSG_LEN, stdin);
        if (read_string[0] == '\n') continue;
        if (read_string == NULL) break;

        pkt.length = strlen(pkt.data);
        pkt.data[pkt.length-1] = '\0';
        irc_send(user, &pkt, 0);
        memset(pkt.data, '\0', pkt.length);
    }
}

int irc_recv(irc_socket_t* user, irc_packet_t* pkt, int flags) {
    ssize_t bytes_received = recv(user->sock, &pkt->length, sizeof(pkt->length), flags);
    bytes_received |= recv(user->sock, pkt->data, pkt->length, flags);

    return bytes_received;
}

void chat_recv(irc_socket_t* user) {
    irc_packet_t pkt = {0};
    while (1) {
        int received = irc_recv(user, &pkt, 0);
        if (received == 0) break;
        if (received <= -1) {
            perror("Error on recv");
            break;
        }

        printf("\x1b[1K\r%s\n", pkt.data);
        memset(pkt.data, '\0', pkt.length);
    }
}

void chat(irc_socket_t* user) {
    pthread_t input_thread;

    // To able to receive and send at the same time, one thread must recv while the other reads stdin and sends
    pthread_create(&input_thread, NULL, &chat_send, (void *)user);
    chat_recv(user);

    pthread_cancel(input_thread);
    close(user->sock);
    printf("quitting...\n");
}

int setup_client(irc_socket_t* user) {
    if (connect(user->sock, (const struct sockaddr *)&user->addr, (socklen_t)user->addr_len) != 0) {
        perror("Error on connecting");
        return false;
    }
    printf("Connection established!\n");

    return true;
}

int setup_server(irc_socket_t* user) {
    if (bind(user->sock, (struct sockaddr *)&user->addr, user->addr_len)) {
        perror("Erro ao fazer bind");
        return false;
    }
    printf("Socket bound\n");

    listen(user->sock, 1);

    user->sock = accept(user->sock, NULL, NULL);
    if (user->sock == -1) {
        perror("Failure on connection accept");
        return false;
    }
    printf("Connection accepted!\n");

    return true;
}

int main(int argc, char** argv) {
    // Create irc socket
    irc_socket_t user = {
        .sock = socket(AF_INET, SOCK_STREAM, 0),
        .addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = INADDR_ANY,
            .sin_port = htons(1837)
        },
        .addr_len = sizeof(struct sockaddr_in)
    };

    // Make debugging easier - set socket as reusable
    if (user.sock == -1) { exit_error("Error on creating socket"); }
    if (setsockopt(user.sock, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) { exit_error("setsockopt(SO_REUSEADDR) failed"); };
    printf("User created\n");

    // Try to connect as client - if there is no server listening, open a server (or error out)
    bool setup_complete = setup_client(&user);
    if (!setup_complete) {
        setup_complete = setup_server(&user);
        if(!setup_complete) exit_error("Setup failed");
    }

    chat(&user);
    return 0;
}