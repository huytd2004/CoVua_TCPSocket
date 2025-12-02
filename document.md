# Chess Server - Hướng dẫn Build và Chạy

## Cấu trúc thư mục

```
chess_server/
├── main.c //main.c + server.h: Socket server, accept connections, tạo thread cho mỗi client
├── server.h
├── client_handler.c //Nhận JSON, parse, route đến handler phù hợp
├── auth_manager.c //Đăng ký, đăng nhập, SHA-256 hash, session ID
├── match_manager.c //Thách đấu, ghép cặp, tạo match, START_GAME
├── game_manager.c //Xử lý nước đi, validate, gửi update, GAME_RESULT
├── cJSON.c
├── cJSON.h
└── Makefile
```

## Makefile

```makefile
CC = gcc
CFLAGS = -Wall -pthread -O2
LDFLAGS = -lssl -lcrypto

TARGET = chess_server
SOURCES = main.c client_handler.c auth_manager.c match_manager.c game_manager.c cJSON.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c server.h cJSON.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGET) $(OBJECTS) users.dat

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
```

## Cài đặt dependencies

### Ubuntu/Debian:
```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev
```

### macOS:
```bash
brew install openssl
```

## Tải cJSON

```bash
# Download cJSON
wget https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
wget https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
```

## Build và chạy

```bash
# Compile
make

# Chạy server
./chess_server

# Hoặc
make run
```

## Test với netcat

### Terminal 1 - Client A:
```bash
nc localhost 8888
{"action":"REGISTER","data":{"username":"Alice","password":"123456"}}
{"action":"LOGIN","data":{"username":"Alice","password":"123456"}}
{"action":"REQUEST_PLAYER_LIST","data":{}}
{"action":"CHALLENGE","data":{"from":"Alice","to":"Bob"}}
```

### Terminal 2 - Client B:
```bash
nc localhost 8888
{"action":"REGISTER","data":{"username":"Bob","password":"123456"}}
{"action":"LOGIN","data":{"username":"Bob","password":"123456"}}
# Đợi nhận INCOMING_CHALLENGE
{"action":"ACCEPT","data":{"from":"Bob","to":"Alice"}}
# Sau khi nhận START_GAME
{"action":"MOVE","data":{"matchId":"M97NSTBSV","from":"D4","to":"E5"}}
```

### Terminal 1 - Tiếp tục:
```bash
# Sau khi nhận START_GAME
{"action":"MOVE","data":{"matchId":"M97NSTBSV","from":"E5","to":"E6"}}
```

## Các tính năng đã implement

✅ **Authentication:**
- REGISTER với hash SHA-256
- LOGIN với session ID
- Lưu users vào file `users.dat`

✅ **Player Management:**
- Danh sách người chơi online
- Trạng thái: ONLINE, IN_MATCH, OFFLINE

✅ **Matchmaking:**
- CHALLENGE gửi lời mời
- INCOMING_CHALLENGE nhận lời mời
- ACCEPT/DECLINE phản hồi
- START_GAME tự động khi accept

✅ **Game Logic:**
- Board 8x8 cơ bản
- Kiểm tra lượt đi
- Validation nước đi đơn giản
- MOVE_OK và OPPONENT_MOVE
- MOVE_INVALID khi sai

✅ **Networking:**
- TCP Socket đa luồng
- JSON protocol với \n terminator
- Thread cho mỗi client
- Mutex để đồng bộ

## Lưu ý

1. **cJSON**: Cần download riêng (không có trong standard library)
2. **OpenSSL**: Cần cài libssl-dev cho SHA-256
3. **Port**: Mặc định 8888, có thể đổi trong main.c
4. **Max clients**: 100 (có thể tăng)
5. **Max matches**: 50 đồng thời

## Mở rộng thêm

Để hoàn thiện hơn, có thể thêm:
- Validation nước đi đầy đủ (từng loại quân) -> đã làm
- Kiểm tra chiếu, chiếu hết -> đã làm
- En passant, nhập thành, phong cấp
- Timer cho mỗi nước đi
- Lưu lịch sử trận đấu
- Database thay vì file
- TLS/SSL cho bảo mật
- Reconnect khi mất kết nối

## Debug

Nếu compile lỗi:
```bash
# Check OpenSSL
pkg-config --modversion openssl

# Link rõ ràng hơn
gcc -Wall -pthread -o chess_server *.c -lssl -lcrypto
```

## Clean up

```bash
make clean  # Xóa binary và object files
rm users.dat  # Xóa database
```