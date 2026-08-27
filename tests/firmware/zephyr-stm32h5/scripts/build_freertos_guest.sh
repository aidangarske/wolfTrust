#!/bin/sh
set -eu

# Build the FreeRTOS NS guest1 image: FreeRTOS kernel + ARM_CM33_NTZ port +
# wolfCrypt + wolfPSA + the OS-neutral FF-M client core, linked against the
# secure-side CMSE veneer import library. P7-S3: the raw wolfHSM/wolfPKCS11
# transport is retired — every secure request is SPM-mediated. Output:
#   build/freertos_guest1/freertos_guest1.{elf,bin}
#
# The clone of FreeRTOS is owned by scripts/clone_freertos.sh (idempotent;
# this script just verifies the workspace is there).

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBTREE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
ROOT=$(CDPATH= cd -- "$SUBTREE_DIR/../../.." && pwd)

FREERTOS_DIR="${FREERTOS_DIR:-${SUBTREE_DIR}/.workspace/freertos}"
FREERTOS_KERNEL="${FREERTOS_DIR}/FreeRTOS/Source"
FREERTOS_PORT="${FREERTOS_KERNEL}/portable/GCC/ARM_CM33_NTZ/non_secure"
APP_DIR="${SUBTREE_DIR}/apps/freertos_guest1"
BUILD_DIR="${SUBTREE_DIR}/build/freertos_guest1"
SECURE_CMSE_IMPLIB="${SECURE_CMSE_IMPLIB:-${ROOT}/build/secure_cmse_implib.o}"

WT_GUEST1_FLASH_BASE="${WT_GUEST1_FLASH_BASE:-0x08040000}"
WT_GUEST1_RAM_BASE="${WT_GUEST1_RAM_BASE:-0x20010000}"
WT_GUEST_FLASH_SIZE="${WT_GUEST_FLASH_SIZE:-0x00020000}"
WT_GUEST_RAM_SIZE="${WT_GUEST_RAM_SIZE:-0x00008000}"

WOLFSSL_DIR="${ROOT}/lib/wolfSSL"
WOLFPSA_DIR="${ROOT}/lib/wolfPSA"
BAREMETAL_NS_DIR="${ROOT}/tests/firmware/stm32h563/nonsecure"

if [ ! -d "${FREERTOS_KERNEL}" ]; then
    echo "missing FreeRTOS workspace: ${FREERTOS_KERNEL}" >&2
    echo "run scripts/clone_freertos.sh first" >&2
    exit 1
fi

if [ ! -f "${SECURE_CMSE_IMPLIB}" ]; then
    echo "missing CMSE implib: ${SECURE_CMSE_IMPLIB}" >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"

GUEST_CC="${GUEST_CC:-arm-none-eabi-gcc}"
GUEST_OBJCOPY="${GUEST_OBJCOPY:-arm-none-eabi-objcopy}"

# Sources -- everything compiled as one TU set fed to gcc.
FREERTOS_KERNEL_SRCS="\
${FREERTOS_KERNEL}/tasks.c \
${FREERTOS_KERNEL}/list.c \
${FREERTOS_KERNEL}/queue.c \
${FREERTOS_PORT}/port.c \
${FREERTOS_PORT}/portasm.c \
${FREERTOS_KERNEL}/portable/MemMang/heap_4.c"

WOLFCRYPT_GUEST_SRCS="\
${WOLFSSL_DIR}/wolfcrypt/src/aes.c \
${WOLFSSL_DIR}/wolfcrypt/src/ecc.c \
${WOLFSSL_DIR}/wolfcrypt/src/random.c \
${WOLFSSL_DIR}/wolfcrypt/src/sha256.c \
${WOLFSSL_DIR}/wolfcrypt/src/asn.c \
${WOLFSSL_DIR}/wolfcrypt/src/coding.c \
${WOLFSSL_DIR}/wolfcrypt/src/error.c \
${WOLFSSL_DIR}/wolfcrypt/src/hash.c \
${WOLFSSL_DIR}/wolfcrypt/src/hmac.c \
${WOLFSSL_DIR}/wolfcrypt/src/memory.c \
${WOLFSSL_DIR}/wolfcrypt/src/sha.c \
${WOLFSSL_DIR}/wolfcrypt/src/sp_cortexm.c \
${WOLFSSL_DIR}/wolfcrypt/src/sp_int.c \
${WOLFSSL_DIR}/wolfcrypt/src/wolfmath.c \
${WOLFSSL_DIR}/wolfcrypt/src/wc_port.c \
${WOLFSSL_DIR}/wolfcrypt/src/port/arm/thumb2-aes-asm_c.c \
${WOLFSSL_DIR}/wolfcrypt/src/port/arm/thumb2-sha256-asm_c.c"

