/**
 * elo_manager.c - ELO Rating System Manager
 *
 * Module quản lý hệ thống điểm ELO cho game cờ vua.
 * Công thức ELO:
 * - K = 32 (hệ số K cho người chơi mới)
 * - Expected = 1 / (1 + 10^((opponent_elo - player_elo) / 400))
 * - New Rating = Old Rating + K * (Score - Expected)
 *   + Score = 1 (thắng), 0.5 (hòa), 0 (thua)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "cJSON.h"
#include "server.h"

#define K_FACTOR 32      // Hệ số K cho tính ELO
#define DEFAULT_ELO 1200 // ELO mặc định cho người chơi mới

// External references từ auth_manager.c
extern pthread_mutex_t auth_mutex;

// Forward declarations
int find_user(const char *username);
void save_users();

// Biến extern từ auth_manager.c - cần truy cập mảng users
extern User users[];
extern int user_count;

/**
 * calculate_expected_score - Tính điểm kỳ vọng
 * @player_elo: Điểm ELO của người chơi
 * @opponent_elo: Điểm ELO của đối thủ
 *
 * Return: Xác suất thắng dự kiến (0.0 - 1.0)
 */
double calculate_expected_score(int player_elo, int opponent_elo)
{
    return 1.0 / (1.0 + pow(10.0, (double)(opponent_elo - player_elo) / 400.0));
}

/**
 * calculate_elo_change - Tính điểm ELO thay đổi
 * @winner_elo: Điểm ELO của người thắng
 * @loser_elo: Điểm ELO của người thua
 * @is_draw: 1 nếu hòa, 0 nếu có người thắng/thua
 *
 * Return: Điểm ELO thay đổi (dương cho người thắng/điểm thấp hơn khi hòa)
 *
 * Lưu ý:
 * - Nếu thắng: winner += return_value, loser -= return_value
 * - Nếu hòa: điểm cao hơn -= return_value, điểm thấp hơn += return_value
 */
int calculate_elo_change(int winner_elo, int loser_elo, int is_draw)
{
    double expected_winner = calculate_expected_score(winner_elo, loser_elo);
    double expected_loser = calculate_expected_score(loser_elo, winner_elo);

    int change;

    if (is_draw)
    {
        // Hòa: điểm dựa trên sự chênh lệch so với kỳ vọng
        // Người có ELO cao hơn kỳ vọng thắng nhiều hơn -> mất điểm khi hòa
        // Người có ELO thấp hơn kỳ vọng thua -> được điểm khi hòa
        change = (int)round(K_FACTOR * (0.5 - expected_winner));
        // Nếu winner_elo > loser_elo: change < 0 (winner mất điểm)
        // Nếu winner_elo < loser_elo: change > 0 (winner được điểm)
    }
    else
    {
        // Thắng: score = 1
        change = (int)round(K_FACTOR * (1.0 - expected_winner));
    }

    // Đảm bảo ít nhất 1 điểm thay đổi nếu không hòa
    if (!is_draw && change == 0)
    {
        change = 1;
    }

    return change;
}

/**
 * update_elo_ratings - Cập nhật điểm ELO sau ván đấu
 * @white_player: Username của người chơi quân trắng
 * @black_player: Username của người chơi quân đen
 * @winner: Username của người thắng, "DRAW" nếu hòa, NULL nếu hủy ván
 *
 * Hàm này thread-safe, tự động lock mutex.
 */
