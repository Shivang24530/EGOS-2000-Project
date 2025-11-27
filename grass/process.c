/*
 * (C) 2025, Cornell University
 * All rights reserved.
 *
 * Description: helper functions for process management
 */

#include "process.h"
#include <string.h>
#define MLFQ_NLEVELS 5
#define MLFQ_RESET_PERIOD 10000000                               /* 10 seconds */
#define MLFQ_LEVEL_RUNTIME(x) ((ulonglong)((x + 1) * 100000ULL)) /* e.g., 100ms for level 0 */
extern struct process proc_set[MAX_NPROCESS + 1];

static void proc_set_status(int pid, enum proc_status status)
{
    for (uint i = 0; i < MAX_NPROCESS; i++)
        if (proc_set[i].pid == pid)
            proc_set[i].status = status;
}

void proc_set_ready(int pid) { proc_set_status(pid, PROC_READY); }
void proc_set_running(int pid) { proc_set_status(pid, PROC_RUNNING); }
void proc_set_runnable(int pid) { proc_set_status(pid, PROC_RUNNABLE); }
void proc_set_pending(int pid) { proc_set_status(pid, PROC_PENDING_SYSCALL); }

int proc_alloc()
{
    static uint curr_pid = 0;
    for (uint i = 1; i <= MAX_NPROCESS; i++)
        if (proc_set[i].status == PROC_UNUSED)
        {
            proc_set[i].pid = ++curr_pid;
            proc_set[i].status = PROC_LOADING;

            /* --- Initialization of lifecycle statistics & MLFQ fields --- */
            proc_set[i].created_time = mtime_get();
            proc_set[i].first_scheduled_time = 0;
            proc_set[i].cpu_time = 0;
            proc_set[i].timer_interrupts = 0;

            /* Start at MLFQ level 0 */
            proc_set[i].mlfq_level = 0;
            proc_set[i].mlfq_remaining = MLFQ_LEVEL_RUNTIME(0);
            proc_set[i].last_scheduled_time = 0;

            /* Not sleeping */
            proc_set[i].sleep_until = 0;

            return curr_pid;
        }

    FATAL("proc_alloc: reach the limit of %d processes", MAX_NPROCESS);
}

/* helper: convert unsigned long long to string */
static void utoa64(unsigned long long v, char *out) {
    char tmp[32];
    int i = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v > 0) {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }
    int j = 0;
    while (i--) out[j++] = tmp[i];
    out[j] = '\0';
}

void proc_free(int pid)
{
    if (pid != GPID_ALL)
    {
        for (uint i = 0; i < MAX_NPROCESS; i++)
        {
            if (proc_set[i].pid == pid && proc_set[i].status != PROC_UNUSED)
            {
                ulonglong now = mtime_get();
                ulonglong turnaround = now - proc_set[i].created_time;
                long long response = (proc_set[i].first_scheduled_time == 0)
                                      ? -1
                                      : (long long)(proc_set[i].first_scheduled_time - proc_set[i].created_time);

                char s_turn[32], s_resp[32], s_cpu[32], s_tinc[32];

                utoa64(turnaround, s_turn);
                if (response < 0) strcpy(s_resp, "-1");
                else utoa64((unsigned long long)response, s_resp);
                utoa64(proc_set[i].cpu_time, s_cpu);
                utoa64(proc_set[i].timer_interrupts, s_tinc);

                printf("PID %d: turnaround=%s usec, response=%s usec, cpu=%s usec, timer_interrupts=%s\n",
                       proc_set[i].pid, s_turn, s_resp, s_cpu, s_tinc);

                earth->mmu_free(pid);
                proc_set[i].status = PROC_UNUSED;
                break;
            }
        }
    }
    else
    {
        for (uint i = 0; i < MAX_NPROCESS; i++)
        {
            if (proc_set[i].pid >= GPID_USER_START &&
                proc_set[i].status != PROC_UNUSED)
            {
                ulonglong now = mtime_get();
                ulonglong turnaround = now - proc_set[i].created_time;
                long long response = (proc_set[i].first_scheduled_time == 0)
                                      ? -1
                                      : (long long)(proc_set[i].first_scheduled_time - proc_set[i].created_time);

                char s_turn[32], s_resp[32], s_cpu[32], s_tinc[32];

                utoa64(turnaround, s_turn);
                if (response < 0) strcpy(s_resp, "-1");
                else utoa64((unsigned long long)response, s_resp);
                utoa64(proc_set[i].cpu_time, s_cpu);
                utoa64(proc_set[i].timer_interrupts, s_tinc);

                printf("PID %d: turnaround=%s usec, response=%s usec, cpu=%s usec, timer_interrupts=%s\n",
                       proc_set[i].pid, s_turn, s_resp, s_cpu, s_tinc);

                earth->mmu_free(proc_set[i].pid);
                proc_set[i].status = PROC_UNUSED;
            }
        }
    }
}


