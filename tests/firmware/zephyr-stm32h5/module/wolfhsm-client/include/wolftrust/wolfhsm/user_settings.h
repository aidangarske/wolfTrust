/* wolfCrypt user_settings.h for the wolfTrust Zephyr non-secure guest.
 *
 * Compiled in via -DWOLFSSL_USER_SETTINGS. wolfPSA pulls in its own
 * user_settings (wolfPSA's wolfpsa/user_settings.h is loaded by the
 * wolfPSA module on the same compile path) — both must agree on the
 * primitive set wolfCrypt compiles in. Mirror tests/firmware/stm32h563/
 * nonsecure/user_settings.h with the heap-related guest-isms removed
 * (Zephyr's libc owns malloc/free; we don't pull in any local heap). */

#ifndef WOLFTRUST_ZEPHYR_USER_SETTINGS_H
#define WOLFTRUST_ZEPHYR_USER_SETTINGS_H

#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WOLFSSL_DIR
#define WOLFSSL_USER_IO
#define NO_WRITEV
#define SIZEOF_LONG_LONG 8

/* SP math layer matching the secure side. */
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_NO_DYN_STACK
#define ECC_TIMING_RESISTANT

/* ECC P-256 only — secure side accepts no other curves. */
#define HAVE_ECC
#define ECC_USER_CURVES
#define NO_ECC192
#define NO_ECC224
#define NO_ECC384
#define NO_ECC521

/* AES-CTR counter mode (needed by wolfPSA's PSA_ALG_CTR path). The
 * secure-side wolfHSM crypto_cb already serves CTR via the same
 * wc_AesCtrEncrypt code path it uses for CBC. */
#define WOLFSSL_AES_COUNTER

/* SHA-256 only. */
#define NO_SHA

/* HMAC enabled (NO_HMAC absent); HKDF too. */
#define HAVE_HKDF

/* HashDRBG with secure-side entropy. The wolfHSM client crypto_cb routes
 * RNG requests to the secure HSM; for the CUSTOM_RAND_GENERATE_BLOCK
 * hook wolfCrypt may also call into, see wolfhsm_client_glue.c. */
#define HAVE_HASHDRBG
#define CUSTOM_RAND_GENERATE_BLOCK wolftrust_guest_rng_stub

#ifndef WOLFTRUST_GUEST_RNG_STUB_DECLARED
#define WOLFTRUST_GUEST_RNG_STUB_DECLARED
#ifdef __cplusplus
extern "C" {
#endif
int wolftrust_guest_rng_stub(unsigned char *output, unsigned int sz);
#ifdef __cplusplus
}
#endif
#endif /* WOLFTRUST_GUEST_RNG_STUB_DECLARED */

#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_MD5
#define NO_PWDBASED
#define NO_PKCS12
#define NO_ASN_TIME

#endif /* WOLFTRUST_ZEPHYR_USER_SETTINGS_H */
