/* production_main.c
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

#include "wolftrust/spm.h"
#include "wolftrust/partition.h"
#include "wolftrust_manifest_generated.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    wt_spm_t spm;
    wt_system_manifest_t invalid_manifest;
    wt_domain_descriptor_t invalid_domains[WT_MANIFEST_MAX_PARTITIONS];
    wt_guest_config_t invalid_config;
    const wt_guest_config_t* configs;
    const wt_domain_descriptor_t* guest_domain;
    size_t config_count;
    size_t domain_count;
    int result;

    result = wt_spm_init(&spm, wt_generated_manifest_get(),
                         WT_MANIFEST_FEATURE_IPC);
    if (result != WT_SPM_VALID || !wt_spm_ready(&spm)) {
        (void)fprintf(stderr, "production manifest rejected: %d\n", result);
        return 1;
    }

    if (wt_partitions_bind_manifest(wt_spm_manifest(&spm)) != 0) {
        (void)fprintf(stderr, "production partition binding rejected\n");
        return 1;
    }

    configs = wt_partitions_config_table(&config_count);
    if (config_count == 0U || spm.manifest->domain_count < 2U) {
        return 1;
    }
    guest_domain = &spm.manifest->domains[1];
    if (wt_partition_validate_port_binding(&configs[0], guest_domain) !=
            WT_PORT_VALID) {
        (void)fprintf(stderr, "production port binding rejected\n");
        return 1;
    }

    invalid_config = configs[0];
    invalid_config.port.provided_capabilities &=
        ~WT_PORT_CAPABILITY_HSM_TRANSPORT;
    if (wt_partition_validate_port_binding(&invalid_config, guest_domain) !=
            WT_PORT_ERROR_CAPABILITY) {
        (void)fprintf(stderr, "missing port capability was accepted\n");
        return 1;
    }

    invalid_config = configs[0];
    invalid_config.port.vector_read_address = invalid_config.vector_table;
    if (wt_partition_validate_port_binding(&invalid_config, guest_domain) !=
            WT_PORT_ERROR_VECTOR_ALIAS) {
        (void)fprintf(stderr, "direct vector address was accepted as alias\n");
        return 1;
    }

    invalid_config = configs[0];
    invalid_config.port.hsm_transport.base = UINTPTR_MAX - 1U;
    invalid_config.port.hsm_transport.size = 4U;
    if (wt_partition_validate_port_binding(&invalid_config, guest_domain) !=
            WT_PORT_ERROR_HSM_TRANSPORT) {
        (void)fprintf(stderr, "overflowing HSM transport was accepted\n");
        return 1;
    }

    invalid_config = configs[0];
    invalid_config.port.hsm_transport.base = invalid_config.vector_table;
    invalid_config.port.hsm_transport.size = 4U;
    if (wt_partition_validate_port_binding(&invalid_config, guest_domain) !=
            WT_PORT_ERROR_HSM_TRANSPORT) {
        (void)fprintf(stderr, "executable HSM transport was accepted\n");
        return 1;
    }

    domain_count = wt_spm_manifest(&spm)->domain_count;
    if (domain_count > sizeof(invalid_domains) / sizeof(invalid_domains[0])) {
        return 1;
    }
    (void)memcpy(invalid_domains, wt_spm_manifest(&spm)->domains,
                 domain_count * sizeof(invalid_domains[0]));
    invalid_manifest = *wt_spm_manifest(&spm);
    invalid_domains[1].restart_policy.restart_limit++;
    invalid_manifest.domains = invalid_domains;
    if (wt_partitions_bind_manifest(&invalid_manifest) == 0) {
        (void)fprintf(stderr, "invalid restart policy was accepted\n");
        return 1;
    }

    (void)printf("production manifest and port capabilities accepted: "
                 "profile=%u domains=%zu\n",
                 (unsigned int)spm.manifest->isolation_profile,
                 spm.manifest->domain_count);
    return 0;
}
