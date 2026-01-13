#include <cstdint>
#include <iostream>

const int STACK_SIZE=48;

struct ThreadContext {
    uint64_t rsp;    
};

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

int main() {
    char stack[STACK_SIZE];
    std::cout << "Address of stack[0] : " << reinterpret_cast<uintptr_t>(&stack[0]) << std::endl;
    std::cout << "Address of stack[STACK_SIZE] : " << reinterpret_cast<uintptr_t>(&stack[STACK_SIZE]) << std::endl;

    char *stack_bottom = &stack[STACK_SIZE];
    uintptr_t address = reinterpret_cast<uintptr_t>(stack_bottom);
    uintptr_t aligned_address = address & ~0xF;

    char* aligned_stack_bottom = reinterpret_cast<char*>(aligned_address);
    void** return_addr_ptr = reinterpret_cast<void**>(aligned_address-8);
    *return_addr_ptr = reinterpret_cast<void*>(&another_fun);
    uintptr_t return_addr = reinterpret_cast<uint64_t>(return_addr_ptr);
    some_fun(ThreadContext{return_addr});
}


