/**
 * game_manager.c - Game Logic Manager Module
 *
 * Module xử lý logic game cờ vua, bao gồm:
 * - Kiểm tra tính hợp lệ của nước đi cho từng loại quân
 * - Phát hiện chiếu, chiếu hết, bế tắc
 * - Xử lý nước đi và cập nhật trạng thái bàn cờ
 * - Xác định kết quả ván đấu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"
#include "server.h"

extern Match matches[];             // Mảng các ván đấu
extern pthread_mutex_t match_mutex; // Mutex bảo vệ truy cập matches

/**
 * game_manager_init - Khởi tạo game manager
 */
void game_manager_init()
{
    // Nothing special to initialize
}

/**
 * notation_to_coords - Chuyển đổi ký hiệu cờ vua sang tọa độ
 * @notation: Ký hiệu cờ vua (VD: "E2", "A8")
 * @row: Pointer để lưu hàng (0-7)
 * @col: Pointer để lưu cột (0-7)
 *
 * Format: [A-H][1-8] -> (row, col)
 * VD: E2 -> row=6, col=4
 *
 * Return: 0 nếu thành công, -1 nếu notation không hợp lệ
 */
int notation_to_coords(const char *notation, int *row, int *col)
{
    if (strlen(notation) != 2)
        return -1; // Phải đúng 2 ký tự

    char file = toupper(notation[0]); // Cột (A-H)
    char rank = notation[1];          // Hàng (1-8)

    // Kiểm tra hợp lệ
    if (file < 'A' || file > 'H' || rank < '1' || rank > '8')
    {
        return -1;
    }

    *col = file - 'A';       // A=0, B=1, ..., H=7
    *row = 8 - (rank - '0'); // Hàng 0 là rank 8 (top-down)

    return 0;
}

/**
 * is_square_under_attack - Kiểm tra ô có bị tấn công không
 * @match: Pointer đến ván đấu
 * @row: Hàng cần kiểm tra
 * @col: Cột cần kiểm tra
 * @by_white: 1 nếu kiểm tra bị quân trắng tấn công, 0 nếu quân đen
 *
 * Kiểm tra xem ô (row, col) có bị quân của by_white tấn công không.
 * Dùng để kiểm tra vua có bị chiếu hay không được đi vào ô nguy hiểm.
 *
 * Return: 1 nếu bị tấn công, 0 nếu an toàn
 */
int is_square_under_attack(Match *match, int row, int col, int by_white)
{
    // Duyệt qua toàn bộ bàn cờ
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            char piece = match->board[r][c];
            if (piece == '.') // Ô trống
                continue;

            int is_white_piece = (piece >= 'a' && piece <= 'z'); // lowercase = trắng
            if (is_white_piece != by_white)
                continue; // Không phải quân của by_white

            char p = tolower(piece);

            // Kiểm tra xem quân này có thể tấn công ô mục tiêu không
            int dr = row - r; // Khoảng cách hàng
            int dc = col - c; // Khoảng cách cột

            // Tốt (Pawn) - chỉ tấn công chéo
            if (p == 'p')
            {
                int dir = by_white ? -1 : 1;   // Trắng đi lên (-), đen đi xuống (+)
                if (dr == dir && abs(dc) == 1) // Tấn công chéo 1 ô
                    return 1;
            }
            // Mã (Knight) - đi chữ L
            else if (p == 'n')
            {
                if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
                    return 1;
            }
            // Tượng (Bishop) - đi chéo
            else if (p == 'b')
            {
                if (abs(dr) == abs(dc) && dr != 0)
                {
                    // Kiểm tra đường đi không bị chặn
                    int step_r = (dr > 0) ? 1 : -1;
                    int step_c = (dc > 0) ? 1 : -1;
                    int check_r = r + step_r;
                    int check_c = c + step_c;
                    int blocked = 0;
                    while (check_r != row || check_c != col)
                    {
                        if (match->board[check_r][check_c] != '.') // Có quân chặn
                        {
                            blocked = 1;
                            break;
                        }
                        check_r += step_r;
                        check_c += step_c;
                    }
                    if (!blocked)
                        return 1;
                }
            }
            // Xe (Rook) - đi ngang/dọc
            else if (p == 'r')
            {
                if (dr == 0 || dc == 0)
                {
                    int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
                    int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
                    int check_r = r + step_r;
                    int check_c = c + step_c;
                    int blocked = 0;
                    while (check_r != row || check_c != col)
                    {
                        if (match->board[check_r][check_c] != '.')
                        {
                            blocked = 1;
                            break;
                        }
                        check_r += step_r;
                        check_c += step_c;
                    }
                    if (!blocked)
                        return 1;
                }
            }
            // Hậu (Queen) - kết hợp xe + tượng
            else if (p == 'q')
            {
                if (dr == 0 || dc == 0 || abs(dr) == abs(dc)) // Ngang/dọc hoặc chéo
                {
                    int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
                    int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
                    int check_r = r + step_r;
                    int check_c = c + step_c;
                    int blocked = 0;
                    while (check_r != row || check_c != col)
                    {
                        if (match->board[check_r][check_c] != '.')
                        {
                            blocked = 1;
                            break;
                        }
                        check_r += step_r;
                        check_c += step_c;
                    }
                    if (!blocked)
                        return 1;
                }
            }
            // Vua (King) - đi 1 ô mọi hướng
            else if (p == 'k')
            {
                if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0))
                    return 1;
            }
        }
    }
    return 0;
}

