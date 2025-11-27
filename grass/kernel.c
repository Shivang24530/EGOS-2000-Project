/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: kernel ≈ 2 handlers
 *   intr_entry() handles timer and device interrupts.
 *   excp_entry() handles system calls and faults (e.g., invalid memory access).
 */

#include "process.h"
#include <string.h>

uint core_in_kernel;
uint core_to_proc_idx[NCORES];
struct process proc_set[MAX_NPROCESS + 1];
/* proc_set[0] is a place holder for idle cores. */

#define curr_proc_idx core_to_proc_idx[core_in_kernel]
#define curr_pid proc_set[curr_proc_idx].pid
#define curr_status proc_set[curr_proc_idx].status
#define curr_saved proc_set[curr_proc_idx].saved_registers

static void intr_entry(uint);
static void excp_entry(uint);

void kernel_entry()
{
    /* With the kernel lock, only one core can enter this point at any time. */
    asm("csrr %0, mhartid" : "=r"(core_in_kernel));

    /* Save the process context. */
    asm("csrr %0, mepc" : "=r"(proc_set[curr_proc_idx].mepc));
    memcpy(curr_saved, SAVED_REGISTER_ADDR, SAVED_REGISTER_SIZE);

    uint mcause;
    asm("csrr %0, mcause" : "=r"(mcause));
    (mcause & (1 << 31)) ? intr_entry(mcause & 0x3FF) : excp_entry(mcause);

    /* Restore the process context. */
    asm("csrw mepc, %0" ::"r"(proc_set[curr_proc_idx].mepc));
    memcpy(SAVED_REGISTER_ADDR, curr_saved, SAVED_REGISTER_SIZE);
}

#define INTR_ID_TIMER 7
#define EXCP_ID_ECALL_U 8
#define EXCP_ID_ECALL_M 11
static void proc_yield();
static void proc_try_syscall(struct process *proc);

static void excp_entry(uint id)
{
    if (id >= EXCP_ID_ECALL_U && id <= EXCP_ID_ECALL_M)
    {
        /* Copy the system call arguments from user space to the kernel. */
        uint syscall_paddr = earth->mmu_translate(curr_pid, SYSCALL_ARG);
        memcpy(&proc_set[curr_proc_idx].syscall, (void *)syscall_paddr,
               sizeof(struct syscall));
        proc_set[curr_proc_idx].syscall.status = PENDING;

        proc_set_pending(curr_pid);
        proc_set[curr_proc_idx].mepc += 4;
        proc_try_syscall(&proc_set[curr_proc_idx]);
        proc_yield();
        return;
    }
    /* Student's code goes here (System Call & Protection | Virtual Memory). */

    /* Kill the current process if curr_pid is a user application. */

    /* Student's code ends here. */
    FATAL("excp_entry: kernel got exception %d", id);
}

static void intr_entry(uint id)
{
    if (id != INTR_ID_TIMER)
        FATAL("excp_entry: kernel got interrupt %d", id);

    /* --- Preemptive Scheduler: update lifecycle statistics for the current process --- */
    /* Compute runtime since last_scheduled_time (or from when it started) */
    if (curr_pid != 0 && proc_set[curr_proc_idx].status != PROC_UNUSED &&
        proc_set[curr_proc_idx].pid >= GPID_USER_START)
    {

        ulonglong now = mtime_get();
        ulonglong last = proc_set[curr_proc_idx].last_scheduled_time;
        if (last == 0)
        {
            /* If this is the first interrupt after process started running, treat
               the whole period since creation as runtime portion (safe fallback). */
            last = proc_set[curr_proc_idx].created_time;
        }

        /* delta microseconds that this process has consumed since last scheduling */
        ulonglong delta = (now > last) ? (now - last) : 0;

        /* update last_scheduled_time for next accounting */
        proc_set[curr_proc_idx].last_scheduled_time = now;

        /* increment timer interrupt counter */
        proc_set[curr_proc_idx].timer_interrupts++;

        /* update cpu_time and MLFQ level accounting */
        mlfq_update_level(&proc_set[curr_proc_idx], delta);
    }
    else
    {
        /* For kernel / idle processes we still reset timer, but no MLFQ changes. */
    }

    /* Now yield to scheduler */
    proc_yield();
}

