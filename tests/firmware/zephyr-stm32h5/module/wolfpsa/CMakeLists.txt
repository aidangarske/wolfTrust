# wolfPSA Zephyr module
#
# Compiles the lib/wolfPSA submodule into the Zephyr image and registers a
# SYS_INIT hook that points wolfPSA at the WH_DEV_ID crypto_cb registered by
# the wolfhsm-client module. The result: app code calling psa_*() ends up
# in wolfPSA → wolfCrypt → crypto_cb → wolfHSM client → CMSE → secure side.
#
# The wolfHSM-client module already arranged WOLFSSL_USER_SETTINGS / WOLFHSM_CFG
# / wolfssl include paths and compiled a wolfCrypt subset. This module
# extends that subset with the wolfPSA-specific defines (WOLFSSL_PSA_ENGINE)
# and pulls every wolfPSA src/*.c into the link; the wolfPSA TUs covering
# algorithms we don't enable (RSA, ChaCha+Poly1305, ML-DSA, …) compile to
# empty objects under their own `#if defined(HAVE_…)` guards. The POSIX
# key-storage backend is suppressed via WOLFPSA_CUSTOM_STORE and a no-op
# stub (psa_store_stub.c) — every PSA key in this port is volatile.

if(CONFIG_WOLFPSA)

  set(WT_REPO_ROOT    ${CMAKE_CURRENT_LIST_DIR}/../../../../..)
  set(WT_WOLFPSA_DIR  ${WT_REPO_ROOT}/lib/wolfPSA)
  set(WT_MODULE_DIR   ${CMAKE_CURRENT_LIST_DIR})

  # Expose wolfPSA's public headers globally. Apps that #include <psa/crypto.h>
  # will resolve to lib/wolfPSA/wolfpsa/psa/crypto.h (NOT mbedTLS's copy —
  # the Kconfig depends on !MBEDTLS to enforce this).
  zephyr_system_include_directories(
    ${WT_WOLFPSA_DIR}
    ${WT_WOLFPSA_DIR}/wolfpsa
  )
  # wolfPSA src/*.c also include "psa_trace.h", "psa_size.h" without the
  # wolfpsa/ prefix — add src/ to the include search path so those resolve.
  zephyr_system_include_directories(${WT_WOLFPSA_DIR}/src)

  zephyr_compile_definitions(
    # Gate every wolfPSA TU.
    WOLFSSL_PSA_ENGINE
    # Suppress the POSIX key-storage backend; psa_store_stub.c covers the API.
    WOLFPSA_CUSTOM_STORE
    # Zephyr has no process environment or stderr-backed trace sink.
    WOLFPSA_NO_TRACE
  )

  zephyr_library_named(wolftrust_wolfpsa)

  # Compile every wolfPSA src/*.c EXCEPT psa_store_posix.c (POSIX-only).
  file(GLOB WOLFPSA_SRCS ${WT_WOLFPSA_DIR}/src/*.c)
  list(FILTER WOLFPSA_SRCS EXCLUDE REGEX "psa_store_posix\\.c$")
  # wolfTrust supplies the target-specific Initial Attestation backend.
  list(FILTER WOLFPSA_SRCS EXCLUDE REGEX "psa_attestation\\.c$")
  zephyr_library_sources(${WOLFPSA_SRCS})

  zephyr_library_sources(
    ${WT_MODULE_DIR}/src/psa_store_stub.c
    ${WT_MODULE_DIR}/src/wolfpsa_zephyr_init.c
  )

  # wolfPSA needs SHA-384/SHA-512 and a few other wolfCrypt sources that
  # the wolfhsm-client module's minimal subset doesn't include. List them
  # here; CMake dedupes by absolute path if any happen to overlap.
  set(WT_WOLFSSL_DIR ${WT_REPO_ROOT}/lib/wolfSSL)
  zephyr_library_sources(
    ${WT_WOLFSSL_DIR}/wolfcrypt/src/sha512.c
    ${WT_WOLFSSL_DIR}/wolfcrypt/src/kdf.c
  )

  # Treat third-party warnings as warnings, not errors. wolfHSM-client
  # already tones things down for its own subset; mirror that for wolfPSA.
  set_source_files_properties(
    ${WOLFPSA_SRCS}
    ${WT_WOLFSSL_DIR}/wolfcrypt/src/sha512.c
    ${WT_WOLFSSL_DIR}/wolfcrypt/src/kdf.c
    PROPERTIES COMPILE_OPTIONS
      "-Wno-unused-function;-Wno-unused-variable;-Wno-unused-parameter;-Wno-type-limits"
  )

endif()
