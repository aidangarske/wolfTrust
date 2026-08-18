/* partitions.c
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfTrust.
 *
 * wolfTrust is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfTrust is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "wolftrust/arch/armv8m/partition.h"
#include "memory_map.h"

#include <string.h>

#ifndef WT_TIMESLICE_MS
#define WT_TIMESLICE_MS 2U
#endif

#define WT_USART_REGION_SIZE 0x00000400U
#if WT_SHARED_UART == 3
#define WT_GUEST0_USART_BASE 0x40004800U
#else
#define WT_GUEST0_USART_BASE 0x40004400U
#endif
#if WT_SHARED_UART == 1 || WT_SHARED_UART == 2
#define WT_GUEST1_USART_BASE 0x40004400U
#else
#define WT_GUEST1_USART_BASE 0x40004800U
#endif
/* Initial restore runs from a Secure exception and returns to a Non-secure
 * Thread/MSP frame. ES must stay set because the exception was taken to Secure
 * state; clearing it trips INVPC on STM32H563 hardware. */
#define WT_EXC_RETURN_NS_THREAD_MSP_FROM_SECURE 0xFFFFFFB9U

static wt_guest_config_t g_partition_configs[] = {
    {
        .guest_id = 0U,
        .name = "guest-a",
        .vector_table = WT_GUEST0_FLASH_BASE,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20010000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {WT_GUEST0_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC, WT_RESOURCE_SHARE_NONE},
            {0x20000000U, 0x00010000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR,
             WT_RESOURCE_SHARE_NONE}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {WT_GUEST0_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20000000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {WT_GUEST0_USART_BASE, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE},
            /* NSC window: NS guests must be able to fetch the SG veneers.
             * Without this region the NS MPU blocks BL into 0x0C000400+
             * (the gateway). Per ARMv8-M, NSC fetches succeed when SAU
             * marks them NSC AND the NS MPU grants execute permission. */
            {WT_FLASH_NSC_BASE, (WT_FLASH_NSC_END - WT_FLASH_NSC_BASE + 1U),
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC}
        },
        .mpu_region_count = 4U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .initial_state = WT_GUEST_READY,
        .timeslice_ms = WT_TIMESLICE_MS,
        .port = {
            .required_capabilities = WT_PORT_CAPABILITY_ALL,
            .provided_capabilities = WT_PORT_CAPABILITY_ALL,
            .vector_read_address =
                WT_FLASH_TO_S_ALIAS(WT_GUEST0_FLASH_BASE),
            .hsm_transport = {
                .base = WT_GUEST0_HSM_BUF_BASE,
                .size = WT_HSM_BUF_SIZE
            }
        }
    },
#if WT_MAX_GUESTS > 1
    {
        .guest_id = 1U,
        .name = "guest-b",
        .vector_table = WT_GUEST1_FLASH_BASE,
        .initial_psp_ns = 0x00000000U,
        .initial_msp_ns = 0x20020000U,
        .irq_mask = {
            .words = {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U}
        },
        .memory_windows = {
            {WT_GUEST1_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC, WT_RESOURCE_SHARE_NONE},
            {0x20010000U, 0x00010000U,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_RESTART_CLEAR,
             WT_RESOURCE_SHARE_NONE}
        },
        .memory_window_count = 2U,
        .mpu_regions = {
            {WT_GUEST1_FLASH_BASE, WT_GUEST_FLASH_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC},
            {0x20010000U, 0x00010000U, WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE},
            {WT_GUEST1_USART_BASE, WT_USART_REGION_SIZE,
             WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_DEVICE},
            /* NSC window — see guest-a above. */
            {WT_FLASH_NSC_BASE, (WT_FLASH_NSC_END - WT_FLASH_NSC_BASE + 1U),
             WT_MEM_ATTR_READ | WT_MEM_ATTR_EXEC}
        },
        .mpu_region_count = 4U,
        .restart_policy = {
            .restart_limit = 3U,
            .restart_window_ticks = 64U,
            .initial_delay_ticks = 1U
        },
        .initial_state = WT_GUEST_READY,
        .timeslice_ms = WT_TIMESLICE_MS,
        .port = {
            .required_capabilities = WT_PORT_CAPABILITY_ALL,
            .provided_capabilities = WT_PORT_CAPABILITY_ALL,
            .vector_read_address =
                WT_FLASH_TO_S_ALIAS(WT_GUEST1_FLASH_BASE),
            .hsm_transport = {
                .base = WT_GUEST1_HSM_BUF_BASE,
                .size = WT_HSM_BUF_SIZE
            }
        }
    }
#endif
};

