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
#include "pv_error_handling.h"
#include "storage.h"
#include "esfs.h"

static kcm_status_e error_handler(esfs_result_e esfs_status)
{
    switch (esfs_status) {
        case ESFS_SUCCESS:
            return KCM_STATUS_SUCCESS;
        case ESFS_INVALID_PARAMETER:
            return KCM_STATUS_INVALID_PARAMETER;
        case ESFS_BUFFER_TOO_SMALL:
            return KCM_STATUS_INSUFFICIENT_BUFFER;
        case ESFS_EXISTS:
            return KCM_STATUS_FILE_EXIST;
        case ESFS_NOT_EXISTS:
            return KCM_STATUS_ITEM_NOT_FOUND;
        case ESFS_INVALID_FILE_VERSION:
            return KCM_STATUS_INVALID_FILE_VERSION;
        case ESFS_CMAC_DOES_NOT_MATCH:
            return KCM_STATUS_FILE_CORRUPTED;
        case ESFS_ERROR:
            return KCM_STATUS_STORAGE_ERROR;
        case ESFS_HASH_CONFLICT:
            return KCM_STATUS_FILE_NAME_CORRUPTED;
        case ESFS_FILE_OPEN_FOR_READ:
        case ESFS_FILE_OPEN_FOR_WRITE:
            return KCM_STATUS_INVALID_FILE_ACCESS_MODE;
        default:
            return  KCM_STATUS_UNKNOWN_STORAGE_ERROR;
    }
}


static bool is_file_accessible(const kcm_ctx_s *ctx)
{
    // FIXME - We need to check file access availability by comparing KCM context TLVs vs the target file header stored in ESFS that contains
    //         TLVs and access rights. In order to retrieve ESFS file TLVs and access rights we should use the following methods
    //         that are currently not implemented:
    //              - esfs_get_meta_data_qty
    //              - esfs_get_meta_data_types
    //              - esfs_get_meta_data_buffer_size
    //              - esfs_read_meta_data
    //              - esfs_get_meta_data_qty

    ctx = ctx;                 // currently unused

    return true;
}

kcm_status_e storage_init()
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    esfs_status = esfs_init();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed initializing ESFS (esfs_status %d)", esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

kcm_status_e storage_finalize()
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    esfs_status = esfs_finalize();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed finalizing ESFS (esfs_status %d)", esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

kcm_status_e storage_reset()
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    esfs_status = esfs_reset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed reset ESFS (esfs_status %d)", esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}


kcm_status_e storage_factory_reset()
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    esfs_status = esfs_factory_reset();
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed factory reset ESFS (esfs_status %d)", esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

kcm_status_e storage_file_write(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length, const uint8_t *data, size_t data_length, bool is_factory, bool is_encrypted)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    kcm_status_e close_file_status = KCM_STATUS_SUCCESS;

    kcm_status = storage_file_create(ctx, file_name, file_name_length, is_factory, is_encrypted);
    SA_PV_ERR_RECOVERABLE_RETURN_IF(kcm_status != KCM_STATUS_SUCCESS, kcm_status, "Failed to create new file");

    kcm_status = storage_file_write_with_ctx(ctx, data, data_length);// we don't check error because we need to close the file in any case

    // Data is only guaranteed to be flushed to the media on efs_close.
    close_file_status = storage_file_close(ctx);

    if (kcm_status != KCM_STATUS_SUCCESS) { // delete the file if didn't succeed to write
        (void)storage_file_delete(ctx, file_name, file_name_length);
        SA_PV_ERR_RECOVERABLE_RETURN_IF(kcm_status != KCM_STATUS_SUCCESS, kcm_status, "Failed to write data");
    }

    SA_PV_ERR_RECOVERABLE_RETURN_IF(close_file_status != KCM_STATUS_SUCCESS, close_file_status, "Failed to close file");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}

kcm_status_e storage_file_size_get(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length, size_t *file_size_out)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    kcm_status_e close_staus = KCM_STATUS_SUCCESS;

    SA_PV_LOG_TRACE_FUNC_ENTER("file_name_length=%" PRIu32 "", (uint32_t)file_name_length);

    kcm_status = storage_file_open(ctx, file_name, file_name_length);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to open the given file");

    kcm_status = storage_file_size_get_with_ctx(ctx, file_size_out);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status = kcm_status, exit, "Failed getting file size");

