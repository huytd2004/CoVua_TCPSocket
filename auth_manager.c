/**
 * auth_manager.c - Authentication Manager Module
 *
 * Module quản lý xác thực người dùng, bao gồm đăng ký, đăng nhập,
 * quản lý session và danh sách người chơi online.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <openssl/sha.h>
#include "cJSON.h"
#include "server.h"

#define MAX_USERS 1000          // Số lượng user tối đa
#define USERS_FILE "users.json" // File lưu trữ thông tin users

// Biến static - chỉ truy cập trong module này
static User users[MAX_USERS];                                  // Mảng lưu thông tin users
static int user_count = 0;                                     // Số lượng users hiện tại
static pthread_mutex_t auth_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex bảo vệ truy cập users

/**
 * sha256_string - Hash chuỗi bằng thuật toán SHA-256
 * @input: Chuỗi đầu vào (password)
 * @output: Buffer để lưu hash dạng hex string (65 bytes)
 *
 * Mã hóa password trước khi lưu vào database để bảo mật.
 */
void sha256_string(const char *input, char *output)
{
    unsigned char hash[SHA256_DIGEST_LENGTH]; // Buffer lưu hash binary (32 bytes)
    SHA256((unsigned char *)input, strlen(input), hash);

    // Chuyển đổi hash binary sang hex string
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(output + (i * 2), "%02x", hash[i]); // Mỗi byte -> 2 ký tự hex
    }
    output[64] = '\0'; // Null terminator (32*2 = 64 ký tự)
}

/**
 * generate_session_id - Tạo session ID ngẫu nhiên
 * @output: Buffer để lưu session ID
 * @length: Độ dài session ID cần tạo
 *
 * Tạo chuỗi hex ngẫu nhiên để xác thực session của user.
 */
void generate_session_id(char *output, int length)
{
    const char charset[] = "0123456789abcdef"; // Ký tự hex
    for (int i = 0; i < length - 1; i++)
    {
        output[i] = charset[rand() % 16]; // Random 1 ký tự hex
    }
    output[length - 1] = '\0'; // Kết thúc chuỗi
}

/**
 * auth_manager_init - Khởi tạo authentication manager
 *
 * Load danh sách users từ file JSON vào memory.
 * Gọi khi server khởi động.
 */
void auth_manager_init()
{
    srand(time(NULL)); // Khởi tạo random seed

    // Load users từ file JSON
    FILE *f = fopen(USERS_FILE, "r");
    if (f)
    {
        // Đọc toàn bộ file vào memory
        fseek(f, 0, SEEK_END); // Di chuyển đến cuối file
        long fsize = ftell(f); // Lấy kích thước file
        fseek(f, 0, SEEK_SET); // Quay về đầu file

        char *json_str = malloc(fsize + 1);
        fread(json_str, 1, fsize, f); // Đọc toàn bộ nội dung
        fclose(f);
        json_str[fsize] = '\0'; // Thêm null terminator

        // Parse JSON string
        cJSON *root = cJSON_Parse(json_str);
        free(json_str);

        if (root)
        {
            // Lấy mảng "users" từ JSON
            cJSON *users_array = cJSON_GetObjectItem(root, "users");
            if (users_array)
            {
                user_count = 0;
                cJSON *user_obj = NULL;
                // Duyệt qua từng user object trong array
                cJSON_ArrayForEach(user_obj, users_array)
                {
                    if (user_count >= MAX_USERS)
                        break; // Đã đầy, dừng load

                    // Lấy username và password_hash từ JSON object
                    cJSON *username = cJSON_GetObjectItem(user_obj, "username");
                    cJSON *password_hash = cJSON_GetObjectItem(user_obj, "password_hash");

                    if (username && password_hash)
                    {
                        // Copy vào mảng users
                        strncpy(users[user_count].username, username->valuestring, MAX_USERNAME - 1);
                        strncpy(users[user_count].password_hash, password_hash->valuestring, 64);
                        users[user_count].is_online = 0; // Ban đầu offline
                        user_count++;
                    }
                }
            }
            cJSON_Delete(root);
            printf("Loaded %d users from database\n", user_count);
        }
    }
    else
    {
        printf("No existing user database found\n");
    }
}