/**
 * is_valid_move - Kiểm tra tính hợp lệ của nước đi
 * @match: Pointer đến ván đấu
 * @from_row, @from_col: Tọa độ xuất phát
 * @to_row, @to_col: Tọa độ đích
 * @player_turn: 0 = trắng, 1 = đen
 *
 * Kiểm tra:
 * - Ranh giới bàn cờ
 * - Quân cờ đúng màu
 * - Quy tắc di chuyển của từng loại quân
 * - Đường đi không bị chặn
 * - Vua không đi vào chiếu
 *
 * Return: 1 nếu hợp lệ, 0 nếu không hợp lệ
 */
int is_valid_move(Match *match, int from_row, int from_col, int to_row, int to_col, int player_turn)
{
    // Kiểm tra ranh giới
    if (from_row < 0 || from_row > 7 || from_col < 0 || from_col > 7 ||
        to_row < 0 || to_row > 7 || to_col < 0 || to_col > 7)
    {
        return 0;
    }

    char piece = match->board[from_row][from_col];

    // Kiểm tra có quân tại ô xuất phát
    if (piece == '.')
        return 0;

    // Kiểm tra quân đúng màu
    int is_white_piece = (piece >= 'a' && piece <= 'z'); // Lowercase = trắng
    if (player_turn == 0 && !is_white_piece)
        return 0; // Lượt trắng nhưng chọn quân đen
    if (player_turn == 1 && is_white_piece)
        return 0; // Lượt đen nhưng chọn quân trắng

    // Kiểm tra không ăn quân cùng màu
    char dest = match->board[to_row][to_col];
    if (dest != '.')
    {
        int is_dest_white = (dest >= 'a' && dest <= 'z');
        if (is_white_piece == is_dest_white)
            return 0; // Không thể ăn quân của mình
    }

    // Tính toán chuyển động
    int dr = to_row - from_row;
    int dc = to_col - from_col;

    char p = tolower(piece);

    // Quy tắc di chuyển theo từng loại quân
    switch (p)
    {
    case 'p': // Tốt (Pawn)
    {
        int dir = is_white_piece ? -1 : 1;      // Trắng lên, đen xuống
        int start_row = is_white_piece ? 6 : 1; // Hàng bắt đầu

        // Di chuyển tiến
        if (dc == 0 && dest == '.') // Phải là ô trống
        {
            if (dr == dir) // Đi 1 ô
                return 1;
            // Đi 2 ô từ vị trí xuất phát
            if (from_row == start_row && dr == 2 * dir && match->board[from_row + dir][from_col] == '.')
                return 1;
        }
        // Ăn chéo
        if (abs(dc) == 1 && dr == dir && dest != '.') // Phải có quân đối phương
            return 1;

        return 0;
    }

    case 'n': // Mã (Knight) - đi chữ L
        if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
            return 1;
        return 0;

    case 'b': // Tượng (Bishop) - đi chéo
        if (abs(dr) == abs(dc) && dr != 0)
        {
            // Kiểm tra đường đi không bị chặn
            int step_r = (dr > 0) ? 1 : -1;
            int step_c = (dc > 0) ? 1 : -1;
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.') // Có quân chặn
                    return 0;
                check_r += step_r;
                check_c += step_c;
            }
            return 1;
        }
        return 0;

    case 'r': // Xe (Rook) - ngang/dọc
        if (dr == 0 || dc == 0)
        {
            // Kiểm tra đường đi
            int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
            int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.')
                    return 0;
                check_r += step_r;
                check_c += step_c;
            }
            return 1;
        }
        return 0;

    case 'q': // Hậu (Queen) - kết hợp xe + tượng
        if (dr == 0 || dc == 0 || abs(dr) == abs(dc))
        {
            // Kiểm tra đường đi
            int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
            int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.')
                    return 0;
                check_r += step_r;
                check_c += step_c;
            }
            return 1;
        }
        return 0;

    case 'k': // Vua (King) - 1 ô mọi hướng
        if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0))
        {
            // Tạm thời thực hiện nước đi để kiểm tra vua có bị chiếu không
            char temp = match->board[to_row][to_col];
            match->board[to_row][to_col] = piece;
            match->board[from_row][from_col] = '.';

            // Kiểm tra vua có bị tấn công ở vị trí mới không
            int under_attack = is_square_under_attack(match, to_row, to_col, !is_white_piece);

            // Khôi phục bàn cờ
            match->board[from_row][from_col] = piece;
            match->board[to_row][to_col] = temp;

            return !under_attack; // Chỉ hợp lệ nếu không bị chiếu
        }
        return 0;

    default:
        return 0;
    }
}

