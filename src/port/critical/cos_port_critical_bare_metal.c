/**
 * @file cos_port_critical_bare_metal.c
 * @brief Bare metal critical section implementation
 *
 * Uses platform-specific interrupt disable/enable primitives.
 * On ARM Cortex-M: __disable_irq() / __enable_irq() (CMSIS)
 * On x86:          cli / sti (inline assembly)
 * On others:       no-op stubs (user must provide their own)
 *
 * For proper nesting support, users should modify this file
 * to save/restore the previous interrupt state using their
 * MCU's specific primitives (e.g. __get_PRIMASK / __set_PRIMASK
 * on Cortex-M).
 *
 * NOTE: On bare metal, ensure interrupts are disabled for the
 * shortest possible time to avoid missing time-critical events.
 */

#include "cos_config.h"

#if COS_RTOS_TYPE == COS_RTOS_BARE_METAL

#include "cos_port_critical.h"
#include "cos_port.h"

/* Platform detection ----------------------------------------*/
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_7EM__) || defined(__ARMCC_VERSION) \
    || defined(__ARM_COMPILER_VERSION) || defined(__arm__)
#define COS_BAREMETAL_ARM 1
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64)
#define COS_BAREMETAL_X86 1
#else
#define COS_BAREMETAL_GENERIC 1
#endif

cos_critical_ctx_t cos_critical_enter(void)
{
#if defined(COS_SIMULATOR)
    /* Host desktop simulator (Windows/Linux SDL): single-threaded cooperative
     * environment. There are NO real interrupts to disable, and emitting the
     * bare-metal `cli` here is an illegal privileged instruction in user mode
     * (STATUS_PRIVILEGED_INSTRUCTION / 0xc0000096). The only callers are the
     * dispatcher queue plus the sensor/battery services, all of which run on
     * the one LVGL/main thread, so a no-op critical section is correct. */
    return 0;
#elif COS_BAREMETAL_ARM
    uint32_t primask = 0;
#if defined(__ARM_ARCH_6M__)
    __asm volatile("mrs %0, primask" : "=r"(primask));
    __asm volatile("cpsid i" ::: "memory");
#else
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask)::"memory");
#endif
    return primask;
#elif COS_BAREMETAL_X86
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile("cli" ::: "memory");
#endif
    return 1;
#else
    return 0;
#endif
}

void cos_critical_leave(cos_critical_ctx_t ctx)
{
#if defined(COS_SIMULATOR)
    (void)ctx;
#elif COS_BAREMETAL_ARM
    if (ctx == 0)
        return;
#if defined(__ARM_ARCH_6M__)
    __asm volatile("cpsie i" ::: "memory");
#else
    __asm volatile("cpsie i" ::: "memory");
#endif
#elif COS_BAREMETAL_X86
    (void)ctx;
#if defined(__GNUC__) || defined(__clang__)
    __asm volatile("sti" ::: "memory");
#endif
#else
    (void)ctx;
#endif
}

#endif /* COS_RTOS_TYPE == COS_RTOS_BARE_METAL */
