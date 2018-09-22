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
#include "factory_configurator_client.h"
#include "fcc_bundle_utils.h"
#include "fcc_output_info_handler.h"
#include "fcc_malloc.h"
#include "fcc_sotp.h"
#include "general_utils.h"
#include "fcc_time_profiling.h"

/**
* Defines for cbor layer
*/
#ifdef USE_CBOR_CONTEXT
#define CONTEXT_NULL , NULL
#define CONTEXT_NULL_COMMA NULL,
#else
#define CONTEXT_NULL
#define CONTEXT_NULL_COMMA
#endif

/**
* Definition of size and value of current protocol scheme version
*/
#define FCC_SIZE_OF_VERSION_FIELD 5
const char fcc_bundle_scheme_version[] = "0.0.1";
/**
* Types of configuration parameter groups
*/
typedef enum fcc_bundle_param_group_type_ {
    FCC_KEY_GROUP_TYPE,                //!< Key group type
    FCC_CERTIFICATE_GROUP_TYPE,        //!< Certificate group type
    FCC_CSR_GROUP_TYPE,                //!< CSR group type
    FCC_CONFIG_PARAM_GROUP_TYPE,       //!< Configuration parameter group type
    FCC_CERTIFICATE_CHAIN_GROUP_TYPE,  //!< Certificate chain group type
    FCC_SCHEME_VERSION_TYPE,           //!< Scheme version group type
    FCC_ENTROPY_TYPE,                  //!< Entropy group type
    FCC_ROT_TYPE,                      //!< Root of trust group type
    FCC_VERIFY_DEVICE_IS_READY_TYPE,   //!< Verify device readiness type
    FCC_FACTORY_DISABLE_TYPE,             //!< Disable FCC flow type
    FCC_MAX_CONFIG_PARAM_GROUP_TYPE    //!< Max group type
} fcc_bundle_param_group_type_e;
/**
* Group lookup record, correlating group's type and name
*/
typedef struct fcc_bundle_group_lookup_record_ {
    fcc_bundle_param_group_type_e group_type;
    const char *group_name;
} fcc_bundle_group_lookup_record_s;
/**
* Group lookup table, correlating for each group its type and name.
* Order is important - it is the order that fcc_bundle_handler() reads the cbor fields.
* FCC_ENTROPY_TYPE and FCC_ROT_TYPE Must be processed first and second respectively.
*/
static const fcc_bundle_group_lookup_record_s fcc_groups_lookup_table[FCC_MAX_CONFIG_PARAM_GROUP_TYPE] = {
    { FCC_SCHEME_VERSION_TYPE,           FCC_BUNDLE_SCHEME_GROUP_NAME },
    { FCC_ENTROPY_TYPE,                  FCC_ENTROPY_NAME },
    { FCC_ROT_TYPE,                      FCC_ROT_NAME },
    { FCC_KEY_GROUP_TYPE,                FCC_KEY_GROUP_NAME },
    { FCC_CERTIFICATE_GROUP_TYPE,        FCC_CERTIFICATE_GROUP_NAME },
    { FCC_CSR_GROUP_TYPE,                FCC_CSR_GROUP_NAME },
    { FCC_CONFIG_PARAM_GROUP_TYPE,       FCC_CONFIG_PARAM_GROUP_NAME },
    { FCC_CERTIFICATE_CHAIN_GROUP_TYPE,  FCC_CERTIFICATE_CHAIN_GROUP_NAME },
    { FCC_VERIFY_DEVICE_IS_READY_TYPE,   FCC_VERIFY_DEVICE_IS_READY_GROUP_NAME },
    { FCC_FACTORY_DISABLE_TYPE,          FCC_FACTORY_DISABLE_GROUP_NAME },
};

