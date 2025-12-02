/**
 * main.c - Chess Server Main Program
 *
 * Server chính cho game cờ vua trực tuyến sử dụng TCP socket và multi-threading.
 * Hỗ trợ nhiều client đồng thời, xác thực người dùng, và quản lý các ván đấu.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include "cJSON.h"
#include "server.h"

#define PORT 8888       // Cổng server lắng nghe
#define MAX_CLIENTS 100 // Số lượng client tối đa

// Biến toàn cục
int server_socket;                                         // Socket chính của server
Client clients[MAX_CLIENTS];                               // Mảng lưu thông tin các client
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex bảo vệ truy cập mảng clients

/**
 * signal_handler - Xử lý tín hiệu Ctrl+C (SIGINT)
 * @sig: Mã tín hiệu
 *
 * Đóng server một cách an toàn khi nhận tín hiệu dừng.
 */
void signal_handler(int sig)
{
    printf("\nShutting down server...\n");
    close(server_socket);
    exit(0);
}

/**
 * main - Hàm chính khởi động server
 *
 * Luồng hoạt động:
 * 1. Khởi tạo các module (auth, match, game)
 * 2. Tạo socket và bind đến cổng
 * 3. Lắng nghe kết nối từ client
 * 4. Tạo thread riêng cho mỗi client kết nối
 *
 * Return: 0 nếu thành công
 */
int main()
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_socket;
    pthread_t thread_id;

    // Đăng ký handler cho tín hiệu Ctrl+C
    signal(SIGINT, signal_handler);

    // Khởi tạo các module quản lý
    auth_manager_init();  // Module xác thực người dùng
    match_manager_init(); // Module quản lý ván đấu
    game_manager_init();  // Module logic game cờ vua

    // Khởi tạo mảng clients - đánh dấu tất cả slot là trống
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].socket = -1;   // Socket không hợp lệ
        clients[i].is_active = 0; // Slot trống
    }

    // Tạo socket TCP
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Cho phép tái sử dụng địa chỉ ngay sau khi đóng server
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Cấu hình địa chỉ server
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // Lắng nghe trên tất cả các interface
    server_addr.sin_port = htons(PORT);       // Chuyển port sang network byte order

    // Gán địa chỉ cho socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Chuyển socket sang chế độ lắng nghe, queue tối đa 10 kết nối chờ
    if (listen(server_socket, 10) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Chess Server started on port %d\n", PORT);
    printf("Waiting for connections...\n");

    // Vòng lặp chính - chấp nhận kết nối từ client
    while (1)
    {
        // Chấp nhận kết nối mới (blocking call)
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue; // Thử lại nếu accept thất bại
        }

        // In thông tin client vừa kết nối
        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        // Tìm slot trống trong mảng clients
        int slot = -1;
        pthread_mutex_lock(&clients_mutex); // Khóa để tránh race condition
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (!clients[i].is_active)
            {
                slot = i;
                clients[i].socket = client_socket;
                clients[i].is_active = 1;
                clients[i].username[0] = '\0'; // Chưa đăng nhập
                clients[i].session_id[0] = '\0';
                clients[i].status = STATUS_OFFLINE;
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        // Nếu không còn slot trống, từ chối kết nối
        if (slot == -1)
        {
            printf("Max clients reached. Rejecting connection.\n");
            close(client_socket);
            continue;
        }

        // Chuẩn bị tham số cho thread
        ClientThreadArgs *args = malloc(sizeof(ClientThreadArgs));
        args->client_index = slot;

        // Tạo thread mới để xử lý client
        if (pthread_create(&thread_id, NULL, client_handler, args) != 0)
        {
            perror("Thread creation failed");
            clients[slot].is_active = 0; // Giải phóng slot
            close(client_socket);
            free(args);
        }
        else
        {
            // Detach thread để tự động giải phóng tài nguyên khi kết thúc
            pthread_detach(thread_id);
        }
    }

    // Đóng socket (không bao giờ đến đây do vòng lặp vô hạn)
    close(server_socket);
    return 0;
}
