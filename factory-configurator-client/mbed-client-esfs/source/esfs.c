/*
 * Copyright (c) 2016 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */



// ----------------------------------------------------------- Includes -----------------------------------------------------------


#include "esfs.h"
#include "esfs_file_name.h"

#include "mbed-trace/mbed_trace.h"

#include "pal_rtos.h"
#include "pal_types.h"
#include "pal_Crypto.h"

#include <string.h>  // For memcmp and strncat



// --------------------------------------------------------- Definitions ----------------------------------------------------------


#define TRACE_GROUP                     "esfs"  // Maximum 4 characters

// We do not really know what other uses (if any) the file system card will have.
// We will assume that it may contain other files and we will keep all cfstore files in one directory.
// A future enhancement could be to put files that are not to be removed at factory reset in a separate directory.
// We prefix with a './' so as to create the files under a folder in the current working directory.
#define ESFS_WORKING_DIRECTORY          "WORKING"
#define ESFS_BACKUP_DIRECTORY           "BACKUP"
#define FACTORY_RESET_DIR               "FR"
#define FACTORY_RESET_FILE              "fr_on"

// We choose a size that does not take up too much stack, but minimizes the number of reads.
#define ESFS_READ_CHUNK_SIZE_IN_BYTES   (64)

#define ESFS_MAX_NAME_LENGTH            (1024)

#define ESFS_BITS_IN_BYTE               (8)
#define ESFS_AES_BLOCK_SIZE_BYTES       (16)
#define ESFS_AES_IV_SIZE_BYTES          (16)
#define ESFS_AES_COUNTER_INDEX_IN_IV    ESFS_AES_NONCE_SIZE_BYTES
#define ESFS_AES_COUNTER_SIZE_BYTES     (8)
#define ESFS_AES_KEY_SIZE_BYTES         (16)
#define ESFS_AES_KEY_SIZE_BITS          (ESFS_AES_KEY_SIZE_BYTES * ESFS_BITS_IN_BYTE)

#define ESFS_AES_BUF_SIZE_BYTES         (256)   // - To avoid dynamic allocations, we use static buffers for AES encryption / decryption.
                                                // - This macro defines the size in bytes of these static buffers.
                                                // - In case we have to encrypt / decrypt a bigger amount of bytes, we loop over the buffer
                                                //   and encrypt / decrypt up to ESFS_AES_BUF_SIZE_BYTES bytes on each step

// This should be incremented when the file format changes
#define ESFS_FILE_FORMAT_VERSION        (1)

#define ESFS_CMAC_SIZE_IN_BYTES         (16)

#define ESFS_FILE_COPY_CHUNK_SIZE       (256)

#define MAX_FULL_PATH_SIZE (PAL_MAX_FOLDER_DEPTH_CHAR + 1 + PAL_MAX(sizeof(ESFS_BACKUP_DIRECTORY), sizeof(ESFS_WORKING_DIRECTORY)) + PAL_MAX(sizeof(FACTORY_RESET_DIR) + sizeof(FACTORY_RESET_FILE), ESFS_QUALIFIED_FILE_NAME_LENGTH))

static bool esfs_initialize = false;



// -------------------------------------------------- Functions Implementation ----------------------------------------------------


//      ---------------------------------------------------------------
//                              Helper Functions
//      ---------------------------------------------------------------


