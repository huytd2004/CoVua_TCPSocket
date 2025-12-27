/**
 * match_manager.c - Match Manager Module
 *
 * Module quản lý các ván đấu cờ vua, bao gồm:
 * - Tạo và khởi tạo ván đấu mới
 * - Xử lý lời thách đấu giữa các người chơi
 * - Quản lý trạng thái và tìm kiếm ván đấu
 * - Khởi tạo bàn cờ chuẩn
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "server.h"

#define MAX_MATCHES 50 // Số lượng ván đấu tối đa

// Forward declaration từ match_history.c
void start_recording_match(const char *match_id);

// Biến toàn cục - không dùng static để có thể truy cập từ module khác
Match matches[MAX_MATCHES];                              // Mảng lưu thông tin các ván đấu
pthread_mutex_t match_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex bảo vệ truy cập matches

/**
 * generate_match_id - Tạo match ID ngẫu nhiên
 * @output: Buffer để lưu match ID
 * @length: Độ dài của ID
 *
 * Format: M + chuỗi ngẫu nhiên (số và chữ hoa)
 * VD: M8A4F2X9P
 */
void generate_match_id(char *output, int length)
{
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    output[0] = 'M'; // Bắt đầu bằng 'M' để nhận diện
    for (int i = 1; i < length - 1; i++)
    {
        output[i] = charset[rand() % 36]; // Random số hoặc chữ hoa
    }
    output[length - 1] = '\0'; // Kết thúc chuỗi
}

/**
 * match_manager_init - Khởi tạo match manager
 *
 * Đánh dấu tất cả các slot ván đấu là trống (inactive).
 * Gọi khi server khởi động.
 */
void match_manager_init()
{
    // Đánh dấu tất cả slot trống
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        matches[i].is_active = 0;
    }
}

/**
 * find_match_by_players - Tìm ván đấu giữa 2 người chơi
 * @player1: Tên người chơi thứ nhất
 * @player2: Tên người chơi thứ hai
 *
 * Return: Index của ván đấu, -1 nếu không tìm thấy
 */
int find_match_by_players(const char *player1, const char *player2)
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (matches[i].is_active)
        {
            // Kiểm tra cả 2 chiều: player1-player2 hoặc player2-player1
            if ((strcmp(matches[i].white_player, player1) == 0 &&
                 strcmp(matches[i].black_player, player2) == 0) ||
                (strcmp(matches[i].white_player, player2) == 0 &&
                 strcmp(matches[i].black_player, player1) == 0))
            {
                return i;
            }
        }
    }
    return -1; // Không tìm thấy
}

/**
 * find_free_match_slot - Tìm slot trống để tạo ván đấu mới
 * Return: Index của slot trống, -1 nếu đầy
 */
int find_free_match_slot()
{
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (!matches[i].is_active)
            return i;
    }
    return -1;
}

/**
 * init_board - Khởi tạo bàn cờ chuẩn
 * @board: Mảng 8x8 đại diện cho bàn cờ
 *
 * Bố trí:
 * - Hàng 0: Quân đen (uppercase) - RNBQKBNR
 * - Hàng 1: Tốt đen - PPPPPPPP
 * - Hàng 2-5: Ô trống - '.'
 * - Hàng 6: Tốt trắng - pppppppp
 * - Hàng 7: Quân trắng (lowercase) - rnbqkbnr
 */
void init_board(char board[8][8])
{
    // Hàng quân chính (Xe, Mã, Tượng, Hậu, Vua)
    const char back_rank[] = "RNBQKBNR";
    for (int i = 0; i < 8; i++)
    {
        board[0][i] = back_rank[i];      // Quân đen (uppercase)
        board[7][i] = back_rank[i] + 32; // Quân trắng (lowercase, +32 trong ASCII)
    }

    // Tốt (Pawns)
    for (int i = 0; i < 8; i++)
    {
        board[1][i] = 'P'; // Tốt đen
        board[6][i] = 'p'; // Tốt trắng
    }

    // Ô trống (hàng 2-5)
    for (int row = 2; row < 6; row++)
    {
        for (int col = 0; col < 8; col++)
        {
            board[row][col] = '.'; // Ký tự chấm đại diện cho ô trống
        }
    }
}

/**
 * create_match - Tạo ván đấu mới và gửi thông báo START_GAME
 * @challenger_idx: Index của người thách đấu
 * @opponent_idx: Index của đối thủ
 *
 * Chức năng:
 * 1. Tìm slot trống cho ván đấu
 * 2. Random phân màu quân trắng/đen
 * 3. Khởi tạo bàn cờ
 * 4. Cập nhật trạng thái người chơi
 * 5. Gửi thông báo START_GAME cho cả 2 bên
 *
 * Return: Index của ván đấu, -1 nếu thất bại
 */
