// game_manager.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "cJSON.h"
#include "server.h"

extern Match matches[];
extern pthread_mutex_t match_mutex;

void game_manager_init()
{
    // Nothing special to initialize
}

// Convert chess notation (e.g., "E2") to row, col
int notation_to_coords(const char *notation, int *row, int *col)
{
    if (strlen(notation) != 2)
        return -1;

    char file = toupper(notation[0]);
    char rank = notation[1];

    if (file < 'A' || file > 'H' || rank < '1' || rank > '8')
    {
        return -1;
    }

    *col = file - 'A';
    *row = 8 - (rank - '0'); // Row 0 is rank 8

    return 0;
}

// Check if a square is under attack by opponent
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

            // Check if this piece can attack the target square
            int dr = row - r;
            int dc = col - c;

            // Pawn attacks
            if (p == 'p')
            {
                int dir = by_white ? -1 : 1; // White moves up (negative), black down
                if (dr == dir && abs(dc) == 1)
                    return 1;
            }
            // Knight
            else if (p == 'n')
            {
                if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
                    return 1;
            }
            // Bishop
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
            // Rook
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
            // Queen
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
            // King
            else if (p == 'k')
            {
                if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0))
                    return 1;
            }
        }
    }
    return 0;
}

// Basic move validation with piece-specific rules
int is_valid_move(Match *match, int from_row, int from_col, int to_row, int to_col, int player_turn)
{
    // Check bounds
    if (from_row < 0 || from_row > 7 || from_col < 0 || from_col > 7 ||
        to_row < 0 || to_row > 7 || to_col < 0 || to_col > 7)
    {
        return 0;
    }

    char piece = match->board[from_row][from_col];

    // Check if there's a piece at source
    if (piece == '.')
        return 0;

    // Check if it's the correct player's piece
    int is_white_piece = (piece >= 'a' && piece <= 'z');
    if (player_turn == 0 && !is_white_piece)
        return 0; // White's turn
    if (player_turn == 1 && is_white_piece)
        return 0; // Black's turn

    // Check if destination has own piece
    char dest = match->board[to_row][to_col];
    if (dest != '.')
    {
        int is_dest_white = (dest >= 'a' && dest <= 'z');
        if (is_white_piece == is_dest_white)
            return 0; // Can't capture own piece
    }

    // Calculate movement
    int dr = to_row - from_row;
    int dc = to_col - from_col;

    char p = tolower(piece);

    // Piece-specific movement rules
    switch (p)
    {
    case 'p': // Pawn
    {
        int dir = is_white_piece ? -1 : 1; // White moves up, black down
        int start_row = is_white_piece ? 6 : 1;

        // Forward move
        if (dc == 0 && dest == '.')
        {
            if (dr == dir)
                return 1;
            // Double move from start
            if (from_row == start_row && dr == 2 * dir && match->board[from_row + dir][from_col] == '.')
                return 1;
        }
        // Capture
        if (abs(dc) == 1 && dr == dir && dest != '.')
            return 1;

        return 0;
    }

    case 'n': // Knight
        if ((abs(dr) == 2 && abs(dc) == 1) || (abs(dr) == 1 && abs(dc) == 2))
            return 1;
        return 0;

    case 'b': // Bishop
        if (abs(dr) == abs(dc) && dr != 0)
        {
            // Check path is clear
            int step_r = (dr > 0) ? 1 : -1;
            int step_c = (dc > 0) ? 1 : -1;
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

    case 'r': // Rook
        if (dr == 0 || dc == 0)
        {
            // Check path is clear
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

    case 'q': // Queen
        if (dr == 0 || dc == 0 || abs(dr) == abs(dc))
        {
            // Check path is clear
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

    case 'k': // King
        if (abs(dr) <= 1 && abs(dc) <= 1 && (dr != 0 || dc != 0))
        {
            // Temporarily make the move to check if king would be in check
            char temp = match->board[to_row][to_col];
            match->board[to_row][to_col] = piece;
            match->board[from_row][from_col] = '.';

            // Check if king is under attack at new position
            int under_attack = is_square_under_attack(match, to_row, to_col, !is_white_piece);

            // Restore board
            match->board[from_row][from_col] = piece;
            match->board[to_row][to_col] = temp;

            return !under_attack;
        }
        return 0;

    default:
        return 0;
    }
}

// Check for checkmate/stalemate (simplified - always returns 0)
int check_game_end(Match *match, char **winner, char **reason)
{
    // This is a simplified version
    // In a real implementation, you would check for:
    // - Checkmate
    // - Stalemate
    // - Insufficient material
    // - 50-move rule
    // - Threefold repetition

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