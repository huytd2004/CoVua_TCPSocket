/**
 * game_control.c - Game Control Module
 *
 * Module xử lý các yêu cầu điều khiển ván cờ:
 * - Xin ngừng ván (ABORT)
 * - Mời hòa (DRAW)
 * - Đấu lại (REMATCH)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "server.h"

extern Match matches[];
extern pthread_mutex_t match_mutex;
extern Client clients[];
extern pthread_mutex_t clients_mutex;

// Forward declarations
int find_match_by_id(const char *match_id);
int create_match(int challenger_idx, int opponent_idx);
void send_game_result(int match_idx, const char *winner, const char *reason);

// Lưu thông tin ván đấu vừa kết thúc để hỗ trợ rematch
#define MAX_RECENT_MATCHES 50
typedef struct
{
    char match_id[32];
    char white_player[32];
    char black_player[32];
    int white_client_idx;
    int black_client_idx;
    int rematch_offered_by; // client_idx của người đề nghị, -1 nếu chưa có
    int is_valid;           // 1 nếu còn hiệu lực
} RecentMatch;

static RecentMatch recent_matches[MAX_RECENT_MATCHES];
static pthread_mutex_t recent_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * save_recent_match - Lưu thông tin ván đấu vừa kết thúc
 */
void save_recent_match(const char *match_id, const char *white, const char *black,
                       int white_idx, int black_idx)
{
    pthread_mutex_lock(&recent_mutex);

    // Tìm slot trống hoặc cũ nhất
    int slot = -1;
    for (int i = 0; i < MAX_RECENT_MATCHES; i++)
    {
        if (!recent_matches[i].is_valid)
        {
            slot = i;
            break;
        }
    }
    if (slot == -1)
        slot = 0; // Ghi đè slot đầu tiên nếu đầy

    strncpy(recent_matches[slot].match_id, match_id, 31);
    strncpy(recent_matches[slot].white_player, white, 31);
    strncpy(recent_matches[slot].black_player, black, 31);
    recent_matches[slot].white_client_idx = white_idx;
    recent_matches[slot].black_client_idx = black_idx;
    recent_matches[slot].rematch_offered_by = -1;
    recent_matches[slot].is_valid = 1;

    pthread_mutex_unlock(&recent_mutex);
}

/**
 * find_recent_match - Tìm ván đấu gần đây theo match_id
 */
