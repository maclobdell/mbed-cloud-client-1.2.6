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
#include <stdbool.h>
#include "key_config_manager.h"
#include "storage.h"
#include "pv_error_handling.h"
#include "kcm_file_prefix_defs.h"
#include "cs_der_certs.h"
#include "cs_der_keys.h"
#include "fcc_malloc.h"


typedef enum {
    KCM_PRIVATE_KEY_DATA,
    KCM_PUBLIC_KEY_DATA,
    KCM_SYMMETRIC_KEY_DATA,
    KCM_CERTIFICATE_DATA,
    KCM_CONFIG_DATA,
} kcm_data_type;

static bool kcm_initialized = false;

static kcm_status_e kcm_add_prefix_to_name(const uint8_t *kcm_name, size_t kcm_name_len, const char *prefix, uint8_t **kcm_buffer_out, size_t *kcm_buffer_size_allocated_out)
{
    size_t prefix_length;

    SA_PV_LOG_TRACE_FUNC_ENTER("name len=%" PRIu32 "", (uint32_t)kcm_name_len);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_buffer_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_buffer_out parameter");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_buffer_size_allocated_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_buffer_size_allocated_out parameter");

    prefix_length = strlen(prefix);

    *kcm_buffer_out = (uint8_t *)fcc_malloc(kcm_name_len + prefix_length);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((*kcm_buffer_out == NULL), KCM_STATUS_OUT_OF_MEMORY, "Failed allocating kcm_buffer_out");

    /* Append prefix and name to allocated buffer */
    memcpy(*kcm_buffer_out, (uint8_t *)prefix, prefix_length);
    memcpy(*kcm_buffer_out + prefix_length, kcm_name, kcm_name_len);

    *kcm_buffer_size_allocated_out = kcm_name_len + prefix_length;

    SA_PV_LOG_TRACE_FUNC_EXIT("kcm_buffer_size_allocated_out=  %" PRIu32 "", (uint32_t)*kcm_buffer_size_allocated_out);
    return KCM_STATUS_SUCCESS;
}


static kcm_status_e kcm_item_name_get_prefix(kcm_item_type_e kcm_item_type, const char** prefix)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;

    switch (kcm_item_type) {
        case KCM_PRIVATE_KEY_ITEM:
            *prefix = KCM_FILE_PREFIX_PRIVATE_KEY;
            break;
        case KCM_PUBLIC_KEY_ITEM:
            *prefix = KCM_FILE_PREFIX_PUBLIC_KEY;
            break;
        case KCM_SYMMETRIC_KEY_ITEM:
            *prefix = KCM_FILE_PREFIX_SYMMETRIC_KEY;
            break;
        case KCM_CERTIFICATE_ITEM:
            *prefix = KCM_FILE_PREFIX_CERTIFICATE;
            break;
        case KCM_CONFIG_ITEM:
            *prefix = KCM_FILE_PREFIX_CONFIG_PARAM;
            break;
        case KCM_CERTIFICATE_CHAIN_ITEM:
            *prefix = KCM_FILE_PREFIX_CERTIFICATE_CHAIN;
            break;
        default:
            status = KCM_STATUS_INVALID_PARAMETER;
            break;
    }
    return status;
}

kcm_status_e kcm_init(void)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    if (!kcm_initialized) {
        status = storage_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "Failed initializing storage\n");
        kcm_initialized = true;
    }

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return status;
}

kcm_status_e kcm_finalize(void)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    if (kcm_initialized) {
        status = storage_finalize();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "Failed finalizing storage\n");
        kcm_initialized = false;
    }

    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return status;
}

