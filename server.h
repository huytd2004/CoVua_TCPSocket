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
 * @elo_rating: Điểm ELO của người chơi (mặc định 1200)
 * @wins: Số trận thắng
 * @losses: Số trận thua
 * @draws: Số trận hòa
 */
typedef struct
{
    char username[MAX_USERNAME];
    char password_hash[65]; // SHA-256 hex string (64 chars + \0)
    int is_online;
    int elo_rating; // Điểm ELO (mặc định 1200)
    int wins;       // Số trận thắng
    int losses;     // Số trận thua
    int draws;      // Số trận hòa
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
    char match_id[32];
    char white_player[32];
    char black_player[32];
    int white_client_idx;
    int black_client_idx;
    int is_active;
    char board[8][8];
    int current_turn;

    // === THÊM CÁC TRƯỜNG NÀY ===
    int white_king_moved;
    int black_king_moved;
    int white_rook_a_moved;
    int white_rook_h_moved;
    int black_rook_a_moved;
    int black_rook_h_moved;
    int en_passant_col;
    int last_move_from_row;
    int last_move_from_col;
    int last_move_to_row;
    int last_move_to_col;
    int halfmove_clock;
    int fullmove_number;
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

/**
 * handle_get_profile - Xử lý yêu cầu xem hồ sơ người chơi
 * @client_idx: Index của client yêu cầu
 * @data: JSON object chứa username cần xem
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_get_profile(int client_idx, cJSON *data);

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

// ============= ELO MANAGER FUNCTIONS =============

/**
 * elo_manager_init - Khởi tạo ELO manager
 */
void elo_manager_init();

/**
 * calculate_elo_change - Tính điểm ELO thay đổi
 * @winner_elo: Điểm ELO của người thắng
 * @loser_elo: Điểm ELO của người thua
 * @is_draw: 1 nếu hòa, 0 nếu có người thắng/thua
 * Return: Điểm ELO thay đổi
 */
int calculate_elo_change(int winner_elo, int loser_elo, int is_draw);

/**
 * update_elo_ratings - Cập nhật điểm ELO sau ván đấu
 * @white_player: Username của người chơi quân trắng
 * @black_player: Username của người chơi quân đen
 * @winner: Username của người thắng, "DRAW" nếu hòa
 */
void update_elo_ratings(const char *white_player, const char *black_player, const char *winner);

/**
 * get_user_elo - Lấy điểm ELO của user
 * @username: Tên user
 * Return: Điểm ELO
 */
int get_user_elo(const char *username);

/**
 * get_user_stats - Lấy thống kê của user
 * @username: Tên user
 * @elo, @wins, @losses, @draws: Pointers để lưu kết quả
 * Return: 0 nếu thành công, -1 nếu không tìm thấy
 */
int get_user_stats(const char *username, int *elo, int *wins, int *losses, int *draws);

// ============= MATCHMAKING FUNCTIONS =============

/**
 * matchmaking_start - Khởi động matchmaking background thread
 */
void matchmaking_start();

/**
 * matchmaking_stop - Dừng matchmaking thread
 */
void matchmaking_stop();

/**
 * add_to_matchmaking_queue - Thêm client vào hàng đợi matchmaking
 * @client_idx: Index của client
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int add_to_matchmaking_queue(int client_idx);

/**
 * remove_from_matchmaking_queue - Xóa client khỏi hàng đợi
 * @client_idx: Index của client
 * Return: 0 nếu thành công, -1 nếu không tìm thấy
 */
int remove_from_matchmaking_queue(int client_idx);

/**
 * handle_find_match - Xử lý yêu cầu tìm trận từ client
 * @client_idx: Index của client
 * @data: JSON data
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_find_match(int client_idx, cJSON *data);

/**
 * handle_cancel_find_match - Xử lý yêu cầu hủy tìm trận
 * @client_idx: Index của client
 * @data: JSON data
 * Return: 0 nếu thành công, -1 nếu thất bại
 */
int handle_cancel_find_match(int client_idx, cJSON *data);

// ============= GAME CONTROL FUNCTIONS =============

/**
 * game_control_init - Khởi tạo game control module
 */
void game_control_init();

/**
 * create_match_with_colors - Tạo ván đấu với màu quân xác định
 * @white_idx: Index của người chơi quân trắng
 * @black_idx: Index của người chơi quân đen
 * Return: Index của ván đấu, -1 nếu thất bại
 */
int create_match_with_colors(int white_idx, int black_idx);

/**
 * save_recent_match - Lưu thông tin ván đấu vừa kết thúc
 */
void save_recent_match(const char *match_id, const char *white, const char *black,
                       int white_idx, int black_idx);

// Abort handlers
int handle_offer_abort(int client_idx, cJSON *data);
int handle_accept_abort(int client_idx, cJSON *data);
int handle_decline_abort(int client_idx, cJSON *data);

// Draw handlers
int handle_offer_draw(int client_idx, cJSON *data);
int handle_accept_draw(int client_idx, cJSON *data);
int handle_decline_draw(int client_idx, cJSON *data);

// Rematch handlers
int handle_offer_rematch(int client_idx, cJSON *data);
int handle_accept_rematch(int client_idx, cJSON *data);
int handle_decline_rematch(int client_idx, cJSON *data);

// ============= MATCH HISTORY FUNCTIONS =============

/**
 * match_history_init - Khởi tạo module lịch sử ván đấu
 */
void match_history_init();

/**
 * start_recording_match - Bắt đầu ghi nhận nước đi cho ván mới
 * @match_id: ID của ván đấu
 */
void start_recording_match(const char *match_id);

/**
 * record_move - Ghi nhận một nước đi
 * @match_id: ID ván đấu
 * @from: Vị trí xuất phát
 * @to: Vị trí đích
 */
void record_move(const char *match_id, const char *from, const char *to);

/**
 * save_match_history - Lưu lịch sử ván đấu vào file
 * @match_id: ID ván đấu
 * @white: Username quân trắng
 * @black: Username quân đen
 * @winner: Người thắng hoặc "DRAW"/"ABORT"
 * @reason: Lý do kết thúc
 * @final_board: Bàn cờ cuối cùng
 */
void save_match_history(const char *match_id, const char *white, const char *black,
                        const char *winner, const char *reason, char final_board[8][8]);

/**
 * stop_recording_match - Dừng ghi nhận nước đi (không lưu)
 * @match_id: ID ván đấu
 */
void stop_recording_match(const char *match_id);

/**
 * handle_get_match_history - Xử lý yêu cầu lấy danh sách ván đã chơi
 * @client_idx: Index của client
 * @data: JSON data
 * Return: 0 nếu thành công
 */
int handle_get_match_history(int client_idx, cJSON *data);

/**
 * handle_get_match_replay - Xử lý yêu cầu xem lại ván đấu
 * @client_idx: Index của client
 * @data: JSON data chứa matchId
 * Return: 0 nếu thành công
 */
int handle_get_match_replay(int client_idx, cJSON *data);

#endif // SERVER_H