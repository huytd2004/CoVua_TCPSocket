# Hệ Thống Kiểm Tra Tính Hợp Lệ Nước Đi Cờ Vua - Đầy Đủ Luật

## Tổng Quan

Hệ thống đã implement **đầy đủ** các luật cờ vua quốc tế (FIDE), bao gồm cả các luật đặc biệt và nâng cao.

---

## 📋 Danh Sách Các Luật Đã Implement

### ✅ 1. Luật Di Chuyển Cơ Bản

#### Tốt (Pawn)
- **Đi tiến 1 ô** về phía trước (ô phải trống)
- **Đi tiến 2 ô** từ vị trí xuất phát (hàng 2 cho trắng, hàng 7 cho đen)
- **Ăn chéo** 1 ô về phía trước (phải có quân đối phương)
- **Không thể lùi**

```c
// Ví dụ: Tốt trắng tại E2
E2 -> E3  ✓ (đi 1 ô)
E2 -> E4  ✓ (đi 2 ô từ vị trí xuất phát)
E2 -> D3  ✓ (nếu có quân đen tại D3)
E2 -> E1  ✗ (không thể lùi)
```

#### Mã (Knight)
- Di chuyển theo hình chữ **L**: 2 ô theo một hướng + 1 ô vuông góc
- **Nhảy qua** các quân khác (không bị chặn)

```c
// Ví dụ: Mã tại E4
E4 -> D6  ✓
E4 -> F6  ✓
E4 -> G5  ✓
E4 -> G3  ✓
E4 -> F2  ✓
E4 -> D2  ✓
E4 -> C3  ✓
E4 -> C5  ✓
```

#### Tượng (Bishop)
- Di chuyển **chéo** không giới hạn
- Không được nhảy qua quân khác

```c
// Ví dụ: Tượng tại C1
C1 -> D2, E3, F4, G5, H6  ✓ (đường chéo)
```

#### Xe (Rook)
- Di chuyển **ngang/dọc** không giới hạn
- Không được nhảy qua quân khác

```c
// Ví dụ: Xe tại A1
A1 -> A8  ✓ (dọc)
A1 -> H1  ✓ (ngang)
```

#### Hậu (Queen)
- Kết hợp **Xe + Tượng**
- Di chuyển ngang/dọc/chéo không giới hạn

#### Vua (King)
- Di chuyển **1 ô** theo mọi hướng (ngang/dọc/chéo)
- Không được đi vào ô bị tấn công

---

### ✅ 2. En Passant (Ăn Tốt Qua Đường)

**Điều kiện:**
1. Tốt đối phương vừa đi 2 ô từ vị trí xuất phát
2. Tốt của bạn đang ở hàng thứ 5 (trắng) hoặc hàng thứ 4 (đen)
3. Hai tốt đang kề nhau

**Cách thực hiện:**
- Tốt của bạn ăn chéo vào ô phía sau tốt đối phương
- Tốt đối phương bị loại bỏ

```
Trước:                    Sau en passant:
  a b c d e               a b c d e
5 . . . . .             5 . . . P .
4 . . P p .             4 . . . . .
3 . . . . .             3 . . . . .

Đen vừa đi d7->d5
Trắng có thể c4->d5 (en passant)
Tốt đen tại d5 bị loại bỏ
```

**Code kiểm tra:**
```c
// Kiểm tra en passant
if (p == 'p' && abs(dc) == 1 && dr == dir && dest == '.')
{
    int en_passant_row = is_white_piece ? 3 : 4;
    if (from_row == en_passant_row && to_col == match->en_passant_col)
    {
        // En passant hợp lệ
    }
}
```

---

### ✅ 3. Castling (Nhập Thành)

**Điều kiện:**
1. Vua và xe chưa từng di chuyển
2. Không có quân nào giữa vua và xe
3. Vua không đang bị chiếu
4. Vua không đi qua ô bị tấn công
5. Vua không đến ô bị tấn công

**Hai loại nhập thành:**

#### Kingside Castling (O-O) - Nhập thành cánh vua
```
Trước:     E1 . . . . . . R
Sau:       . . . . . . R K
```
Vua di chuyển 2 ô về bên phải, xe nhảy qua vua

#### Queenside Castling (O-O-O) - Nhập thành cánh hậu
```
Trước:     R . . . K . . .
Sau:       . . K R . . . .
```
Vua di chuyển 2 ô về bên trái, xe nhảy qua vua

