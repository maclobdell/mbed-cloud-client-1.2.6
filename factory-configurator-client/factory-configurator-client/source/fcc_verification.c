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

#include "factory_configurator_client.h"
#include "fcc_status.h"
#include "fcc_verification.h"
#include "key_config_manager.h"
#include "pv_error_handling.h"
#include "cs_der_certs.h"
#include "cs_utils.h"
#include "fcc_output_info_handler.h"
#include "fcc_malloc.h"
#include "time.h"
#include "cs_der_keys.h"
#include "cs_utils.h"
#define FCC_10_YEARS_IN_SECONDS 315360000//10*365*24*60*60

/*
* The function checks that UTC offset value is inside defined range of valid offsets :-12:00 - +14:00
*/
static bool check_utc_offset_data(char *utc_offset_data, size_t utc_data_size)
{
    uint8_t symbol_index = 0;
    uint8_t first_digit_of_hour = 1;
    uint8_t second_digit_of_hour = 2;
    uint8_t first_digit_of_minutes = 4;
    uint8_t second_digit_of_minutes = 5;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    /*
    The range of UTC offsets taken from https://en.wikipedia.org/wiki/List_of_UTC_time_offsets
    We check only that the offset is -xx:yy or +xx:yy and that the offset is in range of offsets : -12:00 - +14:00
    but we check only that UTC contain restricted symbols(-,+,:_) and numbers at xx or yy.
    */
    //The first char must be '+' or '-'
    if ((utc_offset_data[symbol_index] != '+') && (utc_offset_data[symbol_index] != '-')) {
        return false;
    }

    //The format of utc offset should be -xx:xx or +xx:xx
    if (utc_offset_data[3] != ':') {
        return false;
    }

    //Check that all numbers of hours and minutes are valid
    if (utc_offset_data[first_digit_of_hour] < '0' || utc_offset_data[first_digit_of_hour] > '9') {
        return false;
    }
    if (utc_offset_data[second_digit_of_hour] < '0' || utc_offset_data[second_digit_of_hour] > '9') {
        return false;
    }
    if (utc_offset_data[first_digit_of_minutes] < '0' || utc_offset_data[first_digit_of_minutes] > '9') {
        return false;
    }
    if (utc_offset_data[second_digit_of_minutes] < '0' || utc_offset_data[second_digit_of_minutes] > '9') {
        return false;
    }

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return true;
}

/** The function checks bootstrap server uri data contents.
*
* @param uri_data_buffer[in]             The bootstrap uri data.
* @param size_of_uri_data_buffer[in]      The bootstrap uri data size.
* @return
*     fcc_status_e.
*/
static fcc_status_e fcc_check_uri_contents(bool use_bootstrap, uint8_t* uri_data_buffer, size_t size_of_uri_data_buffer)
{
    const char uri_coap_prefix[] = "coap://";
    const char uri_coaps_prefix[] = "coaps://";
    const char uri_aid_1[] = "?aid=";
    const char uri_aid_2[] = "&aid=";
    bool has_uri_aid = false;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    char *uri_string = NULL;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    bool is_first_to_claim_mode = false;
    uint32_t first_to_claim = 0;
    size_t act_config_param_size = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    // get first to claim
    kcm_status = kcm_item_get_data((const uint8_t*)g_fcc_first_to_claim_parameter_name,
                                   strlen(g_fcc_first_to_claim_parameter_name),
                                   KCM_CONFIG_ITEM,
                                   (uint8_t*)&first_to_claim,
                                   sizeof(uint32_t),
                                   &act_config_param_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS && kcm_status != KCM_STATUS_ITEM_NOT_FOUND), FCC_STATUS_KCM_ERROR, "Failed to get first to claim config parameter");
    if (kcm_status == KCM_STATUS_SUCCESS) {
        SA_PV_ERR_RECOVERABLE_RETURN_IF((act_config_param_size != sizeof(uint32_t)), FCC_STATUS_WRONG_ITEM_DATA_SIZE, "Size of first to claim mode parameter is wrong ");
        is_first_to_claim_mode = (first_to_claim == 1);
    }

    //Allocate buffer for uri string creation
    uri_string = fcc_malloc(size_of_uri_data_buffer + 1);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((uri_string == NULL), FCC_STATUS_MEMORY_OUT, "Failed to allocate memory for URI string");

    //Copy data and create null terminated string
    memcpy(uri_string, uri_data_buffer, size_of_uri_data_buffer);
    (*(uri_string + size_of_uri_data_buffer)) = '\0';

    // Check that uri_string has correct prefix
    if (memcmp(uri_string, uri_coap_prefix, strlen(uri_coap_prefix)) != 0 && memcmp(uri_string, uri_coaps_prefix, strlen(uri_coaps_prefix)) != 0) {
        SA_PV_ERR_RECOVERABLE_GOTO_IF(true, fcc_status = FCC_STATUS_URI_WRONG_FORMAT, exit, "Wrong uri prefix");
    }

    // Check if uri_string contains uri_aid (indicate the uri contains AccountId)
    if ((strstr(uri_string, uri_aid_1) != NULL) || (strstr(uri_string, uri_aid_2) != NULL)) {
        has_uri_aid = true;
    }

    if (is_first_to_claim_mode == true) {
        SA_PV_ERR_RECOVERABLE_GOTO_IF(use_bootstrap == false, fcc_status = FCC_STATUS_FIRST_TO_CLAIM_NOT_ALLOWED, exit, "First to claim not allowed in lwm2m mode");
        SA_PV_ERR_RECOVERABLE_GOTO_IF(has_uri_aid == true, fcc_status = FCC_STATUS_FIRST_TO_CLAIM_NOT_ALLOWED, exit, "First to claim not allowed if account ID exist");
    } else {
        SA_PV_ERR_RECOVERABLE_GOTO_IF(has_uri_aid == false, fcc_status = FCC_STATUS_URI_WRONG_FORMAT, exit, "Wrong uri data");
    }

