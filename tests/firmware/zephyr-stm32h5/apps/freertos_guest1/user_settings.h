/* wolfCrypt + wolfPKCS11 user_settings for the FreeRTOS NS guest.
 *
 * This single header satisfies WOLFSSL_USER_SETTINGS (wolfCrypt's
 * compile-time config trampoline) AND WOLFPKCS11_USER_SETTINGS
 * (wolfPKCS11's pkcs11.h then includes this in place of the autotools-
 * generated wolfpkcs11/options.h). Mirrors the Zephyr guest user_settings
 * but with FREERTOS heap / port hooks turned on. */

#ifndef WOLFTRUST_FREERTOS_NS_USER_SETTINGS_H
#define WOLFTRUST_FREERTOS_NS_USER_SETTINGS_H

#define WOLFCRYPT_ONLY
#define SINGLE_THREADED
#define NO_FILESYSTEM
#define NO_WOLFSSL_DIR
#define WOLFSSL_USER_IO
#define NO_WRITEV
#define SIZEOF_LONG_LONG 8

/* FreeRTOS port hook — XMALLOC / XFREE etc. resolve to FreeRTOS heap_4
 * counterparts where applicable. */
#define FREERTOS

/* SP math layer matching the secure side. */
#define WOLFSSL_SP_MATH
#define WOLFSSL_SP_SMALL
#define WOLFSSL_HAVE_SP_ECC
#define WOLFSSL_SP_NO_DYN_STACK
#define ECC_TIMING_RESISTANT

/* ECC P-256 only — secure side speaks no other curve. */
#define HAVE_ECC
#define ECC_USER_CURVES
#define NO_ECC192
#define NO_ECC224
#define NO_ECC384
#define NO_ECC521

/* SHA-256 only. wolfPKCS11's PKCS11 SHA-1 paths are gated on !NO_SHA. */
#define NO_SHA

/* AES-CBC + AES-CTR (PSA / PKCS11 AES paths). */
#define WOLFSSL_AES_COUNTER

/* HMAC + HKDF kept on. */
#define HAVE_HKDF

/* DRBG + entropy hook. The wolfHSM client glue exports the named stub. */
#define HAVE_HASHDRBG
#define CUSTOM_RAND_GENERATE_BLOCK wolftrust_guest_rng_stub
#ifndef WOLFTRUST_GUEST_RNG_STUB_DECLARED
#define WOLFTRUST_GUEST_RNG_STUB_DECLARED
int wolftrust_guest_rng_stub(unsigned char *output, unsigned int sz);
#endif

/* Trim. */
#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_MD5
#define NO_PWDBASED
#define NO_PKCS12
#define NO_ASN_TIME

/* gcc supports anonymous unions/structs even under -std=c99 — wolfHSM
 * needs HAVE_ANONYMOUS_INLINE_AGGREGATES=1 explicitly because the
 * wolfCrypt auto-detect in types.h defaults it off for C99. */
#define HAVE_ANONYMOUS_INLINE_AGGREGATES 1

/* wolfPKCS11 settings — keep the demo small.
 *   _NO_STORE    : no flash-backed token storage; all keys volatile
 *   _NO_ENV      : no getenv()/setenv() calls (no libc env in NS)
 *   _WOLFHSM     : the slot devId hook this build relies on; the build
 *                  also passes -DWOLFSSL_WOLFHSM_DEVID=WH_DEV_ID so
 *                  wp11_Slot_Init() actually sets slot->devId to the
 *                  wolfHSM client's registered devId. */
#define WOLFPKCS11_NO_STORE
#define WOLFPKCS11_NO_ENV
/* Skip the slot.c C_GetTokenInfo time code (no RTC / time() under -nostdlib). */
#define WOLFPKCS11_NO_TIME

/* wolfPKCS11 internal.h still references `time_t` in function decls and a
 * couple of WP11_Slot fields regardless of WOLFPKCS11_NO_TIME. With
 * -nostdlib we have no <time.h>, so provide the type ourselves. The
 * fields aren't actually used outside the WOLFPKCS11_NO_TIME-gated paths. */
typedef long time_t;

/* wolfPKCS11's WP11_PBKDF2 / WP11_PKCS12_PBKDF wrappers call wc_PBKDF2
 * and wc_PKCS12_PBKDF unconditionally, but our wolfCrypt subset omits
 * them (NO_PWDBASED / NO_PKCS12). The wrappers are dead code at runtime
 * (token-storage / PIN paths are off), but the compiler still needs
 * prototypes + the linker still needs symbols — both supplied by the
 * stubs in apps/freertos_guest1/main.c. Declare here so internal.c
 * (which is compiled before main.c) sees the prototype. */
int wc_PBKDF2(unsigned char *output, const unsigned char *passwd, int pLen,
              const unsigned char *salt, int sLen, int iterations, int kLen,
              int hashType);
int wc_PKCS12_PBKDF(unsigned char *output, const unsigned char *passwd,
                    int pLen, const unsigned char *salt, int sLen,
                    int iterations, int kLen, int hashType, int purpose);

#endif /* WOLFTRUST_FREERTOS_NS_USER_SETTINGS_H */
