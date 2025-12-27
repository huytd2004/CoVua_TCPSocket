/**
 * match_history.c - Match History & Replay Module
 *
 * Module quản lý lịch sử ván đấu và replay:
 * - Lưu nước đi trong ván đấu
 * - Lưu ván đấu vào file JSON
 * - Truy vấn lịch sử ván đấu của user
 * - Xem lại chi tiết ván đấu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pthread.h>
#include "cJSON.h"
#include "server.h"

#define MATCHES_DIR "matches"
#define MAX_MOVES 500 // Số nước đi tối đa trong 1 ván

// Struct lưu lịch sử nước đi cho mỗi ván đấu đang diễn ra
typedef struct
{
    char match_id[32];
    char moves[MAX_MOVES][8]; // Mỗi nước đi dạng "E2E4" (max 7 chars + \0)
    int move_count;
    time_t start_time;
    int is_active;
} ActiveMatchMoves;

#define MAX_ACTIVE_MATCHES 50
static ActiveMatchMoves active_moves[MAX_ACTIVE_MATCHES];
static pthread_mutex_t history_mutex = PTHREAD_MUTEX_INITIALIZER;

// Forward declarations
extern Match matches[];
extern pthread_mutex_t match_mutex;
extern Client clients[];
extern pthread_mutex_t clients_mutex;

/**
 * match_history_init - Khởi tạo module và tạo thư mục matches
 */
void match_history_init()
{
    // Tạo thư mục matches nếu chưa có
    struct stat st = {0};
    if (stat(MATCHES_DIR, &st) == -1)
    {
        mkdir(MATCHES_DIR, 0755);
        printf("Created matches directory\n");
    }

    // Khởi tạo mảng active moves
    pthread_mutex_lock(&history_mutex);
    for (int i = 0; i < MAX_ACTIVE_MATCHES; i++)
    {
        active_moves[i].is_active = 0;
    }
    pthread_mutex_unlock(&history_mutex);

    printf("Match History module initialized\n");
}

/**
 * start_recording_match - Bắt đầu ghi nhận nước đi cho ván đấu mới
 * @match_id: ID của ván đấu
 */
void start_recording_match(const char *match_id)
{
    pthread_mutex_lock(&history_mutex);

    // Tìm slot trống
    for (int i = 0; i < MAX_ACTIVE_MATCHES; i++)
    {
        if (!active_moves[i].is_active)
        {
            strncpy(active_moves[i].match_id, match_id, 31);
            active_moves[i].match_id[31] = '\0';
            active_moves[i].move_count = 0;
            active_moves[i].start_time = time(NULL);
            active_moves[i].is_active = 1;
            break;
        }
    }

    pthread_mutex_unlock(&history_mutex);
}

/**
 * find_active_match_moves - Tìm slot ghi nhận nước đi của ván đấu
 * @match_id: ID của ván đấu
 * Return: Index trong mảng, -1 nếu không tìm thấy
 */
