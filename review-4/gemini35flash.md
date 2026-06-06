# Đánh giá Kỹ thuật Hệ điều hành: Gemini 3.5 Flash

**Ngày đánh giá:** 2026-06-06  
**Người đánh giá:** Gemini 3.5 Flash  
**Đối tượng review:** `/home/ngocnt/operating_system/os`  
**Bối cảnh:** Bổ sung tiếp nối các đánh giá trước đó của Claude 4.6 Sonnet (`claude46sonnet.md`) và GPT-5.5 (`ppt55-note.md`).

---

## 1. Nhận định Tổng quan (Executive Summary)

Project hiện tại là một **bare-metal x86 teaching OS** có cấu trúc rất rõ ràng, kỷ luật kiểm thử (validation harness) cực kỳ nghiêm ngặt. Hệ thống đã vượt qua toàn bộ các bài kiểm thử chuyên sâu trong `make test-deep` một cách trơn tru, chứng minh các tính năng cốt lõi (cooperative & preemptive scheduling, paging, exception panic handling, E820 memory mapping, frame allocator, fixed heap, ring-3 usermode, và syscall validation) hoạt động đúng như thiết kế chứng minh (scaffold proofs).

Tuy nhiên, đi sâu vào chi tiết mã nguồn, chúng tôi phát hiện một số **lỗ hổng bảo mật ẩn, lỗi thiết kế kiến trúc và giới hạn nghiêm trọng** có thể gây crash hệ thống hoặc cản trở việc mở rộng OS trong tương lai.

---

## 2. Các Lỗi Kiến trúc & Bug Phát hiện (Detailed Technical Findings)

