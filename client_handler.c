/**
 * client_handler.c - Client Handler Module
 *
 * Module xử lý giao tiếp với từng client trong thread riêng biệt.
 * Đảm nhận việc nhận, parse và route các message đến handler tương ứng.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "cJSON.h"
#include "server.h"

/**
 * recv_message - Nhận message từ socket (đọc đến khi gặp \n)
 * @socket: Socket descriptor của client
 * @buffer: Buffer để lưu message nhận được
 * @buffer_size: Kích thước tối đa của buffer
 *
 * Đọc từng byte cho đến khi gặp ký tự newline (\n) hoặc hết buffer.
 *
 * Return: Số byte đã đọc, -1 nếu lỗi hoặc client ngắt kết nối
 */
int recv_message(int socket, char *buffer, int buffer_size)
{
    int total = 0; // Tổng số byte đã đọc
    char c;

    // Đọc từng byte cho đến khi gặp newline
    while (total < buffer_size - 1)
    {
        int n = recv(socket, &c, 1, 0); // Đọc 1 byte
        if (n <= 0)
            return -1; // Lỗi hoặc kết nối đóng

        buffer[total++] = c; // Lưu byte vào buffer
        if (c == '\n')       // Gặp ký tự kết thúc message
            break;
    }

    buffer[total] = '\0'; // Thêm null terminator
    return total;
}

/**
 * send_json - Gửi JSON message đến client
 * @client_idx: Index của client trong mảng clients
 * @json: cJSON object cần gửi
 *
 * Chuyển JSON object thành string, thêm newline và gửi qua socket.
 * Sử dụng mutex để đảm bảo thread-safe khi nhiều thread gửi đồng thời.
 *
 * Return: Số byte đã gửi, -1 nếu lỗi
 */
int send_json(int client_idx, cJSON *json)
{
    // Chuyển JSON object thành string (không format)
    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str)
        return -1;

    // Khóa mutex để tránh race condition khi gửi
    pthread_mutex_lock(&clients[client_idx].send_mutex);

    // Thêm newline vào cuối message
    int len = strlen(json_str);
    char *message = malloc(len + 2); // +2 cho '\n' và '\0'
    strcpy(message, json_str);
    message[len] = '\n'; // Message delimiter
    message[len + 1] = '\0';

    // Gửi qua socket
    int result = send(clients[client_idx].socket, message, len + 1, 0);

    pthread_mutex_unlock(&clients[client_idx].send_mutex);

    // Giải phóng bộ nhớ
    free(message);
    free(json_str);

    return result;
}

/**
 * send_error - Gửi error message đến client
 * @client_idx: Index của client
 * @reason: Lý do lỗi (chuỗi mô tả)
 *
 * Tạo JSON message với action="ERROR" và gửi về client.
 */
void send_error(int client_idx, const char *reason)
{
    // Tạo JSON response với format:
    // {"action": "ERROR", "data": {"reason": "..."}}
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "action", "ERROR");
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "reason", reason);
    cJSON_AddItemToObject(json, "data", data);

    send_json(client_idx, json);
    cJSON_Delete(json); // Giải phóng JSON object
}

/**
 * process_message - Parse và xử lý message từ client
 * @client_idx: Index của client gửi message
 * @message: Chuỗi JSON message nhận được
 *
 * Parse JSON, lấy action field và route đến handler tương ứng.
 */
