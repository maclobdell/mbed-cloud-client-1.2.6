// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
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

#ifndef __KEYS_CONFIG_MANAGER_H__
#define __KEYS_CONFIG_MANAGER_H__

#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "kcm_status.h"
#include "kcm_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @file key_config_manager.h
*  \brief Keys and Configuration Manager (KCM) APIs.
*/

/* === Initialization and Finalization === */

/**
*   Initiate the KCM module.
*   Allocates and initializes file storage resources.
*
*    @returns
*       KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_init(void);

/**
*   Finalize the KCM module.
*   Finalizes and frees file storage resources.
*
*    @returns
*       KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_finalize(void);

/* === Keys, Certificates and Configuration data storage === */

/** Store the KCM item into a secure storage.
*
*    @param[in] kcm_item_name KCM item name.
*    @param[in] kcm_item_name_len KCM item name length.
*    @param[in] kcm_item_type KCM item type as defined in `::kcm_item_type_e`
*    @param[in] kcm_item_is_factory True if the KCM item is a factory item, otherwise false.
*    @param[in] kcm_item_data KCM item data buffer. Can be NULL if `kcm_item_data_size` is 0.
*    @param[in] kcm_item_data_size KCM item data buffer size in bytes. Can be 0 if you wish to store an empty file.
*    @param[in] security_desc Security descriptor.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_item_store(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, bool kcm_item_is_factory, const uint8_t *kcm_item_data, size_t kcm_item_data_size, const kcm_security_desc_s security_desc);

/* === Keys, Certificates and Configuration data retrieval === */

/** Retrieve the KCM item data size from a secure storage.
*
*    @param[in] kcm_item_name KCM item name.
*    @param[in] kcm_item_name_len KCM item name length.
*    @param[in] kcm_item_type KCM item type as defined in `::kcm_item_type_e`
*    @param[out] kcm_item_data_size_out KCM item data size in bytes.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_item_get_data_size(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, size_t *kcm_item_data_size_out);

/** Retrieve KCM item data from a secure storage.
*
*    @param[in] kcm_item_name KCM item name.
*    @param[in] kcm_item_name_len KCM item name length.
*    @param[in] kcm_item_type KCM item type as defined in `::kcm_item_type_e`
*    @param[out] kcm_item_data_out KCM item data output buffer. Can be NULL if `kcm_item_data_size` is 0.
*    @param[in] kcm_item_data_max_size The maximum size of the KCM item data output buffer in bytes.
*    @param[out] kcm_item_data_act_size_out Actual KCM item data output buffer size in bytes.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_item_get_data(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, uint8_t *kcm_item_data_out, size_t kcm_item_data_max_size, size_t * kcm_item_data_act_size_out);

/* === Keys, Certificates and Configuration update === */

/** Update KCM item data in a secure storage.
*
*    @param[in] kcm_item_name KCM item name.
*    @param[in] kcm_item_name_len KCM item name length.
*    @param[in] kcm_item_type KCM item type as defined in `::kcm_item_type_e`
*    @param[in] kcm_item_data KCM item data buffer.
*    @param[in] kcm_item_data_size KCM item data buffer size in bytes.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_item_update_data(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, const uint8_t *kcm_item_data, size_t kcm_item_data_size);

/* === Keys, Certificates and Configuration delete === */

/** Delete a KCM item from a secure storage.
*
*    @param[in] kcm_item_name KCM item name.
*    @param[in] kcm_item_name_len KCM item name length.
*    @param[in] kcm_item_type KCM item type as defined in `::kcm_item_type_e`
*
*    @returns
*        KCM_STATUS_SUCCESS status in case of success or one of ::kcm_status_e errors otherwise.
*/
kcm_status_e kcm_item_delete(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type);


/* === Factory Reset === */

/**  Reset the KCM secure storage to factory state.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_factory_reset(void);



#ifndef __DOXYGEN__
/* === Keys and CSR generation === */

/**  Generate a key pair complying the given crypto scheme DER.
*    Saves the private key and exposes the public key.
*
*      @param key_scheme The crypto scheme.
*      @param key_name The key name for which a key pair is generated.
*      @param key_name_len Key name length.
*      @param pub_key_der_out Public key to generate in DER format.
*      @param pub_key_der_size Public key size in bytes.
*      @param priv_key_sec_desc Private key security descriptor.
*      @param pub_key_sec_desc Public key security descriptor.
*
*      @returns
*        Operation status.
*/
kcm_status_e kcm_key_pair_generate_and_store(kcm_crypto_key_scheme_s key_scheme, const uint8_t *key_name, size_t key_name_len,
        uint8_t *pub_key_der_out, size_t pub_key_der_size,
        const kcm_security_desc_s priv_key_sec_desc, const kcm_security_desc_s pub_key_sec_desc);

/** Generate a general CSR from the given private and public keys.
*   Further design is needed
*
*     @param key_name The key name to fetch from storage(public/private).
*     @param key_name_len The key name len.
*     @param csr_out Pointer to generated E2E CSR.
*     @param csr_size_out Size of the E2E CSR.
*
*     @returns
*        Operation status.
*/
kcm_status_e kcm_csr_generate(const uint8_t *key_name, size_t key_name_len,
                              uint8_t **csr_out, size_t *csr_size_out);
#endif //#ifndef __DOXYGEN__

#ifdef __cplusplus
}
#endif

#endif //__KEYS_CONFIG_MANAGER_H__

