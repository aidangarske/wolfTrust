#!/bin/sh
set -eu

# Pulls FreeRTOS into the gitignored .workspace/freertos/ subtree and
# initialises the kernel submodule it bundles. Idempotent: subsequent
# invocations skip the clone and just refresh the kernel submodule. The
# tree is intentionally NOT vendored — wolftrust's dependency story keeps
# FreeRTOS as an external fetch the same way Zephyr is.

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBTREE_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR="${SUBTREE_DIR}/.workspace"
FREERTOS_DIR="${FREERTOS_DIR:-${WORKSPACE_DIR}/freertos}"
FREERTOS_REPO="${FREERTOS_REPO:-https://github.com/FreeRTOS/FreeRTOS.git}"
FREERTOS_REF="${FREERTOS_REF:-main}"

mkdir -p "${WORKSPACE_DIR}"

if [ ! -d "${FREERTOS_DIR}/.git" ]; then
    git clone --branch "${FREERTOS_REF}" --depth 1 \
        "${FREERTOS_REPO}" "${FREERTOS_DIR}"
fi

# Only the kernel submodule is needed for a Cortex-M33 NTZ NS app —
# leave FreeRTOS-Plus / corePKCS11 / coreMQTT etc. unfetched so the
# clone stays cheap. Override via FREERTOS_SUBMODULES if the demo
# grows to need more.
FREERTOS_SUBMODULES="${FREERTOS_SUBMODULES:-FreeRTOS/Source}"

git -C "${FREERTOS_DIR}" submodule update --init --depth 1 \
    ${FREERTOS_SUBMODULES}
