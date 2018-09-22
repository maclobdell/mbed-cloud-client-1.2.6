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

#ifndef __FACTORY_CONFIGURATOR_CLIENT_H__
#define __FACTORY_CONFIGURATOR_CLIENT_H__

#include <stdlib.h>
#include <inttypes.h>
#include "fcc_status.h"
#include "fcc_output_info_handler.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @file factory_configurator_client.h
*  \brief factory configurator client APIs.
*/

/* === Defines === */
#define FCC_ENTROPY_SIZE                   56
#define FCC_ROT_SIZE                       24

/* === Initialization and Finalization === */

/** Initiates the FCC module.
*
*   @returns
*       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
*/
fcc_status_e fcc_init(void);


/** Finalizes the FCC module.
*   Finalizes and frees file storage resources.
*
*    @returns
*       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
*/

fcc_status_e fcc_finalize(void);

/* === Factory clean operation === */

/** Cleans from the device all data that was saved during the factory process.
*  Should be called if the process failed and needs to be executed again.
*
*   @returns
*       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
*/
fcc_status_e fcc_storage_delete(void);


/* === Warning and errors data operations === */

/** The function retrieves pointer to warning and errors structure.
*  Should be called after fcc_verify_device_configured_4mbed_cloud, when possible warning and errors was
*  stored in the structure.
*  The structure contains data of last fcc_verify_device_configured_4mbed_cloud run.*
*   @returns pointer to fcc_output_info_s structure.
*/
fcc_output_info_s* fcc_get_error_and_warning_data(void);

/* === Verification === */

/** Verifies that all mandatory fields needed to connect to mbed Cloud are in place on the device.
 *  Should be called in the end of the factory process
 *
 *    @returns
 *       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
 */
fcc_status_e fcc_verify_device_configured_4mbed_cloud(void);


/* === Developer flow === */

/** This API is for developers only.
*   You can download the `mbed_cloud_dev_credentials.c` file from the portal and thus, skip running FCU on PC side.
*   The API reads all credentials from the `mbed_cloud_dev_credentials.c` file and stores them in the KCM.
*   RoT, Entropy and Time configurations are not a part of fcc_developer_flow() API. Devices that need to set RoT or Entropy
*   should call `fcc_rot_set()`/`fcc_entropy_set()` APIs before fcc_developer_flow().
*   If device does not have it's own time configuration and `fcc_secure_time_set()` was not called before  fcc_developer_flow(),
*   during fcc_verify_device_configured_4mbed_cloud() certificate time validity will not be checked.
*
*
*   @returns
*       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
*/
fcc_status_e fcc_developer_flow(void);

#ifndef __DOXYGEN__ //Not implemented features

/* === Secure Time === */

/** Sets Secure time. This function will set the secure time to what the user provides.
*   Secure time must be set in order to enable certificate expiration validations.
*
*     @param time The secure time to set.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_secure_time_set(uint64_t time);

/* === Entropy and RoT injection === */
/** Sets Entropy.
*   If user wishes to set his own entropy, this function must be called after fcc_init() and prior to any other FCC or KCM functions.
*
*     @param buf The buffer containing the entropy.
*     @param buf_size The size of buf in bytes. Must be exactly FCC_ENTROPY_SIZE.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_entropy_set(const uint8_t *buf, size_t buf_size);

/** Sets root of trust
*   If user wishes to set his own root of trust, this function must be called after fcc_init() and fcc_entropy_set() (if user sets his own entropy),
and prior to any other FCC or KCM functions.
*
*     @param buf The buffer containing the root of trust.
*     @param buf_size The size of buf in bytes. Must be exactly FCC_ROT_SIZE.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_rot_set(const uint8_t *buf, size_t buf_size);


/* === Factory flow disable === */
/** Sets Factory disabled flag to disable further use of the factory flow.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_factory_disable(void);

/** Returns true if the factory flow was disabled by calling fcc_factory_disable() API, outherwise
*   returns false.
*
*   - If the factory flow is already disabled any FCC API(s) will fail.
*
*     @param fcc_factory_disable An output parameter, will be set to "true" in case factory
*                                     flow is already disabled, "false" otherwise.
*
*   @returns
*       FCC_STATUS_SUCCESS in case of success or one of the `::fcc_status_e` errors otherwise.
*/
fcc_status_e fcc_is_factory_disabled(bool *fcc_factory_disable);

/* === CSR generation === */

/** Generates bootstrap CSR from a given private and public keys in DER encoding scheme.
*   Further design is needed
*
*     @param key_name The key name to fetch from storage(public/private).
*     @param key_name_len The key name len.
*     @param bootstrap_csr_out Pointer to generated bootstrap CSR.
*     @param bootstrap_csr_size_out Size of the CSR.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_bootstrap_csr_generate(const uint8_t *key_name, size_t key_name_len,
                                        uint8_t **bootstrap_csr_out, size_t *bootstrap_csr_size_out);


/** Generates E2E CSR from a given private and public keys
*   Further design is needed
*
*     @param key_name The key name to fetch from storage(public/private).
*     @param key_name_len The key name len.
*     @param e2e_csr_out Pointer to generated E2E CSR.
*     @param e2e_csr_size_out Size of the E2E CSR.
*
*     @returns
*        Operation status.
*/
fcc_status_e fcc_e2e_csr_generate(const uint8_t *key_name, size_t key_name_len,
                                  uint8_t **e2e_csr_out, size_t *e2e_csr_size_out);

#endif
#ifdef __cplusplus
}
#endif

#endif //__FACTORY_CONFIGURATOR_CLIENT_H__