exit:
    close_staus = storage_file_close(ctx);
    if (kcm_status == KCM_STATUS_SUCCESS) {
        kcm_status = close_staus;
    }

    return kcm_status;
}

kcm_status_e storage_file_read(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length, uint8_t *buffer_out, size_t buffer_size, size_t *buffer_actual_size_out)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    kcm_status_e close_status = KCM_STATUS_SUCCESS;

    kcm_status = storage_file_open(ctx, file_name, file_name_length);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed to open the given file");

    kcm_status = storage_file_read_with_ctx(ctx, buffer_out, buffer_size, buffer_actual_size_out);
    SA_PV_ERR_RECOVERABLE_GOTO_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status = kcm_status, exit, "Failed ti read file");

exit:
    close_status = storage_file_close(ctx);
    if (kcm_status == KCM_STATUS_SUCCESS) {
        kcm_status = close_status;
    }

    return kcm_status;
}

kcm_status_e storage_file_delete(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length)
{
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    esfs_result_e esfs_status;
    uint16_t esfs_mode = 0;        // FIXME - Unused, yet implemented
    bool success;

    SA_PV_LOG_TRACE_FUNC_ENTER("file_name_length=%" PRIu32 "", (uint32_t)file_name_length);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid file name context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name_length == 0), KCM_STATUS_INVALID_PARAMETER, "Got empty file name");

    esfs_status = esfs_open(file_name, file_name_length, &esfs_mode, &ctx->esfs_file_h);

    //file does not exists, exit from delete function
    if (esfs_status == ESFS_NOT_EXISTS) {
        return error_handler(esfs_status);
    }

    if (esfs_status != ESFS_SUCCESS) { //file exists but there is some corruption. We will delete the file without checking it's permissions
        SA_PV_LOG_ERR("The file exists but corrupted. Delete it without checking permissions");
        esfs_status = ESFS_SUCCESS;

    } else { // check permissions
        success = is_file_accessible(ctx);
        if (!success) {
            SA_PV_LOG_ERR("Caller has no access rights to the given file");
            kcm_status = KCM_STATUS_NOT_PERMITTED;
        }

        esfs_status = esfs_close(&ctx->esfs_file_h);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed closing file (esfs_status %d)", esfs_status);

        if (kcm_status == KCM_STATUS_NOT_PERMITTED) {
            return kcm_status;
        }
    }

    // Delete the file
    esfs_status = esfs_delete(file_name, file_name_length);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed deleting file (esfs_status %d)", esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return kcm_status;
}


kcm_status_e storage_file_create(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length, bool is_factory, bool is_encrypted)
{
    esfs_result_e esfs_status;
    esfs_tlv_item_t esfs_meta_data[1];  // FIXME - Unused, yet implemented

    SA_PV_LOG_TRACE_FUNC_ENTER("file_name_length=%" PRIu32 " ", (uint32_t)file_name_length);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid file name context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name_length == 0), KCM_STATUS_INVALID_PARAMETER, "Got empty file name");

    memset(ctx, 0, sizeof(kcm_ctx_s));

    if (is_factory) {
        ctx->access_flags |= ESFS_FACTORY_VAL;
    }
    if (is_encrypted) {
        ctx->access_flags |= ESFS_ENCRYPTED;
    }

    esfs_status = esfs_create(file_name, file_name_length, &esfs_meta_data[0], 0, ctx->access_flags, &ctx->esfs_file_h);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status == ESFS_EXISTS), KCM_STATUS_FILE_EXIST, "File already exist in ESFS (esfs_status %" PRIu32 ")", (uint32_t)esfs_status);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed creating file (esfs_status %" PRIu32 ")", (uint32_t)esfs_status);

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

/** Open existing file
*
*   @param ctx KCM operation context.
*   @param file_name A binary blob that uniquely identifies the file
*   @param file_name_length The binary blob length in bytes.
@param is_factory True if KCM item is factory item, or false otherwise
@param is_encrypted True if KCM item should be encrypted, or false otherwise
*
*   @returns
*       KCM_STATUS_SUCCESS in case of success otherwise one of kcm_status_e errors
*/
kcm_status_e storage_file_open(kcm_ctx_s *ctx, const uint8_t *file_name, size_t file_name_length)
{

    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
    esfs_result_e esfs_status;
    uint16_t esfs_mode = 0;        // FIXME - Unused, yet implemented
    bool success;

    SA_PV_LOG_TRACE_FUNC_ENTER("file_name_length=%" PRIu32 "", (uint32_t)file_name_length);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid file name context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_name_length == 0), KCM_STATUS_INVALID_PARAMETER, "Got empty file name");

    memset(ctx, 0, sizeof(kcm_ctx_s));

    esfs_status = esfs_open(file_name, file_name_length, &esfs_mode, &ctx->esfs_file_h);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed opening file (esfs_status %d)", esfs_status);

    success = is_file_accessible(ctx);
    if (!success) {
        kcm_status = KCM_STATUS_NOT_PERMITTED;
        esfs_close(&ctx->esfs_file_h);
        memset(ctx, 0, sizeof(kcm_ctx_s));
    }
    SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Caller has no access rights to the given file");

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}

