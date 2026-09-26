/* SPDX-License-Identifier: MPL-2.0 */
/*
 * One-shot RX3 framebuffer conversion through the i.MX6 IPU.
 *
 * This is freestanding so the resulting ARM EABI executable has no dependency
 * on the RX3's glibc. It reads framebuffer metadata, gives the active scanout
 * page to the IPU as RGB565 input, and writes a private 640x400 I420 frame.
 */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long size_t;

#define NULL ((void *)0)

#define SYS_EXIT 1
#define SYS_WRITE 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_IOCTL 54
#define SYS_MUNMAP 91
#define SYS_MMAP2 192

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FB_VMODE_YWRAP 256u

#define IPU_CHECK_TASK 0xc0884901u
#define IPU_QUEUE_TASK 0x40884902u
#define IPU_ALLOC 0xc0044903u
#define IPU_FREE 0x40044904u

#define IPU_TASK_ID_ANY 0
#define IPU_ROTATE_NONE 0

#define PAGE_SIZE 4096u
#define OUTPUT_WIDTH 640u
#define OUTPUT_HEIGHT 400u
#define OUTPUT_FRAME_BYTES (OUTPUT_WIDTH * OUTPUT_HEIGHT * 3u / 2u)
#define OUTPUT_ALLOC_BYTES ((OUTPUT_FRAME_BYTES + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u))

#define FOURCC(a, b, c, d) \
    ((u32)(a) | ((u32)(b) << 8) | ((u32)(c) << 16) | ((u32)(d) << 24))
#define IPU_PIX_FMT_RGB565 FOURCC('R', 'G', 'B', 'P')
#define IPU_PIX_FMT_YUV420P FOURCC('I', '4', '2', '0')

struct fb_bitfield {
    u32 offset;
    u32 length;
    u32 msb_right;
};

struct fb_var_screeninfo {
    u32 xres;
    u32 yres;
    u32 xres_virtual;
    u32 yres_virtual;
    u32 xoffset;
    u32 yoffset;
    u32 bits_per_pixel;
    u32 grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    u32 nonstd;
    u32 activate;
    u32 height;
    u32 width;
    u32 accel_flags;
    u32 pixclock;
    u32 left_margin;
    u32 right_margin;
    u32 upper_margin;
    u32 lower_margin;
    u32 hsync_len;
    u32 vsync_len;
    u32 sync;
    u32 vmode;
    u32 rotate;
    u32 reserved[5];
};

struct fb_fix_screeninfo {
    char id[16];
    u32 smem_start;
    u32 smem_len;
    u32 type;
    u32 type_aux;
    u32 visual;
    u16 xpanstep;
    u16 ypanstep;
    u16 ywrapstep;
    u16 padding;
    u32 line_length;
    u32 mmio_start;
    u32 mmio_len;
    u32 accel;
    u16 capabilities;
    u16 reserved[2];
    u16 tail_padding;
};

struct ipu_pos {
    u32 x;
    u32 y;
};

struct ipu_crop {
    struct ipu_pos pos;
    u32 w;
    u32 h;
};

struct ipu_deinterlace {
    u8 enable;
    u8 motion;
    u8 field_fmt;
    u8 padding;
};

struct ipu_input {
    u32 width;
    u32 height;
    u32 format;
    struct ipu_crop crop;
    u32 paddr;
    struct ipu_deinterlace deinterlace;
    u32 paddr_n;
};

struct ipu_alpha {
    u8 mode;
    u8 gvalue;
    u16 padding;
    u32 loc_alp_paddr;
};

struct ipu_colorkey {
    u8 enable;
    u8 padding[3];
    u32 value;
};

struct ipu_overlay {
    u32 width;
    u32 height;
    u32 format;
    struct ipu_crop crop;
    struct ipu_alpha alpha;
    struct ipu_colorkey colorkey;
    u32 paddr;
};

struct ipu_output {
    u32 width;
    u32 height;
    u32 format;
    u8 rotate;
    u8 padding[3];
    struct ipu_crop crop;
    u32 paddr;
};

struct ipu_task {
    struct ipu_input input;
    struct ipu_output output;
    u8 overlay_en;
    u8 overlay_padding[3];
    struct ipu_overlay overlay;
    u8 priority;
    u8 task_id;
    u16 task_padding;
    s32 timeout;
};

