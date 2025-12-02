/**
 * server.h - Header File cho Chess Server
 *
 * File header chính định nghĩa các cấu trúc dữ liệu, constants,
 * và function prototypes cho toàn bộ chess server.
 *
 * Kiến trúc modular:
 * - main.c: Server chính và thread management
 * - client_handler.c: Xử lý giao tiếp với client
 * - auth_manager.c: Xác thực và quản lý user
 * - match_manager.c: Quản lý ván đấu
 * - game_manager.c: Logic game cờ vua
 */

#ifndef SERVER_H
#define SERVER_H

#include <pthread.h>

// ============= CONSTANTS =============

#define MAX_USERNAME 32   // Độ dài tối đa của username
#define MAX_PASSWORD 64   // Độ dài tối đa của password
#define MAX_SESSION_ID 64 // Độ dài session ID
#define MAX_MATCH_ID 32   // Độ dài match ID
#define BUFFER_SIZE 4096  // Kích thước buffer cho message
#define MAX_CLIENTS 100   // Số lượng client tối đa đồng thời
#define MAX_MATCHES 50    // Số lượng ván đấu tối đa đồng thời

// ============= ENUMS & STRUCTURES =============

/**
 * PlayerStatus - Trạng thái của người chơi
 *
 * @STATUS_OFFLINE: Chưa đăng nhập
 * @STATUS_ONLINE: Đã đăng nhập, sẵn sàng chơi
 * @STATUS_IN_MATCH: Đang trong ván đấu
 */
typedef enum
{
    STATUS_OFFLINE,
    STATUS_ONLINE,
    STATUS_IN_MATCH
} PlayerStatus;

/**
 * Client - Thông tin về một client kết nối
 *
 * @socket: Socket descriptor cho kết nối TCP
 * @is_active: 1 nếu slot đang được sử dụng, 0 nếu trống
 * @username: Tên đăng nhập của user
 * @session_id: ID phiên đăng nhập (xác thực)
 * @status: Trạng thái hiện tại (offline/online/in-match)
 * @send_mutex: Mutex để đảm bảo thread-safe khi gửi message
 */
typedef struct
{
    int socket;
    int is_active;
    char username[MAX_USERNAME];
    char session_id[MAX_SESSION_ID];
    PlayerStatus status;
    pthread_mutex_t send_mutex;
} Client;

/**
 * ClientThreadArgs - Tham số truyền vào thread xử lý client
 *
 * @client_index: Index của client trong mảng clients[]
 */
typedef struct
{
    int client_index;
} ClientThreadArgs;

/**
 * User - Thông tin tài khoản người dùng
 *
 * @username: Tên đăng nhập duy nhất
 * @password_hash: Mật khẩu đã hash bằng SHA-256 (hex string)
 * @is_online: 1 nếu đang online, 0 nếu offline
 */
typedef struct
{
    char username[MAX_USERNAME];
    char password_hash[65]; // SHA-256 hex string (64 chars + \0)
    int is_online;
} User;

/**
 * Match - Thông tin về một ván đấu cờ vua
 *
 * @match_id: ID duy nhất của ván đấu
 * @white_player: Username của người chơi quân trắng
 * @black_player: Username của người chơi quân đen
 * @white_client_idx: Index của white player trong mảng clients[]
 * @black_client_idx: Index của black player trong mảng clients[]
 * @is_active: 1 nếu ván đấu đang diễn ra, 0 nếu kết thúc
 * @board: Mảng 8x8 biểu diễn bàn cờ (lowercase=trắng, uppercase=đen, '.'=trống)
 * @current_turn: 0 = lượt trắng, 1 = lượt đen
 */
typedef struct
{
    char match_id[MAX_MATCH_ID];
    char white_player[MAX_USERNAME];
    char black_player[MAX_USERNAME];
    int white_client_idx;
    int black_client_idx;
    int is_active;
    char board[8][8];
    int current_turn;
} Match;

// ============= GLOBAL VARIABLES =============

/**
 * Biến toàn cục - được chia sẻ giữa các module
 *
 * @clients: Mảng lưu thông tin tất cả các client đang kết nối
 * @clients_mutex: Mutex bảo vệ truy cập mảng clients[] (thread-safe)
 * @match_mutex: Mutex bảo vệ truy cập mảng matches[] (thread-safe)
 * @matches: Mảng lưu thông tin tất cả các ván đấu
 */