static wt_guest_runtime_t g_partition_runtime[
    sizeof(g_partition_configs) / sizeof(g_partition_configs[0])
];
static const wt_profile_capabilities_t g_profile_capabilities = {
    .capabilities = WT_CAPABILITY_SECURITY_STATE |
                    WT_CAPABILITY_PRIVILEGE_STATE |
                    WT_CAPABILITY_ROT_ISOLATION |
                    WT_CAPABILITY_DOMAIN_ISOLATION |
                    WT_CAPABILITY_MEMORY_PROTECTION |
                    WT_CAPABILITY_INTERRUPT_ISOLATION |
                    WT_CAPABILITY_RESTART,
    .max_domains = 8U,
    .max_memory_resources_per_domain = 3U,
    .max_interrupts_per_domain = 1U,
};
static uintptr_t g_bound_exec_bases[
    sizeof(g_partition_configs) / sizeof(g_partition_configs[0])
];
static size_t g_bound_exec_sizes[
    sizeof(g_partition_configs) / sizeof(g_partition_configs[0])
];

static bool wt_guest_reset_handler_valid(const wt_guest_config_t* config,
                                         uintptr_t resetHandler)
{
    uintptr_t entry = resetHandler & ~(uintptr_t)1u;

    return ((resetHandler & 1u) != 0u) &&
           (entry >= config->vector_table) &&
           (entry < (config->vector_table + WT_GUEST_FLASH_SIZE));
}

static uintptr_t wt_guest_reset_handler(const wt_guest_config_t* config)
{
    uintptr_t resetHandler;

    resetHandler = ((const uint32_t*)config->port.vector_read_address)[1];
    if (!wt_guest_reset_handler_valid(config, resetHandler)) {
        /* M33MU requires the Secure alias, while STM32H563 hardware returns
         * zero for CPU reads of Non-secure flash through that alias. Secure
         * software is permitted to read the Non-secure alias, so fall back
         * only after rejecting the first value as an invalid guest entry. */
        resetHandler = ((const uint32_t*)config->vector_table)[1];
    }

    if (!wt_guest_reset_handler_valid(config, resetHandler)) {
        return 0u;
    }

    return resetHandler;
}

const wt_guest_config_t* wt_partitions_config_table(size_t* count)
{
    if (count != NULL) {
        *count = sizeof(g_partition_configs) / sizeof(g_partition_configs[0]);
    }

    return g_partition_configs;
}

wt_guest_runtime_t* wt_partitions_runtime_table(size_t* count)
{
    if (count != NULL) {
        *count = sizeof(g_partition_runtime) / sizeof(g_partition_runtime[0]);
    }

    return g_partition_runtime;
}

const wt_profile_capabilities_t* wt_partitions_profile_capabilities(void)
{
    return &g_profile_capabilities;
}

static const wt_domain_descriptor_t* wt_partition_manifest_domain(
    const wt_system_manifest_t* manifest, wt_domain_id_t id)
{
    size_t i;

    for (i = 0U; i < manifest->domain_count; ++i) {
        if (manifest->domains[i].id == id) {
            return &manifest->domains[i];
        }
    }

    return NULL;
}

static bool wt_port_window_in_resource(
    const wt_hsm_transport_window_t* window,
    const wt_memory_resource_t* resource)
{
    uintptr_t window_end;
    uintptr_t resource_end;

    if (window->size == 0U || resource->size == 0U ||
            window->base > UINTPTR_MAX - window->size ||
            resource->base > UINTPTR_MAX - resource->size) {
        return false;
    }

    window_end = window->base + window->size;
    resource_end = resource->base + resource->size;
    return window->base >= resource->base && window_end <= resource_end;
}