void mlfq_update_level(struct process *p, ulonglong runtime)
{
    if (!p)
        return;

    /* accumulate CPU time */

    p->cpu_time += runtime;

    /* decrement remaining runtime on current level */
    if (p->mlfq_remaining > runtime)
        p->mlfq_remaining -= runtime;
    else
        p->mlfq_remaining = 0;

    /* if time quota for this level exhausted, demote (unless at bottom level) */
    while (p->mlfq_remaining == 0 && p->mlfq_level < MLFQ_NLEVELS - 1)
    {
        p->mlfq_level++;
        p->mlfq_remaining = MLFQ_LEVEL_RUNTIME(p->mlfq_level);
    }

    /* If at bottom level, keep remaining at bottom level runtime (do not demote further) */
    if (p->mlfq_level == MLFQ_NLEVELS - 1 && p->mlfq_remaining == 0)
    {
        p->mlfq_remaining = MLFQ_LEVEL_RUNTIME(p->mlfq_level);
    }
}

void mlfq_reset_level()
{
    static ulonglong MLFQ_last_reset_time = 0;
    ulonglong now = mtime_get();

    /* If there is pending keyboard input, boost GPID_SHELL to level 0 */
    if (!earth->tty_input_empty())
    {
        /* Find shell process and bump it to level 0 */
        for (uint i = 0; i < MAX_NPROCESS; i++)
        {
            if (proc_set[i].pid == GPID_SHELL && proc_set[i].status != PROC_UNUSED)
            {
                proc_set[i].mlfq_level = 0;
                proc_set[i].mlfq_remaining = MLFQ_LEVEL_RUNTIME(0);
            }
        }
    }

    /* Periodic global reset */
    if (MLFQ_last_reset_time == 0 || now - MLFQ_last_reset_time >= MLFQ_RESET_PERIOD)
    {
        for (uint i = 0; i < MAX_NPROCESS; i++)
        {
            if (proc_set[i].status != PROC_UNUSED &&
                proc_set[i].pid >= GPID_USER_START)
            {
                proc_set[i].mlfq_level = 0;
                proc_set[i].mlfq_remaining = MLFQ_LEVEL_RUNTIME(0);
            }
        }
        MLFQ_last_reset_time = now;
    }
}

void proc_sleep(int pid, uint usec)
{
    /* set the process to non-runnable and record wake-up time */
    for (uint i = 0; i < MAX_NPROCESS; i++)
    {
        if (proc_set[i].pid == pid && proc_set[i].status != PROC_UNUSED)
        {
            proc_set[i].sleep_until = mtime_get() + (ulonglong)usec;
            /* use PROC_LOADING to indicate it should not be scheduled */
            proc_set[i].status = PROC_SLEEPING;

            return;
        }
    }
}

void proc_coresinfo()
{
    printf("Cores info: \n");
    for (uint core = 0; core < NCORES; core++)
    {
        uint idx = core_to_proc_idx[core];
        if (idx == 0)
        {
            printf(" core %u: idle\n", core);
        }
        else
        {
            int pid = proc_set[idx].pid;
            printf(" core %u: pid %d\n", core, pid);
        }
    }
}