extern Client clients[MAX_CLIENTS];
extern pthread_mutex_t clients_mutex;
extern pthread_mutex_t match_mutex;
extern Match matches[MAX_MATCHES];

// ============= MODULE INITIALIZATION =============

/**
 * Các hàm khởi tạo cho từng module
 * Gọi từ main() khi server khởi động
 */
void auth_manager_init();  // Khởi tạo module xác thực (load users)
void match_manager_init(); // Khởi tạo module quản lý ván đấu
void game_manager_init();  // Khởi tạo module logic game

// ============= CLIENT HANDLER FUNCTIONS =============

/**
 * client_handler - Thread function xử lý từng client
 * @arg: Pointer tới ClientThreadArgs
 * Return: NULL khi thread kết thúc
 */
void *client_handler(void *arg);

/**
 * send_json - Gửi JSON message tới client (thread-safe)
 * @client_idx: Index của client
 * @json: cJSON object cần gửi
 * Return: Số byte đã gửi, -1 nếu lỗi
 */
int send_json(int client_idx, cJSON *json);

/**
 * recv_message - Nhận message từ socket (đọc đến \n)
 * @socket: Socket descriptor
 * @buffer: Buffer để lưu message
 * @buffer_size: Kích thước buffer
 * Return: Số byte đã đọc, -1 nếu lỗi
 */
int recv_message(int socket, char *buffer, int buffer_size);

// ============= AUTHENTICATION FUNCTIONS =============

/**
 * handle_register - Xử lý đăng ký tài khoản mới
 * @client_idx: Index của client
 * @data: JSON object chứa username và password
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_register(int client_idx, cJSON *data);

/**
 * handle_login - Xử lý đăng nhập
 * @client_idx: Index của client
 * @data: JSON object chứa username và password
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_login(int client_idx, cJSON *data);

/**
 * logout_client - Đăng xuất client
 * @client_idx: Index của client cần logout
 */
void logout_client(int client_idx);

// ============= PLAYER LIST FUNCTIONS =============

/**
 * handle_request_player_list - Gửi danh sách người chơi online
 * @client_idx: Index của client yêu cầu
 * Return: 0 nếu thành công
 */
int handle_request_player_list(int client_idx);

// ============= MATCH MANAGEMENT FUNCTIONS =============

/**
 * handle_challenge - Xử lý lời thách đấu
 * @client_idx: Index của người gửi thách đấu
 * @data: JSON object chứa "from" và "to"
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_challenge(int client_idx, cJSON *data);

/**
 * handle_accept - Xử lý chấp nhận thách đấu
 * @client_idx: Index của người chấp nhận
 * @data: JSON object chứa "from" và "to"
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_accept(int client_idx, cJSON *data);

/**
 * handle_decline - Xử lý từ chối thách đấu
 * @client_idx: Index của người từ chối
 * @data: JSON object chứa "from" và "to"
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_decline(int client_idx, cJSON *data);

// ============= GAME LOGIC FUNCTIONS =============

/**
 * handle_move - Xử lý nước đi cờ
 * @client_idx: Index của người đi
 * @data: JSON object chứa matchId, from, to
 * Return: 0 nếu hợp lệ, -1 nếu không hợp lệ
 */
int handle_move(int client_idx, cJSON *data);

/**
 * send_game_result - Gửi kết quả ván đấu cho cả 2 người chơi
 * @match_idx: Index của ván đấu
 * @winner: Tên người thắng hoặc "DRAW"
 * @reason: Lý do (Checkmate, Stalemate, etc.)
 */
void send_game_result(int match_idx, const char *winner, const char *reason);

// ============= UTILITY FUNCTIONS =============

/**
 * generate_session_id - Tạo session ID ngẫu nhiên
 * @output: Buffer để lưu session ID
 * @length: Độ dài session ID
 */
void generate_session_id(char *output, int length);

/**
 * generate_match_id - Tạo match ID ngẫu nhiên
 * @output: Buffer để lưu match ID
 * @length: Độ dài match ID
 */
void generate_match_id(char *output, int length);

/**
 * sha256_string - Hash chuỗi bằng SHA-256
 * @input: Chuỗi đầu vào (password)
 * @output: Buffer lưu hash hex string (65 bytes)
 */
void sha256_string(const char *input, char *output);

/**
 * find_client_by_username - Tìm client theo username
 * @username: Tên cần tìm
 * Return: Index của client, -1 nếu không tìm thấy
 */
int find_client_by_username(const char *username);

#endif // SERVER_H