int wt_partition_validate_port_binding(
    const wt_guest_config_t* config,
    const wt_domain_descriptor_t* domain)
{
    const wt_guest_port_binding_t* port;
    size_t i;
    bool transport_authorized = false;

    if (config == NULL || domain == NULL ||
            (domain->memory_resource_count != 0U &&
             domain->memory_resources == NULL)) {
        return WT_PORT_ERROR_ARGUMENT;
    }

    port = &config->port;
    if (((port->required_capabilities | port->provided_capabilities) &
            ~WT_PORT_CAPABILITY_ALL) != 0U ||
            (port->required_capabilities &
             port->provided_capabilities) !=
                port->required_capabilities) {
        return WT_PORT_ERROR_CAPABILITY;
    }

    if ((port->required_capabilities &
            WT_PORT_CAPABILITY_VECTOR_READ_ALIAS) != 0U &&
            (port->vector_read_address == 0U ||
             (port->vector_read_address & (sizeof(uint32_t) - 1U)) != 0U ||
             port->vector_read_address == config->vector_table)) {
        return WT_PORT_ERROR_VECTOR_ALIAS;
    }

    if ((port->required_capabilities &
            WT_PORT_CAPABILITY_HSM_TRANSPORT) == 0U) {
        return WT_PORT_VALID;
    }

    for (i = 0U; i < domain->memory_resource_count; ++i) {
        const wt_memory_resource_t* resource =
            &domain->memory_resources[i];

        if ((resource->attributes &
                (WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE)) !=
                    (WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE) ||
                (resource->attributes & WT_MEM_ATTR_EXEC) != 0U) {
            continue;
        }
        if (wt_port_window_in_resource(&port->hsm_transport, resource)) {
            transport_authorized = true;
            break;
        }
    }

    if (!transport_authorized) {
        return WT_PORT_ERROR_HSM_TRANSPORT;
    }

    return WT_PORT_VALID;
}