typedef char assert_var_size[(sizeof(struct fb_var_screeninfo) == 160) ? 1 : -1];
typedef char assert_fix_size[(sizeof(struct fb_fix_screeninfo) == 68) ? 1 : -1];
typedef char assert_task_size[(sizeof(struct ipu_task) == 136) ? 1 : -1];

static inline long syscall1(long number, long arg0)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static inline long syscall2(long number, long arg0, long arg1)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    register long r1 __asm__("r1") = arg1;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r7) : "memory");
    return r0;
}

static inline long syscall3(long number, long arg0, long arg1, long arg2)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    register long r1 __asm__("r1") = arg1;
    register long r2 __asm__("r2") = arg2;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static inline long syscall6(long number, long arg0, long arg1, long arg2,
                            long arg3, long arg4, long arg5)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    register long r1 __asm__("r1") = arg1;
    register long r2 __asm__("r2") = arg2;
    register long r3 __asm__("r3") = arg3;
    register long r4 __asm__("r4") = arg4;
    register long r5 __asm__("r5") = arg5;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7)
                     : "memory");
    return r0;
}

static size_t string_length(const char *value)
{
    size_t length = 0;

    while (value[length])
        length++;

    return length;
}

static int write_all(int fd, const void *buffer, u32 length)
{
    const u8 *cursor = buffer;

    while (length) {
        long written = syscall3(SYS_WRITE, fd, (long)cursor, length);

        if (written <= 0)
            return -1;

        cursor += written;
        length -= (u32)written;
    }

    return 0;
}

static void message(const char *value)
{
    write_all(2, value, (u32)string_length(value));
}

static void clear_bytes(void *buffer, u32 length)
{
    u8 *bytes = buffer;

    while (length--)
        *bytes++ = 0;
}

static int failed(long result)
{
    return result < 0 && result >= -4095;
}