exit:
    fcc_free(uri_string);
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

/**This function retrives kcm data buffer and its size according to the given name.
*
* @param parameter_name[in]                  buffer of parameter name.
* @param size_of_parameter_name[in]          size of parameter name.
* @kcm_type[in]                              type of kcm data to retrieve
* @param kcm_data[out]                       pointer to kcm data.
* @param kcm_data_size[out]                  size of kcm data.
*        fcc_status_e status.
*/
static fcc_status_e fcc_get_kcm_data(const uint8_t *parameter_name, size_t size_of_parameter_name, kcm_item_type_e kcm_type, uint8_t **kcm_data, size_t *kcm_data_size)
{

    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;

    //Get size of kcm data
    kcm_status = kcm_item_get_data_size(parameter_name,
                                        size_of_parameter_name,
                                        kcm_type,
                                        kcm_data_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status == KCM_STATUS_ITEM_NOT_FOUND), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, "KCM is not found");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((*kcm_data_size == 0), fcc_status = FCC_STATUS_EMPTY_ITEM, "KCM item is empty");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_KCM_STORAGE_ERROR, "Failed to get kcm data size");

    //Alocate memory and get device certificate data
    *kcm_data = fcc_malloc(*kcm_data_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((*kcm_data == NULL), fcc_status = FCC_STATUS_MEMORY_OUT, "Failed to allocate buffer for kcm data");

    kcm_status = kcm_item_get_data(parameter_name,
                                   size_of_parameter_name,
                                   kcm_type,
                                   *kcm_data, *kcm_data_size, kcm_data_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status == KCM_STATUS_ITEM_NOT_FOUND), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, exit, "KCM is not found");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_KCM_STORAGE_ERROR, exit, "Failed to get device certificate data");

    return fcc_status;
exit:
    fcc_free(*kcm_data);
    *kcm_data = NULL;
    return fcc_status;
}

/**This function retrives certificate's attribute and it's size according to its type.
*
* @param certificate_data[in]                  buffer of certificate.
* @param size_of_certificate_data[in]          size of certificate data.
* @attribute_type[in]                          type of attribute to retrieve.
* @param size_of_certificate_data[out]         attribute data buffer.
* @param size_of_certificate_data[out]         size of attribute data buffer.
*        fcc_status_e status.
*/
static fcc_status_e fcc_get_certificate_attribute(palX509Handle_t x509_cert, cs_certificate_attribute_type_e attribute_type, uint8_t **attribute_data, size_t *attribute_size)
{

    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
  


    //Get attribute size
    kcm_status = cs_attr_get_data_size_x509_cert(x509_cert,
                                                 attribute_type, 
                                                 attribute_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_INVALID_CERT_ATTRIBUTE, "Failed to get size of attribute");

    *attribute_data = fcc_malloc(*attribute_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((*attribute_data == NULL), fcc_status = FCC_STATUS_MEMORY_OUT, "Failed to allocate memory for attribute");

    //Get data of "CN" attribute
    kcm_status = cs_attr_get_data_x509_cert(x509_cert,
                                            attribute_type,
                                            *attribute_data,
                                            *attribute_size,
                                            attribute_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS || *attribute_size == 0), fcc_status = FCC_STATUS_INVALID_CERT_ATTRIBUTE, exit, "Failed to get attribute data");

    return fcc_status;
exit:
    fcc_free(*attribute_data);
    *attribute_data = NULL;
    return fcc_status;
}
/* The function verifies if current item exists and checks the result with is_should_be_present flag.
*  In case of unsuitability of the flag and existence of the item, the function sets warning with relevant message.
*/
static fcc_status_e verify_existence_and_set_warning(const uint8_t *parameter_name, size_t size_of_parameter_name, kcm_item_type_e parameter_type, bool is_should_be_present)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    size_t item_size = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Check that second mode server uri is not present
    kcm_status = kcm_item_get_data_size(parameter_name,
                                        size_of_parameter_name,
                                        parameter_type,
                                        &item_size);

    if (kcm_status == KCM_STATUS_SUCCESS && is_should_be_present == false) {
        output_info_fcc_status = fcc_store_warning_info((const uint8_t*)parameter_name, size_of_parameter_name, g_fcc_redundant_item_warning_str);
    }
    if (kcm_status != KCM_STATUS_SUCCESS &&  is_should_be_present == true) {
        output_info_fcc_status = fcc_store_warning_info((const uint8_t*)parameter_name, size_of_parameter_name, g_fcc_item_not_set_warning_str);
    }
    SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                    fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                    "Failed to create warning");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
