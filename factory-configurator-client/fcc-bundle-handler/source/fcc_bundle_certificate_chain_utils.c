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

#include "fcc_bundle_handler.h"
#include "cn-cbor.h"
#include "pv_error_handling.h"
#include "fcc_bundle_utils.h"
#include "key_config_manager.h"
#include "fcc_output_info_handler.h"
#include "fcc_malloc.h"
#include "fcc_time_profiling.h"


static bool get_certificate_suffix(char *suff, uint32_t index)
{
    char letters[26] = "abcdefghijklmnopqrstuvwxyz";

    if (index > 25)
        return false;

    *suff =  letters[index];
    return true;
}
static bool add_suffix_to_name(const uint8_t *cert_name, size_t cert_name_len, const char *suff, uint8_t **buffer_out, size_t *buffer_size_allocated_out)
{
    size_t suff_length;


    suff_length = 1;

    *buffer_out = (uint8_t *)fcc_malloc(cert_name_len + suff_length +1);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((*buffer_out == NULL), false, "Failed allocating buffer_out");

    /* Append suffix and name to allocated buffer */
    memcpy(*buffer_out, (uint8_t *)cert_name, cert_name_len);
    memcpy(*buffer_out + cert_name_len, (uint8_t *)suff, suff_length);
    (*(*buffer_out+ cert_name_len + suff_length)) = '\0';
    *buffer_size_allocated_out = cert_name_len + suff_length + 1;

    return true;
}

/** Processes  certificate chain list.
* The function extracts data parameters for each certificate chain and stores it.
*
* @param cert_chains_list_cb[in]   The cbor structure with certificate chain list.
*
* @return
*     true for success, false otherwise.
*/
fcc_status_e fcc_bundle_process_certificate_chains(const cn_cbor *cert_chains_list_cb)
{
    bool status = false;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_result =  KCM_STATUS_SUCCESS;
    uint32_t cert_chain_index = 0;
    uint32_t cert_index = 0;
    cn_cbor *cert_chain_cb = NULL;
    cn_cbor *cert_cb = NULL;
    fcc_bundle_data_param_s certificate_chain;
    uint8_t *certificate_data;
    size_t certificate_data_size = 0;
    char suff;
    uint8_t *cert_complete_name = NULL;
    size_t cert_complete_name_size;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((cert_chains_list_cb == NULL), fcc_status = FCC_STATUS_INVALID_PARAMETER, "Invalid cert_chains_list_cb pointer");

    //Initialize data struct
    memset(&certificate_chain, 0, sizeof(fcc_bundle_data_param_s));

    for (cert_chain_index = 0; cert_chain_index < (uint32_t)cert_chains_list_cb->length; cert_chain_index++) {

        FCC_SET_START_TIMER(fcc_certificate_chain_timer);

        certificate_data = NULL;

        fcc_bundle_clean_and_free_data_param(&certificate_chain);


        //Get certificate chain CBOR struct at index cert_chain_index
        cert_chain_cb = cn_cbor_index(cert_chains_list_cb, cert_chain_index);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((cert_chain_cb == NULL), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to get certificate chain at index (%" PRIu32 ") ", cert_chain_index);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((cert_chain_cb->type != CN_CBOR_MAP), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Wrong type of certificate chain CBOR struct at index (%" PRIu32 ") ", cert_chain_index);

        status = fcc_bundle_get_data_param(cert_chain_cb, &certificate_chain);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != true), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to get certificate chain data at index (%" PRIu32 ") ", cert_chain_index);

        //Get all certificates if certificate chain and store it
        for (cert_index = 0; cert_index < (uint32_t)certificate_chain.array_cn->length ; cert_index++) {

            cert_cb = cn_cbor_index(certificate_chain.array_cn, (unsigned int)cert_index);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((cert_cb == NULL), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to get cert cbor at index (%" PRIu32 ") ", cert_index);

            status = get_data_buffer_from_cbor(cert_cb, &certificate_data, &certificate_data_size);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((status == false || certificate_data == NULL || certificate_data_size == 0), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to get cert data at index (%" PRIu32 ") ", cert_index);
            
            status = get_certificate_suffix(&suff, cert_index);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((status == false), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to get cert prefix at index (%" PRIu32 ") ", cert_index);
            
            status = add_suffix_to_name(certificate_chain.name, certificate_chain.name_len, &suff, &cert_complete_name, &cert_complete_name_size);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((status == false), fcc_status = FCC_STATUS_BUNDLE_ERROR, "Failed to set cert complete name at index (%" PRIu32 ") ", cert_index);
           
            kcm_result = kcm_item_store(cert_complete_name, cert_complete_name_size, KCM_CERTIFICATE_ITEM, true, certificate_data, certificate_data_size, certificate_chain.acl);
            SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_result != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_KCM_ERROR, exit, "Failed to store certificate chain at index (%" PRIu32 ") ", cert_chain_index);

            FCC_END_TIMER((char*)cert_complete_name, cert_complete_name_size, fcc_certificate_chain_timer);

            fcc_free(cert_complete_name);
            cert_complete_name_size = 0;



        }
    }

exit:

    if (kcm_result != KCM_STATUS_SUCCESS) {
        fcc_free(cert_complete_name);
        output_info_fcc_status = fcc_bundle_store_error_info(certificate_chain.name, certificate_chain.name_len, kcm_result);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR, 
                                        "Failed to create output kcm_status error %d", kcm_result);
    }
    fcc_bundle_clean_and_free_data_param(&certificate_chain);
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
