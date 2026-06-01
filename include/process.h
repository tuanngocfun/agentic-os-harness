#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>

#define MAX_PROCESSES 16
#define KERNEL_STACK_SIZE 4096
#define USER_STACK_SIZE 4096
#define USER_STACK_TOP 0xB0000000

enum process_state {
    PROCESS_DEAD = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED
};

struct process {
    uint32_t pid;
    uint32_t esp;
    uint32_t ebp;
    uint32_t eip;
    uint32_t cr3;
    enum process_state state;
    uint32_t *kernel_stack;
    uint32_t *user_stack;
    struct process *next;
};

void process_init(void);
struct process *process_create(uint32_t entry_point, int is_user);
void process_destroy(struct process *proc);
struct process *process_get_current(void);
uint32_t process_get_count(void);

#endif
