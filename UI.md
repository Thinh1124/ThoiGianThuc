Frontend Task: IrriFlow Dashboard (Real-time IoT)

1. Tech Stack (Tối giản & Tốc độ)
   Framework: React (Vite).

Styling: Tailwind CSS (Utility-first).

Icons: Lucide React.

Charts: Recharts (Hiển thị dữ liệu telemetry).

Backend/DB: Supabase SDK (@supabase/supabase-js).

2. Cấu trúc trang (Layout & Components)
   Giao diện được thiết kế theo phong cách Dashboard hiện đại, bao gồm các Component chính:

Real-time Monitor (Stats Cards): Hiển thị 3 chỉ số từ bảng telemetry: Nhiệt độ, Độ ẩm không khí, Độ ẩm đất.

Pump Status: Hiển thị trạng thái máy bơm (IDLE, PUMPING, OVERHEAT, ERROR) dựa trên phản hồi từ Arduino qua ESP32.

Control Panel:

Mode Switch: Chọn giữa 3 chế độ: threshold, schedule, manual.

Manual Toggle: Nút nhấn điều khiển manual_trigger (Bật/Tắt bơm tức thì).

Config Form: Cho phép cập nhật các ngưỡng dry_threshold, wet_threshold, safe_temp, timeout_ms vào bảng config.

Schedule Manager: Giao diện Thêm/Xóa/Sửa các mốc thời gian trong bảng schedules.

3. Luồng dữ liệu Real-time (Trọng tâm)
   Agent cần cấu hình Supabase Realtime để tránh Polling:

Subscribe telemetry: Khi có bản ghi mới được INSERT (từ ESP32), cập nhật ngay các thẻ chỉ số và biểu đồ.

Subscribe config: Theo dõi thay đổi trạng thái bơm và chế độ vận hành để đồng bộ UI với thiết bị thực tế.

Update config & schedules: Các thao tác của người dùng trên Web phải UPDATE trực tiếp vào Supabase. ESP32 sẽ nhận các thay đổi này qua cơ chế Realtime/Poll và đẩy xuống Arduino qua UART.

4. Yêu cầu giao diện (UI/UX)
   Responsive: Ưu tiên Mobile-first (người dùng thường kiểm tra vườn qua điện thoại).

Visual Feedback: Trạng thái bơm cần có màu sắc rõ ràng (Ví dụ: Đang bơm - Xanh lá nhấp nháy, Lỗi - Đỏ).

Gọn nhẹ: Không sử dụng các bộ UI Kit nặng (như AntD/MUI) để tránh làm "rác" thư mục commit.

Ghi chú cho Agent:

Dữ liệu ESP32 đẩy lên bảng telemetry định kỳ 2-5 giây.

Bảng config chỉ có duy nhất 1 dòng (id=1) để lưu trạng thái hệ thống hiện tại.

Bảng schedules lưu danh sách các mốc thời gian bơm cố định.
