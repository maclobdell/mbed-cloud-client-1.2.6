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

#ifndef __FTCD_COMM_BASE_H__
#define __FTCD_COMM_BASE_H__

#include <stdint.h>

/**
* @file ftcd_comm_base.h
*
*  Token      [64bit]   : The message identifier.
*  Status     [32 bit]  : Status of message parameters (exists in response messages only)
*  Length     [32bit]   : The blob length in bytes.
*  Blob       [Length]  : A FT message to be processed by protocol handler.
*  Signature  [32B]     : The hash (SHA256) value of the Blob.
*/


/** Unique message identifier
*/
#define FTCD_MSG_HEADER_TOKEN { 0x6d, 0x62, 0x65, 0x64, 0x70, 0x72, 0x6f, 0x76 }
#define FTCD_MSG_HEADER_TOKEN_SIZE_BYTES 8




typedef enum ftcd_comm_status_ {
    FTCD_COMM_STATUS_SUCCESS,
    FTCD_COMM_STATUS_ERROR, //generic error
    FTCD_COMM_INVALID_TOKEN,
    FTCD_COMM_FAILED_TO_READ_MESSAGE_SIZE,
    FTCD_COMM_FAILED_TO_READ_MESSAGE_BYTES,
    FTCD_COMM_FAILED_TO_READ_MESSAGE_SIGNATURE,
    FTCD_COMM_FAILED_TO_CALCULATE_MESSAGE_SIGNATURE,
    FTCD_COMM_INCONSISTENT_MESSAGE_SIGNATURE,
    FTCD_COMM_FAILED_TO_PROCESS_DATA,
    FTCD_COMM_FAILED_TO_PROCESS_MESSAGE,
    FTCD_COMM_FAILED_TO_SEND_VALID_RESPONSE,

    FTCD_COMM_NETWORK_TIMEOUT,          //socket timeout error
    FTCD_COMM_NETWORK_CONNECTION_ERROR, //socket error
    FTCD_COMM_INTERNAL_ERROR,

    FTCD_COMM_STATUS_MAX_ERROR = 0xFFFFFFFF
} ftcd_comm_status_e;


/**
* \brief ::FtcdCommBase implements the logic of processing incoming requests from the remote Factory Tool Demo.
*/
class FtcdCommBase
{
public:

    /** Not certain that we need to do anything here, but just in case we need
     * to do some clean-up at some point.
     */
    virtual ~FtcdCommBase() = 0;

    /**
    * Initializes Network interface and opens socket
    * Prints IP address
    */
    virtual bool init(void);

    /**
    * Closes the opened socket
    */
    virtual void finish(void);


    /** Reads an inbound factory message from the communication line medium.
     * This function will generate an outbound message with a corresponding response.
     * - The inbound message will be ignored if not factory message compliant
     * - function may block until a valid message received
     *
     * @returns
     *     true upon success, false otherwise
     */
    bool process_message(void);

    /** Writes a response message to the communication line medium.
    *
    * @param response_message The message to send through the communication line medium
    * @param encoded_message_size The message size in bytes
    *
    * @returns
    *     true upon success, false otherwise
    */
    virtual bool send(const uint8_t *response_message, uint32_t response_message_size) = 0;

    /** Detects the message token from the communication line medium.
    *
    * @returns
    *     zero, if token detected and different value otherwise
    */
    virtual ftcd_comm_status_e is_token_detected(void) = 0;

    /** Reads the message size in bytes from the communication line medium.
    * This is the amount of bytes needed to allocate for the upcoming message bytes.
    *
    * @returns
    *     The message size in bytes in case of success, zero bytes otherwise.
    */
    virtual uint32_t read_message_size(void) = 0;

    /** Reads the message size in bytes from the communication line medium.
    * This is the amount of bytes needed to allocate for the upcoming message bytes.
    *
    * @param message_out The buffer to read into and return to the caller.
    * @param message_size The message size in bytes.
    *
    * @returns
    *     true upon success, false otherwise
    */
    virtual bool read_message(uint8_t *message_out, size_t message_size) = 0;

    /** Reads the message size in bytes from the communication line medium.
    * This is the amount of bytes needed to allocate for the upcoming message bytes.
    *
    * @param sig The buffer to read into and return to the caller.
    * @param sig_size The sig buffer size in bytes.
    *
    * @returns
    *     The message size in bytes in case of success, zero bytes otherwise.
    */
    virtual bool read_message_signature(uint8_t *sig, size_t sig_size) = 0;

private:

    /** Creates and sends a Factory Message response (in this format - [TOKEN | STATUS | LENGTH | FT-MESSAGE | SIGNATURE]).
    * This function gets a FT-MESSAGE, allocates the required amount of bytes and constructs the Factory
    * response message accordingly, it sends the message to the remote Factory tool via the given communication line medium.
    *
    * @param cbor_reponse The CBOR response message.
    * @param protocol_response_size The protocol response message size in bytes.
    * @param status_code The status of the response message
    *
    * @returns
    *     true upon success, false otherwise.
    */
    bool _create_and_send_response(const uint8_t *protocol_reponse, uint32_t protocol_response_size, ftcd_comm_status_e status_code);

};

#endif  // __FTCD_COMM_BASE_H__