static int find_active_match_moves(const char *match_id)
{
    for (int i = 0; i < MAX_ACTIVE_MATCHES; i++)
    {
        if (active_moves[i].is_active &&
            strcmp(active_moves[i].match_id, match_id) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * record_move - Ghi nhận một nước đi
 * @match_id: ID của ván đấu
 * @from: Vị trí xuất phát (VD: "E2")
 * @to: Vị trí đích (VD: "E4")
 */
void record_move(const char *match_id, const char *from, const char *to)
{
    pthread_mutex_lock(&history_mutex);

    int idx = find_active_match_moves(match_id);
    if (idx != -1 && active_moves[idx].move_count < MAX_MOVES)
    {
        // Tạo chuỗi nước đi dạng "E2E4"
        char move[8];
        snprintf(move, sizeof(move), "%s%s", from, to);

        // Chuyển thành uppercase
        for (int i = 0; move[i]; i++)
        {
            if (move[i] >= 'a' && move[i] <= 'h')
                move[i] -= 32;
        }

        strncpy(active_moves[idx].moves[active_moves[idx].move_count], move, 7);
        active_moves[idx].moves[active_moves[idx].move_count][7] = '\0';
        active_moves[idx].move_count++;
    }

    pthread_mutex_unlock(&history_mutex);
}

/**
 * board_to_string - Chuyển bàn cờ thành chuỗi
 * @board: Mảng 8x8 bàn cờ
 * @output: Buffer để lưu chuỗi (ít nhất 65 bytes)
 */
static void board_to_string(char board[8][8], char *output)
{
    int pos = 0;
    for (int row = 0; row < 8; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            output[pos++] = board[row][col];
        }
    }
    output[pos] = '\0';
}

/**
 * save_match_history - Lưu lịch sử ván đấu vào file JSON
 * @match_id: ID ván đấu
 * @white: Username quân trắng
 * @black: Username quân đen
 * @winner: Người thắng hoặc "DRAW"/"ABORT"
 * @reason: Lý do kết thúc
 * @final_board: Bàn cờ cuối cùng
 */
void save_match_history(const char *match_id, const char *white, const char *black,
                        const char *winner, const char *reason, char final_board[8][8])
{
    pthread_mutex_lock(&history_mutex);

    int idx = find_active_match_moves(match_id);
    if (idx == -1)
    {
        pthread_mutex_unlock(&history_mutex);
        printf("Warning: No move history found for match %s\n", match_id);
        return;
    }

    // Tạo JSON object
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "matchId", match_id);
    cJSON_AddStringToObject(root, "white", white);
    cJSON_AddStringToObject(root, "black", black);
    cJSON_AddStringToObject(root, "winner", winner);
    cJSON_AddStringToObject(root, "reason", reason);
    cJSON_AddNumberToObject(root, "timestamp", (double)active_moves[idx].start_time);
    cJSON_AddNumberToObject(root, "endTime", (double)time(NULL));
    cJSON_AddNumberToObject(root, "moveCount", active_moves[idx].move_count);

    // Thêm mảng nước đi
    cJSON *moves_array = cJSON_CreateArray();
    for (int i = 0; i < active_moves[idx].move_count; i++)
    {
        cJSON_AddItemToArray(moves_array, cJSON_CreateString(active_moves[idx].moves[i]));
    }
    cJSON_AddItemToObject(root, "moves", moves_array);

    // Thêm bàn cờ cuối
    char board_str[65];
    board_to_string(final_board, board_str);
    cJSON_AddStringToObject(root, "finalBoard", board_str);

    // Đánh dấu không còn active
    active_moves[idx].is_active = 0;

    pthread_mutex_unlock(&history_mutex);

    // Ghi ra file
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", MATCHES_DIR, match_id);

    FILE *f = fopen(filepath, "w");
    if (f)
    {
        char *json_str = cJSON_Print(root);
        fprintf(f, "%s", json_str);
        fclose(f);
        free(json_str);
        printf("Match history saved: %s\n", filepath);
    }
    else
    {
        printf("Error: Could not save match history to %s\n", filepath);
    }

    cJSON_Delete(root);
}

/**
 * load_match_history - Load lịch sử ván đấu từ file
 * @match_id: ID ván đấu
 * Return: cJSON object hoặc NULL nếu không tìm thấy
 */
static cJSON *load_match_history(const char *match_id)
{
    char filepath[128];
    snprintf(filepath, sizeof(filepath), "%s/%s.json", MATCHES_DIR, match_id);

    FILE *f = fopen(filepath, "r");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *json_str = malloc(fsize + 1);
    fread(json_str, 1, fsize, f);
    fclose(f);
    json_str[fsize] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    return root;
}

/**
 * handle_get_match_history - Xử lý yêu cầu lấy danh sách ván đã chơi
 * @client_idx: Index của client
 * @data: JSON data (có thể chứa "username", nếu không thì lấy của client hiện tại)
 */
int handle_get_match_history(int client_idx, cJSON *data)
{
    // Lấy username cần tìm
    char target_username[MAX_USERNAME];

    if (data)
    {
        cJSON *username_obj = cJSON_GetObjectItem(data, "username");
        if (username_obj && cJSON_IsString(username_obj))
        {
            strncpy(target_username, username_obj->valuestring, MAX_USERNAME - 1);
            target_username[MAX_USERNAME - 1] = '\0';
        }
        else
        {
            pthread_mutex_lock(&clients_mutex);
            strncpy(target_username, clients[client_idx].username, MAX_USERNAME - 1);
            pthread_mutex_unlock(&clients_mutex);
        }
    }
    else
    {
        pthread_mutex_lock(&clients_mutex);
        strncpy(target_username, clients[client_idx].username, MAX_USERNAME - 1);
        pthread_mutex_unlock(&clients_mutex);
    }

    // Tạo response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "MATCH_HISTORY");
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "username", target_username);
    cJSON *matches_array = cJSON_CreateArray();

    // Duyệt thư mục matches
    DIR *dir = opendir(MATCHES_DIR);
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // Chỉ xử lý file .json
            if (strstr(entry->d_name, ".json") == NULL)
                continue;

            // Lấy match_id từ tên file
            char match_id[32];
            strncpy(match_id, entry->d_name, 31);
            char *dot = strstr(match_id, ".json");
            if (dot)
                *dot = '\0';

            // Load và kiểm tra user có trong ván không
            cJSON *match_data = load_match_history(match_id);
            if (match_data)
            {
                cJSON *white = cJSON_GetObjectItem(match_data, "white");
                cJSON *black = cJSON_GetObjectItem(match_data, "black");
                cJSON *winner = cJSON_GetObjectItem(match_data, "winner");
                cJSON *timestamp = cJSON_GetObjectItem(match_data, "timestamp");
                cJSON *move_count = cJSON_GetObjectItem(match_data, "moveCount");

                if (white && black)
                {
                    // Kiểm tra user có trong ván đấu này không
                    if (strcmp(white->valuestring, target_username) == 0 ||
                        strcmp(black->valuestring, target_username) == 0)
                    {
                        cJSON *match_info = cJSON_CreateObject();
                        cJSON_AddStringToObject(match_info, "matchId", match_id);
                        cJSON_AddStringToObject(match_info, "white", white->valuestring);
                        cJSON_AddStringToObject(match_info, "black", black->valuestring);
                        if (winner)
                            cJSON_AddStringToObject(match_info, "winner", winner->valuestring);
                        if (timestamp)
                            cJSON_AddNumberToObject(match_info, "timestamp", timestamp->valuedouble);
                        if (move_count)
                            cJSON_AddNumberToObject(match_info, "moveCount", move_count->valueint);

                        cJSON_AddItemToArray(matches_array, match_info);
                    }
                }
                cJSON_Delete(match_data);
            }
        }
        closedir(dir);
    }

    cJSON_AddItemToObject(resp_data, "matches", matches_array);
    cJSON_AddItemToObject(response, "data", resp_data);

    send_json(client_idx, response);
    cJSON_Delete(response);

    return 0;
}

