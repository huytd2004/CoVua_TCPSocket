// main.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include "cJSON.h"
#include "server.h"

#define PORT 8888
#define MAX_CLIENTS 100

int server_socket;
Client clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int sig)
{
    printf("\nShutting down server...\n");
    close(server_socket);
    exit(0);
}

int main()
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_socket;
    pthread_t thread_id;

    // Initialize signal handler
    signal(SIGINT, signal_handler);

    // Initialize modules
    auth_manager_init();
    match_manager_init();
    game_manager_init();

    // Initialize clients array
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = -1;
        clients[i].is_active = 0;
    }

    // Create server socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Configure server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_socket, 10) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Chess Server started on port %d\n", PORT);
    printf("Waiting for connections...\n");

    // Accept loop
    while (1)
    {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // Find free slot
        int slot = -1;
        pthread_mutex_lock(&clients_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!clients[i].is_active)
            {
                slot = i;
                clients[i].socket = client_socket;
                clients[i].is_active = 1;
                clients[i].username[0] = '\0';
                clients[i].session_id[0] = '\0';
                clients[i].status = STATUS_OFFLINE;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        if (slot == -1)
        {
            printf("Max clients reached. Rejecting connection.\n");
            close(client_socket);
            continue;
        }

        // Create thread for client
        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->client_index = slot;

        if (pthread_create(&thread_id, NULL, client_handler, args) != 0)
        {
            perror("Thread creation failed");
            clients[slot].is_active = 0;
            close(client_socket);
            free(args);
        }
        else
        {
            pthread_detach(thread_id);
        }
    }

    close(server_socket);
    return 0;
}
