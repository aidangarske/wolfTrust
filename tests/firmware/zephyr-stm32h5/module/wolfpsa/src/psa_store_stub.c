/* Minimal psa_store backend: keys can't be persisted in this Zephyr port.
 *
 * The wolfPSA upstream Makefile ships psa_store_posix.c which uses open(2)
 * / read(2) / write(2). Zephyr's NS image has no filesystem and no plans
 * for one in this demo — every PSA key we create is volatile, so the
 * persistent-store callbacks are never reached. Define WOLFPSA_CUSTOM_STORE
 * (via the wolfpsa module's compile-definitions) to keep psa_store_posix.c
 * compiled to an empty TU, and provide the API shells here returning
 * `not supported`. */

#include <stddef.h>

#include <wolfpsa/psa_store.h>

int wolfPSA_Store_Open(int type, unsigned long id1, unsigned long id2,
                       int read, void **out)
{
    (void)type; (void)id1; (void)id2; (void)read;
    if (out != NULL) {
        *out = NULL;
    }
    return -1;
}

int wolfPSA_Store_OpenSz(int type, unsigned long id1, unsigned long id2,
                         int read, int variableSz, void **out)
{
    (void)type; (void)id1; (void)id2; (void)read; (void)variableSz;
    if (out != NULL) {
        *out = NULL;
    }
    return -1;
}

int wolfPSA_Store_Remove(int type, unsigned long id1, unsigned long id2)
{
    (void)type; (void)id1; (void)id2;
    return -1;
}

void wolfPSA_Store_Close(void *store)
{
    (void)store;
}

int wolfPSA_Store_Read(void *store, unsigned char *buffer, int len)
{
    (void)store; (void)buffer; (void)len;
    return -1;
}

int wolfPSA_Store_Write(void *store, unsigned char *buffer, int len)
{
    (void)store; (void)buffer; (void)len;
    return -1;
}
