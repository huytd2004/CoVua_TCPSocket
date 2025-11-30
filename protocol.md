# 📝 **protocol.md**

## **1. Giới thiệu**

Tài liệu này mô tả giao thức truyền thông giữa **Client (Unity – C#)** và **Server (C – TCP Socket)** thông qua chuỗi JSON.

Mỗi message đều tuân theo cấu trúc:

```json
{
  "action": "ACTION_NAME",
  "data": { ... }
}
```

* **action**: string mô tả loại message
* **data**: object chứa thông tin chi tiết
* Tất cả message phải kết thúc bằng ký tự `\n` để phân tách gói.

---

# **2. Quy ước chung**

## 2.1 Encoding

* UTF-8

## 2.2 Message termination

* Mỗi gói JSON kết thúc bằng ký tự newline `\n`

Ví dụ:

```
{"action":"PING","data":{}}\n
```

## 2.3 Trạng thái người chơi

* `ONLINE`
* `IN_MATCH`
* `OFFLINE`

---

# **3. Danh sách các message**

---

# 🔐 **4. Authentication**

## 4.1 **REGISTER**

Client → Server

```json
{
  "action": "REGISTER",
  "data": {
    "username": "user123",
    "password": "123456"
  }
}
```

### Response

**REGISTER_SUCCESS**

```json
{
  "action": "REGISTER_SUCCESS",
  "data": {
    "message": "Account created"
  }
}
```

**REGISTER_FAIL**

```json
{
  "action": "REGISTER_FAIL",
  "data": {
    "reason": "Username already exists"
  }
}
```

---

## 4.2 **LOGIN**

Client → Server

```json
{
  "action": "LOGIN",
  "data": {
    "username": "user123",
    "password": "123456"
  }
}
```

### Response

**LOGIN_SUCCESS**

```json
{
  "action": "LOGIN_SUCCESS",
  "data": {
    "sessionId": "abc9f31a",
    "username": "user123"
  }
}
```

**LOGIN_FAIL**

```json
{
  "action": "LOGIN_FAIL",
  "data": {
    "reason": "Invalid password"
  }
}
```

---

# 🟦 **5. Player List**

## 5.1 **REQUEST_PLAYER_LIST**

Client → Server

```json
{
  "action": "REQUEST_PLAYER_LIST",
  "data": {}
}
```

## 5.2 **PLAYER_LIST**

Server → Client

```json
{
  "action": "PLAYER_LIST",
  "data": {
    "players": [
      {"username": "A", "status": "ONLINE"},
      {"username": "B", "status": "IN_MATCH"}
    ]
  }
}
```

---

# 🎮 **6. Matchmaking / Thách đấu**

## 6.1 **CHALLENGE**

Client A → Server

```json
{
  "action": "CHALLENGE",
  "data": {
    "from": "Alice",
    "to": "Bob"
  }
}
```

## 6.2 **INCOMING_CHALLENGE**

Server → Client B

```json
{
  "action": "INCOMING_CHALLENGE",
  "data": {
    "from": "Alice"
  }
}
```

---

## 6.3 **ACCEPT**

Client B → Server

```json
{
  "action": "ACCEPT",
  "data": {
    "from": "Bob",
    "to": "Alice"
  }
}
```

## 6.4 **DECLINE**

Client B → Server

```json
{
  "action": "DECLINE",
  "data": {
    "from": "Bob",
    "to": "Alice"
  }
}
```

---

## 6.5 **START_GAME**

Server → Both clients
Khi trận đấu bắt đầu

```json
{
  "action": "START_GAME",
  "data": {
    "matchId": "M12345",
    "white": "Alice",
    "black": "Bob",
    "board": "<FEN or simple 2D array>"
  }
}
```

---

# ♟ **7. Nước đi**

## 7.1 **MOVE**

Client → Server

```json
{
  "action": "MOVE",
  "data": {
    "matchId": "M12345",
    "from": "E2",
    "to": "E4"
  }
}
```

## 7.2 **MOVE_OK**

Server → Player (người vừa đi)

```json
{
  "action": "MOVE_OK",
  "data": {
    "from": "E2",
    "to": "E4"
  }
}
```

## 7.3 **OPPONENT_MOVE**

Server → Player đối thủ

```json
{
  "action": "OPPONENT_MOVE",
  "data": {
    "from": "E2",
    "to": "E4"
  }
}
```

## 7.4 **MOVE_INVALID**

```json
{
  "action": "MOVE_INVALID",
  "data": {
    "reason": "Illegal move"
  }
}
```

---

# 🏁 **8. Kết thúc trận**

## 8.1 **GAME_RESULT**

```json
{
  "action": "GAME_RESULT",
  "data": {
    "winner": "Alice",
    "reason": "Checkmate"
  }
}
```

Hoặc hòa:

```json
{
  "action": "GAME_RESULT",
  "data": {
    "winner": "DRAW",
    "reason": "Stalemate"
  }
}
```

---

# 🔄 **9. Keep-alive / Ping**

## 9.1 **PING**

Client → Server

```json
{"action":"PING","data":{}}
```

## 9.2 **PONG**

Server → Client

```json
{"action":"PONG","data":{}}
```

---

# ⚠️ **10. Error Message**

```json
{
  "action": "ERROR",
  "data": {
    "reason": "Unknown action"
  }
}
```

---

# 📚 **11. Tổng kết**

| Action                  | Hướng    | Ý nghĩa                      |
| ----------------------- | -------- | ---------------------------- |
| REGISTER                | C → S    | Đăng ký                      |
| REGISTER_SUCCESS / FAIL | S → C    | Kết quả đăng ký              |
| LOGIN                   | C → S    | Đăng nhập                    |
| LOGIN_SUCCESS / FAIL    | S → C    | Kết quả đăng nhập            |
| REQUEST_PLAYER_LIST     | C → S    | Yêu cầu danh sách người chơi |
| PLAYER_LIST             | S → C    | Trả danh sách                |
| CHALLENGE               | C → S    | Thách đấu                    |
| INCOMING_CHALLENGE      | S → C    | Ai đó thách đấu bạn          |
| ACCEPT / DECLINE        | C → S    | Trả lời                      |
| START_GAME              | S → C, C | Bắt đầu game                 |
| MOVE                    | C → S    | Gửi nước đi                  |
| MOVE_OK                 | S → C    | Nước đi hợp lệ               |
| MOVE_INVALID            | S → C    | Nước đi sai                  |
| OPPONENT_MOVE           | S → C    | Nước đi của đối thủ          |
| GAME_RESULT             | S → C    | Kết thúc trận                |
| PING / PONG             | C ↔ S    | Giữ kết nối                  |

---

