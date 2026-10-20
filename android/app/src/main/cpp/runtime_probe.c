/* SDK primitive probe, not a game loader or complete PPU/SPU scheduler. */
#include "runtime_probe.h"
#include "runtime/platform/win32_compat.h"
#include "runtime/platform/guest_poll.h"
#include "runtime/memory/vm.h"
#include "runtime/ppu/ppu_ops.h"
#include "runtime/spu/spu_helpers.h"
#include <stdarg.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

uint8_t *vm_base;
uint32_t ppu_vm_size, ppu_hle_inject_base = 0x20000000u;
uint32_t g_ww_lo, g_ww_hi;
int g_resv_store_active, g_spu_ls_watch_n;

/* Optional loader/debug hooks are disabled, not silently emulated. These
 * fail closed if reached. No SPU context is registered with the scheduler. */
void ppu_resv_break_store(uint64_t ea) { (void)ea; abort(); }
void ps3_ww_report_inline(uint32_t a, uint64_t v, int w) { (void)a;(void)v;(void)w;abort(); }
void spu_ls_watch_slow(uint32_t a, int w, const uint8_t *p, uint32_t pc, uint32_t lr) {
    (void)a;(void)w;(void)p;(void)pc;(void)lr;abort();
}
void spu_ch_wake(spu_context *ctx) { (void)ctx; abort(); }

int d2_sdk_spu_helpers_test(void);
int d2_sdk_spu_numeric_test(void);
static char *output;
static size_t output_size, used;
static unsigned failures;
static void line(const char *format, ...) {
    if (used >= output_size) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(output + used, output_size - used, format, args);
    va_end(args);
    if (n > 0) used += (size_t)n < output_size - used ? (size_t)n : output_size - used;
}
static void check(const char *name, int ok) {
    line("%s=%s\n", name, ok ? "passed" : "failed");
    if (!ok) ++failures;
}

static void memory_and_ppu(void) {
    int32_t result = vm_init();
    line("core_vm_init_result=%d\n", result);
    int ok = result == CELL_OK;
    check("core_vm_reservation", ok);
    if (!ok) return;
    line("core_virtual_bytes=%llu\ncore_host_page_size=%u\n",
         (unsigned long long)VM_TOTAL_SIZE, vm_host_page_size());
    check("core_vm_base_4g_aligned", ((uintptr_t)vm_base & (VM_TOTAL_SIZE - 1)) == 0);
    const uint32_t a = VM_MAIN_MEM_BASE + 128;
    vm_write16(a, 0x1234); vm_write32(a+4, 0x89abcdef); vm_write64(a+8, UINT64_C(0x0123456789abcdef));
    check("core_ppu_big_endian", vm_base[a] == 0x12 && vm_base[a+1] == 0x34 &&
          vm_base[a+4] == 0x89 && vm_base[a+7] == 0xef && vm_base[a+8] == 1 && vm_base[a+15] == 0xef &&
          vm_read16(a) == 0x1234 && vm_read32(a+4) == 0x89abcdef &&
          vm_read64(a+8) == UINT64_C(0x0123456789abcdef));
    ppu_context ctx = {0}; ctx.gpr[1] = UINT64_MAX; ctx.gpr[2] = 1;
    PPU_ADDC(&ctx, 3, 1, 2);
    int ops = ctx.gpr[3] == 0 && ppu_xer_get_ca(&ctx);
    ctx.gpr[1] = 7; ctx.gpr[2] = 12; PPU_SUBF(&ctx, 3, 1, 2);
    ops &= ctx.gpr[3] == 5;
    PPU_ADDI(&ctx, 3, 0, -2); ops &= ctx.gpr[3] == UINT64_MAX - 1;
    check("core_ppu_integer_ops", ops);
    vm_write32(a, 10);
    int cas = ppu_lwarx(&ctx, a) == 10 && ppu_stwcx(&ctx, a, 11) && vm_read32(a) == 11;
    (void)ppu_lwarx(&ctx, a); vm_write32(a, 12);
    cas &= !ppu_stwcx(&ctx, a, 13) && vm_read32(a) == 12;
    check("core_ppu_inline_reservation_cas", cas);
    const uint32_t high = 0xffffc000u;
    int high_ok = vm_commit(high, 0x4000) == CELL_OK;
    if (high_ok) {
        vm_write32(0xfffffffcu, 0xa55a1234u);
        high_ok = vm_read32(0xfffffffcu) == 0xa55a1234u &&
                  vm_protect(high, 0x4000, 1, 0, 0) == CELL_OK &&
                  vm_protect(high, 0x4000, 1, 1, 0) == CELL_OK;
    }
    check("core_vm_top_page_and_protection", high_ok);
}