/**
 * find_king - Tìm vị trí vua trên bàn cờ
 * Return: 1 nếu tìm thấy, 0 nếu không
 */
int find_king(Match *match, int is_white, int *king_row, int *king_col)
{
    char king = is_white ? 'k' : 'K'; // lowercase=trắng, uppercase=đen
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            if (match->board[r][c] == king)
            {
                *king_row = r;
                *king_col = c;
                return 1;
            }
        }
    }
    return 0;
}

/**
 * is_in_check - Kiểm tra vua có bị chiếu không
 * Return: 1 nếu bị chiếu, 0 nếu an toàn
 */
int is_in_check(Match *match, int is_white)
{
    int king_row, king_col;
    if (!find_king(match, is_white, &king_row, &king_col))
        return 0;

    return is_square_under_attack(match, king_row, king_col, !is_white);
}

/**
 * has_legal_moves - Kiểm tra còn nước đi hợp lệ không
 * Return: 1 nếu còn nước đi, 0 nếu hết nước
 */
int has_legal_moves(Match *match, int is_white)
{
    for (int from_r = 0; from_r < 8; from_r++)
    {
        for (int from_c = 0; from_c < 8; from_c++)
        {
            char piece = match->board[from_r][from_c];
            if (piece == '.')
                continue;

            int is_white_piece = (piece >= 'a' && piece <= 'z');
            if (is_white_piece != is_white)
                continue;

            // Try all possible destination squares
            for (int to_r = 0; to_r < 8; to_r++)
            {
                for (int to_c = 0; to_c < 8; to_c++)
                {
                    if (from_r == to_r && from_c == to_c)
                        continue;

                    // Check basic move validity
                    char dest = match->board[to_r][to_c];
                    if (dest != '.')
                    {
                        int is_dest_white = (dest >= 'a' && dest <= 'z');
                        if (is_white_piece == is_dest_white)
                            continue;
                    }

                    // Try the move
                    if (is_valid_move(match, from_r, from_c, to_r, to_c, is_white ? 0 : 1))
                    {
                        // Make temporary move
                        char temp_dest = match->board[to_r][to_c];
                        match->board[to_r][to_c] = piece;
                        match->board[from_r][from_c] = '.';

                        // Check if still in check after move
                        int still_in_check = is_in_check(match, is_white);

                        // Restore board
                        match->board[from_r][from_c] = piece;
                        match->board[to_r][to_c] = temp_dest;

                        if (!still_in_check)
                            return 1; // Found a legal move
                    }
                }
            }
        }
    }
    return 0; // No legal moves
}

// Check if insufficient material for checkmate
int is_insufficient_material(Match *match)
{
    int white_bishops = 0, black_bishops = 0;
    int white_knights = 0, black_knights = 0;
    int white_pieces = 0, black_pieces = 0;

    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            char piece = match->board[r][c];
            if (piece == '.')
                continue;

            char p = tolower(piece);
            int is_white = (piece >= 'a' && piece <= 'z');

            if (p != 'k')
            {
                if (is_white)
                    white_pieces++;
                else
                    black_pieces++;
            }

            // Count material
            if (p == 'q' || p == 'r' || p == 'p')
                return 0; // Has major pieces or pawns

            if (p == 'b')
            {
                if (is_white)
                    white_bishops++;
                else
                    black_bishops++;
            }
            if (p == 'n')
            {
                if (is_white)
                    white_knights++;
                else
                    black_knights++;
            }
        }
    }

    // King vs King
    if (white_pieces == 0 && black_pieces == 0)
        return 1;

    // King + Bishop vs King or King + Knight vs King
    if ((white_pieces == 1 && black_pieces == 0) ||
        (white_pieces == 0 && black_pieces == 1))
    {
        if ((white_bishops == 1 || white_knights == 1) ||
            (black_bishops == 1 || black_knights == 1))
            return 1;
    }

    // King + Bishop vs King + Bishop (same color squares would be draw but simplified)
    if (white_bishops == 1 && black_bishops == 1 &&
        white_knights == 0 && black_knights == 0)
        return 1;

    return 0;
}

