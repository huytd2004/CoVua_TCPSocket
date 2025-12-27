# ♟️ Chess Server - Multiplayer TCP Chess Game

Server cờ vua TCP đa người chơi được viết bằng C, hỗ trợ kết nối từ nhiều client đồng thời với đầy đủ luật cờ vua.

## 📁 Cấu trúc thư mục

```
Bai_tap_nhom/
├── main.c                    # Entry point, khởi tạo server và thread management
├── server.h                  # Header chính, định nghĩa structs và prototypes
├── client_handler.c          # Xử lý kết nối và routing message từ client
├── auth_manager.c            # Đăng ký, đăng nhập, quản lý user
├── match_manager.c           # Tạo và quản lý ván đấu, xử lý thách đấu
├── game_manager.c            # Logic cờ vua đầy đủ (di chuyển, kiểm tra luật)
├── game_manager_handlers.c   # Xử lý nước đi và kết quả game
├── elo_manager.c             # Hệ thống tính điểm ELO
├── matchmaking.c             # Ghép cặp tự động theo ELO
├── game_control.c            # Xin ngừng/Mời hòa/Đấu lại
├── match_history.c           # Lưu và xem lại lịch sử ván đấu
├── cJSON.c                   # Thư viện parse/create JSON
├── cJSON.h                   # Header cho cJSON
├── Makefile                  # Build configuration
├── users.json                # Database lưu thông tin user
├── matches/                  # Thư mục lưu lịch sử các ván đấu (auto-created)
├── protocol.md               # Tài liệu giao thức truyền thông
├── README.md                 # File này
└── CHESS_RULES_IMPLEMENTATION.md  # Tài liệu luật cờ vua đã implement
```

## 🛠️ Makefile

```makefile
CC = gcc
CFLAGS = -Wall -pthread -O2
LDFLAGS = -lssl -lcrypto -lm

TARGET = chess_server
SOURCES = main.c client_handler.c auth_manager.c match_manager.c \
          game_manager.c game_manager_handlers.c elo_manager.c \
          matchmaking.c game_control.c match_history.c cJSON.c
```

### Các lệnh make:

| Lệnh         | Mô tả                              |
|--------------|-----------------------------------|
| `make`       | Build server                       |
| `make clean` | Xóa file build                     |
| `make run`   | Build và chạy server               |

## 📦 Cài đặt Dependencies

### Ubuntu/Debian

```bash
sudo apt update
sudo apt install build-essential libssl-dev
```

### Fedora/RHEL

```bash
sudo dnf install gcc openssl-devel
```

### Arch Linux

```bash
sudo pacman -S gcc openssl
```

## 🔨 Build

```bash
cd Bai_tap_nhom
make clean
make
```

## 🚀 Chạy Server

```bash
./chess_server
```

Server sẽ lắng nghe trên **port 8080** (mặc định).

Output khi khởi động:
```
Auth Manager initialized, loaded X users
Match Manager initialized
Game Manager initialized with full chess rules
Game Control module initialized
Match History module initialized
Matchmaking system started (interval: 2s, ELO threshold: 100)
Server listening on port 8080...
```

## 🧪 Test với Netcat

Mở terminal và kết nối tới server:

```bash
nc localhost 8080
```

### Test các chức năng:

#### 1. Đăng ký tài khoản
```json
{"action":"REGISTER","data":{"username":"alice","password":"123456"}}
```

#### 2. Đăng nhập
```json
{"action":"LOGIN","data":{"username":"alice","password":"123456"}}
```

#### 3. Xem danh sách người chơi online
```json
{"action":"REQUEST_PLAYER_LIST","data":{}}
```

#### 4. Xem hồ sơ người chơi
```json
{"action":"GET_PROFILE","data":{"username":"alice"}}
```

#### 5. Tìm trận tự động (matchmaking)
```json
{"action":"FIND_MATCH","data":{}}
```

#### 6. Hủy tìm trận
```json
{"action":"CANCEL_FIND_MATCH","data":{}}
```

#### 7. Thách đấu người chơi khác
```json
{"action":"CHALLENGE","data":{"from":"alice","to":"bob"}}
```

