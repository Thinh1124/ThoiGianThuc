# Hệ thống tưới cây tự động dùng FreeRTOS (Arduino UNO)

## Giới thiệu

Thiết kế một hệ thống vườn thông minh gồm 2 vi điều khiển: Arduino UNO (board chính, chạy FreeRTOS) và ESP32 (board phụ). Hai board giao tiếp với nhau thông qua giao thức UART (TX/RX), là phương thức truyền dữ liệu nối tiếp đơn giản, gửi dữ liệu dạng bit giữa hai thiết bị.

- Board phụ: đọc DHT11 và độ ẩm đất, hiển thị OLED, gửi dữ liệu qua UART.
- Board chính: chạy FreeRTOS, nhận dữ liệu cảm biến, điều khiển relay bơm, xử lý an toàn.

Mô hình hệ thống:

```
[Board phụ] --UART--> [Board chính]
```

## Cấu trúc thư mục

```
Mainboard/
  Mainboard.ino   # Board chính
SensorBoard/
  SensorBoard.ino      # Board phụ
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

- ESP32 (Board phụ – Sensor Node - Sử dụng FreeRTOS)
  Đọc dữ liệu từ các cảm biến:
  Nhiệt độ và độ ẩm (DHT11)
  Độ ẩm đất (analog)
  Định kỳ (mỗi 2 giây) gửi dữ liệu qua UART cho Arduino theo định dạng chuỗi:
  T:<temp>,H:<hum>,S:<soil>\n
  ESP32 chỉ có nhiệm vụ đọc sensor và truyền dữ liệu, không xử lý điều khiển.

### Board chính

- Relay: IN -> D8, VCC -> 5V, GND -> GND
- LED lỗi: D13
- Arduino UNO R3: Nhận thông tin từ board phụ, cho phép cập nhật các giá trị <DRY_THRESHOLD>,<WET_THRESHOLD>,<SAFETY_TIMEOUT>,<SAFE_TEMP>

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

## Lưu ý

- Cả hai board dùng baudrate `9600`.
- Logic relay trong mã hiện tại là active HIGH (`HIGH` bật, `LOW` tắt).
- Nếu OLED lỗi khởi tạo, board phụ vẫn đọc cảm biến và gửi UART; chỉ bỏ qua hiển thị OLED.

## WEB UI

- Xây dựng 1 trang web cho phép người dùng có thể xem và cập nhật dữ liệu trực tuyến
- Trang web dạng dashboard có:
  - thông tin nhiệt độ độ ẩm không khí, độ ẩm đất
  - cho phép người dùng thay đổi các giá trị <DRY_THRESHOLD>,<WET_THRESHOLD>,<SAFETY_TIMEOUT>,<SAFE_TEMP>
  - cập nhật data realtime