int create_match(int challenger_idx, int opponent_idx)
{
    pthread_mutex_lock(&match_mutex); // Khóa để tránh race condition

    // Tìm slot trống
    int match_idx = find_free_match_slot();
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(challenger_idx, "No available match slots");
        return -1;
    }

    Match *match = &matches[match_idx];
    generate_match_id(match->match_id, 10); // Tạo match ID

    // Random phân màu quân (50-50)
    if (rand() % 2 == 0)
    {
        // Người thách đấu chơi trắng
        strncpy(match->white_player, clients[challenger_idx].username, MAX_USERNAME - 1);
        strncpy(match->black_player, clients[opponent_idx].username, MAX_USERNAME - 1);
        match->white_client_idx = challenger_idx;
        match->black_client_idx = opponent_idx;
    }
    else
    {
        // Đối thủ chơi trắng
        strncpy(match->white_player, clients[opponent_idx].username, MAX_USERNAME - 1);
        strncpy(match->black_player, clients[challenger_idx].username, MAX_USERNAME - 1);
        match->white_client_idx = opponent_idx;
        match->black_client_idx = challenger_idx;
    }

    init_board(match->board); // Khởi tảo bàn cờ chuẩn
    match->current_turn = 0;  // Quân trắng đi trước
    match->is_active = 1;     // Đánh dấu ván đấu active

    // Khởi tạo các trường cho luật nâng cao
    match->white_king_moved = 0;
    match->black_king_moved = 0;
    match->white_rook_a_moved = 0;
    match->white_rook_h_moved = 0;
    match->black_rook_a_moved = 0;
    match->black_rook_h_moved = 0;
    match->en_passant_col = -1; // -1 = không có en passant
    match->last_move_from_row = -1;
    match->last_move_from_col = -1;
    match->last_move_to_row = -1;
    match->last_move_to_col = -1;
    match->halfmove_clock = 0;
    match->fullmove_number = 1;

    // Lưu match_id trước khi unlock để gọi start_recording_match
    char match_id_copy[32];
    strncpy(match_id_copy, match->match_id, 31);
    match_id_copy[31] = '\0';

    pthread_mutex_unlock(&match_mutex);

    // Bắt đầu ghi nhận nước đi cho ván đấu
    start_recording_match(match_id_copy);

    // Cập nhật trạng thái người chơi
    pthread_mutex_lock(&clients_mutex);
    clients[challenger_idx].status = STATUS_IN_MATCH;
    clients[opponent_idx].status = STATUS_IN_MATCH;
    pthread_mutex_unlock(&clients_mutex);

    // Mô tả bàn cờ (giản lược)
    char board_str[256] = "Initial position";

    // Tạo JSON message START_GAME
    cJSON *start_game = cJSON_CreateObject();
    cJSON_AddStringToObject(start_game, "action", "START_GAME");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "matchId", match->match_id);
    cJSON_AddStringToObject(data, "white", match->white_player);
    cJSON_AddStringToObject(data, "black", match->black_player);
    cJSON_AddStringToObject(data, "board", board_str);
    cJSON_AddItemToObject(start_game, "data", data);

    // Gửi cho cả 2 người chơi
    send_json(challenger_idx, start_game);
    send_json(opponent_idx, start_game);

    cJSON_Delete(start_game);

    printf("Match created: %s vs %s (Match ID: %s)\n",
           match->white_player, match->black_player, match->match_id);

    return match_idx;
}

/**
 * create_match_with_colors - Tạo ván đấu với màu quân xác định
 * @white_idx: Index của người chơi quân trắng
 * @black_idx: Index của người chơi quân đen
 *
 * Tương tự create_match nhưng không random màu quân.
 * Dùng cho rematch (đổi màu quân).
 *
 * Return: Index của ván đấu, -1 nếu thất bại
 */
