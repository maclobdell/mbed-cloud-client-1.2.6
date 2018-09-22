// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#ifndef ARM_UPDATE_CRYPTO_H
#define ARM_UPDATE_CRYPTO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "arm_uc_error.h"
#include "arm_uc_types.h"
#include "arm_uc_config.h"

#ifndef ARM_UC_USE_PAL_CRYPTO
#define ARM_UC_USE_PAL_CRYPTO 0
#endif

#if ARM_UC_USE_PAL_CRYPTO
#include "pal.h"
#ifndef palMDHandle_t
#include "pal_Crypto.h"
#endif

typedef palMDHandle_t arm_uc_mdHandle_t;
typedef palMDType_t arm_uc_mdType_t;
typedef struct arm_uc_cipherHandle_t {
    palAesHandle_t aes_context;
    uint8_t* aes_iv;
} arm_uc_cipherHandle_t;

#define ARM_UC_CU_SHA256 PAL_SHA256

#else // ARM_UC_USE_PAL_CRYPTO

#include "mbedtls/md_internal.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/aes.h"
#include "mbedtls/cipher.h"
typedef mbedtls_md_context_t arm_uc_mdHandle_t;
typedef mbedtls_md_type_t arm_uc_mdType_t;
typedef struct arm_uc_cipherHandle_t {
    mbedtls_aes_context aes_context;
    uint8_t  aes_partial[MBEDTLS_MAX_BLOCK_LENGTH];
    uint8_t* aes_iv;
    size_t   aes_nc_off;
} arm_uc_cipherHandle_t;

#define ARM_UC_CU_SHA256 MBEDTLS_MD_SHA256

#endif // ARM_UC_USE_PAL_CRYPTO

/**
 * @brief Verify a public key signature
 * @details This function loads a certificate out of `ca`, and validates `hash` using the certificate and `sig`. If the
 *          certificate used by this function requires a certificate chain validation (i.e. it is not the root of trust,
 *          or it has not been previously validated), certificate chain validation should be done prior to calling this
 *          function.
 *
 * WARNING: this function is to be used only inside a function where its arguments have been error checked.
 * WARNING: This is an internal utility function and is not accessible outside of the manifest manager.
 *
 * @param[in] ca A pointer to a buffer that contains the signing certificate.
 * @param[in] hash A pointer to a buffer containing the hash to verify.
 * @param[in] sig A pointer to a buffer containing a signature by `ca`
 * @retval MFST_ERR_CERT_INVALID when the certificate fails to load
 * @retval MFST_ERR_INVALID_SIGNATURE when signature verification fails
 * @retval MFST_ERR_NONE for a valid signature
 */
arm_uc_error_t ARM_UC_verifyPkSignature(const arm_uc_buffer_t* ca, const arm_uc_buffer_t* hash, const arm_uc_buffer_t* sig);
arm_uc_error_t ARM_UC_cryptoHashSetup(arm_uc_mdHandle_t* h, arm_uc_mdType_t mdType);
arm_uc_error_t ARM_UC_cryptoHashUpdate(arm_uc_mdHandle_t* h, arm_uc_buffer_t* input);
arm_uc_error_t ARM_UC_cryptoHashFinish(arm_uc_mdHandle_t* h, arm_uc_buffer_t* output);
arm_uc_error_t ARM_UC_cryptoDecryptSetup(arm_uc_cipherHandle_t* h, arm_uc_buffer_t* key, arm_uc_buffer_t* iv, int32_t bits);
arm_uc_error_t ARM_UC_cryptoDecryptUpdate(arm_uc_cipherHandle_t* h, const uint8_t* input_ptr, uint32_t input_size, arm_uc_buffer_t* output);
arm_uc_error_t ARM_UC_cryptoDecryptFinish(arm_uc_cipherHandle_t* h, arm_uc_buffer_t* output);

#ifdef __cplusplus
}
#endif

#endif // ARM_UPDATE_CRYPTO_H