/** Prepare a response message
*
* The function prepare response buffer according to result of bundle buffer processing.
* In case of failure, the function prepare buffer with status,scheme version and error logs,
* in case of success - only the status and scheme version.
*
* @param bundle_response_out[in/out]   The pointer to response buffer.
* @param bundle_response_size_out[out/out]     The size of response buffer.
* @param fcc_status[in]     The result of bundle buffer processing.
* @return
*     true for success, false otherwise.
*/
static bool prepare_reponse_message(uint8_t **bundle_response_out, size_t *bundle_response_size_out, fcc_status_e fcc_status)
{
    bool status = false;
    cn_cbor_errback err;
    cn_cbor *cb_map = NULL;
    cn_cbor *cbor_struct_cb = NULL;
    int size_of_cbor_buffer = 0;
    int size_of_out_buffer = 0;
    uint8_t *out_buffer = NULL;
    char *error_string_info = NULL;
    char *warning_string_info = NULL;
    const char success_message[] = { "The Factory process succeeded\n" };
    *bundle_response_out = NULL;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    cb_map = cn_cbor_map_create(CONTEXT_NULL_COMMA &err);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((cb_map == NULL), false, "Failed to create cbor map");

    /**
    * Create cbor with return status
    */
    cbor_struct_cb = cn_cbor_int_create(fcc_status CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((cbor_struct_cb == NULL), status = false, exit, "Failed to create return_status_cb ");

    //Put the cbor return status in cbor map with string key "ReturnStatus"
    status = cn_cbor_mapput_string(cb_map, FCC_RETURN_STATUS_GROUP_NAME, cbor_struct_cb CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), status = false, exit, "Failed top put return status to cbor map");

    /**
    * Create cbor with scheme version
    */
    cbor_struct_cb = NULL;
    cbor_struct_cb = cn_cbor_data_create((const uint8_t *)fcc_bundle_scheme_version,sizeof(fcc_bundle_scheme_version) CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((cbor_struct_cb == NULL), status = false, exit, "Failed to create scheme_version_cb ");

    //Put the cbor return status in cbor map with string key "SchemeVersion"
    status = cn_cbor_mapput_string(cb_map, FCC_BUNDLE_SCHEME_GROUP_NAME, cbor_struct_cb CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), status = false, exit, "Failed top put return status to cbor map");

    /**
    * Create cbor with error info
    */
    cbor_struct_cb = NULL;
    if (fcc_status == FCC_STATUS_SUCCESS) {
        cbor_struct_cb = cn_cbor_data_create((const uint8_t*)success_message, strlen(success_message) CONTEXT_NULL, &err);
    } else {
        error_string_info = fcc_get_output_error_info();
        if (error_string_info == NULL) {
            cbor_struct_cb = cn_cbor_data_create((const uint8_t*)g_fcc_general_status_error_str, strlen(g_fcc_general_status_error_str) CONTEXT_NULL, &err);
        } else {
            cbor_struct_cb = cn_cbor_data_create((const uint8_t*)error_string_info, strlen(error_string_info) CONTEXT_NULL, &err);
       }
    }
    SA_PV_ERR_RECOVERABLE_GOTO_IF((cbor_struct_cb == NULL), status = false, exit, "Failed to create cbor_struct_cb ");

    //Put the cbor info message in cbor map with string key "infoMessage"
    status = cn_cbor_mapput_string(cb_map, FCC_ERROR_INFO_GROUP_NAME, cbor_struct_cb CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), status = false, exit, "Failed top put cbor_struct_cb to cbor map");

    /**
    * Create cbor with warning info
    */
    cbor_struct_cb = NULL;
    status = fcc_get_warning_status();
    warning_string_info = fcc_get_output_warning_info();
    SA_PV_ERR_RECOVERABLE_GOTO_IF(status == true && warning_string_info == NULL, status = false, exit, "Failed to get created warnings");
    if (warning_string_info != NULL) {
        cbor_struct_cb = cn_cbor_data_create((const uint8_t *)warning_string_info, strlen(warning_string_info) CONTEXT_NULL, &err);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((cbor_struct_cb == NULL), status = false, exit, "Failed to create warning_message_cb ");

        //Put the cbor info message in cbor map with string key "WarningInfo"
        status = cn_cbor_mapput_string(cb_map, FCC_WARNING_INFO_GROUP_NAME, cbor_struct_cb CONTEXT_NULL, &err);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), status = false, exit, "Failed top put warning_message_cb to cbor map");
    } 

    status = true;
    //Get size of encoded cbor buffer
    size_of_cbor_buffer = cn_cbor_get_encoded_size(cb_map, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((size_of_cbor_buffer == -1), status = false, exit, "Failed to get cbor buffer size");

    //Allocate out buffer
    out_buffer = fcc_malloc(size_of_cbor_buffer);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((out_buffer == NULL), status = false, exit, "Failed to allocate memory for out buffer");

    //Write cbor blob to output buffer
    size_of_out_buffer = cn_cbor_encoder_write(cb_map, out_buffer, size_of_cbor_buffer, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((size_of_out_buffer == -1), status = false, exit_without_out_buffer, "Failed to  write cbor buffer to output buffer");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((size_of_out_buffer != size_of_cbor_buffer), status = false, exit_without_out_buffer, "Wrong written size for outbut buffer");

    //Update pointer and size of output buffer
    *bundle_response_out = out_buffer;
    *bundle_response_size_out = (size_t)size_of_out_buffer;
    goto exit;

exit_without_out_buffer:
    fcc_free(out_buffer);

    // Nullify pointer so that the user cannot accidentally double free it.
    *bundle_response_out = NULL;
exit:
    fcc_free(warning_string_info);
    if (cb_map != NULL) {
        cn_cbor_free(cb_map CONTEXT_NULL);
    }
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return status;
}

/** Checks bundle scheme version
*
* @param cbor_blob[in]   The pointer to main cbor blob.
* @return
*     true for success, false otherwise.
*/
static bool check_scheme_version(cn_cbor *cbor_blob)
{
    cn_cbor *scheme_version_cb = NULL;
    int result;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_blob == NULL), false, "Invalid cbor_blob");

    scheme_version_cb = cn_cbor_mapget_string(cbor_blob, FCC_BUNDLE_SCHEME_GROUP_NAME);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((scheme_version_cb == NULL), false, "Failed to find scheme version group");

    result = is_memory_equal(scheme_version_cb->v.bytes, scheme_version_cb->length, fcc_bundle_scheme_version, strlen(fcc_bundle_scheme_version));
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!result), false, "Wrong scheme version");

    return true;
}