/**This function verifies certificate expiration according to
*
* @param certificate_data[in]                  buffer of certificate.
* @param size_of_certificate_data[in]          size of certificate data.
* @param certificate_name[in]                  buffer of certificate name.
* @param size_of_certificate_name[in]          size of certificate name buffer.
*    @returns
*        fcc_status_e status.
*/
static fcc_status_e verify_certificate_expiration(palX509Handle_t x509_cert, const uint8_t *certificate_name, size_t size_of_certificate_name)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    size_t size_of_valid_from_attr = 0;
    size_t size_of_valid_until_attr = 0;
    uint64_t *valid_from_attr = NULL;
    uint64_t time = 0;
    uint64_t diff_time = 60; //seconds. This value used to reduce time adjustment 
    uint64_t *valid_until_attr = NULL;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Get "valid_from" certificate attribute
    fcc_status = fcc_get_certificate_attribute(x509_cert, CS_VALID_FROM_ATTRIBUTE_TYPE, (uint8_t**)&valid_from_attr, &size_of_valid_from_attr);
    SA_PV_ERR_RECOVERABLE_GOTO_IF(fcc_status != FCC_STATUS_SUCCESS || size_of_valid_from_attr != sizeof(uint64_t), fcc_status = fcc_status, exit, "Failed to get size of valid_from attribute");

    //Get "valid_until" certificate attribute
    fcc_status = fcc_get_certificate_attribute(x509_cert, CS_VALID_TO_ATTRIBUTE_TYPE, (uint8_t**)&valid_until_attr, &size_of_valid_until_attr);
    SA_PV_ERR_RECOVERABLE_GOTO_IF(fcc_status != FCC_STATUS_SUCCESS || size_of_valid_from_attr != sizeof(uint64_t), fcc_status = fcc_status, exit, "Failed to get size of valid_until attribute");


    //Check device time
    time = pal_osGetTime();
    if (time == 0) {
        output_info_fcc_status = fcc_store_warning_info((const uint8_t*)certificate_name, size_of_certificate_name, g_fcc_cert_time_validity_warning_str);
        SA_PV_LOG_ERR("time is (%" PRIuMAX ") ", (uint64_t)time);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((output_info_fcc_status != FCC_STATUS_SUCCESS), fcc_status = FCC_STATUS_WARNING_CREATE_ERROR, exit, "Failed to create warning");
    } else {
        //Check that the certificate is not expired
        SA_PV_ERR_RECOVERABLE_GOTO_IF((time > (*valid_until_attr)), fcc_status = FCC_STATUS_EXPIRED_CERTIFICATE, exit, "The certificate is expired");

        //Check that start of validity is less than current time
        if (time + diff_time < (*valid_from_attr)) {
            SA_PV_LOG_ERR("valid_from_attr is (%" PRIuMAX ") ", (uint64_t)(*valid_from_attr));
            SA_PV_LOG_ERR("time is (%" PRIuMAX ") ", (uint64_t)time);
            output_info_fcc_status = fcc_store_warning_info((const uint8_t*)certificate_name, size_of_certificate_name, g_fcc_cert_time_validity_warning_str);
            SA_PV_ERR_RECOVERABLE_GOTO_IF((output_info_fcc_status != FCC_STATUS_SUCCESS), fcc_status = FCC_STATUS_WARNING_CREATE_ERROR, exit, "Failed to create warning");
        }

        //Check that the certificate is valid at least for 10 years
        if ((*valid_until_attr) - time < FCC_10_YEARS_IN_SECONDS) {
            output_info_fcc_status = fcc_store_warning_info((const uint8_t*)certificate_name, size_of_certificate_name, g_fcc_cert_validity_less_10_years_warning_str);
            SA_PV_ERR_RECOVERABLE_GOTO_IF((output_info_fcc_status != FCC_STATUS_SUCCESS), fcc_status = FCC_STATUS_WARNING_CREATE_ERROR, exit, "Failed to create warning");
        }

    }
