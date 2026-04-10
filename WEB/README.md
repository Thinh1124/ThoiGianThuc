# WEB - Backend + Frontend cho hệ thống IoT

## Cấu trúc

- `backend/`: API server (Express)
- `frontend/`: Dashboard HTML/JS/CSS (được backend serve static)

## API dùng cho ESP32

- `POST /data`: ESP32 đẩy telemetry
- `GET /config`: ESP32 kéo config

## API dùng cho Web UI

- `GET /data`: lấy dữ liệu realtime
- `POST /config`: cập nhật config từ người dùng

## Chạy local

```bash
cd WEB/backend
npm install
npm run start
```

Mở trình duyệt: `http://localhost:3000`

## Script test mô phỏng ESP32

Mở terminal mới (giữ backend đang chạy), rồi chạy:

```bash
cd WEB/backend
npm run sim
```

Script sẽ:

- gọi `POST /config` để set cấu hình mẫu
- gửi telemetry định kỳ vào `POST /data`
- đọc lại `GET /data` và in kết quả

Biến môi trường tùy chọn:

- `BASE_URL` (mặc định `http://localhost:3000`)
- `INTERVAL_MS` (mặc định `2000`)
- `CYCLES` (mặc định `20`)

Ví dụ chạy nhanh:

```bash
npm run sim:quick
```

## Cập nhật ESP32

Trong `SensorBoard/SensorBoard.ino`, sửa:

- `API_BASE_URL` thành IP máy chạy backend (ví dụ `http://192.168.1.10:3000`)
- `WIFI_SSID`, `WIFI_PASS`

Sau đó nạp lại ESP32.
