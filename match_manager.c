// match_manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "server.h"

#define MAX_MATCHES 50

// Global variables (không dùng static để có thể truy cập từ module khác)
Match matches[MAX_MATCHES];
pthread_mutex_t match_mutex = PTHREAD_MUTEX_INITIALIZER;

// Generate random match ID
void generate_match_id(char *output, int length)
{
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    output[0] = 'M';
    for (int i = 1; i < length - 1; i++)
    {
        output[i] = charset[rand() % 36];
    }
    output[length - 1] = '\0';
}

// Initialize match manager
void match_manager_init()
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        matches[i].is_active = 0;
    }
}

// Find match by players
int find_match_by_players(const char *player1, const char *player2)
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (matches[i].is_active)
        {
            if ((strcmp(matches[i].white_player, player1) == 0 &&
                 strcmp(matches[i].black_player, player2) == 0) ||
                (strcmp(matches[i].white_player, player2) == 0 &&
                 strcmp(matches[i].black_player, player1) == 0))
            {
                return i;
            }
        }
    }
    return -1;
}

// Find free match slot
int find_free_match_slot()
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (!matches[i].is_active)
            return i;
    }
    return -1;
}

// Initialize chess board
void init_board(char board[8][8])
{
    // Back ranks
    const char back_rank[] = "RNBQKBNR";
    for (int i = 0; i < 8; i++)
    {
        board[0][i] = back_rank[i];      // Black pieces
        board[7][i] = back_rank[i] + 32; // White pieces (lowercase)
    }

    // Pawns
    for (int i = 0; i < 8; i++)
    {
        board[1][i] = 'P'; // Black pawns
        board[6][i] = 'p'; // White pawns
    }

    // Empty squares
    for (int row = 2; row < 6; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            board[row][col] = '.';
        }
    }
}

// Create match and send START_GAME
int create_match(int challenger_idx, int opponent_idx)
{
    pthread_mutex_lock(&match_mutex);

    int match_idx = find_free_match_slot();
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(challenger_idx, "No available match slots");
        return -1;
    }

    Match *match = &matches[match_idx];
    generate_match_id(match->match_id, 10);

    // Randomly assign colors
    if (rand() % 2 == 0)
    {
        strncpy(match->white_player, clients[challenger_idx].username, MAX_USERNAME - 1);
        strncpy(match->black_player, clients[opponent_idx].username, MAX_USERNAME - 1);
        match->white_client_idx = challenger_idx;
        match->black_client_idx = opponent_idx;
    }
    else
    {
        strncpy(match->white_player, clients[opponent_idx].username, MAX_USERNAME - 1);
        strncpy(match->black_player, clients[challenger_idx].username, MAX_USERNAME - 1);
        match->white_client_idx = opponent_idx;
        match->black_client_idx = challenger_idx;
    }

    init_board(match->board);
    match->current_turn = 0; // White starts
    match->is_active = 1;

    pthread_mutex_unlock(&match_mutex);

    // Update player status
    pthread_mutex_lock(&clients_mutex);
    clients[challenger_idx].status = STATUS_IN_MATCH;
    clients[opponent_idx].status = STATUS_IN_MATCH;
    pthread_mutex_unlock(&clients_mutex);

    // Create board string (simple representation)
    char board_str[256] = "Initial position";

    // Send START_GAME to both players
    cJSON *start_game = cJSON_CreateObject();
    cJSON_AddStringToObject(start_game, "action", "START_GAME");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "matchId", match->match_id);
    cJSON_AddStringToObject(data, "white", match->white_player);
    cJSON_AddStringToObject(data, "black", match->black_player);
    cJSON_AddStringToObject(data, "board", board_str);
    cJSON_AddItemToObject(start_game, "data", data);

    send_json(challenger_idx, start_game);
    send_json(opponent_idx, start_game);

    cJSON_Delete(start_game);

    printf("Match created: %s vs %s (Match ID: %s)\n",
           match->white_player, match->black_player, match->match_id);

    return match_idx;
}

// Handle CHALLENGE
int handle_challenge(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *from_obj = cJSON_GetObjectItem(data, "from");
    cJSON *to_obj = cJSON_GetObjectItem(data, "to");

    if (!from_obj || !to_obj)
    {
        send_error(client_idx, "Missing from or to field");
        return -1;
    }

    const char *from = from_obj->valuestring;
    const char *to = to_obj->valuestring;

    // Check if sender matches logged-in user
    pthread_mutex_lock(&clients_mutex);
    if (strcmp(clients[client_idx].username, from) != 0)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Username mismatch");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Find opponent
    int opponent_idx = find_client_by_username(to);
    if (opponent_idx == -1)
    {
        send_error(client_idx, "Opponent not found or offline");
        return -1;
    }

    // Check opponent status
    pthread_mutex_lock(&clients_mutex);
    if (clients[opponent_idx].status != STATUS_ONLINE)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Opponent is not available");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Send INCOMING_CHALLENGE to opponent
    cJSON *challenge = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge, "action", "INCOMING_CHALLENGE");
    cJSON *challenge_data = cJSON_CreateObject();
    cJSON_AddStringToObject(challenge_data, "from", from);
    cJSON_AddItemToObject(challenge, "data", challenge_data);

    send_json(opponent_idx, challenge);
    cJSON_Delete(challenge);

    printf("%s challenged %s\n", from, to);
    return 0;
}

// Handle ACCEPT
int handle_accept(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *from_obj = cJSON_GetObjectItem(data, "from");
    cJSON *to_obj = cJSON_GetObjectItem(data, "to");

    if (!from_obj || !to_obj)
    {
        send_error(client_idx, "Missing from or to field");
        return -1;
    }

    const char *from = from_obj->valuestring;
    const char *to = to_obj->valuestring;

    // Find challenger
    int challenger_idx = find_client_by_username(to);
    if (challenger_idx == -1)
    {
        send_error(client_idx, "Challenger not found");
        return -1;
    }

    // Create match
    create_match(challenger_idx, client_idx);

    printf("%s accepted challenge from %s\n", from, to);
    return 0;
}

// Handle DECLINE
int handle_decline(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *from_obj = cJSON_GetObjectItem(data, "from");
    cJSON *to_obj = cJSON_GetObjectItem(data, "to");

    if (!from_obj || !to_obj)
    {
        send_error(client_idx, "Missing from or to field");
        return -1;
    }

    const char *from = from_obj->valuestring;
    const char *to = to_obj->valuestring;

    // Find challenger and notify
    int challenger_idx = find_client_by_username(to);
    if (challenger_idx != -1)
    {
        cJSON *decline = cJSON_CreateObject();
        cJSON_AddStringToObject(decline, "action", "CHALLENGE_DECLINED");
        cJSON *decline_data = cJSON_CreateObject();
        cJSON_AddStringToObject(decline_data, "from", from);
        cJSON_AddItemToObject(decline, "data", decline_data);

        send_json(challenger_idx, decline);
        cJSON_Delete(decline);
    }

    printf("%s declined challenge from %s\n", from, to);
    return 0;
}

// Find match by match ID
int find_match_by_id(const char *match_id)
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (matches[i].is_active && strcmp(matches[i].match_id, match_id) == 0)
        {
            return i;
        }
    }
    return -1;
}

// Get match for a client
int get_client_match(int client_idx)
{
    const char *username = clients[client_idx].username;
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (matches[i].is_active)
        {
            if (strcmp(matches[i].white_player, username) == 0 ||
                strcmp(matches[i].black_player, username) == 0)
            {
                return i;
            }
        }
    }
    return -1;
}