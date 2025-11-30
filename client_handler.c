// client_handler.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "cJSON.h"
#include "server.h"

// Receive message (until \n)
int recv_message(int socket, char *buffer, int buffer_size)
{
    int total = 0;
    char c;

    while (total < buffer_size - 1)
    {
        int n = recv(socket, &c, 1, 0);
        if (n <= 0)
            return -1;

        buffer[total++] = c;
        if (c == '\n')
            break;
    }

    buffer[total] = '\0';
    return total;
}

// Send JSON message
int send_json(int client_idx, cJSON *json)
{
    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str)
        return -1;

    pthread_mutex_lock(&clients[client_idx].send_mutex);

    // Add newline
    int len = strlen(json_str);
    char *message = malloc(len + 2);
    strcpy(message, json_str);
    message[len] = '\n';
    message[len + 1] = '\0';

    int result = send(clients[client_idx].socket, message, len + 1, 0);

    pthread_mutex_unlock(&clients[client_idx].send_mutex);

    free(message);
    free(json_str);

    return result;
}

// Send error message
void send_error(int client_idx, const char *reason)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ERROR");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "reason", reason);
    cJSON_AddItemToObject(json, "data", data);

    send_json(client_idx, json);
    cJSON_Delete(json);
}

// Process received message
void process_message(int client_idx, const char *message)
{
    cJSON *json = cJSON_Parse(message);
    if (!json)
    {
        send_error(client_idx, "Invalid JSON");
        return;
    }

    cJSON *action_obj = cJSON_GetObjectItem(json, "action");
    cJSON *data_obj = cJSON_GetObjectItem(json, "data");

    if (!action_obj || !cJSON_IsString(action_obj))
    {
        send_error(client_idx, "Missing action field");
        cJSON_Delete(json);
        return;
    }

    const char *action = action_obj->valuestring;

    printf("[Client %d] Action: %s\n", client_idx, action);

    // Route to appropriate handler
    if (strcmp(action, "REGISTER") == 0)
    {
        handle_register(client_idx, data_obj);
    }
    else if (strcmp(action, "LOGIN") == 0)
    {
        handle_login(client_idx, data_obj);
    }
    else if (strcmp(action, "REQUEST_PLAYER_LIST") == 0)
    {
        handle_request_player_list(client_idx);
    }
    else if (strcmp(action, "CHALLENGE") == 0)
    {
        handle_challenge(client_idx, data_obj);
    }
    else if (strcmp(action, "ACCEPT") == 0)
    {
        handle_accept(client_idx, data_obj);
    }
    else if (strcmp(action, "DECLINE") == 0)
    {
        handle_decline(client_idx, data_obj);
    }
    else if (strcmp(action, "MOVE") == 0)
    {
        handle_move(client_idx, data_obj);
    }
    else if (strcmp(action, "PING") == 0)
    {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "PONG");
        cJSON_AddItemToObject(response, "data", cJSON_CreateObject());
        send_json(client_idx, response);
        cJSON_Delete(response);
    }
    else
    {
        send_error(client_idx, "Unknown action");
    }

    cJSON_Delete(json);
}

// Client handler thread
void *client_handler(void *arg)
{
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    int client_idx = args->client_index;
    free(args);

    char buffer[BUFFER_SIZE];

    printf("Thread started for client %d\n", client_idx);

    // Initialize mutex
    pthread_mutex_init(&clients[client_idx].send_mutex, NULL);

    // Main receive loop
    while (1)
    {
        int n = recv_message(clients[client_idx].socket, buffer, BUFFER_SIZE);

        if (n <= 0)
        {
            printf("Client %d disconnected\n", client_idx);
            break;
        }

        printf("Client %d: %s", client_idx, buffer);
        process_message(client_idx, buffer);
    }

    // Cleanup
    logout_client(client_idx);
    close(clients[client_idx].socket);

    pthread_mutex_lock(&clients_mutex);
    clients[client_idx].is_active = 0;
    pthread_mutex_unlock(&clients_mutex);

    pthread_mutex_destroy(&clients[client_idx].send_mutex);

    printf("Thread ended for client %d\n", client_idx);
    return NULL;
}