/**
 * game_manager_handlers.c - Game Manager Request Handlers
 *
 * Các hàm xử lý request liên quan đến game logic
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"
#include "server.h"

extern Match matches[];
extern pthread_mutex_t match_mutex;
extern Client clients[];
extern pthread_mutex_t clients_mutex;

// Forward declarations
int notation_to_coords(const char *notation, int *row, int *col);
int is_valid_move(Match *match, int from_row, int from_col, int to_row, int to_col, int player_turn);
void execute_move(Match *match, int from_row, int from_col, int to_row, int to_col, char promotion_piece);
int check_game_end(Match *match, char **winner, char **reason);
int find_match_by_id(const char *match_id);

// ELO functions
void update_elo_ratings(const char *white_player, const char *black_player, const char *winner);

// Game control functions
void save_recent_match(const char *match_id, const char *white, const char *black,
                       int white_idx, int black_idx);

// Match history functions
void record_move(const char *match_id, const char *from, const char *to);
void save_match_history(const char *match_id, const char *white, const char *black,
                        const char *winner, const char *reason, char final_board[8][8]);

/**
 * send_game_result - Gửi kết quả game cho cả 2 người chơi
 */
void send_game_result(int match_idx, const char *winner, const char *reason)
{
    pthread_mutex_lock(&match_mutex);

    if (!matches[match_idx].is_active)
    {
        pthread_mutex_unlock(&match_mutex);
        return;
    }

    Match *match = &matches[match_idx];

    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", "GAME_RESULT");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "winner", winner);
    cJSON_AddStringToObject(data, "reason", reason);
    cJSON_AddStringToObject(data, "matchId", match->match_id); // Thêm matchId cho rematch
    cJSON_AddItemToObject(result, "data", data);

    send_json(match->white_client_idx, result);
    send_json(match->black_client_idx, result);

    cJSON_Delete(result);

    // Cập nhật trạng thái người chơi
    pthread_mutex_lock(&clients_mutex);
    clients[match->white_client_idx].status = STATUS_ONLINE;
    clients[match->black_client_idx].status = STATUS_ONLINE;
    pthread_mutex_unlock(&clients_mutex);

    // Lưu thông tin trước khi deactivate
    char match_id_copy[32];
    char white_player_copy[32];
    char black_player_copy[32];
    char board_copy[8][8];
    int white_idx = match->white_client_idx;
    int black_idx = match->black_client_idx;
    strncpy(match_id_copy, match->match_id, 31);
    strncpy(white_player_copy, match->white_player, 31);
    strncpy(black_player_copy, match->black_player, 31);
    match_id_copy[31] = '\0';
    white_player_copy[31] = '\0';
    black_player_copy[31] = '\0';
    // Copy bàn cờ
    memcpy(board_copy, match->board, sizeof(board_copy));

    // Deactivate match
    match->is_active = 0;

    pthread_mutex_unlock(&match_mutex);

    // Lưu lịch sử ván đấu vào file
    save_match_history(match_id_copy, white_player_copy, black_player_copy,
                       winner, reason, board_copy);

    // Lưu thông tin ván đấu để hỗ trợ rematch
    save_recent_match(match_id_copy, white_player_copy, black_player_copy, white_idx, black_idx);

    // Cập nhật ELO sau khi unlock match_mutex để tránh deadlock
    update_elo_ratings(white_player_copy, black_player_copy, winner);

    printf("Match %s ended. Winner: %s (%s)\n", match_id_copy, winner, reason);
}

/**
 * handle_move - Xử lý nước đi từ client
 */