# wolfPSA subset for the PSA Crypto front-end (psa_crypto_init /
# psa_generate_random / psa_hash_compute); the rest of the API rides the
# neutral FF-M client directly.
WOLFPSA_SRCS="\
${WOLFPSA_DIR}/src/psa_engine.c \
${WOLFPSA_DIR}/src/psa_crypto.c \
${WOLFPSA_DIR}/src/psa_random.c \
${WOLFPSA_DIR}/src/psa_hash_engine.c"

# OS-neutral wolfTrust NS client core: psa_connect/call/close over the
# WolfTrust_FFM_* veneers + the crypto-service RNG helper.
WT_CLIENT_SRCS="\
${ROOT}/src/client/psa_ffm_client.c \
${ROOT}/src/client/ffm_crypto_client.c"

GUEST_GLUE_SRCS="\
${BAREMETAL_NS_DIR}/libc_stubs_guest.c"

APP_SRCS="${APP_DIR}/main.c"

CFLAGS="\
-mcpu=cortex-m33 -mthumb -mgeneral-regs-only \
-ffreestanding -fno-builtin -nostdlib -Os -g \
-Wall -Wextra -Wno-unused-function -Wno-unused-variable \
-Wno-unused-parameter -Wno-type-limits \
-ffunction-sections -fdata-sections \
-I${APP_DIR} \
-I${FREERTOS_KERNEL}/include \
-I${FREERTOS_PORT} \
-I${ROOT}/include \
-I${WOLFSSL_DIR} \
-I${WOLFPSA_DIR} \
-I${WOLFPSA_DIR}/wolfpsa \
-I${WOLFPSA_DIR}/src \
-I${BAREMETAL_NS_DIR} \
-I${ROOT}/port/stm32h563 \
-DWOLFSSL_USER_SETTINGS \
-DWOLFSSL_PSA_ENGINE \
-DWOLFPSA_NO_TRACE \
-DWC_RESEED_INTERVAL=1000000 \
-DWOLFSSL_SP_ASM -DWOLFSSL_SP_ARM_CORTEX_M_ASM -DWOLFSSL_ARM_ARCH=8 \
-DWOLFSSL_ARMASM -DWOLFSSL_ARMASM_NO_HW_CRYPTO -DWOLFSSL_ARMASM_INLINE \
-DWOLFSSL_ARMASM_NO_NEON -DWOLFSSL_ARMASM_THUMB2 \
-DNO_ERROR_STRINGS \
-DWOLFSSL_PUBLIC_MP \
-DWC_RSA_DIRECT \
-include ${APP_DIR}/user_settings.h"

LDFLAGS="\
-Wl,-T${APP_DIR}/freertos_guest1.ld \
-Wl,--defsym=GUEST_FLASH_ORIGIN=${WT_GUEST1_FLASH_BASE} \
-Wl,--defsym=GUEST_FLASH_LENGTH=${WT_GUEST_FLASH_SIZE} \
-Wl,--defsym=GUEST_RAM_ORIGIN=${WT_GUEST1_RAM_BASE} \
-Wl,--defsym=GUEST_RAM_LENGTH=${WT_GUEST_RAM_SIZE} \
-Wl,--gc-sections"

# shellcheck disable=SC2086
${GUEST_CC} ${CFLAGS} ${LDFLAGS} \
    -o "${BUILD_DIR}/freertos_guest1.elf" \
    ${APP_SRCS} \
    ${FREERTOS_KERNEL_SRCS} \
    ${WOLFCRYPT_GUEST_SRCS} \
    ${WOLFPSA_SRCS} \
    ${WT_CLIENT_SRCS} \
    ${GUEST_GLUE_SRCS} \
    "${SECURE_CMSE_IMPLIB}" \
    -lgcc

# Bypass-absence proof (WT-FFM-0054): the raw wolfHSM client must be gone
# from this image — no wh_Client_* code and no reference to the raw
# WolfTrust_HSM_* transport veneers.
if arm-none-eabi-nm "${BUILD_DIR}/freertos_guest1.elf" | \
        grep -Eq "wh_Client_|wolfhsm_guest_init"; then
    echo "guest1 still links raw HSM bypass symbols" >&2
    exit 1
fi

"${GUEST_OBJCOPY}" -O binary \
    "${BUILD_DIR}/freertos_guest1.elf" \
    "${BUILD_DIR}/freertos_guest1.bin"

arm-none-eabi-size "${BUILD_DIR}/freertos_guest1.elf"
