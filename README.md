# Hệ thống tưới cây tự động dùng FreeRTOS (Arduino UNO)

## Giới thiệu

Dự án triển khai hệ thống tưới cây tự động bằng hai bo mạch Arduino UNO.

- Board phụ: đọc DHT11 và độ ẩm đất, hiển thị OLED, gửi dữ liệu qua UART.
- Board chính: chạy FreeRTOS, nhận dữ liệu cảm biến, điều khiển relay bơm, xử lý an toàn.

Mô hình hệ thống:

```
[Board phụ] --UART--> [Board chính]
```

## Cấu trúc thư mục

```
TestFreeRTOS/
  TestFreeRTOS.ino   # Board chính (FreeRTOS + điều khiển relay)
NonDHTLib/
  NonDHTLib.ino      # Board phụ (DHT11 + độ ẩm đất + OLED + UART)
```

## Kết nối phần cứng

### UART giữa hai board

| Board phụ | Board chính |
| --------- | ----------- |
| TX (D1)   | RX (D0)     |
| GND       | GND         |

### Board phụ

- DHT11: DATA -> D2, VCC -> 5V, GND -> GND
- Cảm biến độ ẩm đất: AO -> A0
- OLED I2C: SDA -> A4, SCL -> A5, VCC -> 5V, GND -> GND

### Board chính

- Relay: IN -> D8, VCC -> 5V, GND -> GND
- LED lỗi: D13

## Giao thức UART

Định dạng khung dữ liệu:

```
T:30,H:65,S:450
```

- `T`: nhiệt độ (°C)
- `H`: độ ẩm không khí (%)
- `S`: giá trị độ ẩm đất (ADC)

## Logic điều khiển (Board chính)

Máy trạng thái:

- `IDLE`
- `PUMPING`
- `OVERHEAT`
- `ERROR`

Ngưỡng theo mã nguồn hiện tại:

- `DRY_THRESHOLD = 900`
- `WET_THRESHOLD = 600`
- `SAFE_TEMP = 32`
- `SAFETY_TIMEOUT = 600000 ms` (10 phút)

Nguyên tắc hoạt động:

- Bật bơm khi đất khô (`soilValue > DRY_THRESHOLD`) và nhiệt độ an toàn.
- Tắt bơm khi đất đủ ẩm (`soilValue < WET_THRESHOLD`).
- Dừng bơm, chuyển `OVERHEAT` khi `temperature > SAFE_TEMP`.
- Chuyển `ERROR` khi thời gian bơm vượt `SAFETY_TIMEOUT`.

## Tác vụ FreeRTOS (Board chính)

| Tác vụ         | Chức năng                            | Ưu tiên | Chu kỳ  |
| -------------- | ------------------------------------ | ------- | ------- |
| `Task_UART`    | Nhận UART và phân tích khung `T,H,S` | 2       | 100 ms  |
| `Task_Control` | Xử lý máy trạng thái và relay        | 2       | 500 ms  |
| `Task_Monitor` | In dữ liệu giám sát ra Serial        | 1       | 3000 ms |

## Nạp chương trình

1. Mở `NonDHTLib/NonDHTLib.ino`, nạp cho board phụ.
2. Mở `TestFreeRTOS/TestFreeRTOS.ino`, nạp cho board chính.
3. Rút dây UART trước khi nạp mã.
4. Nạp xong thì nối lại UART và GND chung.

## Lưu ý

- Cả hai board dùng baudrate `9600`.
- Logic relay trong mã hiện tại là active HIGH (`HIGH` bật, `LOW` tắt).
- Nếu OLED lỗi khởi tạo, board phụ vẫn đọc cảm biến và gửi UART; chỉ bỏ qua hiển thị OLED.

## Giấy phép

Dự án phục vụ mục đích học tập và nghiên cứu.