/**
 * save_users - Lưu danh sách users vào file JSON
 *
 * Gọi sau khi đăng ký user mới hoặc thay đổi thông tin.
 */
void save_users()
{
    // Tạo JSON object gốc
    cJSON *root = cJSON_CreateObject();
    cJSON *users_array = cJSON_CreateArray();

    // Chuyển đổi mảng users sang JSON array
    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(user_obj, "username", users[i].username);
        cJSON_AddStringToObject(user_obj, "password_hash", users[i].password_hash);
        cJSON_AddItemToArray(users_array, user_obj);
    }

    cJSON_AddItemToObject(root, "users", users_array);

    // Ghi JSON vào file
    FILE *f = fopen(USERS_FILE, "w");
    if (f)
    {
        char *json_str = cJSON_Print(root); // Format JSON đẹp
        fprintf(f, "%s", json_str);         // Ghi vào file
        fclose(f);
        free(json_str);
    }

    cJSON_Delete(root); // Giải phóng bộ nhớ JSON
}

/**
 * find_user - Tìm user theo username
 * @username: Tên user cần tìm
 *
 * Return: Index của user trong mảng, -1 nếu không tìm thấy
 */
int find_user(const char *username)
{
    for (int i = 0; i < user_count; i++)
    {
        if (strcmp(users[i].username, username) == 0)
        {
            return i;
        }
    }
    return -1;
}

/**
 * handle_register - Xử lý yêu cầu đăng ký tài khoản mới
 * @client_idx: Index của client trong mảng clients
 * @data: JSON object chứa username và password
 *
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_register(int client_idx, cJSON *data)
{
    // Kiểm tra data hợp lệ
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    // Lấy username và password từ JSON
    cJSON *username_obj = cJSON_GetObjectItem(data, "username");
    cJSON *password_obj = cJSON_GetObjectItem(data, "password");

    if (!username_obj || !password_obj)
    {
        send_error(client_idx, "Missing username or password");
        return -1;
    }

    const char *username = username_obj->valuestring;
    const char *password = password_obj->valuestring;

    pthread_mutex_lock(&auth_mutex); // Khóa để tránh race condition

    // Kiểm tra username đã tồn tại chưa
    if (find_user(username) != -1)
    {
        pthread_mutex_unlock(&auth_mutex);

        // Gửi thông báo lỗi về client
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "REGISTER_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Username already exists");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Tạo user mới
    if (user_count >= MAX_USERS)
    {
        pthread_mutex_unlock(&auth_mutex);
        send_error(client_idx, "Server full");
        return -1;
    }

    // Lưu thông tin user vào mảng
    strncpy(users[user_count].username, username, MAX_USERNAME - 1);
    sha256_string(password, users[user_count].password_hash); // Hash password
    users[user_count].is_online = 0;
    user_count++;

    save_users(); // Lưu vào file
    pthread_mutex_unlock(&auth_mutex);

    // Send success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "REGISTER_SUCCESS");
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "message", "Account created");
    cJSON_AddItemToObject(response, "data", resp_data);
    send_json(client_idx, response);
    cJSON_Delete(response);

    printf("User registered: %s\n", username);
    return 0;
}

/**
 * handle_login - Xử lý yêu cầu đăng nhập
 * @client_idx: Index của client trong mảng clients
 * @data: JSON object chứa username và password
 *
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_login(int client_idx, cJSON *data)
{
    if (!data)
    {
        send_error(client_idx, "Missing data");
        return -1;
    }

    cJSON *username_obj = cJSON_GetObjectItem(data, "username");
    cJSON *password_obj = cJSON_GetObjectItem(data, "password");

    if (!username_obj || !password_obj)
    {
        send_error(client_idx, "Missing username or password");
        return -1;
    }

    const char *username = username_obj->valuestring;
    const char *password = password_obj->valuestring;

    // Hash password để so sánh với database
    char password_hash[65];
    sha256_string(password, password_hash);

    pthread_mutex_lock(&auth_mutex);

    // Tìm user trong database
    int user_idx = find_user(username);
    if (user_idx == -1)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "User not found");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    if (strcmp(users[user_idx].password_hash, password_hash) != 0)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Invalid password");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Kiểm tra user đã đăng nhập ở nơi khác chưa
    if (users[user_idx].is_online)
    {
        pthread_mutex_unlock(&auth_mutex);

        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "action", "LOGIN_FAIL");
        cJSON *resp_data = cJSON_CreateObject();
        cJSON_AddStringToObject(resp_data, "reason", "Already logged in");
        cJSON_AddItemToObject(response, "data", resp_data);
        send_json(client_idx, response);
        cJSON_Delete(response);
        return -1;
    }

    // Đăng nhập thành công - đánh dấu online
    users[user_idx].is_online = 1;
    pthread_mutex_unlock(&auth_mutex);

    // Tạo session ID mới cho phiên đăng nhập
    char session_id[MAX_SESSION_ID];
    generate_session_id(session_id, 16);

    // Cập nhật thông tin client
    pthread_mutex_lock(&clients_mutex);
    strncpy(clients[client_idx].username, username, MAX_USERNAME - 1);
    strncpy(clients[client_idx].session_id, session_id, MAX_SESSION_ID - 1);
    clients[client_idx].status = STATUS_ONLINE;
    pthread_mutex_unlock(&clients_mutex);

    // Send success
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "LOGIN_SUCCESS");
    cJSON *resp_data = cJSON_CreateObject();
    cJSON_AddStringToObject(resp_data, "sessionId", session_id);
    cJSON_AddStringToObject(resp_data, "username", username);
    cJSON_AddItemToObject(response, "data", resp_data);
    send_json(client_idx, response);
    cJSON_Delete(response);

    printf("User logged in: %s\n", username);
    return 0;
}

/**
 * logout_client - Đăng xuất client
 * @client_idx: Index của client cần logout
 *
 * Gọi khi client ngắt kết nối hoặc yêu cầu logout.
 */