static HANDLE start_gate;
static atomic_int cancel_workers;
static DWORD WINAPI increment(void *unused) {
    (void)unused;
    if (WaitForSingleObject(start_gate, 5000) != WAIT_OBJECT_0) return 1;
    ppu_context context = {0};
    for (unsigned i = 0; i < 5000; ++i) {
        for (;;) {
            if (atomic_load(&cancel_workers)) return 2;
            uint32_t value = ppu_lwarx(&context, VM_MAIN_MEM_BASE + 256);
            if (ppu_stwcx(&context, VM_MAIN_MEM_BASE + 256, value + 1)) break;
        }
    }
    return 0;
}
static void threads(void) {
    if (!vm_base) { check("core_host_threads_guest_cas", 0); return; }
    vm_write32(VM_MAIN_MEM_BASE + 256, 0);
    start_gate = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE workers[4] = {0}; unsigned count = 0;
    if (start_gate) for (unsigned i = 0; i < 4; ++i) {
        workers[i] = CreateThread(NULL, 0, increment, NULL, 0, NULL);
        if (workers[i]) ++count;
    }
    int ok = start_gate && count == 4;
    if (start_gate) SetEvent(start_gate);
    for (unsigned i = 0; i < 4; ++i) if (workers[i]) {
        DWORD exit_code = 99;
        if (WaitForSingleObject(workers[i], 5000) != WAIT_OBJECT_0) {
            ok = 0; atomic_store(&cancel_workers, 1);
            // Never free guest memory under a live worker after a failed test.
            if (WaitForSingleObject(workers[i], 6000) != WAIT_OBJECT_0) abort();
        }
        ok &= GetExitCodeThread(workers[i], &exit_code) && exit_code == 0;
        CloseHandle(workers[i]);
    }
    check("core_host_threads_guest_cas", ok && vm_read32(VM_MAIN_MEM_BASE + 256) == 20000);
    if (start_gate) CloseHandle(start_gate);
}

static void waits(void) {
    HANDLE automatic = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE manual = CreateEventA(NULL, TRUE, FALSE, NULL);
    int ok = automatic && manual;
    if (ok) {
        ok &= WaitForSingleObject(automatic, 0) == WAIT_TIMEOUT;
        ok &= SetEvent(automatic) && WaitForSingleObject(automatic, 0) == WAIT_OBJECT_0;
        ok &= WaitForSingleObject(automatic, 0) == WAIT_TIMEOUT;
        ok &= SetEvent(manual) && WaitForSingleObject(manual, 0) == WAIT_OBJECT_0 &&
              WaitForSingleObject(manual, 0) == WAIT_OBJECT_0;
        ok &= ResetEvent(manual) && WaitForSingleObject(manual, 0) == WAIT_TIMEOUT;
    }
    check("core_auto_manual_events", ok);
    if (automatic) CloseHandle(automatic);
    if (manual) CloseHandle(manual);
    HANDLE semaphore = CreateSemaphoreA(NULL, 0, 2, NULL);
    ok = semaphore != NULL;
    if (ok) {
        ok &= WaitForSingleObject(semaphore, 0) == WAIT_TIMEOUT;
        ok &= ReleaseSemaphore(semaphore, 2, NULL);
        ok &= WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0 &&
              WaitForSingleObject(semaphore, 0) == WAIT_OBJECT_0 &&
              WaitForSingleObject(semaphore, 0) == WAIT_TIMEOUT;
        CloseHandle(semaphore);
    }
    check("core_counting_semaphore", ok);
}

static HANDLE poll_ready;
static void *poll_worker(void *unused) {
    (void)unused;
    ps3_guest_worker_register(1);
    SetEvent(poll_ready);
    for (;;) {
        uint32_t value;
        memcpy(&value, vm_base + VM_MAIN_MEM_BASE + 512, sizeof value);
        ps3_poll_backoff(100, VM_MAIN_MEM_BASE + 512, &value, sizeof value);
    }
    return NULL;
}
static void guest_poll_shutdown(void) {
    if (!vm_base) { check("core_guest_poll_stop", 0); return; }
    poll_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    pthread_t worker;
    int started = poll_ready && pthread_create(&worker, NULL, poll_worker, NULL) == 0;
    int ok = started && WaitForSingleObject(poll_ready, 5000) == WAIT_OBJECT_0;
    int observed_waiter = 0;
    if (started) {
        ULONGLONG deadline = GetTickCount64() + 1000;
        do {
            observed_waiter = __atomic_load_n(&ps3_poll_waiters, __ATOMIC_SEQ_CST) != 0;
            if (observed_waiter) break;
            Sleep(1);
        } while (GetTickCount64() < deadline);
        ps3_guest_workers_request_stop();
        deadline = GetTickCount64() + 5000;
        while (!ps3_guest_workers_stopped() && GetTickCount64() < deadline) Sleep(1);
        if (!ps3_guest_workers_stopped()) abort(); // Cannot release a live worker's VM.
        ps3_guest_workers_join();
    }
    check("core_guest_poll_stop", ok && observed_waiter &&
          __atomic_load_n(&ps3_poll_waiters, __ATOMIC_SEQ_CST) == 0);
    if (poll_ready) CloseHandle(poll_ready);
}

int d2_runtime_probe_run(char *report, size_t capacity) {
    output = report; output_size = capacity; used = failures = 0;
    if (!report || capacity < 1024) return 1;
    report[0] = 0;
    line("core_scope=sdk-primitives-only\ncore_full_runtime_linked=false\n"
         "core_sdk_tree=a23f74ffccaa440b686dc06694379b6362431038\n"
         "core_ppu_loader_linked=false\ncore_spu_scheduler_linked=false\n"
         "core_optional_debug_hooks=disabled-fail-closed\n");
#ifdef __ANDROID__
    line("core_guest_stop_model=cooperative-poll-boundaries\n");
#else
    line("core_guest_stop_model=host-posix-cancellation\n");
#endif
#if defined(__aarch64__) && defined(__ARM_NEON)
    line("core_spu_backend=arm64-neon\n");
#else
    line("core_spu_backend=ci-scalar\n");
#endif
    memory_and_ppu();
    threads();
    waits();
    guest_poll_shutdown();
    check("core_spu_instruction_vectors", d2_sdk_spu_helpers_test() == 0);
    check("core_spu_numeric_vectors", d2_sdk_spu_numeric_test() == 0);
    vm_shutdown();
    check("core_vm_release", vm_base == NULL);
    line("core_failures=%u\ncore_status=%s\n", failures, failures ? "failed" : "passed");
    return failures ? 1 : 0;
}