void update_elo_ratings(const char *white_player, const char *black_player, const char *winner)
{
    if (!white_player || !black_player || !winner)
    {
        return;
    }

    // Không cập nhật ELO nếu ván bị hủy (ABORT)
    if (strcmp(winner, "ABORT") == 0)
    {
        printf("Match aborted, no ELO change\n");
        return;
    }

    pthread_mutex_lock(&auth_mutex);

    // Tìm cả 2 người chơi
    int white_idx = find_user(white_player);
    int black_idx = find_user(black_player);

    if (white_idx == -1 || black_idx == -1)
    {
        pthread_mutex_unlock(&auth_mutex);
        printf("Error: Could not find players for ELO update\n");
        return;
    }

    int white_elo = users[white_idx].elo_rating;
    int black_elo = users[black_idx].elo_rating;
    int is_draw = (strcmp(winner, "DRAW") == 0);

    int elo_change;

    if (is_draw)
    {
        // Hòa: tính từ góc nhìn của white
        elo_change = calculate_elo_change(white_elo, black_elo, 1);

        users[white_idx].elo_rating += elo_change;
        users[black_idx].elo_rating -= elo_change;

        // Cập nhật số trận hòa
        users[white_idx].draws++;
        users[black_idx].draws++;

        printf("ELO Update (DRAW): %s %d -> %d, %s %d -> %d\n",
               white_player, white_elo, users[white_idx].elo_rating,
               black_player, black_elo, users[black_idx].elo_rating);
    }
    else
    {
        // Có người thắng
        int winner_idx, loser_idx;
        int winner_elo, loser_elo;
        const char *winner_name;
        const char *loser_name;

        if (strcmp(winner, white_player) == 0)
        {
            winner_idx = white_idx;
            loser_idx = black_idx;
            winner_elo = white_elo;
            loser_elo = black_elo;
            winner_name = white_player;
            loser_name = black_player;
        }
        else
        {
            winner_idx = black_idx;
            loser_idx = white_idx;
            winner_elo = black_elo;
            loser_elo = white_elo;
            winner_name = black_player;
            loser_name = white_player;
        }

        elo_change = calculate_elo_change(winner_elo, loser_elo, 0);

        users[winner_idx].elo_rating += elo_change;
        users[loser_idx].elo_rating -= elo_change;

        // Đảm bảo ELO không âm
        if (users[loser_idx].elo_rating < 0)
        {
            users[loser_idx].elo_rating = 0;
        }

        // Cập nhật số trận thắng/thua
        users[winner_idx].wins++;
        users[loser_idx].losses++;

        printf("ELO Update: %s (WINNER) %d -> %d (+%d), %s (LOSER) %d -> %d (-%d)\n",
               winner_name, winner_elo, users[winner_idx].elo_rating, elo_change,
               loser_name, loser_elo, users[loser_idx].elo_rating, elo_change);
    }

    // Lưu vào file
    save_users();

    pthread_mutex_unlock(&auth_mutex);
}

/**
 * get_user_elo - Lấy điểm ELO của user
 * @username: Tên user
 *
 * Return: Điểm ELO, DEFAULT_ELO nếu không tìm thấy
 */
int get_user_elo(const char *username)
{
    pthread_mutex_lock(&auth_mutex);

    int user_idx = find_user(username);
    int elo = DEFAULT_ELO;

    if (user_idx != -1)
    {
        elo = users[user_idx].elo_rating;
    }

    pthread_mutex_unlock(&auth_mutex);
    return elo;
}

/**
 * get_user_stats - Lấy thống kê của user
 * @username: Tên user
 * @elo: Pointer để lưu ELO
 * @wins: Pointer để lưu số trận thắng
 * @losses: Pointer để lưu số trận thua
 * @draws: Pointer để lưu số trận hòa
 *
 * Return: 0 nếu thành công, -1 nếu không tìm thấy user
 */
int get_user_stats(const char *username, int *elo, int *wins, int *losses, int *draws)
{
    pthread_mutex_lock(&auth_mutex);

    int user_idx = find_user(username);
    if (user_idx == -1)
    {
        pthread_mutex_unlock(&auth_mutex);
        return -1;
    }

    if (elo)
        *elo = users[user_idx].elo_rating;
    if (wins)
        *wins = users[user_idx].wins;
    if (losses)
        *losses = users[user_idx].losses;
    if (draws)
        *draws = users[user_idx].draws;

    pthread_mutex_unlock(&auth_mutex);
    return 0;
}

/**
 * elo_manager_init - Khởi tạo ELO manager
 *
 * Gọi sau auth_manager_init() để đảm bảo users đã được load.
 */
void elo_manager_init()
{
    printf("ELO Manager initialized (K=%d, Default ELO=%d)\n", K_FACTOR, DEFAULT_ELO);
}
