/* SPDX-License-Identifier: MPL-2.0
 * Guarded ARM inline hooks shared by firmware-specific preload modules.
 *
 * The including translation unit supplies the minimal libc declarations and
 * constants used below. RX3_INSTALL_HOOK publishes the trampoline through the
 * caller's correctly typed function pointer before executable code changes,
 * so a concurrent first call can always reach the original implementation.
 */

#ifndef RX3_INLINE_HOOK_H
#define RX3_INLINE_HOOK_H

#define RX3_ARM_LDR_PC_LITERAL 0xe51ff004u

struct installed_hook {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
    int active;
};

static void clear_instruction_cache(unsigned long first, unsigned long last)
{
    register unsigned long r0 __asm__("r0") = first;
    register unsigned long r1 __asm__("r1") = last;
    register unsigned long r2 __asm__("r2") = 0;
    register unsigned long r7 __asm__("r7") = 0x0f0002u;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
}

static int write_code(unsigned long address, const void *bytes, size_t length)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned long mask;
    unsigned long first;
    unsigned long last;
    size_t span;

    if (page_size <= 0)
        page_size = 4096;
    mask = (unsigned long)page_size - 1u;
    first = address & ~mask;
    last = (address + length - 1u) & ~mask;
    span = (size_t)(last - first) + (size_t)page_size;

    if (mprotect((void *)first, span, PROT_READ | PROT_WRITE))
        return -1;
    memcpy((void *)address, bytes, length);
    clear_instruction_cache(address, address + length);
    return mprotect((void *)first, span, PROT_READ | PROT_EXEC);
}

static void uninstall_hook(struct installed_hook *hook)
{
    if (!hook->address)
        return;
    if (hook->active)
        (void)write_code(hook->address, hook->original,
                         sizeof(hook->original));
    if (hook->trampoline)
        (void)munmap(hook->trampoline, 4096);
    memset(hook, 0, sizeof(*hook));
}

static void *prepare_hook(struct installed_hook *hook, unsigned long address,
                          const uint8_t guard[8])
{
    uint32_t *trampoline;

    if (memcmp((const void *)address, guard, 8))
        return 0;
    trampoline = mmap(0, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;

    memcpy(trampoline, (const void *)address, 8);
    trampoline[2] = RX3_ARM_LDR_PC_LITERAL;
    trampoline[3] = (uint32_t)(address + 8u);
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 16u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        (void)munmap(trampoline, 4096);
        return 0;
    }

    hook->address = address;
    memcpy(hook->original, guard, sizeof(hook->original));
    hook->trampoline = trampoline;
    hook->active = 0;
    return trampoline;
}

static int activate_hook(struct installed_hook *hook, void *replacement)
{
    uint32_t patch[2] = {
        RX3_ARM_LDR_PC_LITERAL,
        (uint32_t)(unsigned long)replacement,
    };

    if (!hook->address || !hook->trampoline)
        return -1;
    if (write_code(hook->address, patch, sizeof(patch))) {
        /* write_code may have copied the patch before a final mprotect failed.
         * Mark it active so teardown attempts to restore the guarded bytes. */
        hook->active = 1;
        uninstall_hook(hook);
        return -1;
    }
    hook->active = 1;
    return 0;
}

#define RX3_INSTALL_PREPARED_HOOK(prepare, original, hook, address, guard,     \
                                  replacement)                                 \
    __extension__ ({                                                           \
        void *_rx3_trampoline = (prepare)((hook), (address), (guard));          \
        int _rx3_installed = 0;                                                 \
        (original) = (__typeof__(original))_rx3_trampoline;                     \
        if (_rx3_trampoline) {                                                  \
            if (!activate_hook((hook), (void *)(replacement)))                 \
                _rx3_installed = 1;                                             \
            else                                                                \
                (original) = 0;                                                 \
        }                                                                       \
        _rx3_installed;                                                         \
    })

#define RX3_INSTALL_HOOK(original, hook, address, guard, replacement)          \
    RX3_INSTALL_PREPARED_HOOK(prepare_hook, original, hook, address, guard,     \
                              replacement)

#endif /* RX3_INLINE_HOOK_H */