void logout_client(int client_idx)
{
    pthread_mutex_lock(&clients_mutex);
    if (clients[client_idx].username[0] != '\0') // Đã đăng nhập
    {
        pthread_mutex_lock(&auth_mutex);
        int user_idx = find_user(clients[client_idx].username);
        if (user_idx != -1)
        {
            users[user_idx].is_online = 0; // Đánh dấu offline
        }
        pthread_mutex_unlock(&auth_mutex);

        printf("User logged out: %s\n", clients[client_idx].username);
    }
    pthread_mutex_unlock(&clients_mutex);
}

/**
 * find_client_by_username - Tìm client đang online theo username
 * @username: Tên user cần tìm
 *
 * Return: Index của client, -1 nếu không tìm thấy hoặc offline
 */
int find_client_by_username(const char *username)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].is_active && strcmp(clients[i].username, username) == 0)
        {
            return i;
        }
    }
    return -1; // Không tìm thấy
}

/**
 * handle_request_player_list - Gửi danh sách người chơi online
 * @client_idx: Index của client yêu cầu
 *
 * Return: 0 nếu thành công
 */
int handle_request_player_list(int client_idx)
{
    // Tạo JSON response
    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "action", "PLAYER_LIST");
    cJSON *data = cJSON_CreateObject();
    cJSON *players = cJSON_CreateArray();

    // Duyệt qua tất cả clients để lấy danh sách online
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        // Bỏ qua client đang yêu cầu và các client chưa login
        if (clients[i].is_active && clients[i].username[0] != '\0' && i != client_idx)
        {
            cJSON *player = cJSON_CreateObject();
            cJSON_AddStringToObject(player, "username", clients[i].username);

            // Chuyển đổi status enum sang string
            const char *status_str;
            switch (clients[i].status)
            {
            case STATUS_ONLINE:
                status_str = "ONLINE"; // Rảnh, có thể thách đấu
                break;
            case STATUS_IN_MATCH:
                status_str = "IN_MATCH"; // Đang trong ván đấu
                break;
            default:
                status_str = "OFFLINE";
                break;
            }
            cJSON_AddStringToObject(player, "status", status_str);
            cJSON_AddItemToArray(players, player); // Thêm vào array
        }
    }
    pthread_mutex_unlock(&clients_mutex);

    cJSON_AddItemToObject(data, "players", players);
    cJSON_AddItemToObject(response, "data", data);

    send_json(client_idx, response);
    cJSON_Delete(response);

    return 0;
}