### Bug 1: Rò rỉ Stack của Process gây cạn kiệt bộ nhớ (`process.c`)
- **Chi tiết:** Trong [process.c](file:///home/ngocnt/operating_system/os/kernel/process.c#L150-L155), hàm `process_destroy()` giải phóng process bằng cách đổi trạng thái sang `PROCESS_DEAD` và giảm `process_count`, nhưng **không hề thu hồi hoặc tái sử dụng vùng stack** đã cấp phát trong `stack_pool`. Hàm `allocate_stack()` chỉ đơn giản tăng biến toàn cục `stack_offset` một chiều.
- **Hệ quả:** Nếu hệ thống liên tục tạo và hủy các process (ngay cả khi số process đồng thời luôn rất ít), `stack_pool` (64KB) sẽ nhanh chóng bị cạn kiệt. Sau 16 lần tạo process, mọi lượt `process_create` tiếp theo sẽ thất bại vĩnh viễn vì `allocate_stack` trả về `NULL`.
- **Khuyến nghị:** Cần xây dựng cơ chế quản lý danh sách stack trống (free list) hoặc thu hồi stack khi process chết.

### Bug 2: Lỗ hổng kiểm tra con trỏ trong Syscall (`syscall.c`)
- **Chi tiết:** Trong [syscall.c](file:///home/ngocnt/operating_system/os/kernel/syscall.c#L23-L28), hàm `is_user_pointer_valid(ptr, size)` chỉ kiểm tra tính hợp lệ của trang đầu tiên (`ptr`) và trang cuối cùng (`ptr + size - 1`).
- **Hệ quả:** Nếu một buffer của user trải rộng trên 3 trang trở lên (ví dụ: kích thước 12KB) và trang ở giữa không được map (unmapped) hoặc là trang của kernel (supervisor-only), hàm kiểm tra vẫn trả về `1` (hợp lệ). Khi kernel xử lý buffer đó và chạm vào trang ở giữa, nó sẽ kích hoạt Page Fault ở chế độ Kernel-mode, gây crash/panic toàn bộ hệ thống.
- **Khuyến nghị:** Thay đổi logic để lặp qua tất cả các trang nằm trong khoảng từ `ptr` đến `ptr + size` để xác thực quyền truy cập của user.

### Bug 3: Nguy cơ Triple Fault do thiếu các Exception Handler mặc định (`idt.c`)
- **Chi tiết:** Trong [idt.c](file:///home/ngocnt/operating_system/os/kernel/idt.c#L137-L163), chỉ có các vector exception 0 (Div0), 6 (Invalid Opcode), 13 (GPF), 14 (Page Fault) là được đăng ký handler. Các descriptor còn lại từ 0-31 có flags bằng 0 (tức là Gate Descriptor không tồn tại/not present).
- **Hệ quả:** Nếu xảy ra bất kỳ exception nào khác ngoài 4 lỗi trên (ví dụ: Vector 8 - Double Fault, Vector 12 - Stack Fault), CPU sẽ không thể tải handler từ IDT, kích hoạt một GPF. Nếu lỗi xảy ra trong quá trình xử lý đó, CPU sẽ rơi vào vòng lặp Triple Fault và reset máy ảo QEMU lập tức mà không kịp in bất kỳ log panic nào qua COM1.
- **Khuyến nghị:** Đăng ký một Exception Stub mặc định (Default ISR) cho tất cả các vector từ 0 đến 31 chưa sử dụng để bắt và in log panic trước khi `hlt`.

### Bug 4: Xung đột chết người giữa Cooperative và Preemptive Scheduling (`scheduler.c` & `usermode.asm`)
- **Chi tiết:** 
  - Hàm `context_switch` trong [usermode.asm](file:///home/ngocnt/operating_system/os/kernel/usermode.asm#L41-L59) thực hiện chuyển ngữ cảnh dạng cooperative (chỉ push/pop `ebp, ebx, esi, edi` và kết thúc bằng `ret`).
  - Trong khi đó, preemptive scheduler hoạt động qua interrupt `isr_stub_32` lưu toàn bộ interrupt frame (segment registers + `pusha`) và quay về bằng `iretd`.
- **Hệ quả:** Nếu một preemptive process chủ động gọi `yield()`, `yield()` sẽ gọi `context_switch` để switch stack. Cấu trúc stack của preemptive process lúc này sẽ bị diễn dịch sai (các thanh ghi đã lưu trong interrupt frame bị pop nhầm thành `ebp/ebx/esi/edi` và `ret` sẽ nhảy vào một địa chỉ rác), dẫn đến crash hệ thống ngay lập tức.
- **Khuyến nghị:** Cấm hoàn toàn các preemptive process gọi `yield()`, hoặc xây dựng một cấu trúc stack đồng nhất cho cả hai loại tác vụ.

### Bug 5: Giới hạn không gian bộ nhớ của Paging Allocator dưới 4MB (`paging.c`)
- **Chi tiết:** Trong [paging.c](file:///home/ngocnt/operating_system/os/kernel/paging.c#L142-L144), hàm `paging_alloc_frame()` gọi `frame_alloc_below(0x00400000)`. Điều này ép buộc tất cả các Page Directory và Page Table vật lý phải nằm dưới ngưỡng 4MB để kernel có thể truy cập trực tiếp thông qua cơ chế identity-mapped (vùng 0-4MB).
- **Hệ quả:** Vùng nhớ vật lý dưới 4MB thực chất chỉ có khoảng 1MB trống dành cho Page Tables (sau khi trừ đi kernel, VGA, heap và stack). Giới hạn này giới hạn số lượng address spaces tối đa của toàn hệ thống ở mức khoảng ~128. Kernel không thể mở rộng số lượng process nếu không có kỹ thuật **Recursive Page Directory Mapping** (ánh xạ ngược thư mục trang).

### Bug 6: Lệnh `reboot` của Shell không thực sự reboot hệ thống (`shell.c`)
- **Chi tiết:** Lệnh `reboot` trong [shell.c](file:///home/ngocnt/operating_system/os/kernel/shell.c#L70-L73) gọi `int $0x03` (Breakpoint Exception) với mong muốn tạo ra một Triple Fault để khởi động lại máy ảo.
- **Hệ quả:** Vì Vector 3 không được đăng ký, nó sẽ kích hoạt GPF (Vector 13). Trình xử lý GPF hoạt động bình thường, in ra màn hình thông báo `KERNEL_PANIC:0x0000000D` rồi treo máy trong vòng lặp `hlt` vô tận. Người dùng gõ `reboot` sẽ chỉ thấy màn hình panic chứ máy không hề reboot.
- **Khuyến nghị:** Kích hoạt Triple Fault thực sự bằng cách tải một IDT rỗng (`lidt` với limit = 0) rồi gọi `int 3`, hoặc ghi lệnh reset qua PS/2 controller / ACPI.

---

## 3. Lộ trình Phát triển Tiếp theo (Roadmap - Dependency Order)

Để nâng cấp scaffold hiện tại thành một hệ điều hành thực dụng (usable OS) có khả năng chạy chương trình thực tế, chúng tôi đề xuất thứ tự ưu tiên phụ thuộc (dependency order) như sau:

```
[Block Device Driver] (ATA/VirtIO-blk)
         │
         ▼
[Virtual File System (VFS)] (File Descriptor Table)
         │
         ▼
[Simple Filesystem] (FAT12 hoặc Custom FS)
         │
         ▼
[ELF Executable Loader]
         │
         ▼
[Unix-like Process Lifecycle] (fork, exec, wait, exit)
         │
         ▼
[Userland Runtime (libc subset)]
```

### P0 — Nền tảng Lưu trữ (Storage Foundation)
1. **Block Device Driver:** Viết driver cho ATA/IDE (hoặc đơn giản hơn là `virtio-blk` / ramdisk) để cho phép đọc/ghi dữ liệu theo sector.
2. **VFS & File Descriptor Table:** Xây dựng trừu tượng hóa VFS cùng bảng quản lý File Descriptor cho mỗi process để hỗ trợ các syscall tiêu chuẩn: `open`, `read`, `write`, `close`.
3. **Filesystem Driver:** Cài đặt driver FAT12 hoặc một hệ thống file đơn giản để tổ chức tệp tin trên đĩa.

### P1 — Vòng đời Chương trình Người dùng (Userland Program Lifecycle)
4. **ELF Loader:** Thay vì chạy các function pointer có sẵn trong kernel, cần đọc tệp thực thi định dạng ELF từ đĩa, ánh xạ các segment `.text` và `.data` vào address space của user process, thiết lập user stack và nhảy vào entry point `_start`.
5. **Unix Process Model Syscalls:** Bổ sung các syscall:
   - `SYS_FORK`: Sao chép page directory của process cha (sử dụng Copy-On-Write nếu có thể).
   - `SYS_EXEC`: Thay thế image hiện tại bằng chương trình mới từ đĩa.
   - `SYS_EXIT` / `SYS_WAIT`: Đồng bộ hóa trạng thái kết thúc của các process cha-con.

### P2 — Trải nghiệm & Hoàn thiện (User Experience & Hardening)
6. **Userland Heap (`brk` / `mmap`):** Thay thế fixed heap của process hiện tại bằng các syscall cho phép mở rộng không gian nhớ động của userland.
7. **Minimal libc:** Đóng gói các syscall thành thư viện chuẩn C freestanding (malloc, free, printf, string.h) để dễ dàng viết các ứng dụng ring-3.
8. **Shell Utilities:** Viết các chương trình shell độc lập như `ls`, `cat`, `echo` chạy từ đĩa thay vì tích hợp cứng trong kernel code.
