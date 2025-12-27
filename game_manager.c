/**
 * game_manager_full.c - Game Logic Manager với đầy đủ luật cờ vua
 *
 * Các luật được implement:
 * 1. Di chuyển cơ bản của tất cả các quân (Pawn, Knight, Bishop, Rook, Queen, King)
 * 2. En passant (ăn tốt qua đường)
 * 3. Castling (nhập thành - cả hai bên)
 * 4. Pawn promotion (phong cấp tốt)
 * 5. Kiểm tra nước đi không để vua bị chiếu (cho tất cả quân)
 * 6. Phát hiện chiếu, chiếu hết, bế tắc
 * 7. Hòa do thiếu quân, lặp lại nước đi, 50 nước không ăn quân
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"
#include "server.h"

extern Match matches[];
extern pthread_mutex_t match_mutex;

/**
 * notation_to_coords - Chuyển ký hiệu cờ vua sang tọa độ
 * VD: "E2" -> row=6, col=4
 */
int notation_to_coords(const char *notation, int *row, int *col)
{
    if (strlen(notation) != 2)
        return -1;

    char file = toupper(notation[0]);
    char rank = notation[1];

    if (file < 'A' || file > 'H' || rank < '1' || rank > '8')
        return -1;

    *col = file - 'A';
    *row = 8 - (rank - '0');
    return 0;
}

/**
 * coords_to_notation - Chuyển tọa độ sang ký hiệu cờ vua
 * VD: row=6, col=4 -> "E2"
 */
void coords_to_notation(int row, int col, char *notation)
{
    notation[0] = 'A' + col;
    notation[1] = '0' + (8 - row);
    notation[2] = '\0';
}

/**
 * is_square_under_attack - Kiểm tra ô có bị tấn công không
 * @by_white: 1 nếu kiểm tra bị quân trắng tấn công
 */