static int find_recent_match(const char *match_id)
{
    for (int i = 0; i < MAX_RECENT_MATCHES; i++)
    {
        if (recent_matches[i].is_valid &&
            strcmp(recent_matches[i].match_id, match_id) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * get_opponent_idx - Lấy index của đối thủ trong match
 */
static int get_opponent_idx(Match *match, int client_idx)
{
    if (match->white_client_idx == client_idx)
        return match->black_client_idx;
    else if (match->black_client_idx == client_idx)
        return match->white_client_idx;
    return -1;
}

/**
 * is_player_in_match - Kiểm tra client có trong match không
 */
static int is_player_in_match(Match *match, int client_idx)
{
    return (match->white_client_idx == client_idx ||
            match->black_client_idx == client_idx);
}

// ============= OFFER ABORT =============

/**
 * handle_offer_abort - Xử lý yêu cầu ngừng ván
 */
int handle_offer_abort(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    if (!is_player_in_match(match, client_idx))
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

    int opponent_idx = get_opponent_idx(match, client_idx);

    pthread_mutex_unlock(&match_mutex);

    // Lấy username của người gửi
    pthread_mutex_lock(&clients_mutex);
    char from_user[MAX_USERNAME];
    strncpy(from_user, clients[client_idx].username, MAX_USERNAME - 1);
    pthread_mutex_unlock(&clients_mutex);

    // Forward đến đối thủ
    cJSON *offer = cJSON_CreateObject();
    cJSON_AddStringToObject(offer, "action", "ABORT_OFFERED");
    cJSON *offer_data = cJSON_CreateObject();
    cJSON_AddStringToObject(offer_data, "matchId", match_id);
    cJSON_AddStringToObject(offer_data, "from", from_user);
    cJSON_AddItemToObject(offer, "data", offer_data);
    send_json(opponent_idx, offer);
    cJSON_Delete(offer);

    printf("%s offered to abort match %s\n", from_user, match_id);
    return 0;
}

/**
 * handle_accept_abort - Xử lý chấp nhận ngừng ván
 */
int handle_accept_abort(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    if (!is_player_in_match(match, client_idx))
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

    // Lưu thông tin để rematch
    save_recent_match(match->match_id, match->white_player, match->black_player,
                      match->white_client_idx, match->black_client_idx);

    // Gửi kết quả ABORT cho cả 2
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "action", "GAME_RESULT");
    cJSON *result_data = cJSON_CreateObject();
    cJSON_AddStringToObject(result_data, "winner", "ABORT");
    cJSON_AddStringToObject(result_data, "reason", "Game aborted by agreement");
    cJSON_AddItemToObject(result, "data", result_data);

    send_json(match->white_client_idx, result);
    send_json(match->black_client_idx, result);
    cJSON_Delete(result);

    // Cập nhật trạng thái người chơi
    pthread_mutex_lock(&clients_mutex);
    clients[match->white_client_idx].status = STATUS_ONLINE;
    clients[match->black_client_idx].status = STATUS_ONLINE;
    pthread_mutex_unlock(&clients_mutex);

    printf("Match %s aborted by agreement\n", match_id);

    // Deactivate match (không cập nhật ELO)
    match->is_active = 0;

    pthread_mutex_unlock(&match_mutex);

    return 0;
}

/**
 * handle_decline_abort - Xử lý từ chối ngừng ván
 */
int handle_decline_abort(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];
    int opponent_idx = get_opponent_idx(match, client_idx);

    pthread_mutex_unlock(&match_mutex);

    // Thông báo cho người đề nghị
    cJSON *decline = cJSON_CreateObject();
    cJSON_AddStringToObject(decline, "action", "ABORT_DECLINED");
    cJSON *decline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(decline_data, "matchId", match_id);
    cJSON_AddItemToObject(decline, "data", decline_data);
    send_json(opponent_idx, decline);
    cJSON_Delete(decline);

    printf("Abort declined for match %s\n", match_id);
    return 0;
}

// ============= OFFER DRAW =============

/**
 * handle_offer_draw - Xử lý yêu cầu hòa
 */
int handle_offer_draw(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    if (!is_player_in_match(match, client_idx))
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

    int opponent_idx = get_opponent_idx(match, client_idx);

    pthread_mutex_unlock(&match_mutex);

    // Lấy username
    pthread_mutex_lock(&clients_mutex);
    char from_user[MAX_USERNAME];
    strncpy(from_user, clients[client_idx].username, MAX_USERNAME - 1);
    pthread_mutex_unlock(&clients_mutex);

    // Forward đến đối thủ
    cJSON *offer = cJSON_CreateObject();
    cJSON_AddStringToObject(offer, "action", "DRAW_OFFERED");
    cJSON *offer_data = cJSON_CreateObject();
    cJSON_AddStringToObject(offer_data, "matchId", match_id);
    cJSON_AddStringToObject(offer_data, "from", from_user);
    cJSON_AddItemToObject(offer, "data", offer_data);
    send_json(opponent_idx, offer);
    cJSON_Delete(offer);

    printf("%s offered draw in match %s\n", from_user, match_id);
    return 0;
}

/**
 * handle_accept_draw - Xử lý chấp nhận hòa
 */
int handle_accept_draw(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    if (!is_player_in_match(match, client_idx))
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

    // Lưu thông tin để rematch
    save_recent_match(match->match_id, match->white_player, match->black_player,
                      match->white_client_idx, match->black_client_idx);

    pthread_mutex_unlock(&match_mutex);

    // Gọi send_game_result với DRAW (sẽ cập nhật ELO)
    send_game_result(match_idx, "DRAW", "Draw by agreement");

    printf("Match %s ended in draw by agreement\n", match_id);
    return 0;
}

/**
 * handle_decline_draw - Xử lý từ chối hòa
 */
int handle_decline_draw(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];
    int opponent_idx = get_opponent_idx(match, client_idx);

    pthread_mutex_unlock(&match_mutex);

    // Thông báo cho người đề nghị
    cJSON *decline = cJSON_CreateObject();
    cJSON_AddStringToObject(decline, "action", "DRAW_DECLINED");
    cJSON *decline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(decline_data, "matchId", match_id);
    cJSON_AddItemToObject(decline, "data", decline_data);
    send_json(opponent_idx, decline);
    cJSON_Delete(decline);

    printf("Draw declined for match %s\n", match_id);
    return 0;
}

// ============= OFFER REMATCH =============

/**
 * handle_offer_rematch - Xử lý yêu cầu đấu lại
 */
