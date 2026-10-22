#include "cafe/generated.h"
#include "cafe/ppc_ops.h"
#include "cafe/runtime.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace cafe {
namespace {

thread_local PPCContext* t_current = nullptr;

void print_backtrace(const PPCContext& ctx) {
    std::fprintf(stderr, "guest backtrace (LR, then saved return addresses):\n");
    std::fprintf(stderr, "  0x%08X\n", ctx.lr);
    uint32_t frame = ctx.r[1];
    for (int depth = 0; depth < 48; ++depth) {
        if (!guest_memory_committed(frame, 8)) {
            break;
        }
        const uint32_t back = ppc::ld32(guest_base(), frame);
        if (back == 0 || back <= frame || !guest_memory_committed(back, 8)) {
            break;
        }
        std::fprintf(stderr, "  0x%08X\n", ppc::ld32(guest_base(), back + 4));
        frame = back;
    }
}

// What a fault touched: guest memory outside what the title committed, or
// host memory.
void describe_fault(uintptr_t host, int signal) {
    const auto base = reinterpret_cast<uintptr_t>(guest_base());
    if (base != 0 && host >= base && host - base < (uint64_t{1} << 32) + 0x10000) {
        const uint64_t guest = host - base;
        std::fprintf(stderr, "\nguest memory fault: access to unmapped guest address 0x%08llX\n",
                     static_cast<unsigned long long>(guest));
        // Just below the stack pointer: a frame larger than the stack left.
        if (t_current != nullptr && guest < t_current->r[1] && t_current->r[1] - guest <= (4u << 20)) {
            std::fprintf(stderr, "(0x%llX bytes below the stack pointer: a stack overflow)\n",
                         static_cast<unsigned long long>(t_current->r[1] - guest));
        }
    } else {
        std::fprintf(stderr, "\nhost fault (signal %d) at %p\n", signal, reinterpret_cast<void*>(host));
    }
}

#ifdef _WIN32

LONG WINAPI fault_handler(EXCEPTION_POINTERS* exception) {
    const EXCEPTION_RECORD& record = *exception->ExceptionRecord;
    const bool access = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ||
                        record.ExceptionCode == EXCEPTION_IN_PAGE_ERROR;
    if (access && record.NumberParameters >= 2) {
        describe_fault(static_cast<uintptr_t>(record.ExceptionInformation[1]), 11);
    } else {
        std::fprintf(stderr, "\nhost exception 0x%08lX\n", record.ExceptionCode);
    }
    // Where in the recompiled code: `llvm-addr2line -f -e <executable>
    // <image base + offset>` names the guest function (sub_XXXXXXXX).
    const auto pc = static_cast<uintptr_t>(exception->ContextRecord->Rip);
    HMODULE module = nullptr;
    char name[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(pc), &module) &&
        GetModuleFileNameA(module, name, sizeof name) != 0) {
        std::fprintf(stderr, "host code: %s+0x%llx\n", name,
                     static_cast<unsigned long long>(pc - reinterpret_cast<uintptr_t>(module)));
    }
    if (t_current != nullptr) {
        print_guest_state(*t_current);
    }
    std::fflush(stderr);
    std::_Exit(128 + 11);
}

#else

void fault_handler(int signal, siginfo_t* info, void* context) {
    describe_fault(reinterpret_cast<uintptr_t>(info->si_addr), signal);
    // Where in the recompiled code: `addr2line -f -e <executable> <offset>`
    // names the guest function (sub_XXXXXXXX).
    const auto* uc = static_cast<const ucontext_t*>(context);
    const auto pc = static_cast<uintptr_t>(uc->uc_mcontext.gregs[REG_RIP]);
    Dl_info where{};
    if (dladdr(reinterpret_cast<void*>(pc), &where) && where.dli_fname != nullptr) {
        std::fprintf(stderr, "host code: %s+0x%llx\n", where.dli_fname,
                     static_cast<unsigned long long>(pc - reinterpret_cast<uintptr_t>(where.dli_fbase)));
    }
    if (t_current != nullptr) {
        print_guest_state(*t_current);
    }
    std::_Exit(128 + signal);
}

#endif

} // namespace

PPCContext* current_context() { return t_current; }
void set_current_context(PPCContext* ctx) { t_current = ctx; }

void print_guest_state(const PPCContext& ctx) {
    for (int i = 0; i < 32; i += 4) {
        std::fprintf(stderr, "  r%-2d %08X  r%-2d %08X  r%-2d %08X  r%-2d %08X\n", i,
                     ctx.r[i], i + 1, ctx.r[i + 1], i + 2, ctx.r[i + 2], i + 3, ctx.r[i + 3]);
    }
    std::fprintf(stderr, "  lr %08X  ctr %08X  cr %08X  xer %08X\n", ctx.lr, ctx.ctr,
                 ppc::mfcr(ctx), ppc::mfxer(ctx));
    print_backtrace(ctx);
}

void fatal(const char* format, ...) {
    std::fflush(stdout);
    std::fprintf(stderr, "\nttt2: ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    if (t_current != nullptr) {
        print_guest_state(*t_current);
    }
    std::fflush(stderr);
    // Not exit(): other guest threads still use the objects static
    // destructors would tear down.
    std::_Exit(70);
}

void install_fault_handler() {
#ifdef _WIN32
    // Only faults nothing else handles (drivers may catch their own).
    SetUnhandledExceptionFilter(fault_handler);
#else
    static char alternate_stack[64 * 1024];
    stack_t stack{};
    stack.ss_sp = alternate_stack;
    stack.ss_size = sizeof alternate_stack;
    sigaltstack(&stack, nullptr);
    struct sigaction action{};
    action.sa_sigaction = fault_handler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
#endif
}

} // namespace cafe

// ------------------------------------------------- hooks for generated code
extern "C" {

void cafe_ppc_trap(cafe::PPCContext& ctx, uint32_t address) {
    cafe::set_current_context(&ctx);
    cafe::fatal("guest trap instruction at 0x%08X (an assertion in game code)", address);
}

void cafe_ppc_illegal(cafe::PPCContext& ctx, uint32_t address, uint32_t word,
                      const char* reason) {
    cafe::set_current_context(&ctx);
    cafe::fatal("cannot execute instruction 0x%08X at 0x%08X: %s", word, address, reason);
}

void cafe_ppc_null_call(cafe::PPCContext& ctx, uint32_t address) {
    cafe::set_current_context(&ctx);
    cafe::fatal("call through an unresolved weak symbol (address 0) at 0x%08X", address);
}

void cafe_ppc_bad_return(cafe::PPCContext& ctx, uint32_t expected) {
    cafe::set_current_context(&ctx);
    cafe::fatal("a guest call returned with LR=0x%08X instead of its return address "
                "0x%08X: the callee returned somewhere other than its caller "
                "(longjmp-style control flow the port does not model yet)",
                ctx.lr, expected);
}

// The Espresso time base runs at a quarter of the 248.625 MHz bus clock.
uint64_t cafe_ppc_timebase(void) {
    static const auto start = std::chrono::steady_clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    constexpr uint64_t kHz = 62156250;
    const auto t = static_cast<uint64_t>(ns);
    return (t / 1000000000u) * kHz + (t % 1000000000u) * kHz / 1000000000u;
}

void cafe_unimplemented_import(cafe::PPCContext& ctx, const char* module, const char* name) {
    cafe::set_current_context(&ctx);
    cafe::fatal("the game called %s:%s, which the runtime does not implement yet", module,
                name);
}

} // extern "C"