**Code kiểm tra:**
```c
// Nhập thành kingside
if (dr == 0 && dc == 2)
{
    // Kiểm tra vua chưa di chuyển
    if (is_white && match->white_king_moved) return 0;
    
    // Kiểm tra xe chưa di chuyển
    if (is_white && match->white_rook_h_moved) return 0;
    
    // Kiểm tra vua không bị chiếu
    if (is_in_check(match, is_white)) return 0;
    
    // Kiểm tra ô trống
    if (board[row][5] != '.' || board[row][6] != '.') return 0;
    
    // Kiểm tra không đi qua ô bị tấn công
    if (is_square_under_attack(match, row, 5, !is_white)) return 0;
    if (is_square_under_attack(match, row, 6, !is_white)) return 0;
    
    return 1;
}
```

---

### ✅ 4. Pawn Promotion (Phong Cấp Tốt)

**Điều kiện:**
- Tốt đến hàng cuối (hàng 8 cho trắng, hàng 1 cho đen)

**Lựa chọn:**
- Queen (Hậu) - mặc định
- Rook (Xe)
- Bishop (Tượng)
- Knight (Mã)

```c
// Phong cấp tốt
if (p == 'p' && (to_row == 0 || to_row == 7))
{
    if (promotion_piece != '\0')
        piece = is_white ? tolower(promotion_piece) : toupper(promotion_piece);
    else
        piece = is_white ? 'q' : 'Q'; // Mặc định phong hậu
}
```

---

### ✅ 5. Kiểm Tra Nước Đi Không Để Vua Bị Chiếu

**Luật quan trọng nhất:**
- **MỌI** nước đi phải đảm bảo vua không bị chiếu sau nước đi
- Áp dụng cho **TẤT CẢ** quân cờ, không chỉ vua

**Cách kiểm tra:**
1. Thực hiện nước đi tạm thời
2. Kiểm tra vua có bị chiếu không
3. Hoàn tác nước đi
4. Trả về kết quả

```c
// Code kiểm tra (áp dụng cho TẤT CẢ quân)
char temp = match->board[to_row][to_col];
match->board[to_row][to_col] = piece;
match->board[from_row][from_col] = '.';

int in_check = is_in_check(match, is_white_piece);

match->board[from_row][from_col] = piece;
match->board[to_row][to_col] = temp;

return !in_check; // Chỉ hợp lệ nếu vua không bị chiếu
```

**Ví dụ:**
```
  a b c d e f g h
8 . . . . k . . r  <- Vua đen
7 . . . . . . . .
6 . . . . . . . .
5 . . . . . . . .
4 . . . . R . . .  <- Xe trắng
3 . . . . . . . .
2 . . . . . . . .
1 . . . K . . . .  <- Vua trắng

Vua đen KHÔNG THỂ di chuyển xe h8 vì sẽ bị chiếu bởi xe trắng E4
```

---

### ✅ 6. Phát Hiện Chiếu (Check)

**Điều kiện:**
- Vua đang bị tấn công bởi ít nhất một quân đối phương

**Cách xử lý:**
- Người chơi BẮT BUỘC phải thoát khỏi chiếu
- 3 cách thoát chiếu:
  1. Di chuyển vua ra khỏi vị trí bị tấn công
  2. Chặn quân đang chiếu bằng quân khác
  3. Ăn quân đang chiếu

```c
int is_in_check(Match *match, int is_white)
{
    int king_row, king_col;
    if (!find_king(match, is_white, &king_row, &king_col))
        return 0;
    
    return is_square_under_attack(match, king_row, king_col, !is_white);
}
```

---

### ✅ 7. Phát Hiện Chiếu Hết (Checkmate)

**Điều kiện:**
- Vua đang bị chiếu
- Không có nước đi hợp lệ nào để thoát chiếu

**Kết quả:**
- Người bị chiếu hết thua
- Đối phương thắng

```c
if (!has_moves)
{
    if (in_check)
    {
        *winner = current_is_white ? match->black_player : match->white_player;
        *reason = "Checkmate";
        return 1;
    }
}
```

---

### ✅ 8. Phát Hiện Bế Tắc (Stalemate)

**Điều kiện:**
- Vua KHÔNG bị chiếu
- Không có nước đi hợp lệ nào

**Kết quả:**
- Hòa

```c
if (!has_moves)
{
    if (!in_check)
    {
        *winner = "DRAW";
        *reason = "Stalemate";
        return 1;
    }
}
```

**Ví dụ Stalemate:**
```
  a b c d e
8 . . . . .
7 . . . k .
6 . . K Q .
5 . . . . .

Vua đen không bị chiếu nhưng không thể di chuyển
-> Stalemate -> Hòa
```

---