exit:
    fcc_free(valid_from_attr);
    fcc_free(valid_until_attr);
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info((const uint8_t*)certificate_name, size_of_certificate_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS), fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR, "Failed to create output fcc_status error %d", fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
/**This function verifies lwm2m certificate ou attribute is equal to aid from server link.
*
* @param certificate_data[in]                  buffer of certificate.
* @param size_of_certificate_data[in]          size of certificate data.
*        fcc_status_e status.
*/
static fcc_status_e compare_ou_with_aid_server(palX509Handle_t x509_cert)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    uint8_t *ou_attribute_data = NULL;
    size_t ou_attribute_size = 0;
    uint8_t *parameter_name = (uint8_t*)g_fcc_lwm2m_server_uri_name;
    size_t size_of_parameter_name = strlen(g_fcc_lwm2m_server_uri_name);
    uint8_t *server_uri_buffer = NULL;
    size_t item_size = 0;
    char *uri_string = NULL;
    char *aid_substring = NULL;
    size_t  aid_substring_size = 0;
    int result = 0;
    int len_of_aid_sub_string = strlen("&aid=");

    //Get OU certificate attribute
    fcc_status = fcc_get_certificate_attribute(x509_cert, CS_OU_ATTRIBUTE_TYPE, &ou_attribute_data, &ou_attribute_size);
    SA_PV_ERR_RECOVERABLE_RETURN_IF(fcc_status != FCC_STATUS_SUCCESS, fcc_status = fcc_status, "Failed to get size OU attribute");

    //Get aid data
    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_CONFIG_ITEM, &server_uri_buffer, &item_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get kcm data server url");

    uri_string = fcc_malloc(item_size + 1);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((uri_string == NULL), fcc_status = FCC_STATUS_MEMORY_OUT, exit, "Failed to get kcm data server url");

    memcpy(uri_string, server_uri_buffer, item_size);
    (*(uri_string + item_size)) = '\0';

    aid_substring = strstr(uri_string, "&aid=");
    if (aid_substring == NULL) {
        aid_substring = strstr(uri_string, "?aid=");
        SA_PV_ERR_RECOVERABLE_GOTO_IF((aid_substring == NULL), fcc_status = FCC_STATUS_URI_WRONG_FORMAT, exit, "URI format is wrong");
    }

    aid_substring_size = strlen(aid_substring);
    aid_substring_size = aid_substring_size - len_of_aid_sub_string;
    SA_PV_ERR_RECOVERABLE_GOTO_IF((aid_substring_size < ou_attribute_size - 1), fcc_status = FCC_STATUS_URI_WRONG_FORMAT, exit, "URI format is wrong");

    result = memcmp(&(aid_substring[len_of_aid_sub_string]), ou_attribute_data, ou_attribute_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((result != 0), fcc_status = FCC_STATUS_INVALID_LWM2M_CN_ATTR, exit, "CN of LWM2M different from endpoint name");

exit:
    fcc_free(ou_attribute_data);
    fcc_free(server_uri_buffer);
    fcc_free(uri_string);
    return fcc_status;
}
/**This function verifies  certificate's cn attribute is equal to endpoint name.
*
* @param certificate_data[in]                  buffer of certificate.
* @param size_of_certificate_data[in]          size of certificate data.
*        fcc_status_e status.
*/
static fcc_status_e compare_cn_with_endpoint(palX509Handle_t x509_cert)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    //fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    size_t size_of_cn_attr = 0;
    uint8_t *cn_attribute_data = NULL;
    size_t endpoint_name_size;
    uint8_t *endpoint_name_data = NULL;
    int result = 0;

    //Get  CN certificate attribute
    fcc_status = fcc_get_certificate_attribute(x509_cert, CS_CN_ATTRIBUTE_TYPE, &cn_attribute_data, &size_of_cn_attr);
    SA_PV_ERR_RECOVERABLE_RETURN_IF(fcc_status != FCC_STATUS_SUCCESS, fcc_status = fcc_status, "Failed to get size CN attribute");

    //Get attribute returns size of string including  "\0"
    size_of_cn_attr = size_of_cn_attr - 1;

    //Get endpoint name size
    fcc_status = fcc_get_kcm_data((const uint8_t*)g_fcc_endpoint_parameter_name, strlen(g_fcc_endpoint_parameter_name), KCM_CONFIG_ITEM, &endpoint_name_data, &endpoint_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get endpoint name");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((size_of_cn_attr != endpoint_name_size), fcc_status = FCC_STATUS_INVALID_LWM2M_CN_ATTR, exit, "Wrong size of CN");

    result = memcmp(endpoint_name_data, cn_attribute_data, size_of_cn_attr);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((result != 0), fcc_status = FCC_STATUS_INVALID_LWM2M_CN_ATTR, exit, "CN of the certificate is different from endpoint name");

exit:
    fcc_free(cn_attribute_data);
    fcc_free(endpoint_name_data);
    return fcc_status;
}
/** The function checks validity of bootstrap server uri parameter
*
* The function checks the item's size, gets its data and checks it.
*
* @param bootrstrap_server_uri_name[in]             The bootstrap uri name.
* @param size_of_bootrstrap_server_uri_name[in]     The size of bootstrap uri name.
* @return
*     fcc_status_e.
*/
static fcc_status_e verify_server_uri(bool use_bootstrap)
{

    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    size_t item_size = 0;
    uint8_t *server_uri_buffer = NULL;
    uint8_t *parameter_name = NULL;
    size_t size_of_parameter_name = 0;
    uint8_t *second_mode_parameter_name = NULL;
    size_t size_of_second_mode_parameter_name = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Set server uri parameter names of current and second mode according to bootstrap mode
    if (use_bootstrap == true) {
        parameter_name = (uint8_t*)g_fcc_bootstrap_server_uri_name;
        size_of_parameter_name = strlen(g_fcc_bootstrap_server_uri_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_lwm2m_server_uri_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_lwm2m_server_uri_name);
    } else {
        parameter_name = (uint8_t*)g_fcc_lwm2m_server_uri_name;
        size_of_parameter_name = strlen(g_fcc_lwm2m_server_uri_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_bootstrap_server_uri_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_bootstrap_server_uri_name);
    }
    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_CONFIG_ITEM, &server_uri_buffer, &item_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get kcm data server url");

    //Check that server uri of second mode is not present, if yes - set warning
    fcc_status = verify_existence_and_set_warning(second_mode_parameter_name, size_of_second_mode_parameter_name, KCM_CONFIG_ITEM, false);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to verify_existence_and_set_warning");

    //Check server uri data
    fcc_status = fcc_check_uri_contents(use_bootstrap, server_uri_buffer, item_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to check bootstrap uri data");

exit:
    fcc_free(server_uri_buffer);
    //In case kcm or fcc error, record the error with parameter name
    if (kcm_status != KCM_STATUS_SUCCESS || fcc_status != FCC_STATUS_SUCCESS) {
        if (fcc_status == FCC_STATUS_FIRST_TO_CLAIM_NOT_ALLOWED && parameter_name == (uint8_t*)g_fcc_lwm2m_server_uri_name)
        {
            // In case that using lwm2m and first to claim on, change the parameter_name
            parameter_name = (uint8_t*)g_fcc_first_to_claim_parameter_name;
            size_of_parameter_name = strlen(g_fcc_first_to_claim_parameter_name);
        }
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error %d",
                                        fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

/* The function checks UTC offset.
*/
static fcc_status_e check_utc_offset(void)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e fcc_output_status = FCC_STATUS_SUCCESS;
    uint8_t *parameter_name = (uint8_t*)g_fcc_offset_from_utc_parameter_name;
    size_t size_of_parameter_name = strlen(g_fcc_offset_from_utc_parameter_name);
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    size_t item_size = 0;

    uint8_t *utc_offset_data = NULL;
    bool status = false;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_CONFIG_ITEM, &utc_offset_data, &item_size);

    //If the item is missing or empty, write warning
    if (fcc_status == FCC_STATUS_ITEM_NOT_EXIST || fcc_status == FCC_STATUS_EMPTY_ITEM) {
        fcc_output_status = fcc_store_warning_info(parameter_name, size_of_parameter_name, g_fcc_item_not_set_warning_str);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_output_status != FCC_STATUS_SUCCESS),
                                      fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                      exit,
                                      "Failed to create output warning %s",
                                      g_fcc_item_not_set_warning_str);
        fcc_status = FCC_STATUS_SUCCESS;
    } else {
        //If get kcm data returned error, exit with error
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get utc data");

        status = check_utc_offset_data((char*)utc_offset_data, item_size);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((status != true), fcc_status = FCC_STATUS_UTC_OFFSET_WRONG_FORMAT, exit, "Failed to check utc offset");
    }