kcm_status_e kcm_item_store(const uint8_t * kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, bool kcm_item_is_factory, const uint8_t * kcm_item_data, size_t kcm_item_data_size, const kcm_security_desc_s security_desc)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    kcm_ctx_s ctx;
    uint8_t *kcm_complete_name = NULL; // Filename including prefix
    size_t kcm_complete_name_size;
    const char *prefix;
    bool kcm_item_is_encrypted = true; //encrypt by default

    SA_PV_LOG_INFO_FUNC_ENTER("item name =  %.*s len=%" PRIu32 ", data size=%" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len, (uint32_t)kcm_item_data_size);

    // Check if KCM initialized, if not initialize it
    if (!kcm_initialized) {
        kcm_status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "KCM initialization failed\n");
    }


    // Validate function parameters
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_ERR_RECOVERABLE_RETURN_IF(((kcm_item_data == NULL) && (kcm_item_data_size > 0)), KCM_STATUS_INVALID_PARAMETER, "Provided kcm_item_data NULL and kcm_item_data_size greater than 0");

    //temporary check that security descriptor is NULL
    SA_PV_ERR_RECOVERABLE_RETURN_IF((security_desc != NULL), KCM_STATUS_INVALID_PARAMETER, "Security descriptor is not NULL!");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_type != KCM_CONFIG_ITEM && kcm_item_data_size == 0), KCM_STATUS_ITEM_IS_EMPTY, "The data of current item is empty!");

    switch (kcm_item_type) {
        case KCM_PRIVATE_KEY_ITEM:
            kcm_status = cs_der_priv_key_verify(kcm_item_data, kcm_item_data_size);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Private key validation failed");
            break;
        case KCM_PUBLIC_KEY_ITEM:
            kcm_status = cs_der_public_key_verify(kcm_item_data, kcm_item_data_size);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Public key validation failed");
            kcm_item_is_encrypted = false; //do not encrypt public key
            break;
        case KCM_SYMMETRIC_KEY_ITEM:
            //currently possible to write a symmetric key of size 0 since we do not check format
            break;
        case KCM_CERTIFICATE_ITEM:
            kcm_status = cs_parse_der_x509_cert(kcm_item_data, kcm_item_data_size);
            SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Certificate validation failed");
            kcm_item_is_encrypted = false; //do not encrypt certificates
            break;
        case KCM_CONFIG_ITEM:
            break;
        default:
            SA_PV_ERR_RECOVERABLE_RETURN_IF((true), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_type");
    }

    kcm_status = kcm_item_name_get_prefix(kcm_item_type, &prefix);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), Exit, "Failed during kcm_item_name_get_prefix");

    kcm_status = kcm_add_prefix_to_name(kcm_item_name, kcm_item_name_len, prefix, &kcm_complete_name, &kcm_complete_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), Exit, "Failed during kcm_add_prefix_to_name");

    kcm_status = storage_file_write(&ctx, kcm_complete_name, kcm_complete_name_size, kcm_item_data, kcm_item_data_size, kcm_item_is_factory, kcm_item_is_encrypted);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), (kcm_status = kcm_status), Exit, "Failed writing file to storage");

Exit:
    fcc_free(kcm_complete_name);
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}


