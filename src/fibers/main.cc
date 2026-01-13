#include <cstdint>
#include <iostream>
#include <vector>
#include <functional>

const size_t STACK_SIZE = 1024 * 1024 * 2;
const size_t MAX_THREADS = 3;

struct Runtime;
struct Thread;

enum ThreadState {
    RUNNING,
    READY,
    AVAILABLE,
};

struct ThreadContext {
    uint64_t rsp, r15, r14, r13, r12, rbx, rbp;    
};

struct Thread {
    Thread(size_t stack_size)
        : stack(stack_size, 0),
          state(ThreadState::AVAILABLE),
          ctx(ThreadContext{}) {}
    Thread(size_t stack_size, ThreadState state)
        : stack(stack_size, 0),
          state(state),
          ctx(ThreadContext{}) {}
    std::vector<uint8_t> stack;
    ThreadState state;
    ThreadContext ctx;
};

Runtime *global_runtime = nullptr;
extern "C" void guard();
extern "C" void skip();

extern "C" __attribute__((naked)) void switch_thread() {
    __asm__ __volatile__ (
        // 1. Save "Callee-Saved" registers of the current thread into old_ctx (RDI)
        "movq %%rsp, 0x00(%%rdi)\n\t"
        "movq %%r15, 0x08(%%rdi)\n\t"
        "movq %%r14, 0x10(%%rdi)\n\t"
        "movq %%r13, 0x18(%%rdi)\n\t"
        "movq %%r12, 0x20(%%rdi)\n\t"
        "movq %%rbx, 0x28(%%rdi)\n\t"
        "movq %%rbp, 0x30(%%rdi)\n\t"

        // 2. Load "Callee-Saved" registers of the next thread from new_ctx (RSI)
        "movq 0x00(%%rsi), %%rsp\n\t"
        "movq 0x08(%%rsi), %%r15\n\t"
        "movq 0x10(%%rsi), %%r14\n\t"
        "movq 0x18(%%rsi), %%r13\n\t"
        "movq 0x20(%%rsi), %%r12\n\t"
        "movq 0x28(%%rsi), %%rbx\n\t"
        "movq 0x30(%%rsi), %%rbp\n\t"

        // 3. Jump to the return address sitting at the top of the NEW stack
        "ret"
        : // No outputs
        : // No inputs (we access rdi/rsi directly via ABI)
        : "memory"
    );
}

struct Runtime {
    std::vector<Thread> threads;
    int current;

    Runtime() : current(0) {
        threads.emplace_back(STACK_SIZE, ThreadState::RUNNING);
        for(size_t i=0;i<MAX_THREADS;i++) {
            threads.emplace_back(STACK_SIZE);
        }
    }

    void t_return() {
        if (current != 0) {
            threads[current].state = ThreadState::AVAILABLE;
            t_yield();
        }
    }

    bool t_yield() {
        if (threads[current].state == ThreadState::RUNNING) {
            threads[current].state = ThreadState::READY;
        }

        // 2. Start searching from the NEXT index
        int pos = (current + 1) % threads.size();
        while(threads[pos].state != ThreadState::READY) {
            pos = (pos+1)%threads.size();
            if (pos == current) return false;
        }
        
        // deschedule current thread
        if (threads[current].state != ThreadState::AVAILABLE) {
            threads[current].state = ThreadState::READY;
        }

        threads[pos].state = ThreadState::RUNNING;

        int old_pos = current;
        current = pos;

        ThreadContext* old_ctx = &threads[old_pos].ctx;
        ThreadContext* new_ctx = &threads[pos].ctx;
        __asm__ __volatile__ (
            "call switch_thread"
            :
            : "D" (old_ctx), "S" (new_ctx)
            : "memory", "cc", "rax", "rcx", "rdx", "r8", "r9", "r10", "r11"
        );

        return threads.size() > 0;

    }

    void spawn(void* fn) {
        Thread* to_run = &threads[0];
        bool found = false;
        for (auto &t : threads) {
            if (t.state == ThreadState::AVAILABLE) {
                to_run = &t;
                found=true;
                break;
            }
        }
        if (!found) {
            throw std::runtime_error("No available thread");
        }
        size_t stack_size = to_run->stack.size();
        void* stack_bottom = to_run->stack.data() + stack_size;
        uintptr_t aligned_stack_bottom = reinterpret_cast<uintptr_t>(stack_bottom)&~0xF;
        void** aligned_stack_bottom_ptr = reinterpret_cast<void**>(aligned_stack_bottom);
        aligned_stack_bottom_ptr[-2] = reinterpret_cast<void*>(guard);
        aligned_stack_bottom_ptr[-3] = reinterpret_cast<void*>(skip);
        aligned_stack_bottom_ptr[-4] = reinterpret_cast<void*>(fn);
        to_run->ctx.rsp = reinterpret_cast<uint64_t>(aligned_stack_bottom_ptr - 4);
        to_run->state = ThreadState::READY;
        return;
    }
    
    void run() {
        while (t_yield());
        std::cout << "All threads finished." << std::endl;
    }
};


extern "C" void guard() {
    global_runtime->t_return();
}

extern "C" __attribute__((naked)) void skip() {
    __asm__ __volatile__ (
        "ret"
    );
}

extern "C" void yield_thread() {
    global_runtime->t_yield();
}


void another_fun() {
    std::cout << "I am on another function now!" << std::endl;
    while (true);
}

void [[noreturn]] some_fun(const ThreadContext& nxt) {
    std::cout << "I am on some function" << std::endl;
    __asm__ __volatile__ (
        "movq %0, %%rsp \n\t"
        "ret"
        :
        : "r"(nxt.rsp)
        : "memory"
    );
}

void t1() {
    std::cout << "thread 1 started" << std::endl;
    for(int i=0;i<15;i++) {
        std::cout << "thread 1 is on iteration " << std::to_string(i) << std::endl;
        yield_thread();
    }
    std::cout << "Thread 1 finished" << std::endl;
}

void t2() {
    std::cout << "thread 2 started" << std::endl;
    for(int i=0;i<15;i++) {
        std::cout << "thread 2 is on iteration " << std::to_string(i) << std::endl;
        yield_thread();
    }
    std::cout << "Thread 2 finished" << std::endl;
}

int main() {
    auto runtime = Runtime();
    global_runtime = &runtime;
    runtime.spawn(reinterpret_cast<void*>(&t1));
    runtime.spawn(reinterpret_cast<void*>(&t2));
    runtime.run();
    runtime.spawn(reinterpret_cast<void*>(&t1));
    runtime.spawn(reinterpret_cast<void*>(&t2));
    runtime.run();
}