exit:
    fcc_free(utc_offset_data);
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error  %d",
                                        fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
/**This function checks Root CA certificate.
*
* @param device_objects[in]         Structure with set of device security object data.
* @param use_bootstrap[in]          Bootstrap mode.
*    @returns
*        fcc_status_e status.
*/
static fcc_status_e verify_root_ca_certificate(bool use_bootstrap)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    size_t item_size = 0;
    uint8_t *parameter_name = NULL;
    size_t size_of_parameter_name = 0;
    uint8_t *second_mode_parameter_name = NULL;
    size_t size_of_second_mode_parameter_name = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Set CA certificate names of current and second mode
    if (use_bootstrap == true) {
        //Set bootstrap root ca certificate name
        parameter_name = (uint8_t*)g_fcc_bootstrap_server_ca_certificate_name;
        size_of_parameter_name = strlen(g_fcc_bootstrap_server_ca_certificate_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_lwm2m_server_ca_certificate_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_lwm2m_server_ca_certificate_name);
    } else {
        //Set lwm2m root ca certificate name
        parameter_name = (uint8_t*)g_fcc_lwm2m_server_ca_certificate_name;
        size_of_parameter_name = strlen(g_fcc_lwm2m_server_ca_certificate_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_bootstrap_server_ca_certificate_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_bootstrap_server_ca_certificate_name);
    }

    //Check that ca certificate of current mode is present 
    kcm_status = kcm_item_get_data_size((const uint8_t*)parameter_name,
                                        size_of_parameter_name,
                                        KCM_CERTIFICATE_ITEM,
                                        &item_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, store_error_and_exit, "Failed to get size bootstrap root ca certificate size");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((item_size == 0), fcc_status = FCC_STATUS_EMPTY_ITEM, store_error_and_exit, "Empty root CA certificate");

    //Check  that ca certificate of second mode is not present, if yes - set warning
    fcc_status = verify_existence_and_set_warning(second_mode_parameter_name, size_of_second_mode_parameter_name, KCM_CERTIFICATE_ITEM, false);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed in verify_existence_and_set_warning");

    //TBD : check of mbed crypto scheme IOTPREQ-1417
