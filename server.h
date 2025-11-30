// server.h
#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

#define MAX_USERNAME 32
#define MAX_PASSWORD 64
#define MAX_SESSION_ID 64
#define MAX_MATCH_ID 32
#define BUFFER_SIZE 4096
#define MAX_CLIENTS 100 // hoặc số lượng phù hợp
#define MAX_MATCHES 50  // Thêm dòng này

// Player status
typedef enum
{
    STATUS_OFFLINE,
    STATUS_ONLINE,
    STATUS_IN_MATCH
} PlayerStatus;

// Client structure
typedef struct
{
    int socket;
    int is_active;
    char username[MAX_USERNAME];
    char session_id[MAX_SESSION_ID];
    PlayerStatus status;
    pthread_mutex_t send_mutex;
} Client;

// Thread arguments
typedef struct
{
    int client_index;
} ClientThreadArgs;

// User account
typedef struct
{
    char username[MAX_USERNAME];
    char password_hash[65]; // SHA-256 hex string
    int is_online;
} User;

// Match structure
typedef struct
{
    char match_id[MAX_MATCH_ID];
    char white_player[MAX_USERNAME];
    char black_player[MAX_USERNAME];
    int white_client_idx;
    int black_client_idx;
    int is_active;
    char board[8][8]; // Simple board representation
    int current_turn; // 0 = white, 1 = black
} Match;

// Global arrays
extern Client clients[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;
extern pthread_mutex_t match_mutex;
extern Match matches[MAX_MATCHES];

// Module functions
void auth_manager_init();
void match_manager_init();
void game_manager_init();

void *client_handler(void *arg);
int send_json(int client_idx, cJSON *json);
int recv_message(int socket, char *buffer, int buffer_size);

// Auth functions
int handle_register(int client_idx, cJSON *data);
int handle_login(int client_idx, cJSON *data);
void logout_client(int client_idx);

// Player list functions
int handle_request_player_list(int client_idx);

// Match functions
int handle_challenge(int client_idx, cJSON *data);
int handle_accept(int client_idx, cJSON *data);
int handle_decline(int client_idx, cJSON *data);

// Game functions
int handle_move(int client_idx, cJSON *data);
void send_game_result(int match_idx, const char *winner, const char *reason);

// Utility functions
void generate_session_id(char *output, int length);
void generate_match_id(char *output, int length);
void sha256_string(const char *input, char *output);
int find_client_by_username(const char *username);

#endif // SERVER_H