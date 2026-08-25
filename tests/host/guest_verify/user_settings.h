/* user_settings.h
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

/* SHA-256-only wolfCrypt configuration for the guest launch-verify host test. */

#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

#define WOLFCRYPT_ONLY
#define WOLFSSL_USER_IO
#define NO_TLS

#define WOLFSSL_SHA256
#define NO_SHA

#define NO_RSA
#define NO_DH
#define NO_DSA
#define NO_DES3
#define NO_RC4
#define NO_MD4
#define NO_MD5
#define NO_PWDBASED
#define NO_PKCS7
#define NO_ASN
#define NO_CERTS
#define NO_BIG_INT
#define WC_NO_RNG

#define WOLFSSL_IGNORE_FILE_WARN
#define NO_MAIN_DRIVER
#define NO_ERROR_QUEUE
#define NO_INLINE

#endif /* USER_SETTINGS_H */