store_error_and_exit:
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error  %d",
                                        fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
/**This function checks device private key.
*
* @param use_bootstrap[in]          Bootstrap mode.
*    @returns
*        fcc_status_e status.
*/
static fcc_status_e verify_device_certificate_and_private_key(bool use_bootstrap)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    size_t size_of_device_cert = 0;
    uint8_t *device_cert = NULL;
    bool is_self_signed = false;
    uint8_t *parameter_name = NULL;
    size_t size_of_parameter_name = 0;
    uint8_t *second_mode_parameter_name = NULL;
    size_t size_of_second_mode_parameter_name = 0;
    uint8_t *private_key_data = NULL;
    size_t size_of_private_key_data = 0;
    palX509Handle_t x509_cert_handle = NULLPTR;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Set device privat key names of current and second modes
    if (use_bootstrap == true) {
        parameter_name = (uint8_t*)g_fcc_bootstrap_device_private_key_name;
        size_of_parameter_name = strlen(g_fcc_bootstrap_device_private_key_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_lwm2m_device_private_key_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_lwm2m_device_private_key_name);
    } else {
        parameter_name = (uint8_t*)g_fcc_lwm2m_device_private_key_name;
        size_of_parameter_name = strlen(g_fcc_lwm2m_device_private_key_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_bootstrap_device_private_key_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_bootstrap_device_private_key_name);
    }

    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_PRIVATE_KEY_ITEM, &private_key_data, &size_of_private_key_data);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to get device certificate");

    //Check that device private key of second mode is not present, if yes - set warning
    fcc_status = verify_existence_and_set_warning(second_mode_parameter_name, size_of_second_mode_parameter_name, KCM_PRIVATE_KEY_ITEM, false);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed in verify_existence_and_set_warning");


    //Set parameter names of device certificate according to mode
    if (use_bootstrap == true) {
        //Set bootstrap root ca certificate name
        parameter_name = (uint8_t*)g_fcc_bootstrap_device_certificate_name;
        size_of_parameter_name = strlen(g_fcc_bootstrap_device_certificate_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_lwm2m_device_certificate_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_lwm2m_device_certificate_name);
    } else {
        //Set lwm2m device certificate name
        parameter_name = (uint8_t*)g_fcc_lwm2m_device_certificate_name;
        size_of_parameter_name = strlen(g_fcc_lwm2m_device_certificate_name);
        second_mode_parameter_name = (uint8_t*)g_fcc_bootstrap_device_certificate_name;
        size_of_second_mode_parameter_name = strlen(g_fcc_bootstrap_device_certificate_name);
    }

    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_CERTIFICATE_ITEM, &device_cert, &size_of_device_cert);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to get device certificate");

    //Create device certificate handle
    kcm_status = cs_create_handle_from_der_x509_cert(device_cert, size_of_device_cert, &x509_cert_handle);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_INVALID_CERTIFICATE, store_error_and_exit, "Failed to get device certificate descriptor");

    //Check device certificate public key
    kcm_status = cs_check_certifcate_public_key(x509_cert_handle, private_key_data, size_of_private_key_data);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_CERTIFICATE_PUBLIC_KEY_CORRELATION_ERROR, store_error_and_exit, "Failed to check device certificate public key");

    //Check if the certificate of second mode exists, if yes - set warning
    fcc_status = verify_existence_and_set_warning(second_mode_parameter_name, size_of_second_mode_parameter_name, KCM_CERTIFICATE_ITEM, false);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to verify_existence_and_set_warning");

    //Compare device certificate's CN attribute with endpoint name
    fcc_status = compare_cn_with_endpoint(x509_cert_handle);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to compare_cn_with_endpoint");

    //In case LWM2M certificate check it's OU attribute with aid of server link
    if (strcmp((const char*)parameter_name, g_fcc_lwm2m_device_certificate_name) == 0) {
        fcc_status = compare_ou_with_aid_server(x509_cert_handle);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to compare_ou_with_aid_server");
    }

    //Check that device certificate not self-signed
    kcm_status = cs_is_self_signed_x509_cert(x509_cert_handle, &is_self_signed);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_INVALID_CERTIFICATE, store_error_and_exit, "Failed to check if device certificate is self-signed");
    if (is_self_signed == true) {
        output_info_fcc_status = fcc_store_warning_info(parameter_name, size_of_parameter_name, g_fcc_self_signed_warning_str);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                      fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                      store_error_and_exit,
                                      "Failed to create warning %s",
                                      g_fcc_self_signed_warning_str);
    }
    //Check device certificate attributes
    fcc_status = verify_certificate_expiration(x509_cert_handle, parameter_name, size_of_parameter_name);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, store_error_and_exit, "Failed to verify_certificate_validity");

