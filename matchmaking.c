/**
 * matchmaking.c - Matchmaking System Module
 *
 * Module quản lý hệ thống ghép cặp tự động dựa trên điểm ELO.
 * Người chơi có ELO chênh lệch < 100 sẽ được ưu tiên ghép cặp.
 * Sử dụng background thread để liên tục check hàng đợi.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "cJSON.h"
#include "server.h"

#define MAX_QUEUE 100          // Kích thước tối đa hàng đợi
#define ELO_THRESHOLD 100      // Chênh lệch ELO tối đa để ghép cặp
#define MATCHMAKING_INTERVAL 2 // Giây giữa mỗi lần check queue

/**
 * QueueEntry - Thông tin một người trong hàng đợi matchmaking
 */
typedef struct
{
    int client_idx;   // Index trong mảng clients
    int elo_rating;   // Điểm ELO khi vào hàng đợi
    time_t join_time; // Thời điểm vào hàng đợi
    int is_active;    // 1 nếu còn trong hàng đợi
} QueueEntry;

// Biến toàn cục cho matchmaking
static QueueEntry matchmaking_queue[MAX_QUEUE];
static int queue_count = 0;
static pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t matchmaking_thread;
static int matchmaking_running = 0;

// External references
extern Client clients[];
extern pthread_mutex_t clients_mutex;

// Forward declarations from other modules
int create_match(int challenger_idx, int opponent_idx);
int get_user_elo(const char *username);

/**
 * matchmaking_init - Khởi tạo matchmaking system
 *
 * Reset hàng đợi và đánh dấu tất cả slot trống.
 */
void matchmaking_queue_init()
{
    pthread_mutex_lock(&queue_mutex);
    for (int i = 0; i < MAX_QUEUE; i++)
    {
        matchmaking_queue[i].is_active = 0;
    }
    queue_count = 0;
    pthread_mutex_unlock(&queue_mutex);
}

/**
 * find_queue_slot - Tìm slot trống trong hàng đợi
 * Return: Index của slot trống, -1 nếu đầy
 */
static int find_queue_slot()
{
    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (!matchmaking_queue[i].is_active)
            return i;
    }
    return -1;
}

/**
 * is_client_in_queue - Kiểm tra client có trong hàng đợi không
 * @client_idx: Index của client
 * Return: 1 nếu có, 0 nếu không
 */
static int is_client_in_queue(int client_idx)
{
    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (matchmaking_queue[i].is_active &&
            matchmaking_queue[i].client_idx == client_idx)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * add_to_matchmaking_queue - Thêm client vào hàng đợi matchmaking
 * @client_idx: Index của client
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int add_to_matchmaking_queue(int client_idx)
{
    pthread_mutex_lock(&queue_mutex);

    // Kiểm tra đã trong hàng đợi chưa
    if (is_client_in_queue(client_idx))
    {
        pthread_mutex_unlock(&queue_mutex);
        return -1; // Đã trong hàng đợi
    }

    // Tìm slot trống
    int slot = find_queue_slot();
    if (slot == -1)
    {
        pthread_mutex_unlock(&queue_mutex);
        return -1; // Hàng đợi đầy
    }

    // Lấy ELO của người chơi
    pthread_mutex_lock(&clients_mutex);
    const char *username = clients[client_idx].username;
    pthread_mutex_unlock(&clients_mutex);

    int elo = get_user_elo(username);

    // Thêm vào hàng đợi
    matchmaking_queue[slot].client_idx = client_idx;
    matchmaking_queue[slot].elo_rating = elo;
    matchmaking_queue[slot].join_time = time(NULL);
    matchmaking_queue[slot].is_active = 1;
    queue_count++;

    printf("Added to matchmaking queue: client %d (ELO: %d), queue size: %d\n",
           client_idx, elo, queue_count);

    pthread_mutex_unlock(&queue_mutex);
    return 0;
}

/**
 * remove_from_matchmaking_queue - Xóa client khỏi hàng đợi
 * @client_idx: Index của client
 * Return: 0 nếu thành công, -1 nếu không tìm thấy
 */
int remove_from_matchmaking_queue(int client_idx)
{
    pthread_mutex_lock(&queue_mutex);

    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (matchmaking_queue[i].is_active &&
            matchmaking_queue[i].client_idx == client_idx)
        {
            matchmaking_queue[i].is_active = 0;
            queue_count--;
            printf("Removed from matchmaking queue: client %d, queue size: %d\n",
                   client_idx, queue_count);
            pthread_mutex_unlock(&queue_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&queue_mutex);
    return -1; // Không tìm thấy
}

/**
 * find_match_in_queue - Tìm đối thủ phù hợp trong hàng đợi
 * @client_idx: Index của client cần tìm đối thủ
 * Return: Index của đối thủ phù hợp trong hàng đợi, -1 nếu không tìm thấy
 */
int find_match_in_queue(int client_idx)
{
    int player_elo = -1;
    int best_match = -1;
    int best_elo_diff = ELO_THRESHOLD + 1;
    time_t oldest_time = 0;

    // Tìm ELO của người chơi hiện tại
    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (matchmaking_queue[i].is_active &&
            matchmaking_queue[i].client_idx == client_idx)
        {
            player_elo = matchmaking_queue[i].elo_rating;
            break;
        }
    }

    if (player_elo == -1)
        return -1; // Người chơi không trong hàng đợi

    // Tìm đối thủ có ELO gần nhất
    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (matchmaking_queue[i].is_active &&
            matchmaking_queue[i].client_idx != client_idx)
        {
            int elo_diff = abs(matchmaking_queue[i].elo_rating - player_elo);

            // Ưu tiên: ELO gần nhất, sau đó là người đợi lâu nhất
            if (elo_diff < ELO_THRESHOLD)
            {
                if (elo_diff < best_elo_diff ||
                    (elo_diff == best_elo_diff &&
                     matchmaking_queue[i].join_time < oldest_time))
                {
                    best_match = i;
                    best_elo_diff = elo_diff;
                    oldest_time = matchmaking_queue[i].join_time;
                }
            }
        }
    }

    return best_match;
}