static int capture(const char *output_path)
{
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo variable;
    struct ipu_task task;
    u32 output_address = OUTPUT_ALLOC_BYTES;
    u32 bytes_per_pixel;
    u32 visible_offset;
    u32 visible_last_byte;
    long framebuffer_fd = -1;
    long ipu_fd = -1;
    long output_fd = -1;
    long mapped = -1;
    int output_allocated = 0;
    int result = 1;

    clear_bytes(&fixed, sizeof(fixed));
    clear_bytes(&variable, sizeof(variable));
    clear_bytes(&task, sizeof(task));

    framebuffer_fd = syscall3(SYS_OPEN, (long)"/dev/fb0", O_RDONLY, 0);
    if (failed(framebuffer_fd)) {
        message("rx3-ipu-capture: cannot open /dev/fb0\n");
        goto done;
    }

    if (failed(syscall3(SYS_IOCTL, framebuffer_fd,
                        FBIOGET_FSCREENINFO, (long)&fixed)) ||
        failed(syscall3(SYS_IOCTL, framebuffer_fd,
                        FBIOGET_VSCREENINFO, (long)&variable))) {
        message("rx3-ipu-capture: framebuffer metadata ioctl failed\n");
        goto done;
    }

    if (variable.bits_per_pixel != 16 ||
        variable.red.length != 5 || variable.red.offset != 11 ||
        variable.green.length != 6 || variable.green.offset != 5 ||
        variable.blue.length != 5 || variable.blue.offset != 0 ||
        variable.xres == 0 || variable.yres == 0 ||
        variable.xres_virtual > 8192u || variable.yres_virtual > 8192u ||
        (variable.vmode & FB_VMODE_YWRAP) != 0 ||
        ((variable.xres | variable.yres |
          variable.xoffset | variable.yoffset) & 7u) != 0 ||
        variable.xres > variable.xres_virtual ||
        variable.yres > variable.yres_virtual ||
        fixed.line_length == 0 ||
        fixed.line_length != variable.xres_virtual * 2u) {
        message("rx3-ipu-capture: framebuffer is not packed RGB565\n");
        goto done;
    }

    bytes_per_pixel = variable.bits_per_pixel / 8u;
    if (variable.yoffset + variable.yres < variable.yoffset ||
        variable.yoffset + variable.yres > variable.yres_virtual ||
        variable.xoffset + variable.xres < variable.xoffset ||
        variable.xoffset + variable.xres > variable.xres_virtual) {
        message("rx3-ipu-capture: invalid active framebuffer geometry\n");
        goto done;
    }

    visible_offset = variable.yoffset * fixed.line_length +
                     variable.xoffset * bytes_per_pixel;
    visible_last_byte = visible_offset +
        (variable.yres - 1u) * fixed.line_length +
        variable.xres * bytes_per_pixel;

    if (visible_last_byte < visible_offset || visible_last_byte > fixed.smem_len ||
        fixed.smem_start + visible_offset < fixed.smem_start) {
        message("rx3-ipu-capture: active page exceeds framebuffer allocation\n");
        goto done;
    }

    ipu_fd = syscall3(SYS_OPEN, (long)"/dev/mxc_ipu", O_RDWR, 0);
    if (failed(ipu_fd)) {
        message("rx3-ipu-capture: cannot open /dev/mxc_ipu\n");
        goto done;
    }

    if (failed(syscall3(SYS_IOCTL, ipu_fd, IPU_ALLOC, (long)&output_address))) {
        message("rx3-ipu-capture: IPU_ALLOC failed\n");
        goto done;
    }
    output_allocated = 1;

    mapped = syscall6(SYS_MMAP2, 0, OUTPUT_ALLOC_BYTES,
                      PROT_READ | PROT_WRITE, MAP_SHARED, ipu_fd,
                      output_address / PAGE_SIZE);
    if (failed(mapped)) {
        message("rx3-ipu-capture: output mmap failed\n");
        goto done;
    }

    task.input.width = variable.xres_virtual;
    task.input.height = variable.yres_virtual;
    task.input.format = IPU_PIX_FMT_RGB565;
    task.input.crop.pos.x = variable.xoffset;
    task.input.crop.pos.y = variable.yoffset;
    task.input.crop.w = variable.xres;
    task.input.crop.h = variable.yres;
    task.input.paddr = fixed.smem_start;

    task.output.width = OUTPUT_WIDTH;
    task.output.height = OUTPUT_HEIGHT;
    task.output.format = IPU_PIX_FMT_YUV420P;
    task.output.rotate = IPU_ROTATE_NONE;
    task.output.crop.w = OUTPUT_WIDTH;
    task.output.crop.h = OUTPUT_HEIGHT;
    task.output.paddr = output_address;
    task.task_id = IPU_TASK_ID_ANY;

    if (syscall3(SYS_IOCTL, ipu_fd, IPU_CHECK_TASK, (long)&task) != 0) {
        message("rx3-ipu-capture: IPU_CHECK_TASK rejected conversion\n");
        goto done;
    }

    if (failed(syscall3(SYS_IOCTL, ipu_fd, IPU_QUEUE_TASK, (long)&task))) {
        message("rx3-ipu-capture: IPU_QUEUE_TASK failed\n");
        goto done;
    }

    output_fd = syscall3(SYS_OPEN, (long)output_path,
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (failed(output_fd) || write_all((int)output_fd, (void *)mapped,
                                       OUTPUT_FRAME_BYTES) < 0) {
        message("rx3-ipu-capture: cannot write I420 output\n");
        goto done;
    }

    message("rx3-ipu-capture: wrote 640x400 I420 frame\n");
    result = 0;

done:
    if (!failed(output_fd) && output_fd >= 0)
        syscall1(SYS_CLOSE, output_fd);
    if (!failed(mapped) && mapped >= 0)
        syscall2(SYS_MUNMAP, mapped, OUTPUT_ALLOC_BYTES);
    if (!failed(ipu_fd) && ipu_fd >= 0 && output_allocated)
        syscall3(SYS_IOCTL, ipu_fd, IPU_FREE, (long)&output_address);
    if (!failed(ipu_fd) && ipu_fd >= 0)
        syscall1(SYS_CLOSE, ipu_fd);
    if (!failed(framebuffer_fd) && framebuffer_fd >= 0)
        syscall1(SYS_CLOSE, framebuffer_fd);

    return result;
}

__attribute__((used)) static int program_main(int argc, char **argv)
{
    const char *output_path = "/tmp/rx3-ipu-frame.i420";

    if (argc > 1)
        output_path = argv[1];

    return capture(output_path);
}

__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile(
        "ldr r0, [sp]\n"
        "add r1, sp, #4\n"
        "bl program_main\n"
        "mov r7, #1\n"
        "svc 0\n"
    );
}