int handle_move(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    cJSON *from_obj = cJSON_GetObjectItem(data, "from");
    cJSON *to_obj = cJSON_GetObjectItem(data, "to");

    if (!match_id_obj || !from_obj || !to_obj)
    {
        send_error(client_idx, "Missing matchId, from, or to field");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;
    const char *from = from_obj->valuestring;
    const char *to = to_obj->valuestring;

    // Lấy promotion piece nếu có
    char promotion = '\0';
    cJSON *promotion_obj = cJSON_GetObjectItem(data, "promotion");
    if (promotion_obj && cJSON_IsString(promotion_obj))
    {
        promotion = toupper(promotion_obj->valuestring[0]);
    }

    // Tìm match
    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    // Kiểm tra người chơi có trong match không
    int is_white_player = (match->white_client_idx == client_idx);
    int is_black_player = (match->black_client_idx == client_idx);

    if (!is_white_player && !is_black_player)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

    // Kiểm tra lượt đi
    int player_turn = is_white_player ? 0 : 1;
    if (match->current_turn != player_turn)
    {
        pthread_mutex_unlock(&match_mutex);

        cJSON *invalid = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid, "action", "MOVE_INVALID");
        cJSON *invalid_data = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid_data, "reason", "Not your turn");
        cJSON_AddItemToObject(invalid, "data", invalid_data);
        send_json(client_idx, invalid);
        cJSON_Delete(invalid);
        return -1;
    }

    // Chuyển notation sang coordinates
    int from_row, from_col, to_row, to_col;
    if (notation_to_coords(from, &from_row, &from_col) != 0 ||
        notation_to_coords(to, &to_row, &to_col) != 0)
    {
        pthread_mutex_unlock(&match_mutex);

        cJSON *invalid = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid, "action", "MOVE_INVALID");
        cJSON *invalid_data = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid_data, "reason", "Invalid notation");
        cJSON_AddItemToObject(invalid, "data", invalid_data);
        send_json(client_idx, invalid);
        cJSON_Delete(invalid);
        return -1;
    }

    // Kiểm tra tính hợp lệ của nước đi
    if (!is_valid_move(match, from_row, from_col, to_row, to_col, player_turn))
    {
        pthread_mutex_unlock(&match_mutex);

        cJSON *invalid = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid, "action", "MOVE_INVALID");
        cJSON *invalid_data = cJSON_CreateObject();
        cJSON_AddStringToObject(invalid_data, "reason", "Illegal move");
        cJSON_AddItemToObject(invalid, "data", invalid_data);
        send_json(client_idx, invalid);
        cJSON_Delete(invalid);
        return -1;
    }

    // Thực hiện nước đi (sử dụng execute_move để xử lý en passant, castling, promotion)
    execute_move(match, from_row, from_col, to_row, to_col, promotion);

    // Chuyển lượt
    match->current_turn = 1 - match->current_turn;
    if (match->current_turn == 0) // Sau khi đen đi xong
    {
        match->fullmove_number++;
    }

    int opponent_idx = is_white_player ? match->black_client_idx : match->white_client_idx;

    // Lưu match_id để ghi nhận nước đi sau khi unlock
    char match_id_copy[32];
    strncpy(match_id_copy, match->match_id, 31);
    match_id_copy[31] = '\0';

    pthread_mutex_unlock(&match_mutex);

    // Ghi nhận nước đi vào lịch sử
    record_move(match_id_copy, from, to);

    // Gửi MOVE_OK cho người chơi hiện tại
    cJSON *move_ok = cJSON_CreateObject();
    cJSON_AddStringToObject(move_ok, "action", "MOVE_OK");
    cJSON *ok_data = cJSON_CreateObject();
    cJSON_AddStringToObject(ok_data, "from", from);
    cJSON_AddStringToObject(ok_data, "to", to);
    cJSON_AddItemToObject(move_ok, "data", ok_data);
    send_json(client_idx, move_ok);
    cJSON_Delete(move_ok);

    // Gửi OPPONENT_MOVE cho đối thủ
    cJSON *opp_move = cJSON_CreateObject();
    cJSON_AddStringToObject(opp_move, "action", "OPPONENT_MOVE");
    cJSON *opp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(opp_data, "from", from);
    cJSON_AddStringToObject(opp_data, "to", to);
    cJSON_AddItemToObject(opp_move, "data", opp_data);
    send_json(opponent_idx, opp_move);
    cJSON_Delete(opp_move);

    printf("Move in match %s: %s -> %s\n", match_id, from, to);

    // Kiểm tra kết thúc game
    char *winner = NULL;
    char *reason = NULL;
    if (check_game_end(match, &winner, &reason))
    {
        send_game_result(match_idx, winner, reason);
    }

    return 0;
}
