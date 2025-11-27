#pragma once

#include "egos.h"
#include "syscall.h"

enum proc_status {
    PROC_UNUSED,
    PROC_LOADING,
    PROC_READY,
    PROC_RUNNING,
    PROC_RUNNABLE,
    PROC_PENDING_SYSCALL,
    PROC_SLEEPING
};

#define MAX_NPROCESS        16
#define SAVED_REGISTER_NUM  32
#define SAVED_REGISTER_SIZE SAVED_REGISTER_NUM * 4
#define SAVED_REGISTER_ADDR (void*)(EGOS_STACK_TOP - SAVED_REGISTER_SIZE)

struct process {
    int pid;
    struct syscall syscall;
    enum proc_status status;
    uint mepc, saved_registers[SAVED_REGISTER_NUM];

    /* ---------- Lifecycle statistics (microseconds) ---------- */
    ulonglong created_time;         /* when proc_alloc() was called */
    ulonglong first_scheduled_time; /* first time this process was scheduled (0 = not yet) */
    ulonglong cpu_time;             /* accumulated CPU time used */
    ulonglong timer_interrupts;     /* number of times timer interrupted this process */

    /* ---------- MLFQ scheduling fields ---------- */
    int mlfq_level;                 /* 0 (highest priority) .. 4 (lowest) */
    ulonglong mlfq_remaining;       /* remaining runtime allowed at current level (microseconds) */
    ulonglong last_scheduled_time;  /* last timestamp when this process started running */

    /* ---------- Sleeping support ---------- */
    ulonglong sleep_until;         /* if > mtime_get(), process is sleeping until this time */

    /* Other fields can go here if needed in the future */
};


ulonglong mtime_get();

int proc_alloc();
void proc_free(int);
void proc_set_ready(int);
void proc_set_running(int);
void proc_set_runnable(int);
void proc_set_pending(int);

void mlfq_reset_level();
void mlfq_update_level(struct process* p, ulonglong runtime);
void proc_sleep(int pid, uint usec);
void proc_coresinfo();

extern uint core_to_proc_idx[NCORES];
