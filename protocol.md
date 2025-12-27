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
    "reason": "Checkmate",
    "matchId": "M12345"
  }
}
```

Hoặc hòa:

```json
{
  "action": "GAME_RESULT",
  "data": {
    "winner": "DRAW",
    "reason": "Stalemate",
    "matchId": "M12345"
  }
}
```

Hoặc hủy ván:

```json
{
  "action": "GAME_RESULT",
  "data": {
    "winner": "ABORT",
    "reason": "Game aborted by agreement",
    "matchId": "M12345"
  }
}
```

---

# 📊 **9. ELO & Profile**

## 9.1 **GET_PROFILE**

Client → Server

```json
{
  "action": "GET_PROFILE",
  "data": {
    "username": "Alice"
  }
}
```

## 9.2 **PROFILE_INFO**

Server → Client

```json
{
  "action": "PROFILE_INFO",
  "data": {
    "username": "Alice",
    "elo": 1250,
    "wins": 10,
    "losses": 5,
    "draws": 2,
    "isOnline": true
  }
}
```

---

# 🔍 **10. Matchmaking tự động**

## 10.1 **FIND_MATCH**

Client → Server (Tham gia hàng đợi matchmaking)

```json
{
  "action": "FIND_MATCH",
  "data": {}
}
```

## 10.2 **CANCEL_FIND_MATCH**

Client → Server (Hủy tìm trận)

```json
{
  "action": "CANCEL_FIND_MATCH",
  "data": {}
}
```

## 10.3 **MATCHMAKING_STATUS**

Server → Client

```json
{
  "action": "MATCHMAKING_STATUS",
  "data": {
    "status": "SEARCHING"
  }
}
```

Hoặc khi tìm thấy đối thủ:

```json
{
  "action": "MATCHMAKING_STATUS",
  "data": {
    "status": "FOUND",
    "opponent": "Bob"
  }
}
```

Hoặc khi hủy:

```json
{
  "action": "MATCHMAKING_STATUS",
  "data": {
    "status": "CANCELLED"
  }
}
```

---

# 🛑 **11. Game Control**

## 11.1 **OFFER_ABORT** (Xin ngừng ván)

Client → Server

```json
{
  "action": "OFFER_ABORT",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.2 **ABORT_OFFERED**

Server → Opponent

```json
{
  "action": "ABORT_OFFERED",
  "data": {
    "matchId": "M12345",
    "from": "Alice"
  }
}
```

## 11.3 **ACCEPT_ABORT / DECLINE_ABORT**

Client → Server

```json
{
  "action": "ACCEPT_ABORT",
  "data": {
    "matchId": "M12345"
  }
}
```

```json
{
  "action": "DECLINE_ABORT",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.4 **ABORT_DECLINED**

Server → Client (người đề nghị)

```json
{
  "action": "ABORT_DECLINED",
  "data": {
    "matchId": "M12345"
  }
}
```

---

## 11.5 **OFFER_DRAW** (Mời hòa)

Client → Server

```json
{
  "action": "OFFER_DRAW",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.6 **DRAW_OFFERED**

Server → Opponent

```json
{
  "action": "DRAW_OFFERED",
  "data": {
    "matchId": "M12345",
    "from": "Alice"
  }
}
```

## 11.7 **ACCEPT_DRAW / DECLINE_DRAW**

Client → Server

```json
{
  "action": "ACCEPT_DRAW",
  "data": {
    "matchId": "M12345"
  }
}
```

```json
{
  "action": "DECLINE_DRAW",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.8 **DRAW_DECLINED**

Server → Client (người đề nghị)

```json
{
  "action": "DRAW_DECLINED",
  "data": {
    "matchId": "M12345"
  }
}
```

---

## 11.9 **OFFER_REMATCH** (Đấu lại)

Client → Server

```json
{
  "action": "OFFER_REMATCH",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.10 **REMATCH_OFFERED**

Server → Opponent

```json
{
  "action": "REMATCH_OFFERED",
  "data": {
    "matchId": "M12345",
    "from": "Alice"
  }
}
```

## 11.11 **ACCEPT_REMATCH / DECLINE_REMATCH**

Client → Server

```json
{
  "action": "ACCEPT_REMATCH",
  "data": {
    "matchId": "M12345"
  }
}
```

```json
{
  "action": "DECLINE_REMATCH",
  "data": {
    "matchId": "M12345"
  }
}
```

## 11.12 **REMATCH_DECLINED**

Server → Client (người đề nghị)

```json
{
  "action": "REMATCH_DECLINED",
  "data": {
    "matchId": "M12345"
  }
}
```

*Nếu ACCEPT_REMATCH, server sẽ gửi START_GAME mới với `isRematch: true`*

---

# 📜 **12. Match History**

## 12.1 **GET_MATCH_HISTORY**

Client → Server (Lấy danh sách ván đã chơi)

```json
{
  "action": "GET_MATCH_HISTORY",
  "data": {
    "username": "Alice"
  }
}
```

*Nếu không có username, server sẽ lấy của người gửi*

## 12.2 **MATCH_HISTORY**

Server → Client

```json
{
  "action": "MATCH_HISTORY",
  "data": {
    "username": "Alice",
    "matches": [
      {
        "matchId": "M12345ABC",
        "white": "Alice",
        "black": "Bob",
        "winner": "Alice",
        "timestamp": 1703664000,
        "moveCount": 42
      },
      {
        "matchId": "M67890XYZ",
        "white": "Charlie",
        "black": "Alice",
        "winner": "DRAW",
        "timestamp": 1703577600,
        "moveCount": 68
      }
    ]
  }
}
```

## 12.3 **GET_MATCH_REPLAY**

Client → Server (Xem lại ván đấu)

```json
{
  "action": "GET_MATCH_REPLAY",
  "data": {
    "matchId": "M12345ABC"
  }
}
```

## 12.4 **MATCH_REPLAY**

Server → Client

```json
{
  "action": "MATCH_REPLAY",
  "data": {
    "matchId": "M12345ABC",
    "white": "Alice",
    "black": "Bob",
    "winner": "Alice",
    "reason": "Checkmate",
    "timestamp": 1703664000,
    "endTime": 1703665800,
    "moveCount": 42,
    "moves": ["E2E4", "E7E5", "G1F3", "B8C6", "..."],
    "finalBoard": "RNBQKBNRPPPPPPPP................................pppppppprnbqkbnr"
  }
}
```


---

# 🔄 **13. Keep-alive / Ping**

## 13.1 **PING**

Client → Server

```json
{"action":"PING","data":{}}
```

## 13.2 **PONG**

Server → Client

```json
{"action":"PONG","data":{}}
```

---

# ⚠️ **14. Error Message**

```json
{
  "action": "ERROR",
  "data": {
    "reason": "Unknown action"
  }
}
```

---

# 📚 **15. Tổng kết**

| Action                | Hướng  | Ý nghĩa                          |
|-----------------------|--------|----------------------------------|
| **Authentication**    |        |                                  |
| REGISTER              | C → S  | Đăng ký tài khoản                |
| REGISTER_SUCCESS/FAIL | S → C  | Kết quả đăng ký                  |
| LOGIN                 | C → S  | Đăng nhập                        |
| LOGIN_SUCCESS/FAIL    | S → C  | Kết quả đăng nhập                |
| **Player List**       |        |                                  |
| REQUEST_PLAYER_LIST   | C → S  | Yêu cầu danh sách người chơi     |
| PLAYER_LIST           | S → C  | Trả danh sách                    |
| GET_PROFILE           | C → S  | Xem hồ sơ người chơi             |
| PROFILE_INFO          | S → C  | Thông tin hồ sơ                  |
| **Matchmaking**       |        |                                  |
| CHALLENGE             | C → S  | Thách đấu trực tiếp              |
| INCOMING_CHALLENGE    | S → C  | Ai đó thách đấu bạn              |
| ACCEPT/DECLINE        | C → S  | Trả lời thách đấu                |
| FIND_MATCH            | C → S  | Tìm trận tự động                 |
| CANCEL_FIND_MATCH     | C → S  | Hủy tìm trận                     |
| MATCHMAKING_STATUS    | S → C  | Trạng thái matchmaking           |
| START_GAME            | S → C  | Bắt đầu game                     |
| **Game Play**         |        |                                  |
| MOVE                  | C → S  | Gửi nước đi                      |
| MOVE_OK               | S → C  | Nước đi hợp lệ                   |
| MOVE_INVALID          | S → C  | Nước đi sai                      |
| OPPONENT_MOVE         | S → C  | Nước đi của đối thủ              |
| GAME_RESULT           | S → C  | Kết thúc trận                    |
| **Game Control**      |        |                                  |
| OFFER_ABORT           | C → S  | Xin ngừng ván                    |
| ABORT_OFFERED         | S → C  | Đối thủ xin ngừng                |
| ACCEPT_ABORT          | C → S  | Đồng ý ngừng                     |
| DECLINE_ABORT         | C → S  | Từ chối ngừng                    |
| ABORT_DECLINED        | S → C  | Đối thủ từ chối ngừng            |
| OFFER_DRAW            | C → S  | Mời hòa                          |
| DRAW_OFFERED          | S → C  | Đối thủ mời hòa                  |
| ACCEPT_DRAW           | C → S  | Đồng ý hòa                       |
| DECLINE_DRAW          | C → S  | Từ chối hòa                      |
| DRAW_DECLINED         | S → C  | Đối thủ từ chối hòa              |
| OFFER_REMATCH         | C → S  | Đề nghị đấu lại                  |
| REMATCH_OFFERED       | S → C  | Đối thủ đề nghị đấu lại          |
| ACCEPT_REMATCH        | C → S  | Đồng ý đấu lại                   |
| DECLINE_REMATCH       | C → S  | Từ chối đấu lại                  |
| REMATCH_DECLINED      | S → C  | Đối thủ từ chối đấu lại          |
| **Match History**     |        |                                  |
| GET_MATCH_HISTORY     | C → S  | Lấy danh sách ván đã chơi        |
| MATCH_HISTORY         | S → C  | Danh sách ván đấu                |
| GET_MATCH_REPLAY      | C → S  | Xem lại ván đấu                  |
| MATCH_REPLAY          | S → C  | Chi tiết ván đấu + nước đi       |
| **Utility**           |        |                                  |
| PING/PONG             | C ↔ S  | Giữ kết nối                      |
| ERROR                 | S → C  | Thông báo lỗi                    |

---

# 📖 **16. Ghi chú bổ sung**

## 16.1 Hệ thống ELO

- Điểm ELO mặc định: **1200**
- Hệ số K: **32**
- Công thức: Elo_mới = Elo_cũ + K * (Kết_quả - Expected_Score)
- Expected Score: 1 / (1 + 10^((Elo_đối_thủ - Elo_bạn) / 400))

## 16.2 Matchmaking tự động

- Server kiểm tra hàng đợi mỗi **2 giây**
- Ghép cặp người chơi có chênh lệch ELO **< 100**
- Ưu tiên người đợi lâu nhất nếu cùng chênh lệch ELO

## 16.3 Luật cờ vua đã implement

- Di chuyển tất cả các quân
- En passant (ăn tốt qua đường)
- Castling (nhập thành) - cả kingside và queenside
- Pawn promotion (phong cấp tốt)
- Kiểm tra nước đi không để vua bị chiếu
- Phát hiện chiếu hết (checkmate)
- Phát hiện bế tắc (stalemate)
- Hòa do thiếu quân (insufficient material)

## 16.4 Rematch

- Sau khi ván đấu kết thúc, người chơi có thể đề nghị đấu lại
- Khi đấu lại, màu quân sẽ được **đổi ngược** (trắng → đen, đen → trắng)
- Rematch phải được gửi ngay sau ván đấu kết thúc

## 16.5 Match History

- Lịch sử ván đấu được lưu trong thư mục matches/
- Mỗi ván lưu thành file JSON riêng: matches/{matchId}.json
- Bao gồm: thông tin người chơi, kết quả, timestamp, tất cả nước đi, bàn cờ cuối

---