store_error_and_exit:
    fcc_free(private_key_data);
    fcc_free(device_cert);
    cs_close_handle_x509_cert(&x509_cert_handle);
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error  %d",
                                        fcc_status);
    }

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

/* The function checks firmware integrity ca and firmware integrity certificates
*/
static fcc_status_e verify_firmware_update_certificate(void)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e fcc_output_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    uint8_t *parameter_name = (uint8_t*)g_fcc_update_authentication_certificate_name;
    size_t size_of_parameter_name = strlen(g_fcc_update_authentication_certificate_name);
    size_t certificate_data_size = 0;
    uint8_t *certificate_data = NULL;
    palX509Handle_t x509_cert_handle = NULLPTR;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    fcc_status = fcc_get_kcm_data(parameter_name, size_of_parameter_name, KCM_CERTIFICATE_ITEM, &certificate_data, &certificate_data_size);

    if (fcc_status == FCC_STATUS_ITEM_NOT_EXIST || fcc_status == FCC_STATUS_EMPTY_ITEM) {
        fcc_output_status = fcc_store_warning_info((const uint8_t*)parameter_name, size_of_parameter_name, g_fcc_item_not_set_warning_str);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_output_status != FCC_STATUS_SUCCESS),
                                      fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                      exit,
                                      "Failed to create output warning %s",
                                      g_fcc_item_not_set_warning_str);
        fcc_status = FCC_STATUS_SUCCESS;
    } else {
        //If get kcm data returned error, exit with error
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to get update certificate data");

        //Create ca firmware integrity certificate handle
        kcm_status = cs_create_handle_from_der_x509_cert(certificate_data, certificate_data_size, &x509_cert_handle);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_INVALID_CERTIFICATE, exit, "Failed to get device certificate descriptor");

        //Check firmware update certificate expiration
        fcc_status = verify_certificate_expiration(x509_cert_handle, parameter_name, size_of_parameter_name);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status = fcc_status, exit, "Failed to verify_certificate_validity");
    }

exit:

    fcc_free(certificate_data);
    cs_close_handle_x509_cert(&x509_cert_handle);
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error  %d",
                                        fcc_status);
    }
    return fcc_status;
}
//FIXME : once init entropy API will be ready,add fcc_is_entropy_initialized implementation
bool fcc_is_entropy_initialized(void)
{
    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return true;
}
fcc_status_e fcc_check_time_synchronization()
{

    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    uint64_t time = 0;
    uint8_t *parameter_name = (uint8_t*)g_fcc_device_time_zone_parameter_name;
    size_t size_of_parameter_name = strlen(g_fcc_device_time_zone_parameter_name);
    size_t item_size = 0;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    /*
    Time zone defines -  https://en.wikipedia.org/wiki/List_of_tz_database_time_zones
    */
    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Check device time
    time = pal_osGetTime();
    if (time == 0) {
        output_info_fcc_status = fcc_store_warning_info((const uint8_t*)g_fcc_current_time_parameter_name, strlen(g_fcc_current_time_parameter_name), g_fcc_item_not_set_warning_str);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS), fcc_status = FCC_STATUS_WARNING_CREATE_ERROR, "Failed to create warning");
    }

    //Check device time zone
    kcm_status = kcm_item_get_data_size((const uint8_t*)parameter_name,
                                        size_of_parameter_name,
                                        KCM_CONFIG_ITEM,
                                        &item_size);
    //Store warning in case time zone is missing or empty
    if (kcm_status != KCM_STATUS_SUCCESS || item_size == 0) {
        output_info_fcc_status = fcc_store_warning_info(parameter_name, size_of_parameter_name, g_fcc_item_not_set_warning_str);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                        "Failed to create output warning %s",
                                        g_fcc_item_not_set_warning_str);
    }

    //Check UTC offset
    fcc_status = check_utc_offset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed in check_utc_offset");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