#### 8. Chấp nhận thách đấu (từ client bob)
```json
{"action":"ACCEPT","data":{"from":"bob","to":"alice"}}
```

#### 9. Đi nước cờ
```json
{"action":"MOVE","data":{"matchId":"M12345ABC","from":"E2","to":"E4"}}
```

#### 10. Xin ngừng ván
```json
{"action":"OFFER_ABORT","data":{"matchId":"M12345ABC"}}
```

#### 11. Mời hòa
```json
{"action":"OFFER_DRAW","data":{"matchId":"M12345ABC"}}
```

#### 12. Xem lịch sử ván đấu
```json
{"action":"GET_MATCH_HISTORY","data":{}}
```

#### 13. Xem replay ván đấu
```json
{"action":"GET_MATCH_REPLAY","data":{"matchId":"M12345ABC"}}
```

#### 14. Ping/Pong
```json
{"action":"PING","data":{}}
```

### Test với 2 client:

**Terminal 1 (Alice):**
```bash
nc localhost 8080
{"action":"LOGIN","data":{"username":"alice","password":"123456"}}
{"action":"CHALLENGE","data":{"from":"alice","to":"bob"}}
```

**Terminal 2 (Bob):**
```bash
nc localhost 8080
{"action":"LOGIN","data":{"username":"bob","password":"123456"}}
{"action":"ACCEPT","data":{"from":"bob","to":"alice"}}
```

Sau khi game bắt đầu, cả 2 client sẽ nhận được `START_GAME`.

## ✅ Tính năng đã implement

### 🔐 Authentication
- [x] Đăng ký tài khoản (hash SHA-256)
- [x] Đăng nhập với session ID
- [x] Đăng xuất tự động khi disconnect
- [x] Lưu trữ user vào file JSON

### 👥 Player Management
- [x] Xem danh sách người chơi online
- [x] Xem hồ sơ người chơi (ELO, thống kê)
- [x] Trạng thái người chơi (ONLINE, IN_MATCH, OFFLINE)

### 🎮 Matchmaking
- [x] Thách đấu trực tiếp người chơi khác
- [x] Chấp nhận/Từ chối thách đấu
- [x] Ghép cặp tự động theo điểm ELO
- [x] Hủy tìm trận

### ♟️ Chess Logic (Đầy đủ luật cờ vua)
- [x] Di chuyển tất cả các quân (Pawn, Knight, Bishop, Rook, Queen, King)
- [x] En passant (ăn tốt qua đường)
- [x] Castling (nhập thành - cả 2 bên)
- [x] Pawn promotion (phong cấp tốt)
- [x] Kiểm tra nước đi không để vua bị chiếu
- [x] Phát hiện chiếu (check)
- [x] Phát hiện chiếu hết (checkmate)
- [x] Phát hiện bế tắc (stalemate)
- [x] Hòa do thiếu quân (insufficient material)

### 📊 ELO Rating System
- [x] Tính điểm ELO theo công thức chuẩn (K=32)
- [x] Cập nhật ELO sau mỗi ván đấu
- [x] Lưu thống kê thắng/thua/hòa
- [x] ELO mặc định: 1200

### 🎛️ Game Control
- [x] Xin ngừng ván (Abort) - cả 2 đồng ý
- [x] Mời hòa (Draw offer)
- [x] Đấu lại (Rematch) với đổi màu quân

### 📜 Match History
- [x] Ghi nhận tất cả nước đi trong ván
- [x] Lưu lịch sử ván đấu vào file JSON
- [x] Xem danh sách các ván đã chơi
- [x] Xem lại chi tiết ván đấu (replay)

### 🔧 Kỹ thuật
- [x] Multi-threading với pthread
- [x] Thread-safe với mutex
- [x] JSON-based protocol
- [x] TCP persistent connection
- [x] Graceful shutdown (Ctrl+C)

## 📝 Protocol

Xem chi tiết tại [protocol.md](protocol.md)

## 👨‍💻 Tác giả

- **Trần Đoàn Huy** - 20225859
- Môn: Thực hành Lập trình mạng

## 📄 License

MIT License
