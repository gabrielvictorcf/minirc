#include "server.h"

int main(int argc, char const *argv[]) {
    server_t server = server_new(AF_INET, "0.0.0.0", SERVER_PORT);
    printf("Server up and running!\n");

    user_t new_user = {.can_speak = true};
    irc_sock_t* new_conn = &new_user.connection;
    new_conn->addr_len = sizeof(struct sockaddr_in);
    while (true) {
        int client_sock = accept(server.listening.sock, (struct sockaddr*) &new_conn->addr, &new_conn->addr_len);

        if (client_sock == -1) {
            perror("Connection refused");
        } else {
            recv(client_sock, new_user.name, IRC_NAME_LEN, 0);
            user_t* existing_user = server_search_client_by_name(&server, new_user.name);
            if(existing_user) {
                send(client_sock, "rejected", sizeof("accepted"), 0);
                continue;
            }

            send(client_sock, "accepted", sizeof("accepted"), 0);
            new_user.connection.sock = client_sock;

            user_t* server_user = &server.clients[server.client_qty++];
            *server_user = new_user;
            if (server.client_qty == 1) {
                server_add_channel(&server, "#main", server_user, NULL);
            } else {
                channel_t* main_channel = server_search_channel_by_name(&server, "#main");
                channel_add_user(main_channel, server_user, NULL);
            }
        }
    }

    return 0;
}