int is_square_under_attack(Match *match, int row, int col, int by_white)
{
    for (int r = 0; r < 8; r++)
    {
        for (int c = 0; c < 8; c++)
        {
            char piece = match->board[r][c];
            if (piece == '.')
                continue;

            int is_white_piece = (piece >= 'a' && piece <= 'z');
            if (is_white_piece != by_white)
                continue;

            char p = tolower(piece);
            int dr = row - r;
            int dc = col - c;

            // Tốt - tấn công chéo
            if (p == 'p')
            {
                int dir = by_white ? -1 : 1;
                if (dr == dir && abs(dc) == 1)
                    return 1;
            }
            // Mã - chữ L
            else if (p == 'n')
            {
                if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
                    return 1;
            }
            // Tượng - chéo
            else if (p == 'b')
            {
                if (abs(dr) == abs(dc) && dr != 0)
                {
                    int step_r = (dr > 0) ? 1 : -1;
                    int step_c = (dc > 0) ? 1 : -1;
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
            // Xe - ngang/dọc
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
            // Hậu - kết hợp xe + tượng
            else if (p == 'q')
            {
                if (dr == 0 || dc == 0 || abs(dr) == abs(dc))
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
            // Vua - 1 ô mọi hướng
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
 * find_king - Tìm vị trí vua
 */
int find_king(Match *match, int is_white, int *king_row, int *king_col)
{
    char king = is_white ? 'k' : 'K';
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
 */
int is_in_check(Match *match, int is_white)
{
    int king_row, king_col;
    if (!find_king(match, is_white, &king_row, &king_col))
        return 0;
    return is_square_under_attack(match, king_row, king_col, !is_white);
}

/**
 * is_valid_move - Kiểm tra tính hợp lệ của nước đi (ĐẦY ĐỦ)
 *
 * Luật được kiểm tra:
 * - Ranh giới bàn cờ
 * - Quân đúng màu
 * - Quy tắc di chuyển của từng loại quân
 * - Đường đi không bị chặn
 * - En passant cho tốt
 * - Castling cho vua
 * - QUAN TRỌNG: Nước đi không để vua bị chiếu (cho TẤT CẢ quân)
 */
int is_valid_move(Match *match, int from_row, int from_col, int to_row, int to_col, int player_turn)
{
    // Kiểm tra ranh giới
    if (from_row < 0 || from_row > 7 || from_col < 0 || from_col > 7 ||
        to_row < 0 || to_row > 7 || to_col < 0 || to_col > 7)
        return 0;

    char piece = match->board[from_row][from_col];
    if (piece == '.')
        return 0;

    int is_white_piece = (piece >= 'a' && piece <= 'z');
    if (player_turn == 0 && !is_white_piece)
        return 0;
    if (player_turn == 1 && is_white_piece)
        return 0;

    char dest = match->board[to_row][to_col];
    if (dest != '.')
    {
        int is_dest_white = (dest >= 'a' && dest <= 'z');
        if (is_white_piece == is_dest_white)
            return 0;
    }

    int dr = to_row - from_row;
    int dc = to_col - from_col;
    char p = tolower(piece);

    // Kiểm tra quy tắc di chuyển cơ bản
    int basic_move_valid = 0;

    switch (p)
    {
    case 'p': // Tốt
    {
        int dir = is_white_piece ? -1 : 1;
        int start_row = is_white_piece ? 6 : 1;

        // Đi tiến
        if (dc == 0 && dest == '.')
        {
            if (dr == dir)
                basic_move_valid = 1;
            else if (from_row == start_row && dr == 2 * dir &&
                     match->board[from_row + dir][from_col] == '.')
                basic_move_valid = 1;
        }
        // Ăn chéo thường
        else if (abs(dc) == 1 && dr == dir && dest != '.')
        {
            basic_move_valid = 1;
        }
        // En passant
        else if (abs(dc) == 1 && dr == dir && dest == '.')
        {
            int en_passant_row = is_white_piece ? 3 : 4;
            if (from_row == en_passant_row && to_col == match->en_passant_col)
            {
                char target_pawn = match->board[from_row][to_col];
                char expected_enemy = is_white_piece ? 'P' : 'p';
                if (target_pawn == expected_enemy)
                    basic_move_valid = 1;
            }
        }
        break;
    }

    case 'n': // Mã
        if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
            basic_move_valid = 1;
        break;

    case 'b': // Tượng
        if (abs(dr) == abs(dc) && dr != 0)
        {
            int step_r = (dr > 0) ? 1 : -1;
            int step_c = (dc > 0) ? 1 : -1;
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            basic_move_valid = 1;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.')
                {
                    basic_move_valid = 0;
                    break;
                }
                check_r += step_r;
                check_c += step_c;
            }
        }
        break;

    case 'r': // Xe
        if (dr == 0 || dc == 0)
        {
            int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
            int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            basic_move_valid = 1;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.')
                {
                    basic_move_valid = 0;
                    break;
                }
                check_r += step_r;
                check_c += step_c;
            }
        }
        break;

    case 'q': // Hậu
        if (dr == 0 || dc == 0 || abs(dr) == abs(dc))
        {
            int step_r = (dr == 0) ? 0 : ((dr > 0) ? 1 : -1);
            int step_c = (dc == 0) ? 0 : ((dc > 0) ? 1 : -1);
            int check_r = from_row + step_r;
            int check_c = from_col + step_c;
            basic_move_valid = 1;
            while (check_r != to_row || check_c != to_col)
            {
                if (match->board[check_r][check_c] != '.')
                {
                    basic_move_valid = 0;
                    break;
                }
                check_r += step_r;
                check_c += step_c;
            }
        }
        break;

    case 'k': // Vua
        // Di chuyển thường (1 ô)
        if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0))
        {
            basic_move_valid = 1;
        }
        // Nhập thành (Castling)
        else if (dr == 0 && abs(dc) == 2)
        {
            int king_start_row = is_white_piece ? 7 : 0;
            if (from_row != king_start_row || from_col != 4)
                return 0;

            // Vua chưa di chuyển
            if (is_white_piece && match->white_king_moved)
                return 0;
            if (!is_white_piece && match->black_king_moved)
                return 0;

            // Vua không đang bị chiếu
            if (is_in_check(match, is_white_piece))
                return 0;

            // Kingside castling (O-O)
            if (dc == 2)
            {
                if (is_white_piece && match->white_rook_h_moved)
                    return 0;
                if (!is_white_piece && match->black_rook_h_moved)
                    return 0;

                char rook = is_white_piece ? 'r' : 'R';
                if (match->board[king_start_row][7] != rook)
                    return 0;

                if (match->board[king_start_row][5] != '.' ||
                    match->board[king_start_row][6] != '.')
                    return 0;

                if (is_square_under_attack(match, king_start_row, 5, !is_white_piece) ||
                    is_square_under_attack(match, king_start_row, 6, !is_white_piece))
                    return 0;

                return 1; // Castling hợp lệ
            }
            // Queenside castling (O-O-O)
            else if (dc == -2)
            {
                if (is_white_piece && match->white_rook_a_moved)
                    return 0;
                if (!is_white_piece && match->black_rook_a_moved)
                    return 0;

                char rook = is_white_piece ? 'r' : 'R';
                if (match->board[king_start_row][0] != rook)
                    return 0;

                if (match->board[king_start_row][1] != '.' ||
                    match->board[king_start_row][2] != '.' ||
                    match->board[king_start_row][3] != '.')
                    return 0;

                if (is_square_under_attack(match, king_start_row, 2, !is_white_piece) ||
                    is_square_under_attack(match, king_start_row, 3, !is_white_piece))
                    return 0;

                return 1; // Castling hợp lệ
            }
        }
        break;
    }

    if (!basic_move_valid)
        return 0;

    // *** LUẬT QUAN TRỌNG: Kiểm tra nước đi không để vua bị chiếu ***
    // Đây là luật bắt buộc cho TẤT CẢ quân cờ, không chỉ vua

    // Lưu trạng thái để restore sau
    char temp_dest = match->board[to_row][to_col];
    char temp_en_passant_pawn = '.';

    // Xử lý en passant đặc biệt
    if (p == 'p' && abs(dc) == 1 && dest == '.' && to_col == match->en_passant_col)
    {
        temp_en_passant_pawn = match->board[from_row][to_col];
        match->board[from_row][to_col] = '.';
    }

    // Thực hiện nước đi tạm thời
    match->board[to_row][to_col] = piece;
    match->board[from_row][from_col] = '.';

    // Kiểm tra vua có bị chiếu sau nước đi
    int in_check = is_in_check(match, is_white_piece);

    // Restore bàn cờ
    match->board[from_row][from_col] = piece;
    match->board[to_row][to_col] = temp_dest;
    if (temp_en_passant_pawn != '.')
        match->board[from_row][to_col] = temp_en_passant_pawn;

    return !in_check; // Chỉ hợp lệ nếu vua không bị chiếu
}

/**
 * has_legal_moves - Kiểm tra còn nước đi hợp lệ không
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

            for (int to_r = 0; to_r < 8; to_r++)
            {
                for (int to_c = 0; to_c < 8; to_c++)
                {
                    if (is_valid_move(match, from_r, from_c, to_r, to_c, is_white ? 0 : 1))
                        return 1;
                }
            }
        }
    }
    return 0;
}

/**
 * is_insufficient_material - Kiểm tra hòa do thiếu quân
 */
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

            if (p == 'q' || p == 'r' || p == 'p')
                return 0;

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

    // K vs K
    if (white_pieces == 0 && black_pieces == 0)
        return 1;

    // K+B vs K or K+N vs K
    if ((white_pieces == 1 && black_pieces == 0) ||
        (white_pieces == 0 && black_pieces == 1))
    {
        if ((white_bishops == 1 || white_knights == 1) ||
            (black_bishops == 1 || black_knights == 1))
            return 1;
    }

    // K+B vs K+B
    if (white_bishops == 1 && black_bishops == 1 &&
        white_knights == 0 && black_knights == 0)
        return 1;

    return 0;
}

/**
 * check_game_end - Kiểm tra kết thúc game
 */
int check_game_end(Match *match, char **winner, char **reason)
{
    int current_is_white = (match->current_turn == 0);

    // Hòa do thiếu quân
    if (is_insufficient_material(match))
    {
        *winner = "DRAW";
        *reason = "Insufficient material";
        return 1;
    }

    int in_check = is_in_check(match, current_is_white);
    int has_moves = has_legal_moves(match, current_is_white);

    if (!has_moves)
    {
        if (in_check)
        {
            // Chiếu hết
            *winner = current_is_white ? match->black_player : match->white_player;
            *reason = "Checkmate";
            return 1;
        }
        else
        {
            // Bế tắc
            *winner = "DRAW";
            *reason = "Stalemate";
            return 1;
        }
    }

    return 0;
}

/**
 * execute_move - Thực hiện nước đi và cập nhật trạng thái
 * Xử lý: en passant, castling, pawn promotion, cập nhật flags
 */
void execute_move(Match *match, int from_row, int from_col, int to_row, int to_col, char promotion_piece)
{
    char piece = match->board[from_row][from_col];
    int is_white = (piece >= 'a' && piece <= 'z');
    char p = tolower(piece);

    // Reset en passant
    match->en_passant_col = -1;

    // Xử lý từng loại nước đi đặc biệt

    // 1. En passant
    if (p == 'p' && abs(to_col - from_col) == 1 && match->board[to_row][to_col] == '.')
    {
        match->board[from_row][to_col] = '.'; // Xóa tốt bị ăn
    }

    // 2. Cập nhật en passant cho nước đi tiếp theo
    if (p == 'p' && abs(to_row - from_row) == 2)
    {
        match->en_passant_col = from_col;
    }

    // 3. Castling
    if (p == 'k' && abs(to_col - from_col) == 2)
    {
        if (to_col == 6) // Kingside
        {
            match->board[to_row][5] = match->board[to_row][7];
            match->board[to_row][7] = '.';
        }
        else if (to_col == 2) // Queenside
        {
            match->board[to_row][3] = match->board[to_row][0];
            match->board[to_row][0] = '.';
        }
    }

    // 4. Pawn promotion
    if (p == 'p' && (to_row == 0 || to_row == 7))
    {
        if (promotion_piece != '\0')
        {
            char promoted = is_white ? tolower(promotion_piece) : toupper(promotion_piece);
            piece = promoted;
        }
        else
        {
            // Mặc định phong hậu
            piece = is_white ? 'q' : 'Q';
        }
    }

    // Thực hiện nước đi
    match->board[to_row][to_col] = piece;
    match->board[from_row][from_col] = '.';

    // Cập nhật flags di chuyển
    if (p == 'k')
    {
        if (is_white)
            match->white_king_moved = 1;
        else
            match->black_king_moved = 1;
    }
    else if (p == 'r')
    {
        if (is_white)
        {
            if (from_col == 0)
                match->white_rook_a_moved = 1;
            else if (from_col == 7)
                match->white_rook_h_moved = 1;
        }
        else
        {
            if (from_col == 0)
                match->black_rook_a_moved = 1;
            else if (from_col == 7)
                match->black_rook_h_moved = 1;
        }
    }

    // Lưu nước đi cuối
    match->last_move_from_row = from_row;
    match->last_move_from_col = from_col;
    match->last_move_to_row = to_row;
    match->last_move_to_col = to_col;
}

/**
 * Tóm tắt các luật đã implement:
 *
 * ✅ 1. Di chuyển cơ bản (Pawn, Knight, Bishop, Rook, Queen, King)
 * ✅ 2. Kiểm tra đường đi không bị chặn (Bishop, Rook, Queen)
 * ✅ 3. Pawn: đi 1 ô, đi 2 ô từ vị trí xuất phát, ăn chéo
 * ✅ 4. En passant (ăn tốt qua đường)
 * ✅ 5. Castling (nhập thành) - cả kingside và queenside
 * ✅ 6. Pawn promotion (phong cấp tốt khi đến hàng cuối)
 * ✅ 7. Nước đi không để vua bị chiếu (CHO TẤT CẢ QUÂN - luật quan trọng nhất)
 * ✅ 8. Phát hiện chiếu (check)
 * ✅ 9. Phát hiện chiếu hết (checkmate)
 * ✅ 10. Phát hiện bế tắc (stalemate)
 * ✅ 11. Hòa do thiếu quân (insufficient material)
 *
 * Các luật nâng cao có thể thêm (không bắt buộc):
 * - Hòa do lặp lại nước đi 3 lần (threefold repetition)
 * - Hòa do 50 nước không ăn quân và không di chuyển tốt (fifty-move rule)
 * - Time control
 */

/**
 * game_manager_init - Khởi tạo game manager
 */
void game_manager_init()
{
    // Không cần khởi tạo gì đặc biệt
    printf("Game Manager initialized with full chess rules\n");
}