kcm_status_e kcm_item_get_data_size(const uint8_t *kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, size_t *kcm_item_data_size_out)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;
    uint8_t *kcm_complete_name = NULL; // Filename including prefix
    size_t kcm_complete_name_size;
    kcm_ctx_s ctx;
    size_t kcm_data_size = 0;
    const char *prefix;

    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len=%" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len);

    // Check if KCM initialized, if not initialize it
    if (!kcm_initialized) {
        status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "KCM initialization failed\n");
    }

    // Validate function parameters
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_type >= KCM_LAST_ITEM), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_data_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Kcm size out pointer is NULL");

    status = kcm_item_name_get_prefix(kcm_item_type, &prefix);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_item_name_get_prefix");

    status = kcm_add_prefix_to_name(kcm_item_name, kcm_item_name_len, prefix, &kcm_complete_name, &kcm_complete_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_add_prefix_to_name");

    status = storage_file_size_get(&ctx, kcm_complete_name, kcm_complete_name_size, &kcm_data_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed in getting file size");

    *kcm_item_data_size_out = kcm_data_size;
    SA_PV_LOG_INFO_FUNC_EXIT("kcm data size = %" PRIu32 "", (uint32_t)*kcm_item_data_size_out);
Exit:
    fcc_free(kcm_complete_name);

    return status;
}


kcm_status_e kcm_item_get_data(const uint8_t * kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type, uint8_t * kcm_item_data_out, size_t kcm_item_data_max_size, size_t * kcm_item_data_act_size_out)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;
    uint8_t *kcm_complete_name = NULL; // Filename including prefix
    size_t kcm_complete_name_size;
    kcm_ctx_s ctx;
    const char *prefix;

    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len = %" PRIu32 ", data max size = %" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len, (uint32_t)kcm_item_data_max_size);

    // Check if KCM initialized, if not initialize it
    if (!kcm_initialized) {
        status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "KCM initialization failed\n");
    }

    // Validate function parameters
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_type >= KCM_LAST_ITEM), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_type");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_data_act_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_data_act_size_out");
    SA_PV_ERR_RECOVERABLE_RETURN_IF(((kcm_item_data_out == NULL) && (kcm_item_data_max_size > 0)), KCM_STATUS_INVALID_PARAMETER, "Provided kcm_item_data NULL and kcm_item_data_size greater than 0");

    memset(&ctx, 0, sizeof(ctx));
    status = kcm_item_name_get_prefix(kcm_item_type, &prefix);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_item_name_get_prefix");

    status = kcm_add_prefix_to_name(kcm_item_name, kcm_item_name_len, prefix, &kcm_complete_name, &kcm_complete_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_add_prefix_to_name");

    status = storage_file_read(&ctx, kcm_complete_name, kcm_complete_name_size, kcm_item_data_out, kcm_item_data_max_size, kcm_item_data_act_size_out);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed reading file from storage (%d)", status);

    SA_PV_LOG_INFO_FUNC_EXIT("kcm data size = %" PRIu32 "", (uint32_t)*kcm_item_data_act_size_out);
Exit:
    fcc_free(kcm_complete_name);

    return status;
}


kcm_status_e kcm_item_delete(const uint8_t * kcm_item_name, size_t kcm_item_name_len, kcm_item_type_e kcm_item_type)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;
    uint8_t *kcm_complete_name = NULL; // Filename including prefix
    size_t kcm_complete_name_size;
    kcm_ctx_s ctx; // FIXME - Currently not implemented
    const char *prefix;

    SA_PV_LOG_INFO_FUNC_ENTER("item name = %.*s len = %" PRIu32 "", (int)kcm_item_name_len, (char*)kcm_item_name, (uint32_t)kcm_item_name_len);

    // Check if KCM initialized, if not initialize it
    if (!kcm_initialized) {
        status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "KCM initialization failed\n");
    }

    // Validate function parameters
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_name_len == 0), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_name_len");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_item_type >= KCM_LAST_ITEM), KCM_STATUS_INVALID_PARAMETER, "Invalid kcm_item_type");

    status = kcm_item_name_get_prefix(kcm_item_type, &prefix);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_item_name_get_prefix");

    status = kcm_add_prefix_to_name(kcm_item_name, kcm_item_name_len, prefix, &kcm_complete_name, &kcm_complete_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed during kcm_add_prefix_to_name");

    status = storage_file_delete(&ctx, kcm_complete_name, kcm_complete_name_size);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed deleting kcm data");

Exit:
    fcc_free(kcm_complete_name);
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();

    return status;
}

kcm_status_e kcm_factory_reset(void)
{
    kcm_status_e status = KCM_STATUS_SUCCESS;

    SA_PV_LOG_INFO_FUNC_ENTER_NO_ARGS();

    // Check if KCM initialized, if not initialize it
    if (!kcm_initialized) {
        status = kcm_init();
        SA_PV_ERR_RECOVERABLE_RETURN_IF((status != KCM_STATUS_SUCCESS), status, "KCM initialization failed\n");
    }

    status = storage_factory_reset();
    SA_PV_ERR_RECOVERABLE_GOTO_IF((status != KCM_STATUS_SUCCESS), (status = status), Exit, "Failed perform factory reset");

Exit:
    SA_PV_LOG_INFO_FUNC_EXIT_NO_ARGS();
    return status;
}

