#ifndef WOLFTRUST_MONITOR_H
#define WOLFTRUST_MONITOR_H

#include "wolftrust/partition.h"
#include "wolftrust/platform.h"

typedef struct wt_scheduler_state {
    const wt_guest_config_t* configs;
    wt_guest_runtime_t* runtime;
    size_t guest_count;
    wt_guest_id_t current_guest;
    uint32_t monotonic_ticks;
} wt_scheduler_state_t;

void wt_monitor_init(void);
void wt_monitor_start(void);
void wt_monitor_on_secure_timer(const wt_trap_frame_t* frame);
void wt_monitor_on_guest_fault(const wt_trap_frame_t* frame,
                               wt_fault_reason_t reason);
const wt_scheduler_state_t* wt_monitor_state(void);

#endif
