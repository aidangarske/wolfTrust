cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wolftrust_guest0_psa)

if(NOT DEFINED WOLFTRUST_CMSE_IMPLIB)
  message(FATAL_ERROR "WOLFTRUST_CMSE_IMPLIB must point at build/secure_cmse_implib.o")
endif()

zephyr_include_directories(
  ${ZEPHYR_BASE}/modules/cmsis_6
)

set(WT_REPO_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
set(WT_WOLFCOSE_DIR ${WT_REPO_ROOT}/lib/wolfCOSE)

target_sources(app PRIVATE
  src/main.c
  src/attestation_verify.c
  ${WOLFTRUST_CMSE_IMPLIB}
  ${WT_WOLFCOSE_DIR}/src/wolfcose.c
  ${WT_WOLFCOSE_DIR}/src/wolfcose_cbor.c
)
target_include_directories(app PRIVATE ${WT_WOLFCOSE_DIR}/include)
target_compile_definitions(app PRIVATE
  WOLFCOSE_LEAN
  WOLFCOSE_NO_SIGN1_SIGN
  WOLFCOSE_NO_ENCRYPT0
  WOLFCOSE_NO_MAC0
  WOLFCOSE_NO_KEY_ENCODE
  WOLFCOSE_NO_KEY_DECODE
)
if(DEFINED WT_EXPECTED_MEASUREMENT_HEX)
    target_compile_definitions(app PRIVATE
        WT_EXPECTED_MEASUREMENT_HEX="${WT_EXPECTED_MEASUREMENT_HEX}")
endif()

if(NOT DEFINED WT_EXPECTED_LIFECYCLE)
    set(WT_EXPECTED_LIFECYCLE 0x3000u)
endif()
target_compile_definitions(app PRIVATE
    WT_EXPECTED_LIFECYCLE=${WT_EXPECTED_LIFECYCLE})

if(WT_ATTESTATION_DEVELOPMENT_PROFILE)
    target_compile_definitions(app PRIVATE
        WT_ATTESTATION_DEVELOPMENT_PROFILE=1)
endif()

if(WT_GUEST_FAULT_PROBE)
    target_compile_definitions(app PRIVATE WT_GUEST_FAULT_PROBE=1)
endif()

if(WT_M33MU_EXPECT_BKPT)
    target_compile_definitions(app PRIVATE WT_M33MU_EXPECT_BKPT=1)
endif()

set_source_files_properties(
  ${WT_WOLFCOSE_DIR}/src/wolfcose.c
  ${WT_WOLFCOSE_DIR}/src/wolfcose_cbor.c
  PROPERTIES COMPILE_OPTIONS
    "-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter;-Wno-type-limits"
)

# P3a-4a: run Arm's unmodified psa-arch-tests val NSPE in this guest against the
# real SPM. WT_RUN_CONFORMANCE selects the conformance secure image (server/
# client SPs). The val sources and the generated test/manifest headers come from
# the secure build tree (built first). include/ before build/manifest so
# wolfTrust's psa/*.h win; build/manifest/ns holds the i001-only test list.
if(WT_RUN_CONFORMANCE)
  set(WT_CONF_U ${WT_REPO_ROOT}/build/upstream/psa-arch-tests/api-tests)
  set(WT_CONF_GEN ${WT_REPO_ROOT}/build/manifest)
  target_sources(app PRIVATE
    src/conformance_pal.c
    ${WT_CONF_U}/val/common/val_log.c
    ${WT_CONF_U}/val/nspe/val_entry.c
    ${WT_CONF_U}/val/nspe/val_dispatcher.c
    ${WT_CONF_U}/val/nspe/val_framework.c
    ${WT_CONF_U}/val/nspe/val_interfaces.c
    ${WT_CONF_U}/val/nspe/val_peripherals.c
    ${WT_CONF_U}/val/nspe/val_platform.c
    ${WT_CONF_U}/val/nspe/val_crypto.c
    ${WT_CONF_U}/val/nspe/val_storage.c
    ${WT_CONF_U}/val/nspe/val_attestation.c
    ${WT_CONF_U}/platform/targets/common/nspe/pal_weak.c
    ${WT_CONF_U}/ff/ipc/test_i001/test_entry_i001.c
    ${WT_CONF_U}/ff/ipc/test_i001/test_i001.c
    ${WT_CONF_U}/ff/ipc/test_i003/test_entry_i003.c
    ${WT_CONF_U}/ff/ipc/test_i003/test_i003.c
    ${WT_CONF_U}/ff/ipc/test_i047/test_entry_i047.c
    ${WT_CONF_U}/ff/ipc/test_i047/test_i047.c
    ${WT_CONF_U}/ff/ipc/test_i055/test_entry_i055.c
    ${WT_CONF_U}/ff/ipc/test_i055/test_i055.c
    ${WT_CONF_U}/ff/ipc/test_i057/test_entry_i057.c
    ${WT_CONF_U}/ff/ipc/test_i057/test_i057.c
    ${WT_CONF_U}/ff/ipc/test_i058/test_entry_i058.c
    ${WT_CONF_U}/ff/ipc/test_i058/test_i058.c
    ${WT_CONF_U}/ff/ipc/test_i063/test_entry_i063.c
    ${WT_CONF_U}/ff/ipc/test_i063/test_i063.c
    ${WT_CONF_U}/ff/ipc/test_i064/test_entry_i064.c
    ${WT_CONF_U}/ff/ipc/test_i064/test_i064.c
    ${WT_CONF_U}/ff/ipc/test_i065/test_entry_i065.c
    ${WT_CONF_U}/ff/ipc/test_i065/test_i065.c
    ${WT_CONF_U}/ff/ipc/test_i066/test_entry_i066.c
    ${WT_CONF_U}/ff/ipc/test_i066/test_i066.c
    ${WT_CONF_U}/ff/ipc/test_i071/test_entry_i071.c
    ${WT_CONF_U}/ff/ipc/test_i071/test_i071.c
    ${WT_CONF_U}/ff/ipc/test_i088/test_entry_i088.c
    ${WT_CONF_U}/ff/ipc/test_i088/test_i088.c
  )
  target_include_directories(app PRIVATE
    ${WT_REPO_ROOT}/include
    ${WT_CONF_GEN}/ns
    ${WT_CONF_GEN}
    ${WT_CONF_U}/val/common
    ${WT_CONF_U}/val/nspe
    ${WT_CONF_U}/val/spe
    ${WT_CONF_U}/platform/targets/common/nspe
    ${WT_REPO_ROOT}/port/stm32h563/conformance
    ${WT_CONF_U}/ff/ipc/test_i001
    ${WT_CONF_U}/ff/ipc/test_i003
    ${WT_CONF_U}/ff/ipc/test_i047
    ${WT_CONF_U}/ff/ipc/test_i055
    ${WT_CONF_U}/ff/ipc/test_i057
    ${WT_CONF_U}/ff/ipc/test_i058
    ${WT_CONF_U}/ff/ipc/test_i063
    ${WT_CONF_U}/ff/ipc/test_i064
    ${WT_CONF_U}/ff/ipc/test_i065
    ${WT_CONF_U}/ff/ipc/test_i066
    ${WT_CONF_U}/ff/ipc/test_i071
    ${WT_CONF_U}/ff/ipc/test_i088
  )
  target_compile_definitions(app PRIVATE
    WT_RUN_CONFORMANCE VAL_NSPE_BUILD NONSECURE_TEST_BUILD IPC VERBOSITY=3)
  set_source_files_properties(
    ${WT_CONF_U}/val/common/val_log.c
    ${WT_CONF_U}/val/nspe/val_entry.c
    ${WT_CONF_U}/val/nspe/val_dispatcher.c
    ${WT_CONF_U}/val/nspe/val_framework.c
    ${WT_CONF_U}/val/nspe/val_interfaces.c
    ${WT_CONF_U}/val/nspe/val_peripherals.c
    ${WT_CONF_U}/val/nspe/val_platform.c
    ${WT_CONF_U}/val/nspe/val_crypto.c
    ${WT_CONF_U}/val/nspe/val_storage.c
    ${WT_CONF_U}/val/nspe/val_attestation.c
    ${WT_CONF_U}/platform/targets/common/nspe/pal_weak.c
    ${WT_CONF_U}/ff/ipc/test_i001/test_entry_i001.c
    ${WT_CONF_U}/ff/ipc/test_i001/test_i001.c
    ${WT_CONF_U}/ff/ipc/test_i003/test_entry_i003.c
    ${WT_CONF_U}/ff/ipc/test_i003/test_i003.c
    ${WT_CONF_U}/ff/ipc/test_i047/test_entry_i047.c
    ${WT_CONF_U}/ff/ipc/test_i047/test_i047.c
    ${WT_CONF_U}/ff/ipc/test_i055/test_entry_i055.c
    ${WT_CONF_U}/ff/ipc/test_i055/test_i055.c
    ${WT_CONF_U}/ff/ipc/test_i057/test_entry_i057.c
    ${WT_CONF_U}/ff/ipc/test_i057/test_i057.c
    ${WT_CONF_U}/ff/ipc/test_i058/test_entry_i058.c
    ${WT_CONF_U}/ff/ipc/test_i058/test_i058.c
    ${WT_CONF_U}/ff/ipc/test_i063/test_entry_i063.c
    ${WT_CONF_U}/ff/ipc/test_i063/test_i063.c
    ${WT_CONF_U}/ff/ipc/test_i064/test_entry_i064.c
    ${WT_CONF_U}/ff/ipc/test_i064/test_i064.c
    ${WT_CONF_U}/ff/ipc/test_i065/test_entry_i065.c
    ${WT_CONF_U}/ff/ipc/test_i065/test_i065.c
    ${WT_CONF_U}/ff/ipc/test_i066/test_entry_i066.c
    ${WT_CONF_U}/ff/ipc/test_i066/test_i066.c
    ${WT_CONF_U}/ff/ipc/test_i071/test_entry_i071.c
    ${WT_CONF_U}/ff/ipc/test_i071/test_i071.c
    ${WT_CONF_U}/ff/ipc/test_i088/test_entry_i088.c
    ${WT_CONF_U}/ff/ipc/test_i088/test_i088.c
    PROPERTIES COMPILE_OPTIONS
      "-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter"
  )
endif()