int handle_offer_rematch(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&recent_mutex);

    int recent_idx = find_recent_match(match_id);
    if (recent_idx == -1)
    {
        pthread_mutex_unlock(&recent_mutex);
        send_error(client_idx, "Match not found or expired");
        return -1;
    }

    RecentMatch *recent = &recent_matches[recent_idx];

    // Kiểm tra người gửi có trong ván đấu không
    if (recent->white_client_idx != client_idx &&
        recent->black_client_idx != client_idx)
    {
        pthread_mutex_unlock(&recent_mutex);
        send_error(client_idx, "You were not in this match");
        return -1;
    }

    // Đánh dấu người đề nghị
    recent->rematch_offered_by = client_idx;

    // Tìm đối thủ
    int opponent_idx = (recent->white_client_idx == client_idx) ? recent->black_client_idx : recent->white_client_idx;

    pthread_mutex_unlock(&recent_mutex);

    // Lấy username
    pthread_mutex_lock(&clients_mutex);
    char from_user[MAX_USERNAME];
    strncpy(from_user, clients[client_idx].username, MAX_USERNAME - 1);
    pthread_mutex_unlock(&clients_mutex);

    // Forward đến đối thủ
    cJSON *offer = cJSON_CreateObject();
    cJSON_AddStringToObject(offer, "action", "REMATCH_OFFERED");
    cJSON *offer_data = cJSON_CreateObject();
    cJSON_AddStringToObject(offer_data, "matchId", match_id);
    cJSON_AddStringToObject(offer_data, "from", from_user);
    cJSON_AddItemToObject(offer, "data", offer_data);
    send_json(opponent_idx, offer);
    cJSON_Delete(offer);

    printf("%s offered rematch for match %s\n", from_user, match_id);
    return 0;
}

/**
 * handle_accept_rematch - Xử lý chấp nhận đấu lại
 */
int handle_accept_rematch(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&recent_mutex);

    int recent_idx = find_recent_match(match_id);
    if (recent_idx == -1)
    {
        pthread_mutex_unlock(&recent_mutex);
        send_error(client_idx, "Match not found or expired");
        return -1;
    }

    RecentMatch *recent = &recent_matches[recent_idx];

    // Lấy thông tin người chơi (đổi màu quân)
    int new_white_idx = recent->black_client_idx; // Người chơi đen cũ -> trắng mới
    int new_black_idx = recent->white_client_idx; // Người chơi trắng cũ -> đen mới

    // Đánh dấu không còn hiệu lực
    recent->is_valid = 0;

    pthread_mutex_unlock(&recent_mutex);

    // Kiểm tra cả 2 người chơi còn online không
    pthread_mutex_lock(&clients_mutex);
    if (!clients[new_white_idx].is_active || !clients[new_black_idx].is_active)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Opponent is no longer online");
        return -1;
    }

    // Kiểm tra cả 2 đều rảnh
    if (clients[new_white_idx].status != STATUS_ONLINE ||
        clients[new_black_idx].status != STATUS_ONLINE)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "One or both players are not available");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    printf("Rematch accepted for match %s\n", match_id);

    // Tạo ván đấu mới với màu quân đổi ngược
    // Sử dụng create_match_with_colors thay vì create_match để đảm bảo đổi màu
    create_match_with_colors(new_white_idx, new_black_idx);

    return 0;
}

/**
 * handle_decline_rematch - Xử lý từ chối đấu lại
 */
int handle_decline_rematch(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *match_id_obj = cJSON_GetObjectItem(data, "matchId");
    if (!match_id_obj)
    {
        send_error(client_idx, "Missing matchId");
        return -1;
    }

    const char *match_id = match_id_obj->valuestring;

    pthread_mutex_lock(&recent_mutex);

    int recent_idx = find_recent_match(match_id);
    if (recent_idx == -1)
    {
        pthread_mutex_unlock(&recent_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    RecentMatch *recent = &recent_matches[recent_idx];
    int opponent_idx = recent->rematch_offered_by;

    // Đánh dấu không còn hiệu lực
    recent->is_valid = 0;

    pthread_mutex_unlock(&recent_mutex);

    if (opponent_idx != -1)
    {
        // Thông báo cho người đề nghị
        cJSON *decline = cJSON_CreateObject();
        cJSON_AddStringToObject(decline, "action", "REMATCH_DECLINED");
        cJSON *decline_data = cJSON_CreateObject();
        cJSON_AddStringToObject(decline_data, "matchId", match_id);
        cJSON_AddItemToObject(decline, "data", decline_data);
        send_json(opponent_idx, decline);
        cJSON_Delete(decline);
    }

    printf("Rematch declined for match %s\n", match_id);
    return 0;
}

/**
 * game_control_init - Khởi tạo game control module
 */
void game_control_init()
{
    pthread_mutex_lock(&recent_mutex);
    for (int i = 0; i < MAX_RECENT_MATCHES; i++)
    {
        recent_matches[i].is_valid = 0;
    }
    pthread_mutex_unlock(&recent_mutex);
    printf("Game Control module initialized\n");
}