/** Writes buffer to SOTP
*
* @param cbor_bytes[in]   The pointer to a cn_cbor object of type CN_CBOR_BYTES.
* @param sotp_type[in]    enum representing the type of the item to be stored in SOTP.
* @return
*     true for success, false otherwise.
*/

static fcc_status_e fcc_bundle_process_sotp_buffer(cn_cbor *cbor_bytes, fcc_sotp_type_e sotp_type)
{
    uint8_t *buf;
    size_t buf_size;
    fcc_status_e fcc_status;
    bool status;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_bytes->type != CN_CBOR_BYTES), FCC_STATUS_BUNDLE_ERROR, "cn_cbor object of incorrect type");

    status = get_data_buffer_from_cbor(cbor_bytes, &buf, &buf_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status == false), FCC_STATUS_BUNDLE_ERROR, "Unable to retrieve data from cn_cbor");

    fcc_status = fcc_sotp_data_store(buf, buf_size, sotp_type);

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

/** Checks the FCC_VERIFY_DEVICE_IS_READY_GROUP_NAME group value
*
* - if value is '0' - do NOT process device verification flow
* - if value is '1' - process device verification flow
*
* @param cbor_blob[in]  The pointer to main CBOR blob.
*
* @return
*     One of FCC_STATUS_* error codes
*/
static fcc_status_e process_fcc_verify(const cn_cbor *cbor_blob)
{
    uint8_t *buff = NULL;
    size_t buff_size;
    uint32_t fcc_verify_value;
    bool status;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_blob == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid param cbor_blob");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_blob->type != CN_CBOR_UINT), FCC_STATUS_SUCCESS, "Unexpected CBOR type");

    status = get_data_buffer_from_cbor(cbor_blob, &buff, &buff_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!status), FCC_STATUS_BUNDLE_ERROR, "Unable to retrieve data from cn_cbor");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((buff_size != sizeof(fcc_verify_value)), FCC_STATUS_BUNDLE_ERROR, "Incorrect buffer size for device disable");

    memcpy(&fcc_verify_value, buff, buff_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF(((fcc_verify_value != 0) && (fcc_verify_value != 1)), FCC_STATUS_BUNDLE_ERROR, "Unexpected value, should be either 0 or 1");

    if (fcc_verify_value == 1) {
        return fcc_verify_device_configured_4mbed_cloud();
    }

    // Getting here means, VERIFY group exist but set off
    return FCC_STATUS_SUCCESS;
}


