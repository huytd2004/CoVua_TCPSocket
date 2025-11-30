// auth_manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include "cJSON.h"
#include "server.h"

#define MAX_USERS 1000
#define USERS_FILE "users.json"

static User users[MAX_USERS];
static int user_count = 0;
static pthread_mutex_t auth_mutex = PTHREAD_MUTEX_INITIALIZER;

// SHA-256 hash function
void sha256_string(const char *input, char *output)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)input, strlen(input), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(output + (i * 2), "%02x", hash[i]);
    }
    output[64] = '\0';
}

// Generate random session ID
void generate_session_id(char *output, int length)
{
    const char charset[] = "0123456789abcdef";
    for (int i = 0; i < length - 1; i++)
    {
        output[i] = charset[rand() % 16];
    }
    output[length - 1] = '\0';
}

// Initialize auth manager
void auth_manager_init()
{
    srand(time(NULL));

    // Load users from JSON file
    FILE *f = fopen(USERS_FILE, "r");
    if (f)
    {
        fseek(f, 0, SEEK_END);
        long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        char *json_str = malloc(fsize + 1);
        fread(json_str, 1, fsize, f);
        fclose(f);
        json_str[fsize] = '\0';

        cJSON *root = cJSON_Parse(json_str);
        free(json_str);

        if (root)
        {
            cJSON *users_array = cJSON_GetObjectItem(root, "users");
            if (users_array)
            {
                user_count = 0;
                cJSON *user_obj = NULL;
                cJSON_ArrayForEach(user_obj, users_array)
                {
                    if (user_count >= MAX_USERS)
                        break;

                    cJSON *username = cJSON_GetObjectItem(user_obj, "username");
                    cJSON *password_hash = cJSON_GetObjectItem(user_obj, "password_hash");

                    if (username && password_hash)
                    {
                        strncpy(users[user_count].username, username->valuestring, MAX_USERNAME - 1);
                        strncpy(users[user_count].password_hash, password_hash->valuestring, 64);
                        users[user_count].is_online = 0;
                        user_count++;
                    }
                }
            }
            cJSON_Delete(root);
            printf("Loaded %d users from database\n", user_count);
        }
    }
    else
    {
        printf("No existing user database found\n");
    }
}

// Save users to JSON file
void save_users()
{
    cJSON *root = cJSON_CreateObject();
    cJSON *users_array = cJSON_CreateArray();

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(user_obj, "username", users[i].username);
        cJSON_AddStringToObject(user_obj, "password_hash", users[i].password_hash);
        cJSON_AddItemToArray(users_array, user_obj);
    }

    cJSON_AddItemToObject(root, "users", users_array);

    FILE *f = fopen(USERS_FILE, "w");
    if (f)
    {
        char *json_str = cJSON_Print(root);
        fprintf(f, "%s", json_str);
        fclose(f);
        free(json_str);
    }

    cJSON_Delete(root);
}

// Find user by username
int find_user(const char *username)
{
    for (int i = 0; i < user_count; i++)
    {
        if (strcmp(users[i].username, username) == 0)
        {
            return i;
        }
    }
    return -1;
}

// Handle REGISTER
int handle_register(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *username_obj = cJSON_GetObjectItem(data, "username");
    cJSON *password_obj = cJSON_GetObjectItem(data, "password");

    if (!username_obj || !password_obj)
    {
        send_error(client_idx, "Missing username or password");
        return -1;
    }

    const char *username = username_obj->valuestring;
    const char *password = password_obj->valuestring;

    pthread_mutex_lock(&auth_mutex);

    // Check if username exists
    if (find_user(username) != -1)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "REGISTER_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Username already exists");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Create new user
    if (user_count >= MAX_USERS)
    {
        pthread_mutex_unlock(&auth_mutex);
        send_error(client_idx, "Server full");
        return -1;
    }

    strncpy(users[user_count].username, username, MAX_USERNAME - 1);
    sha256_string(password, users[user_count].password_hash);
    users[user_count].is_online = 0;
    user_count++;

    save_users();
    pthread_mutex_unlock(&auth_mutex);

    // Send success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "REGISTER_SUCCESS");
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "message", "Account created");
    cJSON_AddItemToObject(response, "data", resp_data);
    send_json(client_idx, response);
    cJSON_Delete(response);

    printf("User registered: %s\n", username);
    return 0;
}

// Handle LOGIN
int handle_login(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *username_obj = cJSON_GetObjectItem(data, "username");
    cJSON *password_obj = cJSON_GetObjectItem(data, "password");

    if (!username_obj || !password_obj)
    {
        send_error(client_idx, "Missing username or password");
        return -1;
    }

    const char *username = username_obj->valuestring;
    const char *password = password_obj->valuestring;

    // Hash password
    char password_hash[65];
    sha256_string(password, password_hash);

    pthread_mutex_lock(&auth_mutex);

    int user_idx = find_user(username);
    if (user_idx == -1)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "User not found");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    if (strcmp(users[user_idx].password_hash, password_hash) != 0)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Invalid password");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Check if already logged in
    if (users[user_idx].is_online)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Already logged in");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Login success
    users[user_idx].is_online = 1;
    pthread_mutex_unlock(&auth_mutex);

    // Generate session ID
    char session_id[MAX_SESSION_ID];
    generate_session_id(session_id, 16);

    pthread_mutex_lock(&clients_mutex);
    strncpy(clients[client_idx].username, username, MAX_USERNAME - 1);
    strncpy(clients[client_idx].session_id, session_id, MAX_SESSION_ID - 1);
    clients[client_idx].status = STATUS_ONLINE;
    pthread_mutex_unlock(&clients_mutex);

    // Send success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "LOGIN_SUCCESS");
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "sessionId", session_id);
    cJSON_AddStringToObject(resp_data, "username", username);
    cJSON_AddItemToObject(response, "data", resp_data);
    send_json(client_idx, response);
    cJSON_Delete(response);

    printf("User logged in: %s\n", username);
    return 0;
}

// Logout client
void logout_client(int client_idx)
{
    pthread_mutex_lock(&clients_mutex);
    if (clients[client_idx].username[0] != '\0')
    {
        pthread_mutex_lock(&auth_mutex);
        int user_idx = find_user(clients[client_idx].username);
        if (user_idx != -1)
        {
            users[user_idx].is_online = 0;
        }
        pthread_mutex_unlock(&auth_mutex);

        printf("User logged out: %s\n", clients[client_idx].username);
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Find client by username
int find_client_by_username(const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && strcmp(clients[i].username, username) == 0)
        {
            return i;
        }
    }
    return -1;
}

// Handle REQUEST_PLAYER_LIST
int handle_request_player_list(int client_idx)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "PLAYER_LIST");
    cJSON *data = cJSON_CreateObject();
    cJSON *players = cJSON_CreateArray();

    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && clients[i].username[0] != '\0' && i != client_idx)
        {
            cJSON *player = cJSON_CreateObject();
            cJSON_AddStringToObject(player, "username", clients[i].username);

            const char *status_str;
            switch (clients[i].status)
            {
            case STATUS_ONLINE:
                status_str = "ONLINE";
                break;
            case STATUS_IN_MATCH:
                status_str = "IN_MATCH";
                break;
            default:
                status_str = "OFFLINE";
                break;
            }
            cJSON_AddStringToObject(player, "status", status_str);
            cJSON_AddItemToArray(players, player);
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    cJSON_AddItemToObject(data, "players", players);
    cJSON_AddItemToObject(response, "data", data);

    send_json(client_idx, response);
    cJSON_Delete(response);

    return 0;
}