/**
 * send_matchmaking_status - Gửi thông báo trạng thái matchmaking
 * @client_idx: Index của client
 * @status: "SEARCHING", "FOUND", "CANCELLED"
 * @opponent: Username của đối thủ (nếu có)
 */
static void send_matchmaking_status(int client_idx, const char *status, const char *opponent)
{
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "MATCHMAKING_STATUS");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", status);
    if (opponent)
    {
        cJSON_AddStringToObject(data, "opponent", opponent);
    }
    cJSON_AddItemToObject(response, "data", data);
    send_json(client_idx, response);
    cJSON_Delete(response);
}

/**
 * process_matchmaking_queue - Xử lý hàng đợi và ghép cặp
 *
 * Duyệt qua hàng đợi, tìm các cặp có ELO phù hợp và tạo ván đấu.
 */
static void process_matchmaking_queue()
{
    pthread_mutex_lock(&queue_mutex);

    // Cần ít nhất 2 người trong hàng đợi
    if (queue_count < 2)
    {
        pthread_mutex_unlock(&queue_mutex);
        return;
    }

    // Tạo danh sách các entry active
    int active_indices[MAX_QUEUE];
    int active_count = 0;

    for (int i = 0; i < MAX_QUEUE; i++)
    {
        if (matchmaking_queue[i].is_active)
        {
            active_indices[active_count++] = i;
        }
    }

    // Duyệt và ghép cặp
    for (int i = 0; i < active_count; i++)
    {
        int idx1 = active_indices[i];
        if (!matchmaking_queue[idx1].is_active)
            continue;

        int player1_client = matchmaking_queue[idx1].client_idx;
        int player1_elo = matchmaking_queue[idx1].elo_rating;

        // Tìm đối thủ phù hợp
        int best_match = -1;
        int best_elo_diff = ELO_THRESHOLD + 1;

        for (int j = i + 1; j < active_count; j++)
        {
            int idx2 = active_indices[j];
            if (!matchmaking_queue[idx2].is_active)
                continue;

            int elo_diff = abs(matchmaking_queue[idx2].elo_rating - player1_elo);
            if (elo_diff < ELO_THRESHOLD && elo_diff < best_elo_diff)
            {
                best_match = idx2;
                best_elo_diff = elo_diff;
            }
        }

        // Nếu tìm thấy đối thủ phù hợp
        if (best_match != -1)
        {
            int player2_client = matchmaking_queue[best_match].client_idx;
            int player2_elo = matchmaking_queue[best_match].elo_rating;

            // Xóa cả 2 khỏi hàng đợi
            matchmaking_queue[idx1].is_active = 0;
            matchmaking_queue[best_match].is_active = 0;
            queue_count -= 2;

            // Lấy username để log
            pthread_mutex_lock(&clients_mutex);
            char player1_name[MAX_USERNAME] = "";
            char player2_name[MAX_USERNAME] = "";
            strncpy(player1_name, clients[player1_client].username, MAX_USERNAME - 1);
            strncpy(player2_name, clients[player2_client].username, MAX_USERNAME - 1);
            pthread_mutex_unlock(&clients_mutex);

            pthread_mutex_unlock(&queue_mutex);

            printf("Matchmaking: Found match! %s (ELO: %d) vs %s (ELO: %d), diff: %d\n",
                   player1_name, player1_elo, player2_name, player2_elo, best_elo_diff);

            // Gửi thông báo MATCH_FOUND cho cả 2
            send_matchmaking_status(player1_client, "FOUND", player2_name);
            send_matchmaking_status(player2_client, "FOUND", player1_name);

            // Tạo ván đấu
            create_match(player1_client, player2_client);

            // Tiếp tục xử lý queue (đệ quy)
            process_matchmaking_queue();
            return;
        }
    }

    pthread_mutex_unlock(&queue_mutex);
}