void process_message(int client_idx, const char *message)
{
    // Parse JSON string
    cJSON *json = cJSON_Parse(message);
    if (!json)
    {
        send_error(client_idx, "Invalid JSON"); // JSON không hợp lệ
        return;
    }

    // Lấy action và data từ JSON
    cJSON *action_obj = cJSON_GetObjectItem(json, "action");
    cJSON *data_obj = cJSON_GetObjectItem(json, "data");

    // Validate action field
    if (!action_obj || !cJSON_IsString(action_obj))
    {
        send_error(client_idx, "Missing action field");
        cJSON_Delete(json);
        return;
    }

    const char *action = action_obj->valuestring;

    printf("[Client %d] Action: %s\n", client_idx, action); // Log action

    // Route message đến handler tương ứng dựa trên action
    if (strcmp(action, "REGISTER") == 0)
    {
        handle_register(client_idx, data_obj); // Xử lý đăng ký
    }
    else if (strcmp(action, "LOGIN") == 0)
    {
        handle_login(client_idx, data_obj); // Xử lý đăng nhập
    }
    else if (strcmp(action, "REQUEST_PLAYER_LIST") == 0)
    {
        handle_request_player_list(client_idx); // Lấy danh sách người chơi
    }
    else if (strcmp(action, "GET_PROFILE") == 0)
    {
        handle_get_profile(client_idx, data_obj); // Xem hồ sơ người chơi
    }
    else if (strcmp(action, "CHALLENGE") == 0)
    {
        handle_challenge(client_idx, data_obj); // Gửi lời thách đấu
    }
    else if (strcmp(action, "ACCEPT") == 0)
    {
        handle_accept(client_idx, data_obj); // Chấp nhận thách đấu
    }
    else if (strcmp(action, "DECLINE") == 0)
    {
        handle_decline(client_idx, data_obj); // Từ chối thách đấu
    }
    else if (strcmp(action, "MOVE") == 0)
    {
        handle_move(client_idx, data_obj); // Xử lý nước đi
    }
    else if (strcmp(action, "FIND_MATCH") == 0)
    {
        handle_find_match(client_idx, data_obj); // Tìm trận tự động
    }
    else if (strcmp(action, "CANCEL_FIND_MATCH") == 0)
    {
        handle_cancel_find_match(client_idx, data_obj); // Hủy tìm trận
    }
    // === GAME CONTROL ACTIONS ===
    else if (strcmp(action, "OFFER_ABORT") == 0)
    {
        handle_offer_abort(client_idx, data_obj);
    }
    else if (strcmp(action, "ACCEPT_ABORT") == 0)
    {
        handle_accept_abort(client_idx, data_obj);
    }
    else if (strcmp(action, "DECLINE_ABORT") == 0)
    {
        handle_decline_abort(client_idx, data_obj);
    }
    else if (strcmp(action, "OFFER_DRAW") == 0)
    {
        handle_offer_draw(client_idx, data_obj);
    }
    else if (strcmp(action, "ACCEPT_DRAW") == 0)
    {
        handle_accept_draw(client_idx, data_obj);
    }
    else if (strcmp(action, "DECLINE_DRAW") == 0)
    {
        handle_decline_draw(client_idx, data_obj);
    }
    else if (strcmp(action, "OFFER_REMATCH") == 0)
    {
        handle_offer_rematch(client_idx, data_obj);
    }
    else if (strcmp(action, "ACCEPT_REMATCH") == 0)
    {
        handle_accept_rematch(client_idx, data_obj);
    }
    else if (strcmp(action, "DECLINE_REMATCH") == 0)
    {
        handle_decline_rematch(client_idx, data_obj);
    }
    // === MATCH HISTORY ACTIONS ===
    else if (strcmp(action, "GET_MATCH_HISTORY") == 0)
    {
        handle_get_match_history(client_idx, data_obj);
    }
    else if (strcmp(action, "GET_MATCH_REPLAY") == 0)
    {
        handle_get_match_replay(client_idx, data_obj);
    }
    else if (strcmp(action, "PING") == 0)
    {
        // Heartbeat - kiểm tra kết nối còn sống
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "PONG");
        cJSON_AddItemToObject(response, "data", cJSON_CreateObject());
        send_json(client_idx, response);
        cJSON_Delete(response);
    }
    else
    {
        send_error(client_idx, "Unknown action"); // Action không được hỗ trợ
    }

    cJSON_Delete(json);
}

/**
 * client_handler - Thread function xử lý từng client
 * @arg: Pointer đến ClientThreadArgs chứa client_index
 *
 * Thread này chạy độc lập cho mỗi client, xử lý:
 * 1. Nhận message từ socket
 * 2. Parse và route message
 * 3. Cleanup khi client disconnect
 *
 * Return: NULL khi thread kết thúc
 */
void *client_handler(void *arg)
{
    // Lấy tham số và giải phóng
    ClientThreadArgs *args = (ClientThreadArgs *)arg;
    int client_idx = args->client_index;
    free(args); // Giải phóng args ngay sau khi lấy được index

    char buffer[BUFFER_SIZE];

    printf("Thread started for client %d\n", client_idx);

    // Khởi tạo mutex cho việc gửi message (thread-safe)
    pthread_mutex_init(&clients[client_idx].send_mutex, NULL);

    // Vòng lặp chính - nhận và xử lý message
    while (1)
    {
        // Đọc message từ client (blocking call)
        int n = recv_message(clients[client_idx].socket, buffer, BUFFER_SIZE);

        if (n <= 0)
        {
            // Client ngắt kết nối hoặc lỗi socket
            printf("Client %d disconnected\n", client_idx);
            break;
        }

        // Log message nhận được
        printf("Client %d: %s", client_idx, buffer);

        // Parse và xử lý message
        process_message(client_idx, buffer);
    }

    // Cleanup khi client disconnect
    logout_client(client_idx);         // Đăng xuất và cập nhật trạng thái
    close(clients[client_idx].socket); // Đóng socket

    // Đánh dấu slot là trống (thread-safe)
    pthread_mutex_lock(&clients_mutex);
    clients[client_idx].is_active = 0;
    pthread_mutex_unlock(&clients_mutex);

    // Hủy mutex
    pthread_mutex_destroy(&clients[client_idx].send_mutex);

    printf("Thread ended for client %d\n", client_idx);
    return NULL; // Kết thúc thread
}