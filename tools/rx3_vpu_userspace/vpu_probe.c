/* SPDX-License-Identifier: MPL-2.0 */

#include "vpu_lib.h"

typedef unsigned int u32;

#define SYS_EXIT 1
#define SYS_WRITE 4

static inline long syscall1(long number, long arg0)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory");
    return r0;
}

static inline long syscall3(long number, long arg0, long arg1, long arg2)
{
    register long r7 __asm__("r7") = number;
    register long r0 __asm__("r0") = arg0;
    register long r1 __asm__("r1") = arg1;
    register long r2 __asm__("r2") = arg2;
    __asm__ volatile("svc 0" : "+r"(r0)
                     : "r"(r1), "r"(r2), "r"(r7) : "memory");
    return r0;
}

static unsigned int append_text(char *buffer, unsigned int offset,
                                const char *text)
{
    while (*text)
        buffer[offset++] = *text++;

    return offset;
}

static unsigned int append_number(char *buffer, unsigned int offset, u32 value)
{
    char reversed[10];
    unsigned int length = 0;

    do {
        reversed[length++] = '0' + value % 10;
        value /= 10;
    } while (value);

    while (length)
        buffer[offset++] = reversed[--length];

    return offset;
}

static void write_text(const char *text)
{
    unsigned int length = 0;

    while (text[length])
        length++;

    syscall3(SYS_WRITE, 2, (long)text, length);
}

__attribute__((used, noinline)) static int probe(void)
{
    vpu_versioninfo version;
    RetCode result = vpu_Init(0);

    if (result != RETCODE_SUCCESS) {
        write_text("rx3-vpu-probe: vpu_Init failed\n");
        return 1;
    }

    result = vpu_GetVersionInfo(&version);
    if (result == RETCODE_SUCCESS) {
        char line[160];
        unsigned int offset = 0;

        offset = append_text(line, offset, "rx3-vpu-probe: firmware ");
        offset = append_number(line, offset, version.fw_major);
        offset = append_text(line, offset, ".");
        offset = append_number(line, offset, version.fw_minor);
        offset = append_text(line, offset, ".");
        offset = append_number(line, offset, version.fw_release);
        offset = append_text(line, offset, " code ");
        offset = append_number(line, offset, version.fw_code);
        offset = append_text(line, offset, ", library ");
        offset = append_number(line, offset, version.lib_major);
        offset = append_text(line, offset, ".");
        offset = append_number(line, offset, version.lib_minor);
        offset = append_text(line, offset, ".");
        offset = append_number(line, offset, version.lib_release);
        line[offset++] = '\n';
        syscall3(SYS_WRITE, 2, (long)line, offset);
    } else {
        write_text("rx3-vpu-probe: vpu_GetVersionInfo failed\n");
    }

    vpu_UnInit();
    return result == RETCODE_SUCCESS ? 0 : 1;
}

__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile(
        "bl probe\n"
        "mov r7, #1\n"
        "svc 0\n"
    );
}