/**
 * matchmaking_thread_func - Thread function cho matchmaking
 * @arg: Không sử dụng
 *
 * Chạy vô hạn, mỗi 2 giây check hàng đợi và ghép cặp.
 */
static void *matchmaking_thread_func(void *arg)
{
    (void)arg; // Unused

    printf("Matchmaking thread started\n");

    while (matchmaking_running)
    {
        sleep(MATCHMAKING_INTERVAL);
        process_matchmaking_queue();
    }

    printf("Matchmaking thread stopped\n");
    return NULL;
}

/**
 * matchmaking_start - Khởi động matchmaking background thread
 */
void matchmaking_start()
{
    if (matchmaking_running)
        return;

    matchmaking_queue_init();
    matchmaking_running = 1;

    if (pthread_create(&matchmaking_thread, NULL, matchmaking_thread_func, NULL) != 0)
    {
        perror("Failed to create matchmaking thread");
        matchmaking_running = 0;
        return;
    }

    pthread_detach(matchmaking_thread);
    printf("Matchmaking system started (interval: %ds, ELO threshold: %d)\n",
           MATCHMAKING_INTERVAL, ELO_THRESHOLD);
}

/**
 * matchmaking_stop - Dừng matchmaking thread
 */
void matchmaking_stop()
{
    matchmaking_running = 0;
}

/**
 * handle_find_match - Xử lý yêu cầu tìm trận từ client
 * @client_idx: Index của client
 * @data: JSON data (không sử dụng)
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_find_match(int client_idx, cJSON *data)
{
    (void)data; // Unused

    // Kiểm tra client đã đăng nhập chưa
    pthread_mutex_lock(&clients_mutex);
    if (clients[client_idx].username[0] == '\0')
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Not logged in");
        return -1;
    }

    // Kiểm tra đã trong ván đấu chưa
    if (clients[client_idx].status == STATUS_IN_MATCH)
    {
        pthread_mutex_unlock(&clients_mutex);
        send_error(client_idx, "Already in a match");
        return -1;
    }
    pthread_mutex_unlock(&clients_mutex);

    // Thêm vào hàng đợi matchmaking
    if (add_to_matchmaking_queue(client_idx) != 0)
    {
        send_error(client_idx, "Already in matchmaking queue");
        return -1;
    }

    // Gửi thông báo đang tìm trận
    send_matchmaking_status(client_idx, "SEARCHING", NULL);

    return 0;
}

/**
 * handle_cancel_find_match - Xử lý yêu cầu hủy tìm trận
 * @client_idx: Index của client
 * @data: JSON data (không sử dụng)
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_cancel_find_match(int client_idx, cJSON *data)
{
    (void)data; // Unused

    if (remove_from_matchmaking_queue(client_idx) != 0)
    {
        send_error(client_idx, "Not in matchmaking queue");
        return -1;
    }

    // Gửi thông báo đã hủy
    send_matchmaking_status(client_idx, "CANCELLED", NULL);

    return 0;
}
