# NothanOS

Hệ điều hành viết từ đầu cho ARMv7-A, chạy trên BeagleBone Black (TI AM335x, Cortex-A8).

Bootloader, kernel, thư viện C và ứng dụng đều do Vinalinux tự viết, không dựa trên Linux hay BSD. Bản hiện tại chạy một thiết bị điện thoại để bàn: màn hình cảm ứng HDMI, gọi thoại và nhắn tin SMS qua module SIM7600CE.

## Tính năng

- Monolithic kernel, MMU bật, mỗi tiến trình có không gian địa chỉ riêng
- Scheduler ưu tiên cố định, preemptive, O(1), 32 mức
- Tạo tiến trình bằng `spawn` (không có `fork`)
- Bộ nhớ: buddy allocator, slab, page table theo tiến trình
- VFS với FAT32 trên thẻ SD và devfs
- Driver: HDMI (LCDC + TDA19988), USB host (cảm ứng HID), SD/MMC, UART, I2C, GPIO
- Bootloader riêng thay U-Boot
- GUI trên LVGL, điều khiển modem qua AT command

## Kiến trúc

```text
╔═ USER SPACE ═════════════════════════════════════════════════════════════════════════════════╗
║ ┌─────────────────────┐ ┌────────────────────┐ ┌────────────────────┐ ┌────────────────────┐ ║
║ │         GUI         │ │    Phone Daemon    │ │   Storage Daemon   │ │       Shell        │ ║
║ │  LVGL · Phone · SMS │ │    modem owner     │ │background SD writes│ │   admin console    │ ║
║ └─────────────────────┘ └────────────────────┘ └────────────────────┘ └────────────────────┘ ║
║ ┌──────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │               nothanlibc — crt0 · syscall wrappers · printf · string · mem               │ ║
║ └──────────────────────────────────────────────────────────────────────────────────────────┘ ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
╔═ KERNEL SPACE ═══════════════════════════════════════════════════════════════════════════════╗
║ ┌──────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │         System Call Interface — SVC · 19 syscalls · uaccess checks user pointers         │ ║
║ └──────────────────────────────────────────────────────────────────────────────────────────┘ ║
║ ┌─────────────────────┐ ┌────────────────────┐ ┌────────────────────┐ ┌────────────────────┐ ║
║ │      Scheduler      │ │  Process Manager   │ │   Memory Manager   │ │Virtual File System │ ║
║ │    32 prio · O(1)   │ │spawn · exit · kill │ │ buddy·slab·pgtable │ │ FAT32·devfs·block  │ ║
║ └─────────────────────┘ └────────────────────┘ └────────────────────┘ └────────────────────┘ ║
║ ┌────────────────────────────────────────────┐ ┌───────────────────────────────────────────┐ ║
║ │                IPC Channels                │ │                Driver Model               │ ║
║ │           phonebus · storagebus            │ │       platform bus · probe() · cdev       │ ║
║ └────────────────────────────────────────────┘ └───────────────────────────────────────────┘ ║
║ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ║
║ │   Display   │ │   USB host  │ │   SD / MMC  │ │    UART    │ │ I2C · GPIO │ │ INTC·Timer │ ║
║ │  LCDC·HDMI  │ │  HID touch  │ │    HSMMC    │ │UART0·UART1 │ │  pinctrl   │ │ tick 10 ms │ ║
║ └─────────────┘ └─────────────┘ └─────────────┘ └────────────┘ └────────────┘ └────────────┘ ║
║ ┌──────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │                ARM — exception vectors · MMU · context switch · board-bbb                │ ║
║ └──────────────────────────────────────────────────────────────────────────────────────────┘ ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
╔═ HARDWARE ═══════════════════════════════════════════════════════════════════════════════════╗
║ ┌──────────────────────────────────────────────────────────────────────────────────────────┐ ║
║ │                                     Bootloader (MLO)                                     │ ║
║ │                   clock 1 GHz · UART · DDR3 init · load kernel from SD                   │ ║
║ └──────────────────────────────────────────────────────────────────────────────────────────┘ ║
║ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌────────────┐ ┌────────────┐ ┌────────────┐ ║
║ │  Cortex-A8  │ │  DDR3 512MB │ │   SD card   │ │ SIM7600CE  │ │ USB touch  │ │    HDMI    │ ║
║ └─────────────┘ └─────────────┘ └─────────────┘ └────────────┘ └────────────┘ └────────────┘ ║
╚══════════════════════════════════════════════════════════════════════════════════════════════╝
```