// Check for checkmate/stalemate/draw
int check_game_end(Match *match, char **winner, char **reason)
{
    int current_is_white = (match->current_turn == 0);

    // Check for insufficient material
    if (is_insufficient_material(match))
    {
        *winner = "DRAW";
        *reason = "Insufficient material";
        return 1;
    }

    // Check if current player is in check
    int in_check = is_in_check(match, current_is_white);

    // Check if current player has legal moves
    int has_moves = has_legal_moves(match, current_is_white);

    if (!has_moves)
    {
        if (in_check)
        {
            // Checkmate
            *winner = current_is_white ? match->black_player : match->white_player;
            *reason = "Checkmate";
            return 1;
        }
        else
        {
            // Stalemate
            *winner = "DRAW";
            *reason = "Stalemate";
            return 1;
        }
    }

    return 0; // Game continues
}

// Handle MOVE
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

    // Find match
    pthread_mutex_lock(&match_mutex);

    int match_idx = find_match_by_id(match_id);
    if (match_idx == -1)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "Match not found");
        return -1;
    }

    Match *match = &matches[match_idx];

    // Check if it's player's turn
    int is_white_player = (match->white_client_idx == client_idx);
    int is_black_player = (match->black_client_idx == client_idx);

    if (!is_white_player && !is_black_player)
    {
        pthread_mutex_unlock(&match_mutex);
        send_error(client_idx, "You are not in this match");
        return -1;
    }

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

    // Convert notation to coordinates
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

    // Validate move
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

    // Execute move
    char piece = match->board[from_row][from_col];
    match->board[to_row][to_col] = piece;
    match->board[from_row][from_col] = '.';

    // Switch turn
    match->current_turn = 1 - match->current_turn;

    int opponent_idx = is_white_player ? match->black_client_idx : match->white_client_idx;

    pthread_mutex_unlock(&match_mutex);

    // Send MOVE_OK to current player
    cJSON *move_ok = cJSON_CreateObject();
    cJSON_AddStringToObject(move_ok, "action", "MOVE_OK");
    cJSON *ok_data = cJSON_CreateObject();
    cJSON_AddStringToObject(ok_data, "from", from);
    cJSON_AddStringToObject(ok_data, "to", to);
    cJSON_AddItemToObject(move_ok, "data", ok_data);
    send_json(client_idx, move_ok);
    cJSON_Delete(move_ok);

    // Send OPPONENT_MOVE to opponent
    cJSON *opp_move = cJSON_CreateObject();
    cJSON_AddStringToObject(opp_move, "action", "OPPONENT_MOVE");
    cJSON *opp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(opp_data, "from", from);
    cJSON_AddStringToObject(opp_data, "to", to);
    cJSON_AddItemToObject(opp_move, "data", opp_data);
    send_json(opponent_idx, opp_move);
    cJSON_Delete(opp_move);

    printf("Move in match %s: %s -> %s\n", match_id, from, to);

    // Check for game end
    char *winner = NULL;
    char *reason = NULL;
    if (check_game_end(match, &winner, &reason))
    {
        send_game_result(match_idx, winner, reason);
    }

    return 0;
}

// Send game result to both players
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
    cJSON_AddItemToObject(result, "data", data);

    send_json(match->white_client_idx, result);
    send_json(match->black_client_idx, result);

    cJSON_Delete(result);

    // Update player status
    pthread_mutex_lock(&clients_mutex);
    clients[match->white_client_idx].status = STATUS_ONLINE;
    clients[match->black_client_idx].status = STATUS_ONLINE;
    pthread_mutex_unlock(&clients_mutex);

    // Deactivate match
    match->is_active = 0;

    pthread_mutex_unlock(&match_mutex);

    printf("Match %s ended. Winner: %s (%s)\n", match->match_id, winner, reason);
}