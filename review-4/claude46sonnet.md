Sau khi đọc source, đây là ranking theo **dependency order + học được concept gì**:

---

**P0 — Foundation blocker (mọi thứ khác phụ thuộc vào đây)**

**1. Filesystem (VFS layer + driver đơn giản, FAT12 hoặc custom)**

Đây là gap lớn nhất tuyệt đối. Không có FS thì không có: persistent programs, shell utilities, ELF loader from disk, file I/O syscalls. Mọi thứ "thú vị" tiếp theo đều bị block bởi cái này. Concept quan trọng: VFS abstraction, block device layer, directory tree, inode.

**2. Expanded syscall ABI**

8 syscall hiện tại (`SYS_PUTS`, `SYS_ECHO`, ...) là test/demo syscalls, không phải functional ABI. Cần tối thiểu: `SYS_EXIT`, `SYS_OPEN/READ/WRITE/CLOSE`, `SYS_FORK` (hoặc `SYS_SPAWN`), `SYS_EXEC`, `SYS_WAIT`, `SYS_GETPID`. Không có cái này thì user program không làm được gì thực chất dù đã có ring-3.

**3. ELF loader**

Hiện tại process_create() nhận `entry_point` là function pointer trong kernel. Cần load ELF binary từ disk, map các segment vào address space đúng, jump vào `_start`. Phụ thuộc FS (để đọc file) nhưng có thể prototype với kernel-embedded ELF trước.

---

**P1 — Userland foundation (unlock real programs)**

**4. Dynamic heap / `brk`+`mmap` equivalent**

Fixed 1 MiB heap tại `0x200000–0x2FFFFF` sẽ bị hit ngay khi chạy bất kỳ program thực tế nào. User programs cần heap của riêng mình, không share với kernel heap.

**5. Fork/exec process lifecycle**

`process_create()` hiện là kernel-internal. Cần `SYS_FORK` (clone address space) + `SYS_EXEC` (replace image) để user program tạo child process — đây là core concept của UNIX process model.

**6. Minimal libc**

Không có libc, viết user program cực kỳ painful. Tối thiểu cần: `malloc/free` (user heap), `printf` wrapper over `SYS_PUTS`, `string.h` functions trong user space. Kernel đã có freestanding `string.c` nhưng không accessible từ ring-3.

---

**P2 — Completeness**

**7. IPC cơ bản (pipes + signals)**

Trước networking hay graphics. Pipes enable shell piping (`cmd1 | cmd2`), signals enable process control (`SIGKILL`, `SIGCHLD`). Quan trọng về mặt concept OS và unblock shell utilities.

**8. Security/permissions model**

UIDs/GIDs, file permission bits, setuid. Quan trọng nhưng cần FS trước.

**9. SMP (multi-core)**

Major architectural shift — scheduler phải lock-safe, per-CPU state, APIC thay vì PIC. Scheduler hiện có `irq_save/irq_restore` nhưng global state không SMP-safe. High complexity, high learning value.

**10. Networking (TCP/IP stack)**

Largely independent với FS path. E1000 NIC driver → Ethernet → ARP/IP → TCP → BSD sockets API. Rất phức tạp, nhưng không block bất kỳ feature nào khác.

---

**P3 — Advanced / self-hosting path**

**11. Modular driver model**

Hiện tại tất cả driver compiled thẳng vào kernel. Driver framework + loadable modules quan trọng hơn về mặt architecture nhưng có thể defer.

**12. Graphics (framebuffer/VBE)**

VGA text mode đủ cho teaching OS. Framebuffer → GUI là một con đường dài riêng.

**13. Shell utilities + compiler toolchain**

`ls`, `cat`, `cp` phụ thuộc FS. Compiler/linker on-device là endpoint của self-hosting — cần gần như mọi thứ trên trước.

---

**Tóm lại dependency graph:**

```
FS → ELF loader ──────┐
                       ├→ fork/exec → shell utilities
Syscall ABI ──────────┘
                      
Dynamic heap → libc → user programs thực tế

IPC → shell piping
SMP → production scheduler
Networking → distributed apps
```

Nếu mục tiêu là "teaching OS hoàn chỉnh nhất trong thời gian ngắn nhất": **FS + Syscall ABI + ELF loader** là chuỗi unlock nhiều nhất.