| Thành phần | Thư mục |
| --- | --- |
| Bootloader | `bootloader/` |
| Kiến trúc ARM, MMU, board data | `nothan-kernel/arch/arm/` |
| Syscall, process, scheduler, VFS, filesystem | `nothan-kernel/kernel/` |
| Memory manager | `nothan-kernel/mm/` |
| Driver model và driver | `nothan-kernel/drivers/` |
| GUI, daemon, shell | `userspace/{gui,phone_daemon,storage_daemon,sh}/` |
| Thư viện C | `userspace/lib/` |

Ứng dụng giao tiếp với nhau qua `phonebus` và `storagebus`: hai kênh IPC dạng character device trong kernel.

## Phần cứng

- BeagleBone Black Rev.C (AM3358, 512 MB DDR3)
- Thẻ microSD ≥ 128 MB
- USB-to-Serial 3.3V TTL nối UART0
- Màn hình HDMI 800×480 có cảm ứng USB
- Module SIM7600CE nối UART1 (tuỳ chọn, cần cho gọi/SMS)

## Bắt đầu

### Cài toolchain

Yêu cầu Ubuntu 22.04 trở lên.

```bash
sudo bash scripts/setup-environment.sh
```

Chỉ dùng `arm-none-eabi-gcc`, không dùng `arm-linux-gnueabihf-gcc`.

### Build

```bash
make
```

Kết quả gồm `bootloader/MLO` và `nothan-kernel/build/kernel.bin` (đã nhúng sẵn userspace). Có thể build riêng từng phần bằng `make bootloader`, `make kernel` hoặc `make userspace`.

### Flash thẻ SD

```bash
sudo ./scripts/setup-sdcard.sh /dev/sdX       # lần đầu
sudo ./scripts/deploy-and-flash.sh /dev/sdX
```

Script ghi thẳng vào sector của thẻ, vì vậy cần kiểm tra đúng device bằng `lsblk` trước khi chạy.

### Boot

```bash
screen /dev/ttyUSB0 115200
```

Cắm thẻ vào board, giữ nút BOOT rồi cấp nguồn. Khi khởi động xong, log in ra `[KERN] NothanOS started` và hiện shell.

### Chạy GUI trên máy host

```bash
sudo apt install libsdl2-dev
make -C userspace/sim && ./userspace/sim/nothan-sim
```

Lệnh này build GUI chạy trên SDL, có giả lập modem.

## Đóng góp

- Không copy, port hay paraphrase code từ upstream nào, kể cả Linux, BSD hay musl.
- Driver không hardcode địa chỉ hoặc IRQ. Dữ liệu riêng của board đặt trong `arch/arm/mach-omap2/board-bbb.c`, mọi khởi tạo phần cứng đặt trong `probe()`.
- Code style: `gnu11`, indent bằng tab, brace kiểu K&R.
- Commit message viết bằng tiếng Anh, theo dạng `Type(scope): summary`, ví dụ `Fix(mmc): ...`.

## Thư viện bên thứ ba

- [LVGL](https://lvgl.io) 9.5, giấy phép MIT

## License

[Prosperity Public License 3.0.0](LICENSE). Muốn dùng cho mục đích thương mại quá 30 ngày thì cần [giấy phép thương mại](COMMERCIAL-LICENSE.md).