/** Close file in storage
*
*   @param ctx KCM operation context.
@param is_factory True if KCM item is factory item, or false otherwise
@param is_encrypted True if KCM item should be encrypted, or false otherwise
*
*   @returns
*       KCM_STATUS_SUCCESS in case of success otherwise one of kcm_status_e errors
*/
kcm_status_e storage_file_close(kcm_ctx_s *ctx)
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");

    // Data is only guaranteed to be flushed to the media on efs_close.
    esfs_status = esfs_close(&ctx->esfs_file_h);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed closing file (esfs_status %d)", esfs_status);

    memset(ctx, 0, sizeof(ctx));
    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}


kcm_status_e storage_file_write_with_ctx(kcm_ctx_s *ctx, const uint8_t *data, size_t data_length)
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER("data_length=%" PRIu32 "", (uint32_t)data_length);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF(((data == NULL) && (data_length > 0)), KCM_STATUS_INVALID_PARAMETER, "Provided NULL data buffer and data_length greater than 0");

    if (data_length != 0) {
        esfs_status = esfs_write(&ctx->esfs_file_h, data, data_length);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed writing (%" PRIu32 " B) size to file (esfs_status %" PRIu32 ")", (uint32_t)data_length, (uint32_t)esfs_status);
    }

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;

}

kcm_status_e storage_file_size_get_with_ctx(kcm_ctx_s *ctx, size_t *file_size_out)
{
    esfs_result_e esfs_status;

    SA_PV_LOG_TRACE_FUNC_ENTER_NO_ARGS();

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((file_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid pointer to file size");

    esfs_status = esfs_file_size(&ctx->esfs_file_h, file_size_out);
    SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed getting file size (esfs_status %d)", esfs_status);

    ctx->is_file_size_checked = true;
    ctx->file_size = *file_size_out;

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}


kcm_status_e storage_file_read_with_ctx(kcm_ctx_s *ctx, uint8_t *buffer_out, size_t buffer_size, size_t *buffer_actual_size_out)
{
    esfs_result_e esfs_status;
    kcm_status_e kcm_status;

    SA_PV_LOG_TRACE_FUNC_ENTER("buffer_size=%" PRIu32 "", (uint32_t)buffer_size);

    SA_PV_ERR_RECOVERABLE_RETURN_IF((ctx == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid context");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((buffer_out == NULL && buffer_size !=0 ), KCM_STATUS_INVALID_PARAMETER, "Invalid pointer to read buffer");
    SA_PV_ERR_RECOVERABLE_RETURN_IF((buffer_actual_size_out == NULL), KCM_STATUS_INVALID_PARAMETER, "Invalid pointer to output size");

    if (ctx->is_file_size_checked == false) {
        kcm_status = storage_file_size_get_with_ctx(ctx, buffer_actual_size_out);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((kcm_status != KCM_STATUS_SUCCESS), kcm_status, "Failed getting file data size (kcm_status %d)", kcm_status);
    }

    SA_PV_ERR_RECOVERABLE_RETURN_IF((buffer_size < ctx->file_size), KCM_STATUS_INSUFFICIENT_BUFFER, "Buffer too small");

    if (ctx->file_size != 0) {
        esfs_status = esfs_read(&ctx->esfs_file_h, buffer_out, buffer_size, buffer_actual_size_out);
        SA_PV_ERR_RECOVERABLE_RETURN_IF((esfs_status != ESFS_SUCCESS), error_handler(esfs_status), "Failed reading file data (esfs_status %d)", esfs_status);
    }

    SA_PV_LOG_TRACE_FUNC_EXIT_NO_ARGS();

    return KCM_STATUS_SUCCESS;
}