/** Checks the FCC_FACTORY_DISABLE_GROUP_NAME group value
*
* - if value is '0' - do NOT process device disable flow
* - if value is '1' - process device disable flow
*
* @param cbor_blob[in]  The pointer to main CBOR blob.
*
* @return
*     One of FCC_STATUS_* error codes
*/
static fcc_status_e process_fcc_disable(const cn_cbor *cbor_blob)
{
    uint8_t *buff = NULL;
    size_t buff_size;
    uint32_t fcc_disable_value;
    bool status;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_blob == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid param cbor_blob");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((cbor_blob->type != CN_CBOR_UINT), FCC_STATUS_SUCCESS, "Unexpected CBOR type");

    status = get_data_buffer_from_cbor(cbor_blob, &buff, &buff_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((!status), FCC_STATUS_BUNDLE_ERROR, "Unable to retrieve data from cn_cbor");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((buff_size != sizeof(fcc_disable_value)), FCC_STATUS_BUNDLE_ERROR, "Incorrect buffer size for device disable");

    memcpy(&fcc_disable_value, buff, buff_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF(((fcc_disable_value != 0) && (fcc_disable_value != 1)), FCC_STATUS_BUNDLE_ERROR, "Unexpected value, should be either 0 or 1");

    if (fcc_disable_value == 1) {
        return fcc_factory_disable();
    }

    // Getting here means, DISABLE group exist but set off
    return FCC_STATUS_SUCCESS;
}

fcc_status_e fcc_bundle_handler(const uint8_t *encoded_blob, size_t encoded_blob_size, uint8_t **bundle_response_out, size_t *bundle_response_size_out)
{
    bool status = false;
    bool is_fcc_factory_disabled;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    cn_cbor *main_list_cb = NULL;
    cn_cbor *group_value_cb = NULL;
    cn_cbor_errback err;
    size_t group_index;
    fcc_bundle_param_group_type_e group_type;
    size_t num_of_groups_in_message = 0;

    // (false) group is 'N/A', hasn't been seen in the inbound CBOR blob)
    // (true)  group has been seen and processed in the given inbound CBOR blob
    bool is_device_verify_group_exist = false;  // mark as not exist (default value)
    
    // if true, device verify is redundant
    bool device_is_already_disabled = false;

    FCC_SET_START_TIMER(fcc_bundle_timer);

    SA_PV_LOG_INFO_FUNC_ENTER("encoded_blob_size = %" PRIu32 "", (uint32_t)encoded_blob_size);

    // Check if factory flow is disabled, if yes, do not proceed
    fcc_status = fcc_is_factory_disabled(&is_fcc_factory_disabled);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status != FCC_STATUS_SUCCESS), fcc_status, "Failed for fcc_is_factory_disabled");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((is_fcc_factory_disabled), FCC_STATUS_FACTORY_DISABLED_ERROR, "FCC is disabled, service not available");

    // Check params
    SA_PV_ERR_RECOVERABLE_RETURN_IF((bundle_response_out == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid bundle_response_out");
    // Set to NULL so that the user does not accidentally free a non NULL pointer after the function returns.
    *bundle_response_out = NULL;

    SA_PV_ERR_RECOVERABLE_RETURN_IF((bundle_response_size_out == NULL), FCC_STATUS_INVALID_PARAMETER, "Invalid bundle_response_size_out");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((encoded_blob == NULL), fcc_status = FCC_STATUS_INVALID_PARAMETER, exit, "Invalid encoded_blob");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((encoded_blob_size == 0), fcc_status = FCC_STATUS_INVALID_PARAMETER, exit, "Invalid encoded_blob_size");

    /*Initialize fcc_output_info_s structure , in case of error during store process the
    function will exit without fcc_verify_device_configured_4mbed_cloud where we perform additional fcc_clean_output_info_handler*/
    fcc_clean_output_info_handler();

    /* Decode CBOR message
    Check the size of the CBOR structure */
    main_list_cb = cn_cbor_decode(encoded_blob, encoded_blob_size CONTEXT_NULL, &err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((main_list_cb == NULL), fcc_status = FCC_STATUS_BUNDLE_ERROR, exit, "cn_cbor_decode failed (%" PRIu32 ")", (uint32_t)err.err);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((main_list_cb->type != CN_CBOR_MAP), fcc_status = FCC_STATUS_BUNDLE_ERROR, free_cbor_list_and_out, "Wrong CBOR structure type");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((main_list_cb->length <= 0 || main_list_cb->length > FCC_MAX_CONFIG_PARAM_GROUP_TYPE *FCC_CBOR_MAP_LENGTH), fcc_status = FCC_STATUS_BUNDLE_ERROR, free_cbor_list_and_out, "Wrong CBOR structure size");

    /* Check scheme version*/
    status = check_scheme_version(main_list_cb);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), fcc_status = FCC_STATUS_BUNDLE_INVALID_SCHEME, free_cbor_list_and_out, "check_scheme_version failed");

    //Go over parameter groups
    for (group_index = 0; group_index < FCC_MAX_CONFIG_PARAM_GROUP_TYPE; group_index++) {
        //Get content of current group (value of map, when key of map is name of group and value is list of params of current group)
        SA_PV_LOG_INFO(" fcc_groups_lookup_table[group_index].group_name is %s", fcc_groups_lookup_table[group_index].group_name);
        group_value_cb = cn_cbor_mapget_string(main_list_cb, fcc_groups_lookup_table[group_index].group_name);

        if (group_value_cb != NULL) {
            //Get type of group
            group_type = fcc_groups_lookup_table[group_index].group_type;
            num_of_groups_in_message++;

            switch (group_type) {
                case FCC_SCHEME_VERSION_TYPE:
                    break;
                case FCC_KEY_GROUP_TYPE:
                    FCC_SET_START_TIMER(fcc_gen_timer);
                    fcc_status = fcc_bundle_process_keys(group_value_cb);
                    FCC_END_TIMER("Total keys process", 0 ,fcc_gen_timer);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_keys failed");
                    break;
                case FCC_CERTIFICATE_GROUP_TYPE:
                    FCC_SET_START_TIMER(fcc_gen_timer);
                    fcc_status = fcc_bundle_process_certificates(group_value_cb);
                    FCC_END_TIMER("Total certificates process", 0, fcc_gen_timer);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_certificates failed");
                    break;
                case FCC_CONFIG_PARAM_GROUP_TYPE:
                    FCC_SET_START_TIMER(fcc_gen_timer);
                    fcc_status = fcc_bundle_process_config_params(group_value_cb);
                    FCC_END_TIMER("Total config params process", 0, fcc_gen_timer);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_config_params failed");
                    break;
                case FCC_CERTIFICATE_CHAIN_GROUP_TYPE:
                    FCC_SET_START_TIMER(fcc_gen_timer);
                    fcc_status = fcc_bundle_process_certificate_chains(group_value_cb);
                    FCC_END_TIMER("Total certificate chains process", 0, fcc_gen_timer);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_certificate_chains failed");
                    break;
                case FCC_ENTROPY_TYPE: // Entropy for random generator
                    fcc_status = fcc_bundle_process_sotp_buffer(group_value_cb, FCC_SOTP_TYPE_ENTROPY);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_sotp_buffer failed for entropy");
                    break;
                case FCC_ROT_TYPE: // Key for ESFS
                    fcc_status = fcc_bundle_process_sotp_buffer(group_value_cb, FCC_SOTP_TYPE_ROT);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_bundle_process_sotp_buffer failed for ROT");
                    break;
                case FCC_VERIFY_DEVICE_IS_READY_TYPE:
                    is_device_verify_group_exist = true;
                    fcc_status = process_fcc_verify(group_value_cb);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "process_device_verify failed");
                    break;
                case FCC_FACTORY_DISABLE_TYPE:
                    device_is_already_disabled = true;
                    fcc_status = process_fcc_disable(group_value_cb);
                    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_factory_disable failed");
                    break;
                default:
                    fcc_status = FCC_STATUS_BUNDLE_UNSUPPORTED_GROUP;
                    SA_PV_LOG_ERR("Wrong group type");
                    goto free_cbor_list_and_out;
            }
        }
    }
    
    SA_PV_ERR_RECOVERABLE_GOTO_IF((num_of_groups_in_message == 0), fcc_status = FCC_STATUS_INVALID_PARAMETER, free_cbor_list_and_out, "No groups in message");
    SA_PV_ERR_RECOVERABLE_GOTO_IF(((size_t)(main_list_cb->length/FCC_CBOR_MAP_LENGTH)!= num_of_groups_in_message), fcc_status = FCC_STATUS_BUNDLE_INVALID_GROUP, free_cbor_list_and_out, "One ore more names of groups are invalid");

    if (!is_device_verify_group_exist && !device_is_already_disabled) {
        // device VERIFY group does NOT exist in the CBOR message and device is NOT disabled.
        // Perform device verification to keep backward compatibility.
        FCC_SET_START_TIMER(fcc_gen_timer);
        fcc_status = fcc_verify_device_configured_4mbed_cloud();
        FCC_END_TIMER("Total verify device", 0, fcc_gen_timer);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, free_cbor_list_and_out, "fcc_verify_device_configured_4mbed_cloud failed");
    }

free_cbor_list_and_out:
    cn_cbor_free(main_list_cb CONTEXT_NULL);

exit:
    //Prepare bundle response message
    status = prepare_reponse_message(bundle_response_out, bundle_response_size_out, fcc_status);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((status != true), FCC_STATUS_BUNDLE_RESPONSE_ERROR, "Failed to prepare out response");
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    FCC_END_TIMER("Total fcc_bundle_handler device", 0, fcc_bundle_timer);
    return fcc_status;
}