fcc_status_e fcc_check_device_general_info()
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    size_t config_param_size = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();
    //Check FCC_ENDPOINT_NAME_CONFIG_PARAM_NAME
    kcm_status = kcm_item_get_data_size((const uint8_t*)&g_fcc_endpoint_parameter_name,
                                        strlen(g_fcc_endpoint_parameter_name),
                                        KCM_CONFIG_ITEM,
                                        &config_param_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, exit, "Failed to get size of %s ", g_fcc_endpoint_parameter_name);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((config_param_size == 0), fcc_status = FCC_STATUS_EMPTY_ITEM, exit, "Size of %s is 0 ", g_fcc_endpoint_parameter_name);

exit:
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info((const uint8_t*)g_fcc_endpoint_parameter_name, strlen(g_fcc_endpoint_parameter_name), fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error %d",
                                        fcc_status);
    }
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

fcc_status_e fcc_check_device_meta_data(void)
{
    int config_param_index = 0;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    size_t config_param_size = 0;
    uint8_t *parameter_name = NULL;
    size_t size_of_parameter_name = 0;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    for (config_param_index = 0; config_param_index < FCC_MAX_CONFIG_PARAM_TYPE; config_param_index++) {

        //Set current configuration parameter to local variable
        parameter_name = (uint8_t*)fcc_config_param_lookup_table[config_param_index].config_param_name;
        size_of_parameter_name = strlen(fcc_config_param_lookup_table[config_param_index].config_param_name);

        //Check that current configuration parameter is present
        kcm_status = kcm_item_get_data_size((const uint8_t*)parameter_name,
                                            size_of_parameter_name,
                                            KCM_CONFIG_ITEM,
                                            &config_param_size);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, exit, "Failed to get size of %s ", fcc_config_param_lookup_table[config_param_index].config_param_name);
    }

exit:
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error %d",
                                        fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

fcc_status_e fcc_get_bootstrap_mode(bool *use_bootstrap)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    fcc_status_e output_info_fcc_status = FCC_STATUS_SUCCESS;
    size_t config_param_size = sizeof(uint32_t);
    size_t act_config_param_size = 0;
    uint32_t bootstrap;
    uint8_t *parameter_name = (uint8_t*)g_fcc_use_bootstrap_parameter_name;
    size_t size_of_parameter_name = strlen(g_fcc_use_bootstrap_parameter_name);

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    //Get configuration parameter
    kcm_status = kcm_item_get_data((const uint8_t*)parameter_name,
                                   size_of_parameter_name,
                                   KCM_CONFIG_ITEM,
                                   (uint8_t*)&bootstrap,
                                   config_param_size,
                                   &act_config_param_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), fcc_status = FCC_STATUS_ITEM_NOT_EXIST, exit, "Failed to get data bootstrap mode parameter");
    SA_PV_ERR_RECOVERABLE_GOTO_IF((act_config_param_size != sizeof(uint32_t)), fcc_status = FCC_STATUS_WRONG_ITEM_DATA_SIZE, exit, "Size of bootstrap mode parameter is wrong ");

    if (bootstrap != 0 && bootstrap != 1) {
        SA_PV_ERR_RECOVERABLE_GOTO_IF((true), fcc_status = FCC_STATUS_BOOTSTRAP_MODE_ERROR, exit, "Invalid bootstrap mode");
    }
    if (bootstrap == 0) {
        *use_bootstrap = false;
        output_info_fcc_status = fcc_store_warning_info(parameter_name, size_of_parameter_name, g_fcc_bootstrap_mode_false_warning_str);
        SA_PV_ERR_RECOVERABLE_GOTO_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                      fcc_status = FCC_STATUS_WARNING_CREATE_ERROR,
                                      exit,
                                      "Failed to create output warning %s",
                                      g_fcc_bootstrap_mode_false_warning_str);
    } else {
        *use_bootstrap = true;
    }

exit:
    if (fcc_status != FCC_STATUS_SUCCESS) {
        output_info_fcc_status = fcc_store_error_info(parameter_name, size_of_parameter_name, fcc_status);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((output_info_fcc_status != FCC_STATUS_SUCCESS),
                                        fcc_status = FCC_STATUS_OUTPUT_INFO_ERROR,
                                        "Failed to create output fcc_status error  %d",
                                        fcc_status);
    }
    SA_PV_LOG_TRACE_FUNC_EXIT("use_bootstrap is %d", *use_bootstrap);
    return fcc_status;
}

fcc_status_e fcc_check_device_security_objects(bool use_bootstrap)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    fcc_status = verify_root_ca_certificate(use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to verify root CA certificate");

    //Check bootstrap server URI
    fcc_status = verify_server_uri(use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to verify server URI");

    //Check device certificate and private key
    fcc_status = verify_device_certificate_and_private_key(use_bootstrap);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to verify device certificate and private key");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}

fcc_status_e fcc_check_firmware_update_integrity(void)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    fcc_status = verify_firmware_update_certificate();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((fcc_status != FCC_STATUS_SUCCESS), fcc_status, "Failed to verify integrity CA certificate");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();
    return fcc_status;
}