int create_match_with_colors(int white_idx, int black_idx)
{
    pthread_mutex_lock(&match_mutex);

    int match_idx = find_free_match_slot();
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(white_idx, "No available match slots");
        return -1;
    }

    Match *match = &matches[match_idx];
    generate_match_id(match->match_id, 10);

    // Gán màu quân theo tham số (không random)
    pthread_mutex_lock(&clients_mutex);
    strncpy(match->white_player, clients[white_idx].username, MAX_USERNAME - 1);
    strncpy(match->black_player, clients[black_idx].username, MAX_USERNAME - 1);
    pthread_mutex_unlock(&clients_mutex);

    match->white_client_idx = white_idx;
    match->black_client_idx = black_idx;

    init_board(match->board);
    match->current_turn = 0;
    match->is_active = 1;

    // Khởi tạo các trường cho luật nâng cao
    match->white_king_moved = 0;
    match->black_king_moved = 0;
    match->white_rook_a_moved = 0;
    match->white_rook_h_moved = 0;
    match->black_rook_a_moved = 0;
    match->black_rook_h_moved = 0;
    match->en_passant_col = -1;
    match->last_move_from_row = -1;
    match->last_move_from_col = -1;
    match->last_move_to_row = -1;
    match->last_move_to_col = -1;
    match->halfmove_clock = 0;
    match->fullmove_number = 1;

    // Lưu match_id trước khi unlock để gọi start_recording_match
    char match_id_copy[32];
    strncpy(match_id_copy, match->match_id, 31);
    match_id_copy[31] = '\0';

    pthread_mutex_unlock(&match_mutex);

    // Bắt đầu ghi nhận nước đi cho ván đấu (rematch)
    start_recording_match(match_id_copy);

    // Cập nhật trạng thái người chơi
    pthread_mutex_lock(&clients_mutex);
    clients[white_idx].status = STATUS_IN_MATCH;
    clients[black_idx].status = STATUS_IN_MATCH;
    pthread_mutex_unlock(&clients_mutex);

    // Tạo JSON message START_GAME
    cJSON *start_game = cJSON_CreateObject();
    cJSON_AddStringToObject(start_game, "action", "START_GAME");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "matchId", match->match_id);
    cJSON_AddStringToObject(data, "white", match->white_player);
    cJSON_AddStringToObject(data, "black", match->black_player);
    cJSON_AddStringToObject(data, "board", "Initial position");
    cJSON_AddBoolToObject(data, "isRematch", 1);
    cJSON_AddItemToObject(start_game, "data", data);

    send_json(white_idx, start_game);
    send_json(black_idx, start_game);
    cJSON_Delete(start_game);

    printf("Rematch created: %s (white) vs %s (black) (Match ID: %s)\n",
           match->white_player, match->black_player, match->match_id);

    return match_idx;
}

/**
 * handle_challenge - Xử lý yêu cầu thách đấu
 * @client_idx: Index của người gửi thách đấu
 * @data: JSON object chứa "from" và "to"
 *
 * Kiểm tra:
 * - Username khớp với client
 * - Đối thủ online và rảnh
 *
 * Gửi INCOMING_CHALLENGE đến đối thủ.
 *
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
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

    // Kiểm tra username khớp với client đang đăng nhập
    pthread_mutex_lock(&clients_mutex);
    if (strcmp(clients[client_idx].username, from) != 0)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Username mismatch");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Tìm đối thủ
    int opponent_idx = find_client_by_username(to);
    if (opponent_idx == -1)
    {
        send_error(client_idx, "Opponent not found or offline");
        return -1;
    }

    // Kiểm tra đối thủ có rảnh không
    pthread_mutex_lock(&clients_mutex);
    if (clients[opponent_idx].status != STATUS_ONLINE)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Opponent is not available");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Gửi thông báo INCOMING_CHALLENGE đến đối thủ
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

/**
 * handle_accept - Xử lý chấp nhận thách đấu
 * @client_idx: Index của người chấp nhận
 * @data: JSON object chứa "from" và "to"
 *
 * Tìm người thách đấu và tạo ván đấu mới.
 *
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
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

    const char *from = from_obj->valuestring; // Người chấp nhận
    const char *to = to_obj->valuestring;     // Người thách đấu

    // Tìm người thách đấu
    int challenger_idx = find_client_by_username(to);
    if (challenger_idx == -1)
    {
        send_error(client_idx, "Challenger not found");
        return -1;
    }

    // Tạo ván đấu mới
    create_match(challenger_idx, client_idx);

    printf("%s accepted challenge from %s\n", from, to);
    return 0;
}

/**
 * handle_decline - Xử lý từ chối thách đấu
 * @client_idx: Index của người từ chối
 * @data: JSON object chứa "from" và "to"
 *
 * Gửi thông báo CHALLENGE_DECLINED về người thách đấu.
 *
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
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

    const char *from = from_obj->valuestring; // Người từ chối
    const char *to = to_obj->valuestring;     // Người bị từ chối

    // Tìm người thách đấu và thông báo
    int challenger_idx = find_client_by_username(to);
    if (challenger_idx != -1)
    {
        // Gửi thông báo đã bị từ chối
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

/**
 * find_match_by_id - Tìm ván đấu theo match ID
 * @match_id: ID của ván đấu
 *
 * Return: Index của ván đấu, -1 nếu không tìm thấy
 */
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

/**
 * get_client_match - Tìm ván đấu hiện tại của client
 * @client_idx: Index của client
 *
 * Return: Index của ván đấu, -1 nếu không đang trong ván nào
 */
int get_client_match(int client_idx)
{
    const char *username = clients[client_idx].username;
    for (int i = 0; i < MAX_MATCHES; i++)
    {
        if (matches[i].is_active)
        {
            // Kiểm tra client có trong ván đấu này không (trắng hoặc đen)
            if (strcmp(matches[i].white_player, username) == 0 ||
                strcmp(matches[i].black_player, username) == 0)
            {
                return i;
            }
        }
    }
    return -1; // Không đang trong ván đấu nào
}