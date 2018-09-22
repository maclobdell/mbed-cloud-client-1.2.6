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

#include <stdlib.h>
#include <string.h>
#include "pv_endian.h"
#include "pv_log.h"
#include "ftcd_comm_base.h"
#include "fcc_bundle_handler.h"
#include "cs_hash.h"
#include "fcc_status.h"
#include "fcc_malloc.h"

#define TRACE_GROUP "fcbs"

FtcdCommBase::~FtcdCommBase()
{
}


bool FtcdCommBase::init()
{
    return true;
}

void FtcdCommBase::finish()
{
}


bool FtcdCommBase::process_message()
{
    bool success = false;
    ftcd_comm_status_e status_code;
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;
    uint8_t *response_protocol_message;
    size_t response_protocol_message_size;

    mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Factory flow begins...\r");

#ifdef TEST_SERIAL_MULTI_MESSAGE
    while (true) {
#endif
        response_protocol_message = NULL;
        response_protocol_message_size = 0;

        do {
            //detect token
            status_code = is_token_detected();
            if (status_code == FTCD_COMM_NETWORK_TIMEOUT) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Network timeout occurred\r");
                return false;
            } else if (status_code == FTCD_COMM_NETWORK_CONNECTION_ERROR) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Network connection error occurred\r");
                return false;
            }

            // Read message LENGTH
            uint32_t message_size = read_message_size();
            message_size = pv_le32_to_h(message_size);
            if (message_size == 0) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Unable to read message size (got ZERO)\r");
                status_code = FTCD_COMM_FAILED_TO_READ_MESSAGE_SIZE;
                break;
            }

            //read message
            uint8_t *message = (uint8_t *)fcc_malloc(message_size);
            success = read_message(message, message_size);
            if (!success) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed getting message bytes\r");
                status_code = FTCD_COMM_FAILED_TO_READ_MESSAGE_BYTES;
                fcc_free(message);
                break;
            }

            //read message signature
            uint8_t sig_from_message[CS_SHA256_SIZE];
            success = read_message_signature(sig_from_message, sizeof(sig_from_message));
            if (!success) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed getting signature bytes\r");
                status_code = FTCD_COMM_FAILED_TO_READ_MESSAGE_SIGNATURE;
                fcc_free(message);
                break;
            }

            //calculate message signature
            uint8_t self_calculated_sig[CS_SHA256_SIZE];
            kcm_status_e kcm_status = KCM_STATUS_SUCCESS;
            kcm_status = cs_hash(CS_SHA256,message, message_size, self_calculated_sig, sizeof(self_calculated_sig));
            if (kcm_status != KCM_STATUS_SUCCESS) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed calculating message signature\r");
                status_code = FTCD_COMM_FAILED_TO_CALCULATE_MESSAGE_SIGNATURE;
                fcc_free(message);
                break;
            }

            //compare signatures
            if (memcmp(self_calculated_sig, sig_from_message, CS_SHA256_SIZE) != 0) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Inconsistent message signature\r");
                status_code = FTCD_COMM_INCONSISTENT_MESSAGE_SIGNATURE;
                fcc_free(message);
                break;
            }

            // process request and get back response
            fcc_status = fcc_bundle_handler(message, message_size, &response_protocol_message, &response_protocol_message_size);
            if ((fcc_status == FCC_STATUS_BUNDLE_RESPONSE_ERROR) || (response_protocol_message == NULL) || (response_protocol_message_size == 0)) {
                status_code = FTCD_COMM_FAILED_TO_PROCESS_DATA;
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed to process data\r");
                fcc_free(message);
                break;
            }

            fcc_free(message);
            status_code = FTCD_COMM_STATUS_SUCCESS; //comm message status OK - passed all checks

            // Success
            mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Successfully processed comm message\r");

        } while (0);

        success = _create_and_send_response(response_protocol_message, response_protocol_message_size, status_code);
        if (!success) {
            mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed sending response message to remote host\r");
            status_code = FTCD_COMM_FAILED_TO_SEND_VALID_RESPONSE;
            success = _create_and_send_response(NULL, 0, status_code);
            if (!success) {
                mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed sending response message to remote host for second time!\r");
            }
        }

        fcc_free(response_protocol_message);

#ifdef TEST_SERIAL_MULTI_MESSAGE
    }
#endif

    if (status_code != FTCD_COMM_STATUS_SUCCESS) {
        return false;
    }
    return true;
}

bool FtcdCommBase::_create_and_send_response(const uint8_t *protocol_reponse, uint32_t protocol_response_size, ftcd_comm_status_e status_code)
{
    bool success = true;
    kcm_status_e kcm_status = KCM_STATUS_SUCCESS;

    // Construct response
    uint8_t token[] = FTCD_MSG_HEADER_TOKEN;

    status_code = static_cast<ftcd_comm_status_e>(pv_h_to_le32(static_cast<uint32_t>(status_code)));
    uint32_t response_size = 0;

    if (status_code == FTCD_COMM_STATUS_SUCCESS) {
        // Factory message format - [TOKEN | STATUS | LENGTH | FT-MESSAGE | SIGNATURE]
        response_size = sizeof(uint64_t) + sizeof(status_code) + sizeof(uint32_t) + protocol_response_size + CS_SHA256_SIZE;
    } else { //invalid comm mesage
        // Factory message format - [TOKEN | STATUS ]
        response_size = sizeof(uint64_t) + sizeof(status_code);
    }

    uint8_t *response = (uint8_t *)fcc_malloc(response_size);

    uint32_t offset = 0;

    // TOKEN
    memcpy(response, &token, FTCD_MSG_HEADER_TOKEN_SIZE_BYTES);
    offset = FTCD_MSG_HEADER_TOKEN_SIZE_BYTES;

    //STATUS
    memcpy(response + offset, &status_code, sizeof(status_code));
    offset += sizeof(status_code);

    if (status_code == FTCD_COMM_STATUS_SUCCESS) {

        // Params check
        if (protocol_reponse == NULL) {
            mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Invalid cbor_reponse\r");
        }
        if (protocol_response_size == 0) {
            mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Got an empty CBOR response\r");
        }

        // Calculate FT message signature
        uint8_t sig[CS_SHA256_SIZE];

        uint32_t cbor_msg_size_le = pv_h_to_le32(protocol_response_size);

        kcm_status = cs_hash(CS_SHA256, protocol_reponse, protocol_response_size, sig, sizeof(sig));
        if (kcm_status != KCM_STATUS_SUCCESS) {
            mbed_tracef(TRACE_LEVEL_CMD, TRACE_GROUP,"Failed calculating response message signature\r");
            //construct error packet and send it
            fcc_free(response);
            return false;
        }

        // LENGTH
        memcpy(response + offset, &cbor_msg_size_le, sizeof(uint32_t));
        offset += sizeof(uint32_t);

        // FT-MESSAGE
        memcpy(response + offset, protocol_reponse, protocol_response_size);
        offset += protocol_response_size;

        // SIGNATURE
        memcpy(response + offset, sig, sizeof(sig));
    }

    // Send the response...
    success = send(response, response_size);

    fcc_free(response);

    return success;
}