static void proc_yield()
{
    if (curr_status == PROC_RUNNING)
        proc_set_runnable(curr_pid);

    /* Student's code goes here (Multiple Projects). */

    /* ------------------ Measure / account current process ------------------ */
    /* Save current index/pid before we change curr_proc_idx later. */
    int old_idx = curr_proc_idx;
    struct process *curr = &proc_set[old_idx];

    /* If the current slot holds a valid user process, account for its runtime
     * since it was last scheduled. intr_entry() already updates on timer
     * interrupts; here we handle accounting for voluntary yields or edge cases. */
    if (curr->status != PROC_UNUSED && curr->pid >= GPID_USER_START)
    {
        ulonglong now = mtime_get();
        if (curr->last_scheduled_time != 0 && now > curr->last_scheduled_time)
        {
            ulonglong delta = now - curr->last_scheduled_time;
            /* update cpu_time and MLFQ bookkeeping */
            mlfq_update_level(curr, delta);
        }
        /* reset last_scheduled_time so that next scheduling accounts fresh */
        curr->last_scheduled_time = 0;
    }

    /* ------------------ Reset / boost MLFQ as needed ------------------ */
    mlfq_reset_level();

    /* ------------------ Choose next process by MLFQ ------------------ */
    /* Choose runnable process with lowest mlfq_level; tie-break by circular order. */
    int next_idx = MAX_NPROCESS;
    int best_idx = -1;
    int best_level = 1000000; /* big initial value */
    ulonglong now = mtime_get();

    for (uint i = 1; i <= MAX_NPROCESS; i++) {
        uint idx = (old_idx + i) % MAX_NPROCESS;
        struct process *p = &proc_set[idx];

        /* Handle pending syscalls first */
        if (p->status == PROC_PENDING_SYSCALL)
            proc_try_syscall(p);

        /* Skip unused slots */
        if (p->status == PROC_UNUSED)
            continue;

        /* If sleeping, decide wake or skip */
        if (p->status == PROC_SLEEPING) {
            if (p->sleep_until > now) {
                /* still sleeping -> skip this slot */
                continue;
            } else {
                /* wake up the process */
                p->status = PROC_RUNNABLE;
            }
        }

        /* Eligible statuses: READY or RUNNABLE */
        if (p->status == PROC_READY || p->status == PROC_RUNNABLE) {
            /* If mlfq_level is uninitialized (edge case), treat as lowest number 0 */
            int lvl = (p->mlfq_level >= 0) ? p->mlfq_level : 0;
            if (lvl < best_level) {
                best_level = lvl;
                best_idx = (int)idx;
            }
            /* tie-breaker: keep first seen (circular fairness) */
        }
    }

    if (best_idx >= 0)
    {
        next_idx = best_idx;

        /* Prepare lifecycle stats for the next process */
        struct process *nextp = &proc_set[next_idx];
        ulonglong now2 = mtime_get();

        /* If this is the first time it's scheduled, record response time */
        if (nextp->first_scheduled_time == 0)
        {
            nextp->first_scheduled_time = now2;
        }

        /* Mark start time for CPU accounting */
        nextp->last_scheduled_time = now2;
    }
    else
    {
        /* No runnable process found: follow template behavior (error as before) */
        FATAL("proc_yield: no process to run on core %d", core_in_kernel);
    }

    /* Student's code ends here. */
    uint mstatus, M_MODE = 3, U_MODE = 0;
    uint GRASS_MODE = (earth->translation == SOFT_TLB) ? M_MODE : U_MODE;
    asm("csrr %0, mstatus" : "=r"(mstatus));
    mstatus = (mstatus & ~(3 << 11)) | (GRASS_MODE << 11);
    asm("csrw mstatus, %0" ::"r"(mstatus));

    curr_proc_idx = next_idx;
    earth->mmu_switch(curr_pid);
    earth->mmu_flush_cache();
    if (curr_status == PROC_READY)
    {
        /* Setup argc, argv and program counter for a newly created process. */
        curr_saved[0] = APPS_ARG;
        curr_saved[1] = APPS_ARG + 4;
        proc_set[curr_proc_idx].mepc = APPS_ENTRY;
    }
    proc_set_running(curr_pid);
    earth->timer_reset(core_in_kernel);
}

static void proc_try_send(struct process *sender)
{
    for (uint i = 0; i < MAX_NPROCESS; i++)
    {
        struct process *dst = &proc_set[i];
        if (dst->pid == sender->syscall.receiver &&
            dst->status != PROC_UNUSED)
        {
            /* Return if dst is not receiving or not taking msg from sender. */
            if (!(dst->syscall.type == SYS_RECV &&
                  dst->syscall.status == PENDING))
                return;
            if (!(dst->syscall.sender == GPID_ALL ||
                  dst->syscall.sender == sender->pid))
                return;

            dst->syscall.status = DONE;
            dst->syscall.sender = sender->pid;
            /* Copy the system call arguments within the kernel PCB. */
            memcpy(dst->syscall.content, sender->syscall.content,
                   SYSCALL_MSG_LEN);
            return;
        }
    }
    FATAL("proc_try_send: unknown receiver pid=%d", sender->syscall.receiver);
}

static void proc_try_recv(struct process *receiver)
{
    if (receiver->syscall.status == PENDING)
        return;

    /* Copy the system call struct from the kernel back to user space. */
    uint syscall_paddr = earth->mmu_translate(receiver->pid, SYSCALL_ARG);
    memcpy((void *)syscall_paddr, &receiver->syscall, sizeof(struct syscall));

    /* Set the receiver and sender back to RUNNABLE. */
    proc_set_runnable(receiver->pid);
    proc_set_runnable(receiver->syscall.sender);
}

static void proc_try_syscall(struct process *proc)
{
    switch (proc->syscall.type)
    {
    case SYS_RECV:
        proc_try_recv(proc);
        break;
    case SYS_SEND:
        proc_try_send(proc);
        break;
    default:
        FATAL("proc_try_syscall: unknown syscall type=%d", proc->syscall.type);
    }
}