int wt_partitions_bind_manifest(const wt_system_manifest_t* manifest)
{
    size_t count;
    size_t i;

    if (manifest == NULL || manifest->domains == NULL) {
        return -1;
    }

    count = sizeof(g_partition_configs) / sizeof(g_partition_configs[0]);
    for (i = 0U; i < count; ++i) {
        wt_guest_config_t* config = &g_partition_configs[i];
        const wt_domain_descriptor_t* domain;

        /* Domain zero is the SPM; each H5 guest is a Non-secure application
         * domain one higher in the generated manifest. The Secure Partitions
         * (crypto, attestation) are separate secure domains above the guests
         * and are not bound to a guest here. */
        if (config->guest_id == WT_DOMAIN_ID_INVALID) {
            return -1;
        }
        domain = wt_partition_manifest_domain(manifest,
                                              (wt_domain_id_t)config->guest_id + 1U);
        if (domain == NULL ||
                domain->domain_class != WT_DOMAIN_CLASS_NONSECURE_APPLICATION ||
                domain->security_state != WT_SECURITY_STATE_NONSECURE ||
                domain->privilege_state != WT_PRIVILEGE_STATE_UNPRIVILEGED ||
                domain->restart_policy.action != WT_RESTART_ACTION_DOMAIN) {
            return -1;
        }
        if (wt_partition_validate_port_binding(config, domain) !=
                WT_PORT_VALID) {
            return -1;
        }

        /* The generated manifest's restart policy is authoritative: the SPM
         * honors the declared limits, not a compiled-in copy. */
        config->restart_policy.restart_limit =
            domain->restart_policy.restart_limit;
        config->restart_policy.restart_window_ticks =
            domain->restart_policy.restart_window_ticks;
        config->restart_policy.initial_delay_ticks =
            domain->restart_policy.initial_delay_ticks;
        /* The manifest's declared initial lifecycle drives the runtime state:
         * an NS application declared READY boots runnable; STOPPED stays out
         * of the schedule until an explicit lifecycle action. */
        config->initial_state = (wt_guest_state_t)domain->initial_lifecycle;

        if (domain->memory_resource_count != config->memory_window_count ||
                domain->memory_resource_count > config->mpu_region_count) {
            return -1;
        }
        for (size_t resource = 0U;
                resource < domain->memory_resource_count; ++resource) {
            const wt_memory_resource_t* manifest_resource =
                &domain->memory_resources[resource];
            uint32_t mpu_attributes = manifest_resource->attributes &
                (WT_MEM_ATTR_READ | WT_MEM_ATTR_WRITE | WT_MEM_ATTR_EXEC |
                 WT_MEM_ATTR_DEVICE);

            config->memory_windows[resource] = *manifest_resource;
            config->mpu_regions[resource].base = manifest_resource->base;
            config->mpu_regions[resource].size = manifest_resource->size;
            config->mpu_regions[resource].attributes = mpu_attributes;
        }

        if (domain->entry_point == 0U ||
                domain->interrupt_resource_count > WT_MAX_IRQ_WORDS * 32U ||
                (domain->interrupt_resource_count != 0U &&
                 domain->interrupt_resources == NULL)) {
            return -1;
        }
        memset(&config->irq_mask, 0, sizeof(config->irq_mask));
        for (size_t interrupt = 0U;
                interrupt < domain->interrupt_resource_count; ++interrupt) {
            uint32_t irq = domain->interrupt_resources[interrupt].interrupt;
            size_t word = irq / 32U;
            uint32_t bit = irq % 32U;

            if (word >= WT_MAX_IRQ_WORDS) {
                return -1;
            }
            config->irq_mask.words[word] |= (uint32_t)1U << bit;
        }
        g_bound_exec_bases[i] = 0U;
        g_bound_exec_sizes[i] = 0U;
        for (size_t executable = 0U;
                executable < domain->memory_resource_count; ++executable) {
            const wt_memory_resource_t* resource =
                &domain->memory_resources[executable];

            if ((resource->attributes & WT_MEM_ATTR_EXEC) != 0U) {
                g_bound_exec_bases[i] = resource->base;
                g_bound_exec_sizes[i] = resource->size;
                break;
            }
        }
        if (g_bound_exec_sizes[i] == 0U) {
            return -1;
        }

        if (domain->stack_base > (uintptr_t)-1 - domain->stack_size) {
            return -1;
        }
        config->initial_msp_ns = domain->stack_base + domain->stack_size;
    }

    return 0;
}

void wt_partition_reset_runtime(const wt_guest_config_t* config,
                                wt_guest_runtime_t* runtime)
{
    uint32_t restart_count;
    uint32_t first_restart_tick;

    if (config == NULL || runtime == NULL) {
        return;
    }

    restart_count = runtime->restart_count;
    first_restart_tick = runtime->first_restart_tick;
    memset(runtime, 0, sizeof(*runtime));
    runtime->restart_count = restart_count;
    runtime->first_restart_tick = first_restart_tick;
    runtime->state = config->initial_state;
    runtime->context.psp_ns = config->initial_psp_ns;
    runtime->context.msp_ns = config->initial_msp_ns;
    runtime->context.vector_table_ns = config->vector_table;
    /* Reset PC is the guest's reset-handler pointer at vector[1]; the slot
     * already carries the Thumb bit. wt_jump_to_ns strips it before BXNS.
     * Read via the Secure alias of the underlying flash bank — on m33mu
     * a Secure-side read of the 0x08... NS alias returns zero, so we
     * remap to 0x0C... (secure-MPU region 7 covers the guest images). */
    runtime->context.pc = wt_guest_reset_handler(config);
    if (config->guest_id >=
            sizeof(g_bound_exec_bases) / sizeof(g_bound_exec_bases[0]) ||
            g_bound_exec_sizes[config->guest_id] == 0U ||
            runtime->context.pc < g_bound_exec_bases[config->guest_id] ||
            runtime->context.pc >= g_bound_exec_bases[config->guest_id] +
                g_bound_exec_sizes[config->guest_id]) {
        runtime->context.pc = 0U;
        return;
    }
    runtime->context.lr = 0U;
    runtime->context.xpsr = 0x01000000U;
    runtime->context.exc_return = WT_EXC_RETURN_NS_THREAD_MSP_FROM_SECURE;
    runtime->context.frame_stacked = false;
}