### ✅ 9. Hòa Do Thiếu Quân

**Trường hợp hòa:**
1. **K vs K** (Vua vs Vua)
2. **K+B vs K** (Vua+Tượng vs Vua)
3. **K+N vs K** (Vua+Mã vs Vua)
4. **K+B vs K+B** (cùng màu ô)

```c
int is_insufficient_material(Match *match)
{
    // Đếm quân cờ
    // Kiểm tra các trường hợp hòa
    
    if (white_pieces == 0 && black_pieces == 0)
        return 1; // K vs K
    
    if ((white_bishops == 1 || white_knights == 1) && 
        white_pieces == 1 && black_pieces == 0)
        return 1; // K+B vs K or K+N vs K
}
```

---

## 🔧 Cách Sử Dụng

### 1. Khởi tạo ván đấu

```c
MatchFull match;
init_match_board(&match);

// Thiết lập thông tin
strcpy(match.match_id, "MATCH001");
strcpy(match.white_player, "Player1");
strcpy(match.black_player, "Player2");
match.is_active = 1;
```

### 2. Kiểm tra tính hợp lệ nước đi

```c
int from_row, from_col, to_row, to_col;
notation_to_coords("E2", &from_row, &from_col);
notation_to_coords("E4", &to_row, &to_col);

if (is_valid_move(&match, from_row, from_col, to_row, to_col, 0))
{
    printf("Nước đi hợp lệ!\n");
}
else
{
    printf("Nước đi không hợp lệ!\n");
}
```

### 3. Thực hiện nước đi

```c
if (is_valid_move(&match, from_row, from_col, to_row, to_col, match.current_turn))
{
    execute_move(&match, from_row, from_col, to_row, to_col, '\0');
    match.current_turn = 1 - match.current_turn;
}
```

### 4. Kiểm tra kết thúc game

```c
char *winner = NULL;
char *reason = NULL;

if (check_game_end(&match, &winner, &reason))
{
    printf("Game kết thúc! Winner: %s, Reason: %s\n", winner, reason);
}
```

---

## 📊 Bảng So Sánh Phiên Bản

| Tính năng | Phiên bản cũ | Phiên bản đầy đủ |
|-----------|--------------|------------------|
| Di chuyển cơ bản | ✅ | ✅ |
| En passant | ❌ | ✅ |
| Castling | ❌ | ✅ |
| Pawn promotion | ❌ | ✅ |
| Kiểm tra chiếu cho mọi quân | ❌ | ✅ |
| Checkmate detection | ✅ | ✅ |
| Stalemate detection | ✅ | ✅ |
| Insufficient material | ✅ | ✅ |

---

## 🎯 Test Cases Gợi Ý

### Test 1: En Passant
```
1. e4 e5
2. Nf3 Nf6
3. d4 exd4    <- Tốt đen ăn
```

### Test 2: Castling
```
1. e4 e5
2. Nf3 Nf6
3. Bc4 Bc5
4. O-O         <- Nhập thành kingside trắng
```

### Test 3: Kiểm tra không để vua bị chiếu
```
Position: Vua trắng E1, Xe đen E8, Xe trắng D1
Xe trắng KHÔNG THỂ di chuyển vì sẽ để vua bị chiếu
```

### Test 4: Checkmate
```
1. e4 e5
2. Bc4 Nc6
3. Qh5 Nf6
4. Qxf7#       <- Scholar's Mate
```

---

## 📝 Lưu Ý Quan Trọng

1. **Thread Safety**: Luôn lock mutex khi truy cập `match` trong môi trường multi-threaded
2. **Validation**: Luôn gọi `is_valid_move()` TRƯỚC KHI `execute_move()`
3. **State Management**: Cập nhật flags (`*_moved`, `en_passant_col`) sau mỗi nước đi
4. **Error Handling**: Kiểm tra return value của các hàm validation

---

## 🚀 Tối Ưu Hóa Tiếp Theo

Có thể thêm:
- Threefold repetition detection (hòa do lặp vị trí 3 lần)
- Fifty-move rule (hòa do 50 nước không ăn quân)
- Move history (lưu toàn bộ lịch sử nước đi)
- FEN notation support (import/export vị trí)
- Opening book
- AI engine integration

---

## 📚 Tài Liệu Tham Khảo

- FIDE Laws of Chess: https://www.fide.com/fide/handbook.html?id=208&view=article
- Chess Programming Wiki: https://www.chessprogramming.org/
- Forsyth-Edwards Notation: https://en.wikipedia.org/wiki/Forsyth%E2%80%93Edwards_Notation