/**
 * handle_get_match_replay - Xử lý yêu cầu xem lại ván đấu
 * @client_idx: Index của client
 * @data: JSON data chứa matchId
 */
int handle_get_match_replay(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj || !cJSON_IsString(match_id_obj))
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    // Load lịch sử ván đấu
    cJSON *match_data = load_match_history(match_id);
    if (!match_data)
    {
        send_error(client_idx, "Match not found");
        return -1;
    }

    // Tạo response với toàn bộ dữ liệu
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "MATCH_REPLAY");

    // Copy toàn bộ match_data vào response
    cJSON *resp_data = cJSON_Duplicate(match_data, 1);
    cJSON_AddItemToObject(response, "data", resp_data);

    send_json(client_idx, response);

    cJSON_Delete(response);
    cJSON_Delete(match_data);

    printf("Sent replay for match %s to client %d\n", match_id, client_idx);
    return 0;
}

/**
 * stop_recording_match - Dừng ghi nhận nước đi (khi abort không lưu)
 * @match_id: ID của ván đấu
 */
void stop_recording_match(const char *match_id)
{
    pthread_mutex_lock(&history_mutex);

    int idx = find_active_match_moves(match_id);
    if (idx != -1)
    {
        active_moves[idx].is_active = 0;
    }

    pthread_mutex_unlock(&history_mutex);
}
