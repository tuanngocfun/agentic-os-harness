Có. Nếu rank **các feature quan trọng chưa implement** cho repo OS này, mình sẽ xếp như sau.

## Ranking theo impact thực tế

|   Rank | Feature thiếu                                | Vì sao quan trọng                                                                                                                                                                                                                        |
| -----: | -------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
|  **0** | **Core stress + static hardening**           | Trước khi mở rộng breadth, repo tự nói nên giữ các gate hiện tại xanh dưới stress/static review. Trong `harness_profile.yaml:142-152`, next task là core stress, còn filesystem/networking/graphics bị đưa vào `forbidden_next_breadth`. |
|  **1** | **Persistent storage + block device driver** | Không có disk I/O thật thì filesystem cũng chưa có nền. Cần ít nhất ATA/IDE hoặc virtio-blk/ramdisk để đọc/ghi block.                                                                                                                    |
|  **2** | **Filesystem / VFS**                         | Đây là feature thiếu quan trọng nhất để thành usable OS. `filesystem: not_started` trong `harness_profile.yaml:55`. Không có FS thì không lưu file, không load program, không có config, không có shell utilities thực tế.               |
|  **3** | **Executable loader**                        | Cần ELF loader hoặc format đơn giản để load chương trình userland từ disk. Hiện có ring-3 tests, nhưng chưa có mô hình “chạy binary từ filesystem”.                                                                                      |
|  **4** | **Richer syscall ABI + file descriptors**    | `include/syscall.h` chỉ có 8 syscall numbers, trong đó vài cái là test marker. Cần `open/read/write/close`, `stat`, `exit`, `wait`, `spawn/exec`, memory syscalls.                                                                       |
|  **5** | **Real process lifecycle**                   | Hiện `MAX_PROCESSES 16`, có process scaffold, scheduler, CR3 isolation proof. Nhưng chưa có process tree, exit status, wait, blocking sleep, fd table, program image lifecycle.                                                          |
|  **6** | **Production-grade virtual memory**          | Repo đã có paging/address-space isolation gate, nhưng `AGENTS.md:76` vẫn nói chưa prove production-grade VM. Cần page fault handler tốt hơn, user/kernel copy discipline, demand allocation, guard pages, mmap/brk, maybe COW later.     |
|  **7** | **Dynamic kernel memory management**         | Hiện heap fixed 1 MiB tại `0x00200000–0x2FFFFF`. Có allocator test, nhưng chưa phải kernel heap trưởng thành tích hợp với frame allocator, slab/buddy, growth, fragmentation policy.                                                     |
|  **8** | **Userland runtime / libc subset**           | Muốn self-host hoặc viết app tử tế cần libc-like layer: startup code, syscall wrappers, malloc/free userland, printf, string, errno.                                                                                                     |
|  **9** | **Shell utilities + command execution**      | Hiện shell chủ yếu là test shell. Cần command execution từ FS, PATH-like lookup, redirection, pipes nếu muốn giống OS thật.                                                                                                              |
| **10** | **Security model**                           | Hiện có ring-3/syscall negative path/address-space tests, nhưng chưa có users, permissions, ACL/capabilities, file permission, process privilege model.                                                                                  |
| **11** | **Networking**                               | `networking: not_started` trong `harness_profile.yaml:56`. Quan trọng, nhưng sau FS/userland/syscall vì networking cần driver, buffers, sockets, timers, security.                                                                       |
| **12** | **SMP / multicore support**                  | Quan trọng cho production, nhưng chưa cần cho teaching OS giai đoạn này. Khi có SMP thì scheduler, allocator, frame allocator, syscalls đều phải có locking.                                                                             |
| **13** | **Graphics mode / GUI**                      | Nice-to-have. Repo cũng đưa `graphics_mode` vào forbidden breadth hiện tại. Không nên làm sớm nếu mục tiêu là OS core đúng.                                                                                                              |
| **14** | **Self-hosting toolchain**                   | Đây là milestone rất xa: compiler, assembler, linker, make/build tools chạy trong OS. Muốn tới đây phải có FS, ELF loader, syscalls, libc, shell utilities trước.                                                                        |

## Nếu chọn top 5 nên làm tiếp

Mình sẽ chọn thứ tự này:

```text
0. Core stress/static hardening
1. Block device / persistent storage
2. Minimal filesystem + VFS
3. ELF executable loader
4. File descriptor syscall layer: open/read/write/close/exit/wait/spawn
5. Userland runtime + shell command execution
```

Lý do mình đặt **core stress/static hardening ở rank 0**: vì repo hiện đang có nhiều proof rất tốt, nhưng nếu thêm filesystem/networking quá sớm thì rất dễ tạo “feature demo” mà phá nền tảng. Chính repo cũng nói current next work là giữ core gates xanh trước khi thêm breadth.

## Ranking nếu mục tiêu là “self-hosting OS”

Nếu mục tiêu cuối là tự build chính nó, ranking phải đổi một chút:

```text
1. Persistent storage + filesystem
2. ELF loader / executable format loader
3. File I/O syscalls + file descriptors
4. Process lifecycle: spawn/exec/exit/wait
5. Userland libc subset
6. Shell utilities
7. Toolchain port: assembler, linker, compiler, make
8. Memory management strong enough for toolchain workload
```

Networking, graphics, SMP đều **không phải blocker trực tiếp** cho self-hosting. Filesystem + loader + syscalls + libc mới là blocker thật.

## Đánh giá thẳng

Feature thiếu nghiêm trọng nhất hiện tại **không phải networking hay graphics**. Thiếu nghiêm trọng nhất là:

> **OS chưa có persistent program world.**

Tức là chưa có thế giới nơi user có thể lưu file, load binary, chạy program, program gọi syscall file I/O, shell execute command, rồi program kết thúc với exit code.

Hiện tại project đã có “early kernel mechanics” khá tốt. Nhưng để bước từ **kernel scaffold** sang **usable OS**, đường critical path là:

```text
storage → filesystem → executable loader → syscalls/file descriptors → userland runtime → real shell utilities
```

Đây mới là trục quan trọng nhất.
