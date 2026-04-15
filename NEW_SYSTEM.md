# 📝 IrriFlow System Configuration (Real-time Upgrade - V2)

## 1. Tổng quan kiến trúc (System Architecture)

Hệ thống IrriFlow nâng cấp sử dụng **Supabase** làm trung tâm điều phối dữ liệu thời gian thực.

- **Board chính (Arduino UNO):** Chạy FreeRTOS, thực thi lệnh bơm và bảo vệ an toàn phần cứng (Overheat, Timeout).
- **Board phụ (ESP32):** Gateway WiFi, đọc cảm biến, đồng bộ dữ liệu với Supabase qua REST API/WebSockets.
- **Cloud (Supabase):** Database PostgreSQL tích hợp Real-time (CDC) để đẩy cấu hình và nhận telemetry.
- **Web Dashboard:** Giao diện React điều khiển 3 chế độ: Tự động (Ngưỡng), Hẹn giờ, và Thủ công.

## 2. Thông số UART (Giữ nguyên để tương thích)

- **Tốc độ:** 9600 baud.
- **ESP32 -> UNO (Dữ liệu/Config):** - `T:<temp>,H:<hum>,S:<soil>\n`
  - `CFG:dry=<val>,wet=<val>,timeout=<val>,safe=<val>\n`
- **UNO -> ESP32 (Phản hồi):** `P:<pump_state>\n` (0: OFF, 1: ON).

## 3. Cấu trúc Database Supabase (Đã cập nhật)

### Bảng `telemetry` (Lưu lịch sử cảm biến)

- `temp`, `hum`, `soil`, `pump_status`, `created_at`.

### Bảng `config` (Cấu hình vận hành - Enable Realtime)

- `dry_threshold`, `wet_threshold`, `safe_temp`, `timeout_ms`.
- `pump_mode`: `threshold` (theo độ ẩm), `schedule` (theo lịch), `manual` (thủ công).
- `manual_trigger`: boolean (dùng để bật/tắt bơm tức thì ở chế độ manual).

### Bảng `schedules` (Lịch trình bơm mới)

- `id`: uuid.
- `start_time`: time (Ví dụ: 08:00:00).
- `duration_minutes`: int (Thời gian bơm).
- `days_of_week`: int[] (Mảng các ngày trong tuần, 1=CN, 2=T2...).
- `enabled`: boolean (Bật/tắt lịch cụ thể).

## 4. Logic Điều khiển nâng cấp

1. **Chế độ Threshold:** ESP32 so sánh `soil` với `dry_threshold` từ bảng `config`.
2. **Chế độ Schedule:** - Backend (hoặc Edge Function) kiểm tra bảng `schedules`.
   - Khi đến giờ, cập nhật `manual_trigger = true` trong bảng `config`.
   - ESP32 nhận thay đổi realtime và ra lệnh cho UNO qua UART.
3. **Chế độ Manual:** Web thay đổi trực tiếp `manual_trigger` trong bảng `config`.

## 5. Lộ trình thực hiện (D2 còn lại)

- **Hoàn thành Database:** Tạo bảng `schedules` và bật Realtime CDC trên Supabase.
- **Cập nhật ESP32:** Thêm logic nhận `pump_mode` và `manual_trigger` từ Supabase.
- **Frontend:** Xây dựng UI cho phép Thêm/Sửa/Xóa các bản ghi trong bảng `schedules`.

## 6. Ghi chú bảo mật

- Sử dụng **Anon Key** cho ESP32 và Frontend Dashboard.
- Disable RLS tạm thời cho các bảng trong quá trình phát triển 48h để tăng tốc kết nối.