esfs_result_e esfs_init(void)
{
    esfs_result_e result = ESFS_SUCCESS;
    tr_info("esfs_init - enter");
    if (!esfs_initialize)
    {
        palStatus_t pal_result = PAL_SUCCESS;
        esfs_file_t file_handle = {0};
        char dir_path[MAX_FULL_PATH_SIZE] = { 0 };

        pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, dir_path);
        if (pal_result != PAL_SUCCESS)
        {
            tr_err("esfs_init() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }

        strncat(dir_path, "/" ESFS_WORKING_DIRECTORY, sizeof(ESFS_WORKING_DIRECTORY));

        //Looping on first file system operation to work around IOTMORF-914 - sd-driver initialization
        for(int i=0 ; i<100; i++)
        {
            // Create the esfs subfolder working
            pal_result = pal_fsMkDir(dir_path);
            if ((pal_result == PAL_SUCCESS) || (pal_result == PAL_ERR_FS_NAME_ALREADY_EXIST))
                break;
            tr_err("esfs_init() %d", i);
            pal_osDelay(50);

        }

        if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NAME_ALREADY_EXIST))
        {
                tr_err("esfs_init() - pal_fsMkDir() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
                result = ESFS_ERROR;
                goto errorExit;
        }

        pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, dir_path);
        if (pal_result != PAL_SUCCESS)
        {

            tr_err("esfs_init() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }

        strncat(dir_path, "/" ESFS_BACKUP_DIRECTORY, sizeof(ESFS_BACKUP_DIRECTORY));

        // Create the directory ESFS_BACKUP_DIRECTORY
        pal_result = pal_fsMkDir(dir_path);
        if (pal_result != PAL_SUCCESS)
        {
            // Any error apart from file exist returns error.
            if (pal_result != PAL_ERR_FS_NAME_ALREADY_EXIST)
            {
                tr_err("esfs_init() - pal_fsMkDir() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
                result = ESFS_ERROR;
                goto errorExit;
            }
        }

        // create the correct path for factory reset file fr_on
        strncat(dir_path, "/" FACTORY_RESET_DIR "/" FACTORY_RESET_FILE, sizeof(FACTORY_RESET_DIR) + sizeof(FACTORY_RESET_FILE));
        pal_result = pal_fsFopen(dir_path, PAL_FS_FLAG_READONLY, &(file_handle.file));
        // (res == PAL_SUCCESS) : flag file can be opened for reading --> file \FR\fr_on found
        //                        previous factory reset failed during execution
        // (res == PAL_ERR_FS_NO_FILE) : flag file was not found --> good scenario
        // (res != PAL_ERR_FS_NO_FILE) : file system problem
        if (pal_result == PAL_SUCCESS)
        {
            //  Close the file before factory reset
            pal_result = pal_fsFclose(&(file_handle.file));
            if (pal_result != PAL_SUCCESS)
            {
                tr_err("esfs_init() - unexpected filesystem behavior pal_fsFclose() failed with pal_status = 0x%x", (unsigned int)pal_result);
                result = ESFS_ERROR;
                goto errorExit;
            }
            // previous factory reset failed during execution - therefore we call this factory_reset again
            result = esfs_factory_reset();
            if (result != ESFS_SUCCESS)
            {
                tr_err("esfs_init() - esfs_factory_reset() failed with esfs_result_e = 0x%x", result);
                result = ESFS_ERROR;
                goto errorExit;
            }
        } else if (pal_result != PAL_ERR_FS_NO_FILE)
        {
            tr_err("esfs_init() - unexpected filesystem behavior pal_fsFopen() failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }

        esfs_initialize = true;
    }
    return ESFS_SUCCESS;

errorExit:
    return result;

}

esfs_result_e esfs_finalize(void)
{
    esfs_initialize = false;
    tr_info("esfs_finalize - enter");
    return ESFS_SUCCESS;
}


// Validate that a file handle has been initialized by create or open.
static esfs_result_e esfs_validate(esfs_file_t *file_handle)
{
    if(file_handle && file_handle->blob_name_length > 0)
    {
        return ESFS_SUCCESS;
    }
    else
    {
        return ESFS_ERROR;
    }
}


/**********************************************************************************************************************************
Function   : esfs_not_encrypted_file_header_size

Description: This function returns the size in bytes of the file header without the metadata values part.
             This is actually the non-encrypted part of the file header.
             It is useful for calculation the file pointer position for AES encryption / decryption which starts only from the
             encrypted part of the file.

Parameters : file_handle - [IN] A pointer to a file handle for which we calculate the size.

Note       : This is a static function so no sanity check is made on arguments

Return     : The size in bytes of the non-encrypted part of the file header
**********************************************************************************************************************************/
static size_t esfs_not_encrypted_file_header_size(esfs_file_t *file_handle)
{
    esfs_tlv_properties_t *tlv_properties = &(file_handle->tlv_properties);

    return ( file_handle->blob_name_length         +    // Name length field
             sizeof(file_handle->blob_name_length) +    // Name field
             sizeof(uint16_t)                      +    // Version field
             sizeof(uint16_t)                      +    // Mode field
             (((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0) ? ESFS_AES_NONCE_SIZE_BYTES : 0) +   // Nonce field [non mandatory field]
             sizeof(tlv_properties->number_of_items)                                            +   // Metadata number of elements field
             (tlv_properties->number_of_items * sizeof(tlv_properties->tlv_items[0]))               // Metadata tlv headers
           );
}


// Returns the size in bytes of the file header.
// This can only be called after the header has been read.
static size_t esfs_file_header_size(esfs_file_t *file_handle)
{
    size_t metadata_size = 0;
    esfs_tlv_properties_t *tlv_properties = &file_handle->tlv_properties;

    for(int i = 0; i < tlv_properties->number_of_items; i++)
    {
        metadata_size += tlv_properties->tlv_items[i].length_in_bytes;
    }

    return esfs_not_encrypted_file_header_size(file_handle) + metadata_size;
}


// Helper function to calculate the cmac on data that is written.
static esfs_result_e esfs_fwrite_and_calc_cmac(const void *pbuf, size_t *num_bytes, esfs_file_t *file_handle)
{
    if(pal_CMACUpdate(file_handle->signature_ctx, pbuf, *num_bytes) != PAL_SUCCESS)
    {
        tr_err("esfs_fwrite_and_calc_cmac() - pal_CMACUpdate failed");
        return ESFS_ERROR;
    }

    if(pal_fsFwrite(&file_handle->file, pbuf, *num_bytes, num_bytes) != PAL_SUCCESS)
    {
        tr_err("esfs_fwrite_and_calc_cmac() - pal_fsFwrite failed");
        return ESFS_ERROR;
    }

    return ESFS_SUCCESS;
}


/**********************************************************************************************************************************
Function   : esfs_memcpy_reverse

Description: This function copies the first <len_bytes> bytes from input buffer <src_ptr> to output buffer <dest_ptr> in
             reversed order (e.g. '1' '2' '3' data array will be copied as '3' '2' '1').
             Note: The function assumes that the memory areas of the input buffers src_ptr and dest_ptr do not overlap.

Parameters : dest_ptr  - [IN / OUT] A pointer to the destination buffer to which bytes will be copied.
             src_ptr   - [IN]       A pointer to the source buffer from which bytes will be copied.
             len_bytes - [IN]       Number of bytes to be copied.

Note       : This is a static function so no sanity check is made on arguments

Return     : A pointer to the output buffer <dest_ptr>
**********************************************************************************************************************************/
static void *esfs_memcpy_reverse(void *dest_ptr, const void *src_ptr, uint32_t len_bytes)
{
    uint8_t       *tmp_dest_ptr = (uint8_t *)dest_ptr;
    const uint8_t *tmp_src_ptr  = (const uint8_t *)src_ptr;


    // Make the reverse copy
    while(len_bytes > 0)
    {
        *(tmp_dest_ptr++) = *(tmp_src_ptr + len_bytes - 1);
        len_bytes--;
    }

    return dest_ptr;
}



/**********************************************************************************************************************************
Function   : esfs_calc_file_pos_for_aes

Description: This function calculates the file position for the purpose of AES encrypt / decrypt:
             The returned position is relative to the beginning of the encrypted data.
             The file is encrypted starting from the meta data part (the meta data values).

Parameters : file_handle - [IN]  A pointer to a file handle on which we calculate the position.
             position    - [OUT] A pointer to size_t to be filled in with the returned position.

Note       : This is a static function so no sanity check is made on arguments

Return     : ESFS_SUCCESS on success. Error code otherwise
**********************************************************************************************************************************/
static esfs_result_e esfs_calc_file_pos_for_aes(esfs_file_t *file_handle, size_t *position)
{
    palStatus_t pal_status = PAL_SUCCESS;

    size_t non_encrypt_size = 0;


    *position = 0;

    // Get current position inside the file
    pal_status = pal_fsFtell( &(file_handle->file), (int32_t *)position );

    if(pal_status != PAL_SUCCESS)
    {
        tr_err("esfs_calc_file_pos_for_aes() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)pal_status);
        return ESFS_ERROR;
    }

    // Calculate non_encrypt_size to be subtracted from position
    non_encrypt_size = esfs_not_encrypted_file_header_size(file_handle);

    if(*position < non_encrypt_size)
    {
        tr_err("esfs_calc_file_pos_for_aes() - Error. Position is in non encrypted part.");
        return ESFS_ERROR;
    }


    *position -= non_encrypt_size;


    return ESFS_SUCCESS;
}


/**********************************************************************************************************************************
Function   : esfs_set_counter_in_iv_by_file_pos

Description: This function fills in the last 8 bytes of the IV [iv128_arr] with the counter calculated according to
             the input position.

Parameters : position  - [IN]     The position in the file when count starts from the encrypted data part (the meta data values).
             iv128_arr - [IN/OUT] A 16 bytes buffer holding the IV.
                                  First 8 bytes contain the NONCE, and last 8 bytes will be filled in with the counter.

Note       : This is a static function so no sanity check is made on arguments

Return     : ESFS_SUCCESS on success. Error code otherwise
**********************************************************************************************************************************/
static void esfs_set_counter_in_iv_by_file_pos(size_t position, uint8_t *iv128_arr)
{
    uint64_t counter = 0;


    // Calculate counter part of IV
    counter = (uint64_t)(position / ESFS_AES_BLOCK_SIZE_BYTES);


    // Copy the counter part to the IV
#if BIG__ENDIAN == 1
    memcpy(iv128_arr + ESFS_AES_COUNTER_INDEX_IN_IV, &counter, ESFS_AES_COUNTER_SIZE_BYTES);
#else
    esfs_memcpy_reverse(iv128_arr + ESFS_AES_COUNTER_INDEX_IN_IV, &counter, ESFS_AES_COUNTER_SIZE_BYTES);
#endif
}


/**********************************************************************************************************************************
Function   : esfs_aes_enc_dec_by_file_pos

Description: This function encrypts / decrypts data using AES-CTR.
             This is the basic function used for AES encrypt / decrypt.
             Due to the nature of AES-CTR which works on blocks, special handling is required in case the data in the file is not
             on block boundaries. In this case we encrypt / decrypt this "partial block data" in a temporal buffer after copying
             the data to the corresponding index inside this buffer. The rest of the data is being encrypted / decrypted normally.

Parameters : aes_ctx     - [IN]  The per-initiated AES context.
             buf_in      - [IN]  A buffer containing to data to be encrypted / decrypted.
             buf_out     - [OUT] A buffer to be filled in with the encrypted / decrypted data.
             len_bytes   - [IN]  Number of bytes to encrypt / decrypt.
             position    - [IN]  The position in the file when count starts from the encrypted data part (the meta data values).
             nonce64_ptr - [IN]  An 8 bytes buffer holding the NONCE part of the IV.

Note       : This is a static function so no sanity check is made on arguments

Return     : ESFS_SUCCESS on success. Error code otherwise
**********************************************************************************************************************************/
static esfs_result_e esfs_aes_enc_dec_by_file_pos( palAesHandle_t  aes_ctx,
                                                   const uint8_t  *buf_in,
                                                   uint8_t        *buf_out,
                                                   size_t          len_bytes,
                                                   size_t          position,
                                                   uint8_t        *nonce64_ptr
                                                 )
{
    palStatus_t pal_status = PAL_SUCCESS;

    uint8_t prev_remainder     = 0;  // Size in bytes of partial block PREVIOUSLY encrypted / decrypted
    uint8_t partial_block_size = 0;  // Size in bytes of partial block for NEXT encrypt / decrypt

    uint8_t partial_block_size_temp = 0;

    uint8_t partial_block_in[ESFS_AES_BLOCK_SIZE_BYTES]  = {0}; // Will contain data for next partial encrypt / decrypt
    uint8_t partial_block_out[ESFS_AES_BLOCK_SIZE_BYTES] = {0};

    uint8_t iv_arr[ESFS_AES_IV_SIZE_BYTES] = {0};   // Will contain nonce [bytes 0 - 7] and counter [bytes 8 - 15]

/*

    -------- partial_block_in:  Size = block_size [16 bytes]
    |
    |
   \|/

    -----------------------------------------------------------------------------------------
    |                      |                                            |                   |
    |  0  ...           0  |         Data copied form buf_in            |  0  ...        0  |
    |                      |                                            |                   |
    -----------------------------------------------------------------------------------------
               ^                               ^                                ^
               |                               |                                |
               |                               |                                |
               |                               |                                |
        Size: prev_remainder                   |                          Size: might be 0
                                               |
                                               |
                                       Size: partial_block_size
                                       (might consume the buffer till its end)


*/

    prev_remainder = (position % ESFS_AES_BLOCK_SIZE_BYTES);

    partial_block_size_temp = ESFS_AES_BLOCK_SIZE_BYTES - prev_remainder;
    partial_block_size      = PAL_MIN(partial_block_size_temp, len_bytes);

    // Prepare partial_block_in: Copy data for next encrypt / decrypt from buf_in to partial_block_in
    memcpy(partial_block_in + prev_remainder, buf_in, partial_block_size);

    // Prepare iv_arr: Copy nonce into bytes [0 - 7] of IV buffer
    memcpy(iv_arr, nonce64_ptr, ESFS_AES_NONCE_SIZE_BYTES);

    // Prepare iv_arr: Set counter in bytes [8 - 15] of IV buffer
    esfs_set_counter_in_iv_by_file_pos(position, iv_arr);


    // Encrypt / decrypt partial block [run on entire block, and copy later only desired part)
    pal_status = pal_aesCTRWithZeroOffset(aes_ctx, partial_block_in, partial_block_out, ESFS_AES_BLOCK_SIZE_BYTES, iv_arr);

    if(pal_status != PAL_SUCCESS)
    {
        tr_err("esfs_aes_enc_dec_by_file_pos() - pal_aesCTRWithZeroOffset() failed with pal_status = 0x%x", (unsigned int)pal_status);
        return ESFS_ERROR;
    }

    // Copy partial_block_out to buf_out
    memcpy(buf_out, partial_block_out + prev_remainder, partial_block_size);


    // Encrypt / decrypt the rest of the data
    if(len_bytes > partial_block_size)
    {
        // Set updated counter in bytes [8 - 15] of IV buffer
        esfs_set_counter_in_iv_by_file_pos(position + partial_block_size, iv_arr);

        pal_status = pal_aesCTRWithZeroOffset(aes_ctx, buf_in + partial_block_size, buf_out + partial_block_size, len_bytes - partial_block_size, iv_arr);

        if(pal_status != PAL_SUCCESS)
        {
            tr_err("esfs_aes_enc_dec_by_file_pos() - pal_aesCTRWithZeroOffset() failed with pal_status = 0x%x", (unsigned int)pal_status);
            return ESFS_ERROR;
        }
    }


    return ESFS_SUCCESS;
}


/**********************************************************************************************************************************
Function   : esfs_read_and_decrypt

Description: This function reads encrypted data from a file, decrypts it, and writes it into a buffer.

Parameters : file_handle    - [IN]  A pointer to a file handle from which we read data.
             buffer         - [IN]  The buffer to fill in with decrypted file data.
             bytes_to_read  - [IN]  Number of bytes to read from the file.
             read_bytes_ptr - [OUT] A pointer to size_t to be filled in with number of bytes actually read from the file.

Note       : This is a static function so no sanity check is made on arguments

Return     : ESFS_SUCCESS on success. Error code otherwise
**********************************************************************************************************************************/
static esfs_result_e esfs_read_and_decrypt(esfs_file_t *file_handle, void *buffer, size_t bytes_to_read, size_t *read_bytes_ptr)
{
    esfs_result_e result     = ESFS_SUCCESS;
    palStatus_t   pal_status = PAL_SUCCESS;

    size_t position = 0;


    // Get file pointer position for AES - Must be done before calling pal_fsFread() which modifies the file pointer position
    result = esfs_calc_file_pos_for_aes(file_handle, &position);

    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_read_and_decrypt() - esfs_calc_file_pos_for_aes() failed with status = 0x%x", result);
        return result;
    }


    // Read file's encrypted data into buffer
    pal_status = pal_fsFread( &(file_handle->file), buffer, bytes_to_read, read_bytes_ptr );

    if((pal_status != PAL_SUCCESS) || (*read_bytes_ptr != bytes_to_read))
    {
        tr_err("esfs_read_and_decrypt() - pal_fsFread() failed with pal_status = 0x%x", (unsigned int)pal_status);
        return ESFS_ERROR;
    }


    // AES decrypt in-place - decrypt the encrypted data inside buffer, into buffer [out parameter]
    result = esfs_aes_enc_dec_by_file_pos(file_handle->aes_ctx, buffer, buffer, bytes_to_read, position, file_handle->nonce);

    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_read_and_decrypt() - esfs_aes_enc_dec_by_file_pos() failed with status = 0x%x", (unsigned int)result);
        return result;
    }


    return ESFS_SUCCESS;
}


/**********************************************************************************************************************************
Function   : esfs_encrypt_fwrite_and_calc_cmac

Description: This function takes a plain text buffer, encrypts it, writes the encrypted data to a file, and updates the
             CMAC signature.

             Since we cannot modify the data of the input buffer (const), this operation cannot be done in-place, so we need
             to use another buffer for the encryption result. In order to avoid dynamically allocation, we use a buffer
             of size ESFS_AES_BUF_SIZE_BYTES statically allocated on the stack. This forces us to encrypt and write in a loop -
             each iteration encrypts and writes maximum size of ESFS_AES_BUF_SIZE_BYTES bytes.

Parameters : buffer         - [IN]     The buffer to encrypt and write to the file.
             bytes_to_write - [IN/OUT] A pointer to size_t containing the number of bytes to write, and to be to be filled in
                                       with the number of bytes actually been written to the file.
             file_handle    - [IN]     A pointer to a file handle to which we write the data.

Note       : This is a static function so no sanity check is made on arguments

Return     : ESFS_SUCCESS on success. Error code otherwise
**********************************************************************************************************************************/
static esfs_result_e esfs_encrypt_fwrite_and_calc_cmac(const void *buffer, size_t *bytes_to_write, esfs_file_t *file_handle)
{
    esfs_result_e result = ESFS_SUCCESS;

    size_t position    = 0;
    size_t write_bytes = 0;

    size_t remaining_bytes_to_write = *bytes_to_write;

    const uint8_t *buffer_tmp_ptr = (uint8_t *)buffer;  // Will point to the next reading point in buffer as we read it

    uint8_t encrypted_data[ESFS_AES_BUF_SIZE_BYTES] = {0}; // Will hold encrypted data to be written to the file


    if(buffer == NULL)
    {
        tr_err("esfs_encrypt_fwrite_and_calc_cmac() - Bad arguments error. Input buffer is NULL.");
        return ESFS_ERROR;
    }


    *bytes_to_write = 0;    // Reset out parameter

    // Get file pointer position for AES - Must be done before calling esfs_fwrite_and_calc_cmac() which modifies the file pointer position
    result = esfs_calc_file_pos_for_aes(file_handle, &position);

    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_encrypt_fwrite_and_calc_cmac() - esfs_calc_file_pos_for_aes failed with result=0x%x", result);
        return result;
    }


    // On every iteration in the loop, encrypt ESFS_AES_BUF_SIZE_BYTES bytes, and write them to the file
    while(remaining_bytes_to_write >= ESFS_AES_BUF_SIZE_BYTES)
    {
        // AES encrypt into encrypted_data
        result = esfs_aes_enc_dec_by_file_pos(file_handle->aes_ctx, buffer_tmp_ptr, encrypted_data, ESFS_AES_BUF_SIZE_BYTES, position, file_handle->nonce);

        if(result != ESFS_SUCCESS)
        {
            tr_err("esfs_encrypt_fwrite_and_calc_cmac() - esfs_aes_enc_dec_by_file_pos failed with result=0x%x", result);
            return result;
        }

        write_bytes = ESFS_AES_BUF_SIZE_BYTES;

        // Write the encrypted data to the file
        result = esfs_fwrite_and_calc_cmac(encrypted_data, &write_bytes, file_handle);

        if((result != ESFS_SUCCESS) || (write_bytes != ESFS_AES_BUF_SIZE_BYTES))
        {
            tr_err("esfs_encrypt_fwrite_and_calc_cmac() - esfs_fwrite_and_calc_cmac() status = 0x%x, written bytes = %zu, expected = %u",
                    (unsigned int)result, write_bytes, (unsigned int)ESFS_AES_BUF_SIZE_BYTES);

            // esfs_fwrite_and_calc_cmac() failed so we cannot be sure of the state of the file - mark the file as invalid
            file_handle->file_invalid = 1;

            return ESFS_ERROR;
        }

        (*bytes_to_write) += write_bytes;

        position       += ESFS_AES_BUF_SIZE_BYTES;
        buffer_tmp_ptr += ESFS_AES_BUF_SIZE_BYTES;

        remaining_bytes_to_write -= ESFS_AES_BUF_SIZE_BYTES;
    }


    // AES encrypt the leftover of buffer
    if(remaining_bytes_to_write > 0)
    {
        // AES encrypt into encrypted_data
        result = esfs_aes_enc_dec_by_file_pos(file_handle->aes_ctx, buffer_tmp_ptr, encrypted_data, remaining_bytes_to_write, position, file_handle->nonce);

        if(result != ESFS_SUCCESS)
        {
            tr_err("esfs_encrypt_fwrite_and_calc_cmac() - esfs_aes_enc_dec_by_file_pos failed with result=0x%x", result);
            return result;
        }

        write_bytes = remaining_bytes_to_write;

        // Write the encrypted data to the file
        result = esfs_fwrite_and_calc_cmac(encrypted_data, &write_bytes, file_handle);

        if((result != ESFS_SUCCESS) || (write_bytes != remaining_bytes_to_write))
        {
            tr_err("esfs_encrypt_fwrite_and_calc_cmac() - esfs_fwrite_and_calc_cmac() status = 0x%x, written bytes = %zu, expected = %zu",
                    (unsigned int)result, write_bytes, remaining_bytes_to_write);

            // esfs_fwrite_and_calc_cmac() failed so we cannot be sure of the state of the file - mark the file as invalid
            file_handle->file_invalid = 1;

            return ESFS_ERROR;
        }

        (*bytes_to_write) += write_bytes;
    }


    return ESFS_SUCCESS;
}


esfs_result_e esfs_reset(void)
{
    esfs_result_e result = ESFS_SUCCESS;
    palStatus_t pal_result = PAL_SUCCESS;
    char dir_path[MAX_FULL_PATH_SIZE] = { 0 };
    tr_info("esfs_reset - enter");
    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, dir_path);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_reset() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(dir_path, "/" ESFS_WORKING_DIRECTORY, sizeof(ESFS_WORKING_DIRECTORY));


    // delete the files in working dir
    pal_result = pal_fsRmFiles(dir_path);
    // the use case is that esfs folder may not exist
    if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NO_FILE) && (pal_result != PAL_ERR_FS_NO_PATH))
    {
        tr_err("esfs_reset() - pal_fsRmFiles(ESFS_WORKING_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // delete working directory
    pal_result = pal_fsRmDir(dir_path);
    if (pal_result != PAL_SUCCESS)
    {
        // Any error apart from dir not exist returns error.
        if ((pal_result != PAL_ERR_FS_NO_FILE) && (pal_result != PAL_ERR_FS_NO_PATH))
        {
            tr_err("esfs_reset() - pal_fsRmDir(ESFS_WORKING_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, dir_path);
    if (pal_result != PAL_SUCCESS)
    {

        tr_err("esfs_reset() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(dir_path, "/" ESFS_BACKUP_DIRECTORY, sizeof(ESFS_BACKUP_DIRECTORY));


    // delete the files in backup dir
    pal_result = pal_fsRmFiles(dir_path);
    // the use case is that esfs folder may not exist
    if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NO_FILE) && (pal_result != PAL_ERR_FS_NO_PATH))
    {
        tr_err("esfs_reset() - pal_fsRmFiles(ESFS_BACKUP_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    pal_result = pal_fsRmDir(dir_path);
    if (pal_result != PAL_SUCCESS)
    {
        // Any error apart from dir not exist returns error.
        if ((pal_result != PAL_ERR_FS_NO_FILE) && (pal_result != PAL_ERR_FS_NO_PATH))
        {
            tr_err("esfs_reset() - pal_fsRmDir(ESFS_BACKUP_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    if (esfs_finalize() != ESFS_SUCCESS)
    {
        tr_err("esfs_reset() - esfs_finalize() failed");
        result = ESFS_ERROR;
        goto errorExit;
    }

    if (esfs_init() != ESFS_SUCCESS)
    {
        tr_err("esfs_reset() - esfs_init() failed");
        result = ESFS_ERROR;
        goto errorExit;
    }

    return ESFS_SUCCESS;

errorExit:
    return result;
}


esfs_result_e esfs_factory_reset(void) {
    palStatus_t   pal_result = PAL_SUCCESS;
    esfs_result_e result = ESFS_SUCCESS;
    esfs_file_t file_handle = { 0 };
    char working_dir_path[MAX_FULL_PATH_SIZE] = { 0 };
    char full_path_backup_dir[MAX_FULL_PATH_SIZE] = { 0 };
    tr_info("esfs_factory_reset - enter");
    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, full_path_backup_dir);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_factory_reset() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        return ESFS_ERROR;
    }

    strncat(full_path_backup_dir, "/" ESFS_BACKUP_DIRECTORY "/" FACTORY_RESET_DIR, sizeof(ESFS_BACKUP_DIRECTORY) + sizeof(FACTORY_RESET_DIR));
    // Create the factory reset subfolder - FR
    pal_result = pal_fsMkDir(full_path_backup_dir);
    if (pal_result != PAL_SUCCESS)
    {
        // Any error apart from file exist returns error.
        if (pal_result != PAL_ERR_FS_NAME_ALREADY_EXIST)
        {
            tr_err("esfs_factory_reset() - pal_fsMkDir(ESFS_BACKUP_DIRECTORY/FACTORY_RESET_DIR) failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    strncat(full_path_backup_dir, "/" FACTORY_RESET_FILE, sizeof(FACTORY_RESET_FILE));
    // Create the fr_on flag file
    pal_result = pal_fsFopen(full_path_backup_dir, PAL_FS_FLAG_READWRITEEXCLUSIVE, &(file_handle.file));

    // (res == PAL_SUCCESS) : factory reset is called on the first time
    // (res == PAL_ERR_FS_NAME_ALREADY_EXIST) : factory reset is called again after it was failed 
    // on the first time and therefore the file exists
    if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NAME_ALREADY_EXIST))
    {
        tr_err("esfs_factory_reset() - unexpected filesystem behavior pal_fsFopen() failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // close the file only if we opened it
    if (pal_result == PAL_SUCCESS)
    {
        pal_result = pal_fsFclose(&(file_handle.file));
        if (pal_result != PAL_SUCCESS)
        {
            tr_err("esfs_factory_reset() - unexpected filesystem behavior pal_fsFclose() failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, working_dir_path);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_factory_reset() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // Check if there is a single partition by comparing the primary and secondary mount points.
    // This is the only reliable way to do it, since the logic that determines the number of partitions is
    // hidden behind the PAL API.
    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, full_path_backup_dir);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_factory_reset() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }
    bool is_single_partition = (strcmp(working_dir_path,full_path_backup_dir) == 0);

    strncat(working_dir_path, "/" ESFS_WORKING_DIRECTORY, sizeof(ESFS_WORKING_DIRECTORY));

    // We can only format the working folder if it is dedicated for exclusive use of esfs and
    // it is not the only partition that exists. The assumption here is that if it is the only partition,
    // then the backup folder is also on that partition. In that case, formatting would remove the backup partition,
    // which we do not want to do!
    if (pal_fsIsPrivatePartition(PAL_FS_PARTITION_PRIMARY) && !is_single_partition)
    {
        pal_result = pal_fsFormat(PAL_FS_PARTITION_PRIMARY);
        if (pal_result != PAL_SUCCESS)
        {
            tr_err("esfs_factory_reset() - pal_fsFormat() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
        pal_result = pal_fsMkDir(working_dir_path);
        if (pal_result != PAL_SUCCESS)
        {
            tr_err("esfs_factory_reset() - pal_fsMkDir(ESFS_WORKING_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }
    else
    {
        // delete the files in working dir
        pal_result = pal_fsRmFiles(working_dir_path);
        // the use case is that esfs folder may not exist
        if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NO_FILE) && (pal_result != PAL_ERR_FS_NO_PATH))
        {
            tr_err("esfs_factory_reset() - pal_fsRmFiles(ESFS_WORKING_DIRECTORY) failed with pal_status = 0x%x", (unsigned int)pal_result);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }




    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, full_path_backup_dir);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_factory_reset() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        return ESFS_ERROR;
    }
    strncat(full_path_backup_dir, "/" ESFS_BACKUP_DIRECTORY, sizeof(ESFS_BACKUP_DIRECTORY));

    pal_result = pal_fsCpFolder(full_path_backup_dir, working_dir_path);

    if ((pal_result != PAL_SUCCESS) && (pal_result != PAL_ERR_FS_NO_FILE))
    {
        tr_err("esfs_factory_reset() - pal_fsCpFolder() from backup to working failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(full_path_backup_dir, "/" FACTORY_RESET_DIR "/" FACTORY_RESET_FILE, sizeof(FACTORY_RESET_DIR) + sizeof(FACTORY_RESET_FILE));
    // delete the flag file because factory reset flow ended successfully 
    pal_result = pal_fsUnlink(full_path_backup_dir);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_factory_reset() - pal_fsUnlink(ESFS_BACKUP_DIRECTORY/FACTORY_RESET_DIR/FACTORY_RESET_FILE) failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
     }

    return ESFS_SUCCESS;

errorExit:
    return result;
}
// Internal function to check the files validity.
// Checks the name given against the name written in the file.
// Checks the version.
// Initializes some fields of file_handle: blob_name_length, esf_mode
// Assumes that the read position is at the start of the file.
// return esf_success - name matches;
//                        ESFS_HASH_CONFLICT - name does not match
//                        ESFS_WRONG_FILE_VERSION - version does not match
//                        ESFS_ERROR - other problem
// On ESFS_SUCCESS or ESFS_HASH_CONFLICT the read position is set after the name.
// On failure the position is undefined.
static esfs_result_e esfs_check_file_validity(const uint8_t* name, size_t name_length, esfs_file_t *file_handle)
{
    char buffer[ESFS_READ_CHUNK_SIZE_IN_BYTES];
    size_t num_bytes = 0;
    int i;
    esfs_result_e result = ESFS_ERROR;
    palStatus_t res;
    uint16_t version;

    // Read the version
    num_bytes = sizeof(version);
    res = pal_fsFread(&file_handle->file, (void *)( &version ), sizeof(version), &num_bytes);
    //tr_info("esfs_open res=0x%x tmp=%d items=%d",(unsigned int)res,(int)tmp,(int)items);
    if (res != PAL_SUCCESS || num_bytes != sizeof(version))
    {
        tr_err("esfs_check_file_validity() - pal_fsFread() failed with pal result = 0x%x and num_bytes bytes = %zu",
            (unsigned int)res, num_bytes);
        goto errorExit;
    }
    if(version != ESFS_FILE_FORMAT_VERSION)
    {
        tr_err("esfs_check_file_validity() - invalid parameter : pal_fsFread() failed with version = %u", (unsigned int)version);
        result = ESFS_INVALID_FILE_VERSION;
        goto errorExit;
    }

    // Read the mode
    res = pal_fsFread(&file_handle->file, (void *)( &file_handle->esfs_mode ), sizeof(file_handle->esfs_mode), &num_bytes);
    if (res != PAL_SUCCESS || num_bytes != sizeof(file_handle->esfs_mode))
    {
        tr_err("esfs_check_file_validity() - pal_fsFread() failed with pal result = 0x%x and num_bytes bytes = %zu",
            (unsigned int)res, num_bytes);
        goto errorExit;
    }

    // Read the name length
    res = pal_fsFread(&file_handle->file, (void *)( &file_handle->blob_name_length ), sizeof(file_handle->blob_name_length), &num_bytes);
    if (res != PAL_SUCCESS || num_bytes != sizeof(file_handle->blob_name_length))
    {
        tr_err("esfs_check_file_validity() - pal_fsFread() failed with pal result = 0x%x and num_bytes bytes = %zu",
            (unsigned int)res, num_bytes);
        goto errorExit;
    }
    if (name_length != file_handle->blob_name_length)
    {
        tr_err("esfs_check_file_validity() - esfs hash conflict : The hash of the name conflicts with the hash of another name");
        // The hash of the name conflicts with the hash of another name.
        result = ESFS_HASH_CONFLICT;
        goto errorExit;
    }
    // Check the name chunk by chunk
    for (i = name_length; i > 0; i -= ESFS_READ_CHUNK_SIZE_IN_BYTES)
    {
        // Read a chunk
        num_bytes = 0;
        res = pal_fsFread(&file_handle->file, (void *)buffer, PAL_MIN(i, ESFS_READ_CHUNK_SIZE_IN_BYTES), &num_bytes);
        if (res != PAL_SUCCESS || num_bytes == 0)
        {
            tr_err("esfs_check_file_validity() - pal_fsFread() failed with pal result = 0x%x and num_bytes bytes = %zu",
                (unsigned int)res, num_bytes);
            goto errorExit;
        }
        // Check that the chunk matches
        //tr_info("Comparing %s (%d bytes) name_length=%d", name, (int )num_bytes,(int )name_length);
        if (memcmp(buffer, name, num_bytes) != 0)
        {
            tr_err("esfs_check_file_validity() - esfs hash conflict : The hash of the name conflicts with the hash of another name");
            // The hash of the name conflicts with the hash of another name.
            result = ESFS_HASH_CONFLICT;
            goto errorExit;
        }
        name += num_bytes;
    }
    return ESFS_SUCCESS;
errorExit:
    return result;
}


// Internal function to check the name against the name written in the file.
// Assume that the read position is set to before the name length.
// return esf_success - name matches;
//                        ESFS_HASH_CONFLICT - name does not match ;
//                        ESFS_ERROR - other problem
// On ESFS_SUCCESS or ESFS_HASH_CONFLICT the read position is set after the name.
// On failure the position is undefined.
static esfs_result_e esfs_check_cmac(esfs_file_t *file_handle)
{
    // General purpose reusable buffer. It should be at least 2*ESFS_CMAC_SIZE_IN_BYTES bytes
    unsigned char buffer[ESFS_READ_CHUNK_SIZE_IN_BYTES];

    size_t num_bytes;
    int i;
    esfs_result_e result = ESFS_ERROR;
    palStatus_t res;
    int32_t file_size;
    int32_t initial_pos;

    palCMACHandle_t signature_ctx;
    uint16_t cmac_created = 0;

    // Verify that the at least 2*ESFS_CMAC_SIZE_IN_BYTES bytes
    PAL_ASSERT_STATIC(sizeof(buffer) >= (2*ESFS_CMAC_SIZE_IN_BYTES));

    // Get current position
    res = pal_fsFtell(&file_handle->file, &initial_pos);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Seek to end of file
    res = pal_fsFseek(&file_handle->file, 0, PAL_FS_OFFSET_SEEKEND);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_fsFseek() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Get new position
    res = pal_fsFtell(&file_handle->file, &file_size);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Set to the start of the file
    res = pal_fsFseek(&file_handle->file, 0, PAL_FS_OFFSET_SEEKSET);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_fsFseek() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }


    res = pal_osGetDeviceKey128Bit(palOsStorageSignatureKey, &buffer[0], ESFS_CMAC_SIZE_IN_BYTES);  // Now first 16 Bytes of buffer contain the key
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_osGetDeviceKey128Bit() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Start CMAC with the key
    res = pal_CMACStart(&signature_ctx, &buffer[0], 128, PAL_CIPHER_ID_AES);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_CMACStart() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    else
    {
        cmac_created = 1;
    }

    // Iterate over the file in chunks to calculate the cmac
    // buffer will contain only data read form the file
    for (i = file_size - ESFS_CMAC_SIZE_IN_BYTES; i > 0; i -= ESFS_READ_CHUNK_SIZE_IN_BYTES)
    {
        // Read a chunk
        // Here we read the file as is - plain text or encrypted
        res = pal_fsFread(&file_handle->file, buffer, PAL_MIN(i, ESFS_READ_CHUNK_SIZE_IN_BYTES), &num_bytes);
        if (res != PAL_SUCCESS || num_bytes == 0)
        {
            tr_err("esfs_check_cmac() - pal_fsFread() (Iterate over the file in chunks) failed with pal result = 0x%x and num_bytes bytes = %zu",
                (unsigned int)res, num_bytes);
            goto errorExit;
        }
        // Update the cmac calculation according to the data that was read.
        res = pal_CMACUpdate(signature_ctx, buffer, num_bytes);
        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_check_cmac() - pal_CMACStart() (Iterate over the file in chunks) failed with pal_status = 0x%x", (unsigned int)res);
            goto errorExit;
        }
     }

    res = pal_CMACFinish(&signature_ctx, &buffer[0], &num_bytes);   // Now first 16 Bytes of buffer contain the CMAC signature
    tr_info("esfs_close len=%d", (int)num_bytes);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_CMACFinish() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    cmac_created = 0;

    // Read the signature from the file
    res = pal_fsFread(&file_handle->file, &buffer[ESFS_CMAC_SIZE_IN_BYTES], ESFS_CMAC_SIZE_IN_BYTES, &num_bytes);
    if (res != PAL_SUCCESS || num_bytes != ESFS_CMAC_SIZE_IN_BYTES)
    {
        tr_err("esfs_check_cmac() - pal_fsFread() (signature) failed with pal result = 0x%x and num_bytes bytes = %zu",
            (unsigned int)res, num_bytes);
        goto errorExit;
    }

    // Now first 16 Bytes of buffer contain the CMAC calculated signature,
    // and next 16 Bytes contain the CMAC signature written at the end of the file

    // Restore initial position
    res = pal_fsFseek(&file_handle->file, initial_pos, PAL_FS_OFFSET_SEEKSET);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_check_cmac() - pal_fsFseek() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Compare the cmac that we read from the file with the one that we calculated.
    if(memcmp(&buffer[0], &buffer[ESFS_CMAC_SIZE_IN_BYTES], ESFS_CMAC_SIZE_IN_BYTES) != 0)
    {
        tr_err("esfs_check_cmac() - cmac that we read from the file does not match the one that we calculated");
        return ESFS_CMAC_DOES_NOT_MATCH;
    }
    else
    {
        return ESFS_SUCCESS;
    }
errorExit:
    if(cmac_created)
    {
        // Clean up cmac. Ignore error.
        (void)pal_CMACFinish(&signature_ctx, &buffer[0], &num_bytes);
    }
    // No need to restore position on failure.
    return result;

}


// Helper function
// Restores current position unless it fails.
// On failure the position is undefined.
static palStatus_t esfs_get_physical_file_size(palFileDescriptor_t* fd, int32_t *file_size)
{
    int32_t current_pos = 0;
    palStatus_t res;

    // Get current position
    res = pal_fsFtell(fd, &current_pos);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_get_physical_file_size() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    // Seek to end of file
    res = pal_fsFseek(fd, 0, PAL_FS_OFFSET_SEEKEND);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_get_physical_file_size() - pal_fsFseek() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    // Get new position
    res = pal_fsFtell(fd, file_size);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_get_physical_file_size() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    // Restore old position
    res = pal_fsFseek(fd, current_pos, PAL_FS_OFFSET_SEEKSET);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_get_physical_file_size() - pal_fsFseek() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }

errorExit:
    return res;
}


static esfs_result_e esfs_copy_file(const char *src_file, const char *dst_file)
{
    bool is_src_file_opened = false;
    bool is_dst_file_opened = false;
    esfs_file_t file_handle = { 0 };
    esfs_file_t file_handle_copy = { 0 };
    esfs_result_e result = ESFS_ERROR;
    palStatus_t res = PAL_SUCCESS;
    size_t bytes_to_read = ESFS_FILE_COPY_CHUNK_SIZE;
    size_t num_bytes_read = 0;
    size_t num_bytes_write = 0;
    uint8_t buffer[ESFS_FILE_COPY_CHUNK_SIZE] = {0};
    int32_t file_size = 0;
    int32_t copied_bytes = 0;
    // Open src file read only mode
    res = pal_fsFopen(src_file, PAL_FS_FLAG_READONLY, &(file_handle.file));
    if (res != PAL_SUCCESS)
    {
        // File cannot be opened so return an error
        tr_err("esfs_copy_file() - pal_fsFopen() src file failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_NOT_EXISTS;
        goto errorExit;
    }
    is_src_file_opened = true;
    // Open for reading and writing exclusively, If the file already exists, trunced file
    res = pal_fsFopen(dst_file, PAL_FS_FLAG_READWRITETRUNC, &(file_handle_copy.file));
    if (res != PAL_SUCCESS)
    {
        // File cannot be opened so return an error
        tr_err("esfs_copy_file() - pal_fsFopen() dst file failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }
    is_dst_file_opened = true;

    res = esfs_get_physical_file_size(&(file_handle.file), &file_size);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_copy_file() - esfs_get_physical_file_size() failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }
    while (copied_bytes < file_size)
    {
        if (copied_bytes + (int32_t)bytes_to_read > file_size)
        {
            bytes_to_read = file_size - copied_bytes;
        }
        res = pal_fsFread(&(file_handle.file), buffer, bytes_to_read, &num_bytes_read);
        if (res != PAL_SUCCESS)
        {
            tr_err("esfs_copy_file() - pal_fsFread() failed with pal_status = 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }

        res = pal_fsFwrite(&(file_handle_copy.file), buffer, bytes_to_read, &num_bytes_write);
        if ((res != PAL_SUCCESS) || (num_bytes_write != bytes_to_read))
        {
            tr_err("esfs_copy_file() - pal_fsFwrite() failed with pal result = 0x%x and num_bytes_write bytes = %zu",
                (unsigned int)res, num_bytes_write);
            result = ESFS_ERROR;
            goto errorExit;
        }

        copied_bytes += bytes_to_read;
            
    }


    res = pal_fsFclose(&(file_handle.file));
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_copy_file() - pal_fsFclose() for src file failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }
    res = pal_fsFclose(&(file_handle_copy.file));
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_copy_file() - pal_fsFclose() for dst file failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }
    return ESFS_SUCCESS;

errorExit:
    if (is_src_file_opened)
    {
        // we will not delete the src file
        pal_fsFclose(&(file_handle.file));
    }

    if (is_dst_file_opened)
    {
        pal_fsFclose(&(file_handle_copy.file));
        // Clean up if possible. Ignore return value.
        (void)pal_fsUnlink(dst_file);
    }
    return result;
}


// We assume a null terminated name for the time being.
static esfs_result_e esfs_create_internal( const uint8_t *name,
                                           size_t name_length,
                                           const esfs_tlv_item_t *meta_data,
                                           size_t meta_data_qty,
                                           uint16_t esfs_mode,
                                           esfs_file_t *file_handle,
                                           const char* full_path_to_create
                                         )
{
    uint8_t key[ESFS_CMAC_SIZE_IN_BYTES];

    esfs_result_e result = ESFS_ERROR;
    palStatus_t res = PAL_SUCCESS;

    int32_t position = 0;
    size_t num_bytes = 0;
    size_t i;
    uint16_t file_created = 0;
    uint16_t u16;


    // Create the file
    res = pal_fsFopen(full_path_to_create, PAL_FS_FLAG_READWRITEEXCLUSIVE, &file_handle->file);

    if(res != PAL_SUCCESS)
    {
        if(res == PAL_ERR_FS_NAME_ALREADY_EXIST)
        {
            result = ESFS_EXISTS;
            // Check if there is a different name in the file
            // Check that the name written inside the file is the same as that given. If not
            // you should choose a different name.
            res = pal_fsFopen(full_path_to_create, PAL_FS_FLAG_READONLY, &file_handle->file);
            if(res == PAL_SUCCESS)
            {
                file_handle->esfs_mode = 0;
                // result can be ESFS_HASH_CONFLICT or ESFS_WRONG_FILE_VERSION
                esfs_result_e check_result  = esfs_check_file_validity(name, name_length, file_handle);
                if(check_result == ESFS_HASH_CONFLICT || check_result == ESFS_INVALID_FILE_VERSION)
                {
                    result = check_result;
                }
                pal_fsFclose(&file_handle->file);
            }
        }
        // more informative message will be written after hash conflict will be implemented
        tr_err("esfs_create_internal() - pal_fsFopen() failed");
        goto errorExit;
    }

    
    file_created = 1;

    res = pal_osGetDeviceKey128Bit(palOsStorageSignatureKey, &key[0], ESFS_CMAC_SIZE_IN_BYTES);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_create_internal() - pal_osGetDeviceKey128Bit() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }
    res = pal_CMACStart(&file_handle->signature_ctx, &key[0], 128, PAL_CIPHER_ID_AES);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_create_internal() - pal_CMACStart() failed with pal_status = 0x%x", (unsigned int)res);
        goto errorExit;
    }


    // Write the version
    u16 = ESFS_FILE_FORMAT_VERSION;
    num_bytes = sizeof(u16);
    result = esfs_fwrite_and_calc_cmac(&u16, &num_bytes, file_handle);
    if(result != ESFS_SUCCESS || num_bytes != sizeof(u16))
    {
        tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for esfs version failed with esfs result = 0x%x and num_bytes bytes = %zu",
            result, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // Write the mode
    num_bytes = sizeof(esfs_mode);
    result = esfs_fwrite_and_calc_cmac(&esfs_mode, &num_bytes, file_handle);
    if(result != ESFS_SUCCESS || num_bytes != sizeof(esfs_mode))
    {
        tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for esfs_mode failed with esfs result = 0x%x and num_bytes bytes = %zu",
            result, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // Header
    // Write the name length
    u16 = (uint16_t)name_length;
    num_bytes = sizeof(u16);
    result = esfs_fwrite_and_calc_cmac(&u16, &num_bytes, file_handle);
    if(result != ESFS_SUCCESS || num_bytes != sizeof(u16))
    {
        tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for name_length failed with esfs result = 0x%x and num_bytes bytes = %zu",
            result, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // Write the name
    num_bytes = name_length;
    result = esfs_fwrite_and_calc_cmac(name, &num_bytes, file_handle);
    if(result != ESFS_SUCCESS || num_bytes != name_length)
    {
        tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for name failed with esfs result = 0x%x and num_bytes bytes = %zu",
            result, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // In case of an encrypted esfs, write the AES nonce
    if((esfs_mode & ESFS_ENCRYPTED) != 0)
    {
        num_bytes = ESFS_AES_NONCE_SIZE_BYTES;

        result = esfs_fwrite_and_calc_cmac((void *)(file_handle->nonce), &num_bytes, file_handle);

        if( (result != ESFS_SUCCESS) || (num_bytes != ESFS_AES_NONCE_SIZE_BYTES) )
        {
            tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for AES nonce failed with esfs result = 0x%x and num_bytes bytes = %zu",
                result, num_bytes);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    // Write the Metadata header
    // Write the number of items of meta data
    u16 = (uint16_t)meta_data_qty;
    num_bytes = sizeof(u16),
    result = esfs_fwrite_and_calc_cmac(&u16, &num_bytes, file_handle);
    if(result != ESFS_SUCCESS || num_bytes != sizeof(u16))
    {
        tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for number of items of meta data failed with esfs result = 0x%x and num_bytes bytes = %zu",
            result, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    if(meta_data_qty != 0)
    {
        res = pal_fsFtell(&file_handle->file, &position);
        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_create_internal() - pal_fsFtell() failed with pal_status = 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }
        position += (sizeof(file_handle->tlv_properties.tlv_items[0]) * meta_data_qty);
        for(i = 0; i < meta_data_qty; i++ )
        {
            file_handle->tlv_properties.tlv_items[i].type = meta_data[i].type;
            file_handle->tlv_properties.tlv_items[i].length_in_bytes = meta_data[i].length_in_bytes;
            file_handle->tlv_properties.tlv_items[i].position = (uint16_t)position;
            // Increment position for next iteration
            position += meta_data[i].length_in_bytes;
        }

        // Write the metadata items
        num_bytes = sizeof(file_handle->tlv_properties.tlv_items[0])*meta_data_qty;
        result = esfs_fwrite_and_calc_cmac(&file_handle->tlv_properties.tlv_items[0], &num_bytes, file_handle);
        if(result != ESFS_SUCCESS || num_bytes != sizeof(file_handle->tlv_properties.tlv_items[0])*meta_data_qty)
        {
            tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for meta data items failed with esfs result = 0x%x and num_bytes bytes = %zu",
                result, num_bytes);
            result = ESFS_ERROR;
            goto errorExit;
        }

        // Set the number_of_items field here since it is in use later in this function
        // when we calculate the file header size
        file_handle->tlv_properties.number_of_items = meta_data_qty;

        // Write the Metadata data values
        // If encrypted esfs is requested (by the esfs_mode argument), then this part should be encrypted
        for(i = 0; i < meta_data_qty; i++ )
        {
            num_bytes = meta_data[i].length_in_bytes;

            if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
            {
                result = esfs_encrypt_fwrite_and_calc_cmac(meta_data[i].value, &num_bytes, file_handle);
            }
            else
            {
                result = esfs_fwrite_and_calc_cmac(meta_data[i].value, &num_bytes, file_handle);
            }

            if(result != ESFS_SUCCESS || num_bytes != meta_data[i].length_in_bytes)
            {
                tr_err("esfs_create_internal() - esfs_fwrite_and_calc_cmac() for meta data item values failed with esfs result = 0x%x and num_bytes bytes = %zu",
                    result, num_bytes);
                result = ESFS_ERROR;
                goto errorExit;
            }
        }
    }


    file_handle->file_flag = ESFS_WRITE;

    return ESFS_SUCCESS;

errorExit:

    if(file_created)
    {
        pal_fsFclose(&file_handle->file);
        // Clean up if possible. Ignore return value.
        (void)pal_fsUnlink(full_path_to_create);
    }

    return result;
}


//      ---------------------------------------------------------------
//                              API Functions
//      ---------------------------------------------------------------


esfs_result_e esfs_create(const uint8_t *name, size_t name_length, const esfs_tlv_item_t *meta_data, size_t meta_data_qty, uint16_t esfs_mode, esfs_file_t *file_handle)
{
    char file_full_path[MAX_FULL_PATH_SIZE] = { 0 };

    palStatus_t   res    = PAL_SUCCESS;
    esfs_result_e result = ESFS_ERROR;

    bool is_aes_ctx_created = false;

    uint8_t aes_key[ESFS_AES_KEY_SIZE_BYTES] = {0}; // For AES encryption


    // Verify that the structure is always packed to six bytes, since we read and write it as a whole.
    PAL_ASSERT_STATIC(sizeof(esfs_tlvItem_t) == 6);

    // Verify that the array is always packed without padding, since we read and write it as a whole.
    PAL_ASSERT_STATIC(sizeof(esfs_tlvItem_t[ESFS_MAX_TYPE_LENGTH_VALUES]) == ESFS_MAX_TYPE_LENGTH_VALUES * sizeof(esfs_tlvItem_t));

    tr_info("esfs_create - enter");


    // Check parameters
    if (!file_handle || !name || name_length == 0 || name_length > ESFS_MAX_NAME_LENGTH || meta_data_qty > ESFS_MAX_TYPE_LENGTH_VALUES)
    {
        tr_err("esfs_create() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    for(size_t meta_data_index = 0; meta_data_index < meta_data_qty; meta_data_index++ )
    {
        if ((!meta_data[meta_data_index].value) || (meta_data[meta_data_index].length_in_bytes == 0))
        {
            tr_err("esfs_create() failed with bad parameters for metadata");
            result = ESFS_INVALID_PARAMETER;
            goto errorExit;
        }

    }

    res = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, file_full_path);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_create() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(file_full_path, "/" ESFS_WORKING_DIRECTORY "/", sizeof(ESFS_WORKING_DIRECTORY) + 1);

    // If esef is in encryption mode, make the required initializations
    if((esfs_mode & ESFS_ENCRYPTED) != 0)
    {
        // ** Create AES context for AES encryption
        res = pal_initAes( &(file_handle->aes_ctx) );

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_initAes() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }

        is_aes_ctx_created = true;

        // ** Get AES key from PAL
        // Note: On each call, PAL should return the same 128 bits key
        res = pal_osGetDeviceKey128Bit(palOsStorageEncryptionKey, aes_key, ESFS_AES_KEY_SIZE_BYTES);

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_osGetDeviceKey128Bit() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }

        // ** Assign generated AES key to AES context
        res = pal_setAesKey( file_handle->aes_ctx,
                             aes_key,
                             ESFS_AES_KEY_SIZE_BITS,
                             PAL_KEY_TARGET_ENCRYPTION
                           );

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_setAesKey() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }

        // ** Generate the AES nonce for AES usage
        res = pal_osRandomBuffer(file_handle->nonce, ESFS_AES_NONCE_SIZE_BYTES);

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_osRandomBuffer() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }
    }


    // We set the blob_name_length filed here because it is in use later in this function when we calculate the file header size.
    // Since this field is also used to check the file handle validity [ esfs_validate() ] we set it to zero on an error exit.
    file_handle->blob_name_length = name_length;

    file_handle->esfs_mode = esfs_mode;

    file_handle->file_invalid = 0;

    file_handle->tlv_properties.number_of_items = 0;

    if (esfs_get_name_from_blob(name, name_length, file_handle->short_file_name, ESFS_FILE_NAME_LENGTH) != ESFS_SUCCESS)
    {
        tr_err("esfs_create() - esfs_get_name_from_blob() failed");
        goto errorExit;
    }
    strncat(file_full_path, file_handle->short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

    // Check if the file exists in esfs working directory
    res = pal_fsFopen(file_full_path, PAL_FS_FLAG_READWRITEEXCLUSIVE, &file_handle->file);
    if (res != PAL_SUCCESS)
    {
        if (res == PAL_ERR_FS_NAME_ALREADY_EXIST)
        {
            result = ESFS_EXISTS;
            // Check if there is a different name in the file
            // Check that the name written inside the file is the same as that given. If not
            // you should choose a different name.
            res = pal_fsFopen(file_full_path, PAL_FS_FLAG_READONLY, &file_handle->file);
            if (res == PAL_SUCCESS)
            {
                file_handle->esfs_mode = 0;
                // result can be ESFS_HASH_CONFLICT or ESFS_WRONG_FILE_VERSION
                esfs_result_e check_result = esfs_check_file_validity(name, name_length, file_handle);
                if (check_result == ESFS_HASH_CONFLICT || check_result == ESFS_INVALID_FILE_VERSION)
                {
                    result = check_result;
                }
                pal_fsFclose(&file_handle->file);
            }
        }
        // more informative message will be written after hash conflict will be implemented
        tr_err("esfs_create() - pal_fsFopen() for working dir file failed");
        goto errorExit;
    }

    // close the file - it was opened in order to verify whether the file exists or not
    res = pal_fsFclose(&file_handle->file);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_create() - pal_fsFclose() for working dir file failed with pal status 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        // Clean up if possible. Ignore return value.
        (void)pal_fsUnlink(file_full_path);
        goto errorExit;
    }
    res = pal_fsUnlink(file_full_path);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_create() - pal_fsUnlink() for working dir file failed with pal status 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // factory reset file
    if (esfs_mode & (uint16_t)ESFS_FACTORY_VAL)
    {

        res = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, file_full_path);
        if (res != PAL_SUCCESS)
        {

            tr_err("esfs_create() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }

        strncat(file_full_path, "/" ESFS_BACKUP_DIRECTORY, sizeof(ESFS_BACKUP_DIRECTORY));

        // Create the esfs subfolder for backup
        res = pal_fsMkDir(file_full_path);
        if (res != PAL_SUCCESS)
        {
            // Any error apart from file exist returns error.
            if (res != PAL_ERR_FS_NAME_ALREADY_EXIST)
            {
                tr_err("esfs_create() - pal_fsMkDir() for backup dir failed with pal status 0x%x", (unsigned int)res);
                goto errorExit;
            }
        }
        strncat(file_full_path, "/", 1);
        strncat(file_full_path, file_handle->short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);
        // Check if the file exists in esfs backup directory
        res = pal_fsFopen(file_full_path, PAL_FS_FLAG_READWRITEEXCLUSIVE, &file_handle->file);
        
        // * If res == PAL_SUCCESS: File does not exist and we will create it
        // * If res != PAL_SUCCESS && res == PAL_ERR_FS_NAME_ALREADY_EXIST: Update factory reset file
        // * If res != PAL_SUCCESS && res != PAL_ERR_FS_NAME_ALREADY_EXIST: Error is returned
        if (res != PAL_SUCCESS)
        {
            if (res == PAL_ERR_FS_NAME_ALREADY_EXIST)
            {
                // Check if there is a different name in the file
                // Check that the name written inside the file is the same as that given. If not
                // you should choose a different name.
                res = pal_fsFopen(file_full_path, PAL_FS_FLAG_READONLY, &file_handle->file);
                if (res == PAL_SUCCESS)
                {
                    file_handle->esfs_mode = 0;
                    // result can be ESFS_HASH_CONFLICT or ESFS_WRONG_FILE_VERSION
                    esfs_result_e check_result = esfs_check_file_validity(name, name_length, file_handle);

                    if (check_result == ESFS_HASH_CONFLICT || check_result == ESFS_INVALID_FILE_VERSION)
                    {
                        tr_err("esfs_create() - esfs_check_file_validity() failed with status 0x%x", check_result);
                        result = check_result;
                        goto errorExit;
                    }
                    //TODO - here should add hash conflict functionality
                    // if we reach this point - update factory reset file
                    // we need to close the file and delete it, follow by creating it again
                    // creation will be done in esfs_create_internal function
                }
                else
                {
                    // more informative message will be written after hash conflict will be implemented
                    tr_err("esfs_create() - pal_fsFopen() failed");
                    result = ESFS_ERROR;
                    goto errorExit;
                }
            }
            else   
            {
                // more informative message will be written after hash conflict will be implemented
                tr_err("esfs_create() - pal_fsFopen() for backup dir file failed");
                result = ESFS_ERROR;
                goto errorExit;
            }
        }

        /*closing and deleting the factory reset file that was created*/
        res = pal_fsFclose(&file_handle->file);
        if (res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_fsFclose() for backup dir file failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            // Clean up if possible. Ignore return value.
            (void)pal_fsUnlink(file_full_path);
            goto errorExit;
        }
        // Delete backup file
        res = pal_fsUnlink(file_full_path);
        if (res != PAL_SUCCESS)
        {
            tr_err("esfs_create() - pal_fsUnlink() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }


    // file_full_path contains the correct location (working/backup)
    result = esfs_create_internal(name, name_length, meta_data, meta_data_qty, esfs_mode, file_handle, file_full_path);

    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_create() - esfs_create_internal() failed with result 0x%x", result);
        goto errorExit;
    }

    return ESFS_SUCCESS;

errorExit:

    // Invalidate blob_name_length filed since it is used to check the file handle validity  [ esfs_validate() ]
    if(file_handle != NULL)
    {
        file_handle->blob_name_length = 0;
    }

    if(is_aes_ctx_created)
    {
        pal_freeAes( &(file_handle->aes_ctx) );
    }
    return result;
}


esfs_result_e esfs_open(const uint8_t *name, size_t name_length, uint16_t *esfs_mode, esfs_file_t *file_handle)
{
    char working_dir_path[MAX_FULL_PATH_SIZE] = { 0 };
    esfs_result_e result = ESFS_ERROR;
    palStatus_t res = PAL_SUCCESS;
    size_t num_bytes = 0;
    uint16_t file_opened = 0;

    bool is_aes_ctx_created = false;

    uint8_t aes_key[ESFS_AES_KEY_SIZE_BYTES] = {0}; // For AES decryption

    uint16_t meta_data_qty;
    int32_t file_size;

    tr_info("esfs_open - enter");
    // Check parameters
    if(!file_handle || !name || name_length == 0 || name_length > ESFS_MAX_NAME_LENGTH)
    {
        tr_err("esfs_open() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    res = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, working_dir_path);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_open() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)res);
        return ESFS_ERROR;
    }

    strncat(working_dir_path, "/" ESFS_WORKING_DIRECTORY "/", sizeof(ESFS_WORKING_DIRECTORY) + 1);


    // This is used to esfs_validate the file handle so we set it to zero here and only when open
    // succeeds to the real value.
    file_handle->blob_name_length = 0;

    file_handle->file_invalid = 0;

    if(esfs_get_name_from_blob(name, name_length, file_handle->short_file_name, ESFS_FILE_NAME_LENGTH) != ESFS_SUCCESS)
    {
        tr_err("esfs_open() - esfs_get_name_from_blob() failed");
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(working_dir_path, file_handle->short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

   // Open the file read only
    res = pal_fsFopen(working_dir_path, PAL_FS_FLAG_READONLY, &file_handle->file);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_open() - pal_fsFopen() for working dir file failed with pal_status = 0x%x", (unsigned int)res);
        // File cannot be opened so return an error
        result = ESFS_NOT_EXISTS;
        goto errorExit;
    }

     file_opened = 1;

    // Check that the name written inside the file is the same as that given
    // Note: After this call, the read position will be set to the point after the "Name Blob"
    result = esfs_check_file_validity(name, name_length, file_handle);
    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_open() - esfs_check_file_validity() failed with status = 0x%x", result);
        // File cannot be opened so return an error
        goto errorExit;
    }

    // Check the signature if required
    result = esfs_check_cmac(file_handle);
    if(result != ESFS_SUCCESS)
    {
        tr_err("esfs_open() - esfs_check_cmac() (signature) failed with status = 0x%x", result);
        goto errorExit;
    }

    if (esfs_mode)
    {
        *esfs_mode = file_handle->esfs_mode;    // file_handle->esfs_mode was set by esfs_check_file_validity()
    }

    // If esfs is in encryption mode, make the required initializations
    if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
    {
        // ** Create AES context for AES decryption
        res = pal_initAes( &(file_handle->aes_ctx) );

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_open() - pal_initAes() failed with status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }

        is_aes_ctx_created = true;

        // ** Get AES key from PAL
        // Note: On each call, PAL should return the same 128 bits key
        res = pal_osGetDeviceKey128Bit(palOsStorageEncryptionKey, aes_key, ESFS_AES_KEY_SIZE_BYTES);

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_open() - pal_osGetDeviceKey128Bit() failed with status 0x%x", (unsigned int)res);
            result = ESFS_ERROR ;
            goto errorExit;
        }

        // ** Assign generated AES key to AES context
        res = pal_setAesKey( file_handle->aes_ctx,
                             aes_key,
                             ESFS_AES_KEY_SIZE_BITS,
                             PAL_KEY_TARGET_ENCRYPTION
                           );

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_open() - pal_setAesKey() failed with status 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }

        // ** Read the AES nonce into file_handle->nonce
        num_bytes = 0;

        res = pal_fsFread(&(file_handle->file), file_handle->nonce, ESFS_AES_NONCE_SIZE_BYTES, &num_bytes);

        if((res != PAL_SUCCESS) || (num_bytes != ESFS_AES_NONCE_SIZE_BYTES))
        {
            tr_err("esfs_open() - pal_fsFread() (AES nonce) failed with pal result = 0x%x and num_bytes bytes = %zu",
                (unsigned int)res, num_bytes);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    file_handle->tlv_properties.number_of_items = 0;

    // Read the number of items of meta data
    num_bytes = 0;
    res = pal_fsFread(&file_handle->file, (void *)( &meta_data_qty ), sizeof(meta_data_qty), &num_bytes);
    if(res != PAL_SUCCESS || num_bytes != sizeof(meta_data_qty))
    {
        tr_err("esfs_open() - pal_fsFread() (number of items of meta data) failed with pal result = 0x%x and num_bytes bytes = %zu",
            (unsigned int)res, num_bytes);
        result = ESFS_ERROR;
        goto errorExit;
    }

    // Read the metadata properties if there are any
    if(meta_data_qty != 0)
    {
        num_bytes = 0;
        res = pal_fsFread( &file_handle->file,
                           (void *) ( &(file_handle->tlv_properties.tlv_items[0]) ),
                           (sizeof(file_handle->tlv_properties.tlv_items[0]) * meta_data_qty),
                           &num_bytes
                         );

        if(res != PAL_SUCCESS || num_bytes != sizeof(file_handle->tlv_properties.tlv_items[0])*meta_data_qty)
        {
            tr_err("esfs_open() - pal_fsFread() (metadata properties) failed with pal result = 0x%x and num_bytes bytes = %zu",
                (unsigned int)res, num_bytes);
            result = ESFS_ERROR;
            goto errorExit;
        }

        // Skip to the start of the data by calculating the last metadata position plus its length
        esfs_tlvItem_t *ptypeLengthValueItem = &file_handle->tlv_properties.tlv_items[meta_data_qty - 1];

        res = pal_fsFseek(&file_handle->file,
                           ptypeLengthValueItem->position + ptypeLengthValueItem->length_in_bytes,
                           PAL_FS_OFFSET_SEEKSET
                         );

        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_open() - pal_fsFseek() failed with pal status 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    file_handle->tlv_properties.number_of_items = meta_data_qty;

    // We are at the start of the data section
    file_handle->current_read_pos = 0;

    // Calculate the size of the data only by getting the file size and deducting the header and cmac
    res = esfs_get_physical_file_size(&file_handle->file, &file_size);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_open() - esfs_get_physical_file_size() failed with status 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }

    file_handle->data_size =  file_size - esfs_file_header_size(file_handle);

    // We deduct the cmac bytes at the end of the file since they are not part of the data
    file_handle->data_size -= ESFS_CMAC_SIZE_IN_BYTES;

    file_handle->file_flag = ESFS_READ;
    file_handle->blob_name_length = name_length;

    return ESFS_SUCCESS;

errorExit:
    if(file_opened)
    {
        pal_fsFclose(&file_handle->file);
    }

    if(is_aes_ctx_created)
    {
        pal_freeAes( &(file_handle->aes_ctx) );
    }

    return result;
}

esfs_result_e esfs_write(esfs_file_t *file_handle, const void *buffer, size_t bytes_to_write)
{
    esfs_result_e result = ESFS_ERROR;

    size_t num_bytes;
    tr_info("esfs_write - enter");
    if((esfs_validate(file_handle) != ESFS_SUCCESS) || (!buffer) || (bytes_to_write == 0))
    {
        tr_err("esfs_write() failed with bad parameters");
        return ESFS_INVALID_PARAMETER;
    }

    if(file_handle->file_flag == ESFS_READ)
    {
        tr_err("esfs_write() write failed - file is opened for read only");
        result = ESFS_FILE_OPEN_FOR_READ;
        goto errorExit;
    }
    else
    {
        // Write data
        // If encrypted esfs is requested (file_handle->esfs_mode), then this part should be encrypted
        num_bytes = bytes_to_write;

        // The data should be encrypted if the encrypted esfs is requested by the esfs_mode argument
        if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
        {
            result = esfs_encrypt_fwrite_and_calc_cmac(buffer, &num_bytes, file_handle);
        }
        else
        {
            result = esfs_fwrite_and_calc_cmac(buffer, &num_bytes, file_handle);
        }

        if(result != ESFS_SUCCESS || num_bytes != bytes_to_write)
        {
            tr_err("esfs_write() - esfs_fwrite_and_calc_cmac()/esfs_encrypt_fwrite_and_calc_cmac() for data failed with esfs result = 0x%x and num_bytes bytes = %zu",
                result, num_bytes);
            // Since the write failed, we cannot be sure of the state of the file, so we mark it as invalid.
            file_handle->file_invalid = 1;
            result = ESFS_ERROR;
            goto errorExit;
        }
    }

    return ESFS_SUCCESS;

errorExit:
    return result;
}

esfs_result_e esfs_read(esfs_file_t *file_handle, void *buffer, size_t bytes_to_read, size_t *read_bytes)
{
    size_t num_bytes = 0;
    esfs_result_e result = ESFS_ERROR;
    tr_info("esfs_read - enter");
    if(esfs_validate(file_handle) != ESFS_SUCCESS || read_bytes == NULL || !buffer)
    {
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    if(file_handle->file_flag != ESFS_READ)
    {
        result = ESFS_FILE_OPEN_FOR_WRITE;
        goto errorExit;
    }
    else
    {
        // Limit how many bytes we can actually read depending on the size of the data section.
        size_t remaining_bytes = file_handle->data_size - file_handle->current_read_pos;
        bytes_to_read = PAL_MIN(remaining_bytes, bytes_to_read);

        // Read data
        // If required according to esfs_mode, the read data will be decrypted
        if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
        {
            if(esfs_read_and_decrypt(file_handle, buffer, bytes_to_read, &num_bytes) != ESFS_SUCCESS)
            {
                goto errorExit;
            }
        }
        else
        {
            if(pal_fsFread( &(file_handle->file), buffer, bytes_to_read, &num_bytes ) != PAL_SUCCESS)
            {
                goto errorExit;
            }
        }

        *read_bytes = num_bytes;

        // Update the current position
        file_handle->current_read_pos += num_bytes;
    }

    return ESFS_SUCCESS;

errorExit:
    tr_err("esfs_read errorExit result=0x%x", result);
    return result;
}

esfs_result_e esfs_seek(esfs_file_t *file_handle, int32_t offset, esfs_seek_origin_e whence, uint32_t *position)
{
    palStatus_t res;
    esfs_result_e result = ESFS_ERROR;
    tr_info("esfs_seek - enter");
    if(esfs_validate(file_handle) != ESFS_SUCCESS)
    {
        tr_err("esfs_seek() failed with bad parameters");
        return ESFS_INVALID_PARAMETER;
    }

    if(file_handle->file_flag != ESFS_READ)
    {
        tr_err("esfs_seek() seek failed - file is opened for write only");
        result = ESFS_FILE_OPEN_FOR_WRITE;
        goto errorExit;
    }
    else
    {
        pal_fsOffset_t pal_whence;
        // ESFS whence enum values are in sync with those of pal
        if(whence == ESFS_SEEK_SET)
        {
            if(offset > (int32_t)file_handle->data_size || offset < 0)
            {
                tr_err("esfs_seek() failed with bad parameters in offset calculation : ESFS_SEEK_SET");
                result = ESFS_INVALID_PARAMETER;
                goto errorExit;
            }
            // Add the offset to the start of the data
            offset += esfs_file_header_size(file_handle);
            pal_whence = PAL_FS_OFFSET_SEEKSET;
        }
        else if(whence == ESFS_SEEK_END)
        {
            if(offset < -(int32_t)file_handle->data_size || offset > 0)
            {
                tr_err("esfs_seek() failed with bad parameters in offset calculation : ESFS_SEEK_END");
                result = ESFS_INVALID_PARAMETER;
                goto errorExit;
            }
            // Deduct the cmac size from the offset because it is located after the data section.
            offset -= ESFS_CMAC_SIZE_IN_BYTES;
            pal_whence = PAL_FS_OFFSET_SEEKEND;
        }
        else if(whence == ESFS_SEEK_CUR)
        {
            if(offset + file_handle->current_read_pos > (int32_t)file_handle->data_size || offset + (int32_t)file_handle->current_read_pos < 0)
            {
                tr_err("esfs_seek() failed with bad parameters in offset calculation : ESFS_SEEK_CUR");
                result = ESFS_INVALID_PARAMETER;
                goto errorExit;
            }
            pal_whence = PAL_FS_OFFSET_SEEKCUR;
        }
        else
        {
            tr_err("esfs_seek() failed with bad parameters - wrong whence");
            result = ESFS_INVALID_PARAMETER;
            goto errorExit;
        }
        res = pal_fsFseek(&file_handle->file, offset, pal_whence);
        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_seek() - pal_fsFseek() failed with pal status 0x%x", (unsigned int)res);
            goto errorExit;
        }
    }
    // Get current position if position is not NULL
    if(position)
    {
        res = pal_fsFtell(&file_handle->file, (int32_t *)position);
        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_seek() - pal_fsFtell() failed with pal status 0x%x", (unsigned int)res);
            goto errorExit;
        }

        // Ignore the file header data
        *position -= esfs_file_header_size(file_handle);

        // Update the current position
        file_handle->current_read_pos = *position;
    }

    return ESFS_SUCCESS;

errorExit:
    return result;
}


esfs_result_e esfs_file_size(esfs_file_t *file_handle, size_t *size_in_bytes)
{
    palStatus_t res = PAL_SUCCESS;
    int32_t file_size;
    esfs_result_e result = ESFS_ERROR;

    tr_info("esfs_file_size - enter");
    if((esfs_validate(file_handle) != ESFS_SUCCESS) || (!size_in_bytes))
    {
        tr_err("esfs_file_size() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    res = esfs_get_physical_file_size(&file_handle->file, &file_size);
    if (res != PAL_SUCCESS) {
        tr_err("esfs_file_size() - esfs_get_physical_file_size() failed with status 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Deduct header size
    *size_in_bytes = file_size - esfs_file_header_size(file_handle);

    // Deduct signature size (128 bits) only if it has been written already. Since it is written on
    // esfs_close it will only be there for files that have been opened with esfs_open.
    if(file_handle->file_flag == ESFS_READ)
    {
        *size_in_bytes -= ESFS_CMAC_SIZE_IN_BYTES;
    }

    return ESFS_SUCCESS;

errorExit:
    return result;
}

esfs_result_e esfs_close(esfs_file_t *file_handle)
{
    char full_path_working_dir[MAX_FULL_PATH_SIZE] = { 0 };
    size_t len;
    size_t bytes_written;
    unsigned char cmac[ESFS_CMAC_SIZE_IN_BYTES];
    palStatus_t res;
    uint16_t failed_to_write_CMAC = 0;
    uint16_t file_esfs_mode = 0;
    esfs_file_flag_e esfs_file_flag = 0;
    char esfs_short_file_name[ESFS_QUALIFIED_FILE_NAME_LENGTH] = {0};
    esfs_result_e result = ESFS_ERROR;

    tr_info("esfs_close - enter");
    if(esfs_validate(file_handle) != ESFS_SUCCESS)
    {
        tr_err("esfs_close() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    res = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, full_path_working_dir);
    if (res != PAL_SUCCESS)
    {
        tr_err("esfs_close() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)res);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(full_path_working_dir, "/" ESFS_WORKING_DIRECTORY "/", sizeof(ESFS_WORKING_DIRECTORY) + 1);


    // Close AES context if needed
    if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
    {
        pal_freeAes( &(file_handle->aes_ctx) );
    }
 
    esfs_file_flag = file_handle->file_flag;
    file_esfs_mode = file_handle->esfs_mode;
    strncpy(esfs_short_file_name, file_handle->short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

    if(file_handle->file_flag == ESFS_WRITE)
    {
        // Finish signature calculation
        res = pal_CMACFinish(&file_handle->signature_ctx, &cmac[0], &len);
        tr_info("esfs_close len=%d", (int)len);
        if(res != PAL_SUCCESS)
        {
            tr_err("esfs_close() - pal_CMACFinish() failed with pal status 0x%x", (unsigned int)res);
            goto errorExit;
        }
        // Write signature
        res = pal_fsFwrite(&file_handle->file, cmac, len, &bytes_written);
        if(res != PAL_SUCCESS || len != bytes_written)
        {
            tr_err("esfs_close() - pal_fsFwrite() (signature) failed with pal result = 0x%x and bytes_written bytes = %zu",
                (unsigned int)res, bytes_written);
            // mark the file invalid on a failed write
            file_handle->file_invalid = 1;
            // Continue so that we delete the file, but we should return failure later
            failed_to_write_CMAC = 1;
        }
    }

    res = pal_fsFclose(&file_handle->file);
    if(res == PAL_SUCCESS)
    {
        // Remove a file that is invalid. It may have become invalid due to a failed write.
        if(file_handle->file_invalid)
        {
            strncat(full_path_working_dir,file_handle->short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

            res = pal_fsUnlink(full_path_working_dir);
            if(res != PAL_SUCCESS)
            {
                tr_err("esfs_close() - pal_fsUnlink() failed with pal status 0x%x", (unsigned int)res);
                goto errorExit;
            }
        }
    }
    else
    {
        tr_err("esfs_close() - pal_fsFclose() failed with pal status 0x%x", (unsigned int)res);
        goto errorExit;
    }

    if(failed_to_write_CMAC)
    {
        goto errorExit;
    }


    if ((file_esfs_mode & ESFS_FACTORY_VAL) && (esfs_file_flag == ESFS_WRITE) && !(file_handle->file_invalid))
    {
        char full_path_backup_dir[MAX_FULL_PATH_SIZE] = { 0 };

        res = pal_fsGetMountPoint(PAL_FS_PARTITION_SECONDARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, full_path_backup_dir);
        if (res != PAL_SUCCESS)
        {
            tr_err("esfs_close() - pal_fsGetMountPoint() for backup directory failed with pal_status = 0x%x", (unsigned int)res);
            result = ESFS_ERROR;
            goto errorExit;
        }

        strncat(full_path_backup_dir, "/" ESFS_BACKUP_DIRECTORY "/", sizeof(ESFS_BACKUP_DIRECTORY) + 1);

        strncat(full_path_working_dir, esfs_short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH -1);
        strncat(full_path_backup_dir, esfs_short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

        if (esfs_copy_file(full_path_backup_dir, full_path_working_dir) != ESFS_SUCCESS)
        {
            tr_err("esfs_close() - esfs_copy_file() failed");
            goto errorExit;
        }
    }

    return ESFS_SUCCESS;
errorExit:
    return result;
}

esfs_result_e esfs_delete(const uint8_t *name, size_t name_length)
{

    palStatus_t pal_result = PAL_SUCCESS;
    char working_dir_path[MAX_FULL_PATH_SIZE] = { 0 };
    char short_file_name[ESFS_QUALIFIED_FILE_NAME_LENGTH];
    esfs_result_e result = ESFS_ERROR;

    tr_info("esfs_delete - enter");
    // Check parameters
    if(!name || name_length == 0)
    {
        tr_err("esfs_delete() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }
    if(esfs_get_name_from_blob(name, name_length, short_file_name, ESFS_FILE_NAME_LENGTH ) != ESFS_SUCCESS)
    {
        tr_err("esfs_delete() - esfs_get_name_from_blob() failed");
        goto errorExit;
    }
    tr_info("esfs_delete %s", short_file_name);

    pal_result = pal_fsGetMountPoint(PAL_FS_PARTITION_PRIMARY, PAL_MAX_FOLDER_DEPTH_CHAR + 1, working_dir_path);
    if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_delete() - pal_fsGetMountPoint() for working directory failed with pal_status = 0x%x", (unsigned int)pal_result);
        result = ESFS_ERROR;
        goto errorExit;
    }

    strncat(working_dir_path, "/" ESFS_WORKING_DIRECTORY "/", sizeof(ESFS_WORKING_DIRECTORY) + 1);

    // We do not verify that name is the actual name in the file because currently we do not allow the situation of hash
    // clash to arise.

    strncat(working_dir_path,short_file_name, ESFS_QUALIFIED_FILE_NAME_LENGTH - 1);

    tr_info("esfs_delete %s", working_dir_path);
    pal_result = pal_fsUnlink(working_dir_path);

    if ((pal_result == PAL_ERR_FS_NO_FILE) || (pal_result == PAL_ERR_FS_NO_PATH))
    {
        tr_err("esfs_delete() - pal_fsUnlink() failed with pal status 0x%x", (unsigned int)pal_result);
        result = ESFS_NOT_EXISTS;
        goto errorExit;
    }
    else if (pal_result != PAL_SUCCESS)
    {
        tr_err("esfs_delete() - pal_fsUnlink() failed with pal status 0x%x", (unsigned int)pal_result);
        goto errorExit;
    }

    return ESFS_SUCCESS;
errorExit:
    return result;
}

esfs_result_e esfs_get_meta_data_properties(esfs_file_t *file_handle, esfs_tlv_properties_t **meta_data_properties)
{
    esfs_result_e result = ESFS_ERROR;
    tr_info("esfs_get_meta_data_properties - enter");
    if((esfs_validate(file_handle) != ESFS_SUCCESS) || (!meta_data_properties))
    {
        tr_err("esfs_get_meta_data_properties() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    if (file_handle->file_flag != ESFS_READ)
    {
        tr_err("esfs_get_meta_data_properties() failed - file is opened for write only");
        result = ESFS_FILE_OPEN_FOR_WRITE;
        goto errorExit;
    }

    *meta_data_properties = &file_handle->tlv_properties;
    return ESFS_SUCCESS;
errorExit:
    return result;
}


esfs_result_e esfs_read_meta_data(esfs_file_t *file_handle, uint32_t index, esfs_tlv_item_t *meta_data)
{
    int32_t current_pos = 0;
    int32_t offset_to_restore;
    size_t num_bytes = 0;
    palStatus_t res;
    esfs_result_e result = ESFS_ERROR;
    bool is_read_error = false;

    tr_info("esfs_read_meta_data - enter");
    if(esfs_validate(file_handle) != ESFS_SUCCESS || index >= ESFS_MAX_TYPE_LENGTH_VALUES || !meta_data || (file_handle->tlv_properties.tlv_items[index].length_in_bytes == 0))
    {
        tr_err("esfs_read_meta_data() failed with bad parameters");
        result = ESFS_INVALID_PARAMETER;
        goto errorExit;
    }

    if(file_handle->file_flag != ESFS_READ)
    {
        tr_err("esfs_read_meta_data() failed - file is opened for write only");
        result = ESFS_FILE_OPEN_FOR_WRITE;
        goto errorExit;
    }
    // Get current position
    res = pal_fsFtell(&file_handle->file, &current_pos);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_read_meta_data() - pal_fsFtell() failed with pal status 0x%x", (unsigned int)res);
        goto errorExit;
    }


    // Jump to position of TLV
    res = pal_fsFseek(&file_handle->file, file_handle->tlv_properties.tlv_items[index].position, PAL_FS_OFFSET_SEEKSET);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_read_meta_data() - pal_fsFseek() failed with pal status 0x%x", (unsigned int)res);
        goto errorExit;
    }

    // Read data
    // If required according to esfs_mode, the read data will be decrypted
    if((file_handle->esfs_mode & ESFS_ENCRYPTED) != 0)
    {
        if(esfs_read_and_decrypt( file_handle,
                                  meta_data->value,
                                  file_handle->tlv_properties.tlv_items[index].length_in_bytes,
                                  &num_bytes
                                ) != ESFS_SUCCESS)
        {
            is_read_error = true;
        }
    }
    else
    {
        if(pal_fsFread( &(file_handle->file),
                        meta_data->value,
                        file_handle->tlv_properties.tlv_items[index].length_in_bytes,
                        &num_bytes
                      ) != PAL_SUCCESS)
        {
            is_read_error = true;
        }
    }

    if(is_read_error || (num_bytes != file_handle->tlv_properties.tlv_items[index].length_in_bytes))
    {
        tr_err("esfs_read_meta_data() - read data failed is_read_error = %s and num_bytes  = %zu",
            is_read_error ? "true" : "false", num_bytes);
        goto errorExit;
    }


    // Before restoring old position, make sure offset_to_restore is not a negative number
    offset_to_restore = current_pos;
    if(offset_to_restore < 0)
    {
        tr_err("esfs_read_meta_data() failed - current_pos is negative");
        goto errorExit;
    }

    // Restore old position
    res = pal_fsFseek(&file_handle->file, offset_to_restore, PAL_FS_OFFSET_SEEKSET);
    if(res != PAL_SUCCESS)
    {
        tr_err("esfs_read_meta_data() - pal_fsFseek() failed with pal status 0x%x", (unsigned int)res);
        goto errorExit;
    }
    // Update meta_data fields
    meta_data->type = file_handle->tlv_properties.tlv_items[index].type;
    meta_data->length_in_bytes = file_handle->tlv_properties.tlv_items[index].length_in_bytes;

    return ESFS_SUCCESS;

errorExit:
    return result;
}
