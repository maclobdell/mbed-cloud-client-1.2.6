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

#include "unity.h"
#include "unity_fixture.h"
#include "pal.h"
#include "pal_tls_utils.h"
//#include "pal_socket_test_utils.h"
#include "PlatIncludes.h"
#include "pal_network.h"
#include "stdlib.h"


PAL_PRIVATE palSocket_t g_socket = 0;
PAL_PRIVATE void * g_interfaceCTX = NULL;
PAL_PRIVATE uint32_t g_interfaceCTXIndex = 0;

TEST_GROUP(pal_tls);

TEST_SETUP(pal_tls)
{
    palStatus_t status = PAL_SUCCESS;
#ifdef PAL_CERT_TIME_VERIFY
    uint64_t currentTime = 1491151775; // 02/04/2017
#endif //PAL_CERT_TIME_VERIFY
    pal_init();

    if (g_interfaceCTX == NULL)
    {
        g_interfaceCTX = palTestGetNetWorkInterfaceContext();
        status = pal_registerNetworkInterface(g_interfaceCTX , &g_interfaceCTXIndex);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }

    g_socket = 0;

#ifdef PAL_CERT_TIME_VERIFY
    status = pal_osSetTime(currentTime);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
#endif //PAL_CERT_TIME_VERIFY
}

TEST_TEAR_DOWN(pal_tls)
{
    if (0 != g_socket)
    {
        pal_close(&g_socket);
    }

    pal_destroy();
}

/**
* @brief Test TLS cofiguration initialization and uninitialization.
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Initialize TLS configuration using `pal_initTLSConfiguration`.       | PAL_SUCCESS |
* | 2 | Uninitialize TLS configuration using `pal_tlsConfigurationFree`.     | PAL_SUCCESS |
*/
TEST(pal_tls, tlsConfiguration)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSTransportMode_t transportationMode =     PAL_TLS_MODE;
    /*#1*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_TRUE(NULLPTR != palTLSConf);
    /*#2*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(NULLPTR, palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}

int palTestEntropySource(void *data, unsigned char *output, size_t len, size_t *olen)
{
    palStatus_t status = PAL_SUCCESS;
    (void)data;

    status = pal_osRandomBuffer(output, len);
    if (PAL_SUCCESS == status)
    {
        *olen = len;
    }
    else
    {
        return -1;
    }
    return 0;
}

static void handshakeUDP(bool socketNonBlocking)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode =     PAL_DTLS_MODE;
    palSocketAddress_t socketAddr = {0};
    palSocketLength_t addressLength = 0;
    char serverResponse[PAL_TLS_MESSAGE_SIZE] = {0};
    uint32_t actualLen = 0;
    uint32_t written = 0;
    palX509_t pubKey = {(const void*)g_pubKey,sizeof(g_pubKey)};
    palPrivateKey_t prvKey = {(const void*)g_prvKey,sizeof(g_prvKey)};
    palTLSSocket_t tlsSocket = {g_socket, &socketAddr, 0, transportationMode};
    palX509_t caCert = { (const void*)pal_test_cas,sizeof(pal_test_cas) };
	uint8_t coapHelloWorldRequest[16] = { 0x50,0x01,0x57,0x3e,0xff,0x2f,0x68,0x65,0x6c,0x6c,0x6f,0x57,0x6f,0x72,0x6c,0x64 };

    /*#1*/
    status = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, socketNonBlocking, 0, &g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_getAddressInfo(PAL_TLS_TEST_SERVER_ADDRESS, &socketAddr, &addressLength);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    tlsSocket.addressLength = addressLength;
    tlsSocket.socket = g_socket;
    /*#3*/
    status = pal_setSockAddrPort(&socketAddr, DTLS_SERVER_PORT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    //status = pal_sslDebugging(true);
    //TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#6*/
    status = pal_setOwnCertAndPrivateKey(palTLSConf, &pubKey, &prvKey);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    status = pal_setCAChain(palTLSConf, &caCert, NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#8*/
    status = pal_tlsSetSocket(palTLSConf, &tlsSocket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#9*/

	status = pal_setHandShakeTimeOut(palTLSConf, 30000);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#10*/

    do
    {
        status = pal_handShake(palTLSHandle, palTLSConf);
    }
    while (PAL_ERR_TLS_WANT_READ == status || PAL_ERR_TLS_WANT_WRITE == status);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#11*/
    status = pal_sslGetVerifyResult(palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#12*/
	status = pal_sslWrite(palTLSHandle, coapHelloWorldRequest, sizeof(coapHelloWorldRequest), &written);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#13*/
    pal_osDelay(5000);
    /*#14*/
    do status = pal_sslRead(palTLSHandle, serverResponse, PAL_TLS_MESSAGE_SIZE, &actualLen);
    while (PAL_ERR_TLS_WANT_READ == status);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#15*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#16*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#17*/
    status = pal_close(&g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}


static void handshakeTCP(bool socketNonBlocking)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode =     PAL_TLS_MODE;
    palSocketAddress_t socketAddr = {0};
    palSocketLength_t addressLength = 0;
    char serverResponse[PAL_TLS_MESSAGE_SIZE] = {0};
    uint32_t actualLen = 0;
    uint32_t written = 0;
    palX509_t pubKey = {(const void*)g_pubKey,sizeof(g_pubKey)};
    palPrivateKey_t prvKey = {(const void*)g_prvKey,sizeof(g_prvKey)};
    palTLSSocket_t tlsSocket = { g_socket, &socketAddr, 0, transportationMode };
    palX509_t caCert = { (const void*)pal_test_cas,sizeof(pal_test_cas) };
    uint64_t curTimeInSec, timePassedInSec;
    const uint64_t minSecSinceEpoch = PAL_MIN_SEC_FROM_EPOCH + 1; //At least 47 years passed from 1.1.1970 in seconds
    

    /*#1*/
    status = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, socketNonBlocking, 0, &g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_getAddressInfo(PAL_TLS_TEST_SERVER_ADDRESS, &socketAddr, &addressLength);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    tlsSocket.addressLength = addressLength;
    tlsSocket.socket = g_socket;
    /*#3*/
    if (true == socketNonBlocking)
    {
        status = pal_setSockAddrPort(&socketAddr, TLS_SERVER_PORT_NB);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    else //blocking
    {
        status = pal_setSockAddrPort(&socketAddr, TLS_SERVER_PORT);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }

    /*#4*/
    status = pal_connect(g_socket, &socketAddr, addressLength);
    if (PAL_ERR_SOCKET_IN_PROGRES == status)
    {
        pal_osDelay(400);
    }
    else
    {
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    /*#5*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    TEST_ASSERT_NOT_EQUAL(palTLSConf, NULLPTR);
    /*#6*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    //status = pal_sslDebugging(true);
    //TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    status = pal_setOwnCertAndPrivateKey(palTLSConf, &pubKey, &prvKey);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#8*/
    status = pal_setCAChain(palTLSConf, &caCert, NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#9*/
    status = pal_tlsSetSocket(palTLSConf, &tlsSocket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#10*/
    if (true == socketNonBlocking)
    {
        status = pal_osSetTime(minSecSinceEpoch);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status); // More than current epoch time -> success
        do
        {
            curTimeInSec = pal_osGetTime();
            TEST_ASSERT_TRUE(curTimeInSec >= minSecSinceEpoch);        
            timePassedInSec = curTimeInSec - minSecSinceEpoch;
            status = pal_handShake(palTLSHandle, palTLSConf);
        }
        while ( (PAL_ERR_TLS_WANT_READ == status || PAL_ERR_TLS_WANT_WRITE == status) &&
                (timePassedInSec < PAL_SECONDS_PER_MIN)); //2 minutes to wait for handshake
    }
    else //blocking
    {
        status = pal_handShake(palTLSHandle, palTLSConf);
    }
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#11*/
    status = pal_sslGetVerifyResult(palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#12*/
    status = pal_sslWrite(palTLSHandle, TLS_GET_REQUEST, sizeof(TLS_GET_REQUEST), &written);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#13*/
    pal_osDelay(5000);
    /*#14*/
    status = pal_sslRead(palTLSHandle, serverResponse, PAL_TLS_MESSAGE_SIZE, &actualLen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#15*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#16*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#17*/
    status = pal_close(&g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

}

/**
* @brief Test TLS initialization and uninitialization.
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Initialize TLS configuration using `pal_initTLSConfiguration`.       | PAL_SUCCESS |
* | 2 | Initialize TLS context using `pal_initTLS`.                          | PAL_SUCCESS |
* | 3 | Add a NULL entropy source using `pal_addEntropySource`.             | PAL_ERR_INVALID_ARGUMENT |
* | 4 | Add a valid entropy source using `pal_addEntropySource`.             | PAL_SUCCESS |
* | 5 | Uninitialize TLS context using `pal_freeTLS`.                        | PAL_SUCCESS |
* | 6 | Uninitialize TLS configuration using `pal_tlsConfigurationFree`.     | PAL_SUCCESS |
*/
TEST(pal_tls, tlsInitTLS)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode =     PAL_TLS_MODE;
    /*#1*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_addEntropySource(NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_INVALID_ARGUMENT, status);
    /*#4*/
    status = pal_addEntropySource(palTestEntropySource);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#6*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}


/**
* @brief Test TLS initialization and uninitialization with additional keys.
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Initialize TLS configuration using `pal_initTLSConfiguration`.       | PAL_SUCCESS |
* | 2 | Add keys to the configuration using `pal_setOwnCertAndPrivateKey`.           | PAL_SUCCESS |
* | 3 | Initialize TLS context using `pal_initTLS`.                          | PAL_SUCCESS |
* | 4 | Uninitialize TLS context using `pal_freeTLS`.                        | PAL_SUCCESS |
* | 5 | Uninitialize TLS configuration using `pal_tlsConfigurationFree`.     | PAL_SUCCESS |
*/
TEST(pal_tls, tlsPrivateAndPublicKeys)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode = PAL_TLS_MODE;
    palX509_t pubKey = { (const void*)g_pubKey,sizeof(g_pubKey) };
    palPrivateKey_t prvKey = { (const void*)g_prvKey,sizeof(g_prvKey) };

    /*#1*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_NOT_EQUAL(palTLSConf, NULLPTR);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_setOwnCertAndPrivateKey(palTLSConf, &pubKey, &prvKey);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}


/**
* @brief Test TLS initialization and uninitialization with additional certificate and pre-shared keys.
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Initialize TLS configuration using `pal_initTLSConfiguration`.       | PAL_SUCCESS |
* | 2 | Set pre-shared keys to the configuration using `pal_setPSK`.            | PAL_SUCCESS |
* | 3 | Set certificate chain to the configuration using `pal_setCAChain`.      | PAL_SUCCESS |
* | 4 | Initialize TLS context using `pal_initTLS`.                          | PAL_SUCCESS |
* | 5 | Uninitialize TLS context using `pal_freeTLS`.                        | PAL_SUCCESS |
* | 6 | Uninitialize TLS configuration using `pal_tlsConfigurationFree`.     | PAL_SUCCESS |
*/
TEST(pal_tls, tlsCACertandPSK)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode = PAL_TLS_MODE;
    palX509_t caCert = { (const void*)g_ca_cert,sizeof(g_ca_cert) };
    /*#1*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_NOT_EQUAL(palTLSConf, NULLPTR);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_setPSK(palTLSConf, g_psk_id, sizeof(g_psk_id) - 1, g_psk, sizeof(g_psk));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#3*/
    status = pal_setCAChain(palTLSConf, &caCert, NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#6*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}


/**
* @brief Test TLS handshake (TCP blocking).
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a TCP (blocking) socket.                                        | PAL_SUCCESS |
* | 2 | Perform a DNS lookup on the server address.                                | PAL_SUCCESS |
* | 3 | Set the server port.                                                     | PAL_SUCCESS |
* | 4 | Connect the TCP socket to the server.                                        | PAL_SUCCESS |
* | 5 | Initialize the TLS configuration using `pal_initTLSConfiguration`.         | PAL_SUCCESS |
* | 6 | Initialize the TLS context using `pal_initTLS`.                            | PAL_SUCCESS |
* | 7 | Set the certificate and keys to the configuration using `pal_setOwnCertAndPrivateKey`.| PAL_SUCCESS |
* | 8 | Set the certificate chain to the configuration using `pal_setCAChain`.        | PAL_SUCCESS |
* | 9 | Set the socket chain to the configuration using `pal_tlsSetSocket`.           | PAL_SUCCESS |
* | 10 | Perform a TLS handshake with the server using `pal_handShaket`.           | PAL_SUCCESS |
* | 11 | Verify the handshake result using `pal_sslGetVerifyResult`.               | PAL_SUCCESS |
* | 12 | Write data over open TLS connection using `pal_sslWrite`.            | PAL_SUCCESS |
* | 13 | Wait for the response.                                                  | PAL_SUCCESS |
* | 14 | Read data from the open TLS connection using `pal_sslRead`.               | PAL_SUCCESS |
* | 15 | Uninitialize the TLS context using `pal_freeTLS`.                         | PAL_SUCCESS |
* | 16 | Uninitialize the TLS configuration using `pal_tlsConfigurationFree`.      | PAL_SUCCESS |
* | 17 | Close the TCP socket.                                                   | PAL_SUCCESS |
*/
TEST(pal_tls, tlsHandshakeTCP)
{
    handshakeTCP(false);
}

/**
* @brief Test TLS handshake (TCP non-blocking).
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a TCP (non-blocking) socket.                                    | PAL_SUCCESS |
* | 2 | Perform a DNS lookup on the server address.                                | PAL_SUCCESS |
* | 3 | Set the server port.                                                     | PAL_SUCCESS |
* | 4 | Connect the TCP socket to the server.                                        | PAL_SUCCESS |
* | 5 | Initialize the TLS configuration using `pal_initTLSConfiguration`.         | PAL_SUCCESS |
* | 6 | Initialize the TLS context using `pal_initTLS`.                            | PAL_SUCCESS |
* | 7 | Set the certificate and keys to the configuration using `pal_setOwnCertAndPrivateKey`.| PAL_SUCCESS |
* | 8 | Set the certificate chain to the configuration using `pal_setCAChain`.        | PAL_SUCCESS |
* | 9 | Set the socket chain to the configuration using `pal_tlsSetSocket`.           | PAL_SUCCESS |
* | 10 | Perform a TLS handshake with the server using `pal_handShaket` in a loop. | PAL_SUCCESS |
* | 11 | Verify the handshake result using `pal_sslGetVerifyResult`.               | PAL_SUCCESS |
* | 12 | Write data over the open TLS connection using `pal_sslWrite`.            | PAL_SUCCESS |
* | 13 | Wait for the response.                                                  | PAL_SUCCESS |
* | 14 | Read data from the open TLS connection using `pal_sslRead`.               | PAL_SUCCESS |
* | 15 | Uninitialize the TLS context using `pal_freeTLS`.                         | PAL_SUCCESS |
* | 16 | Uninitialize the TLS configuration using `pal_tlsConfigurationFree`.      | PAL_SUCCESS |
* | 17 | Close the TCP socket.                                                   | PAL_SUCCESS |
*/
TEST(pal_tls, tlsHandshakeTCP_nonBlocking)
{
    handshakeTCP(true);
}

/**
* @brief Test (D)TLS handshake (UDP -blocking).
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a UDP (blocking) socket.                                        | PAL_SUCCESS |
* | 2 | Perform a DNS lookup on the server address.                                | PAL_SUCCESS |
* | 3 | Set the server port.                                                     | PAL_SUCCESS |
* | 4 | Initialize the TLS configuration using `pal_initTLSConfiguration`.         | PAL_SUCCESS |
* | 5 | Initialize the TLS context using `pal_initTLS`.                            | PAL_SUCCESS |
* | 6 | Set the certificate and keys to the configuration using `pal_setOwnCertAndPrivateKey`.| PAL_SUCCESS |
* | 7 | Set the certificate chain to the configuration using `pal_setCAChain`.        | PAL_SUCCESS |
* | 8 | Set the socket chain to the configuration using `pal_tlsSetSocket`.           | PAL_SUCCESS |
* | 9 | Set the timeout for the handshake using `pal_setHandShakeTimeOut`.         | PAL_SUCCESS |
* | 10 | Perform a TLS handshake with the server using `pal_handShaket` in a loop. | PAL_SUCCESS |
* | 11 | Verify the handshake result using `pal_sslGetVerifyResult`.               | PAL_SUCCESS |
* | 12 | Write data over the open TLS connection using `pal_sslWrite`.            | PAL_SUCCESS |
* | 13 | Wait for the response.                                                  | PAL_SUCCESS |
* | 14 | Read data from the open TLS connection using `pal_sslRead`.               | PAL_SUCCESS |
* | 15 | Uninitialize the TLS context using `pal_freeTLS`.                         | PAL_SUCCESS |
* | 16 | Uninitialize the TLS configuration using `pal_tlsConfigurationFree`.      | PAL_SUCCESS |
* | 17 | Close the UDP socket.                                                   | PAL_SUCCESS |
*/
TEST(pal_tls, tlsHandshakeUDP)
{
    handshakeUDP(false);
}

/**
* @brief Test (D)TLS handshake (UDP -NonBlocking).
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a UDP (blocking) socket.                                        | PAL_SUCCESS |
* | 2 | Perform a DNS lookup on the server address.                                | PAL_SUCCESS |
* | 3 | Set the server port.                                                     | PAL_SUCCESS |
* | 4 | Initialize the TLS configuration using `pal_initTLSConfiguration`.         | PAL_SUCCESS |
* | 5 | Initialize the TLS context using `pal_initTLS`.                            | PAL_SUCCESS |
* | 6 | Set the certificate and keys to the configuration using `pal_setOwnCertAndPrivateKey`.| PAL_SUCCESS |
* | 7 | Set the certificate chain to the configuration using `pal_setCAChain`.        | PAL_SUCCESS |
* | 8 | Set the socket chain to the configuration using `pal_tlsSetSocket`.           | PAL_SUCCESS |
* | 9 | Set the timeout for the handshake using `pal_setHandShakeTimeOut`.         | PAL_SUCCESS |
* | 10 | Perform a TLS handshake with the server using `pal_handShaket` in a loop. | PAL_SUCCESS |
* | 11 | Verify the handshake result using `pal_sslGetVerifyResult`.               | PAL_SUCCESS |
* | 12 | Write data over the open TLS connection using `pal_sslWrite`.            | PAL_SUCCESS |
* | 13 | Wait for the response.                                                  | PAL_SUCCESS |
* | 14 | Read data from the open TLS connection using `pal_sslRead`.               | PAL_SUCCESS |
* | 15 | Uninitialize the TLS context using `pal_freeTLS`.                         | PAL_SUCCESS |
* | 16 | Uninitialize the TLS configuration using `pal_tlsConfigurationFree`.      | PAL_SUCCESS |
* | 17 | Close the UDP socket.                                                   | PAL_SUCCESS |
*/
TEST(pal_tls, tlsHandshakeUDP_NonBlocking)
{
    handshakeUDP(true);
}

/**
* @brief Test (D)TLS handshake (UDP non-blocking) with a very short timeout to see if you get a timeout.
*
*
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a UDP (blocking) socket.                                        | PAL_SUCCESS |
* | 2 | Perform a DNS lookup on server adderss.                                | PAL_SUCCESS |
* | 3 | Set the server port.                                                     | PAL_SUCCESS |
* | 4 | Initialize the TLS configuration using `pal_initTLSConfiguration`.         | PAL_SUCCESS |
* | 5 | Initialize the TLS context using `pal_initTLS`.                            | PAL_SUCCESS |
* | 6 | Set the certificate and keys to the configuration using `pal_setOwnCertAndPrivateKey`.| PAL_SUCCESS |
* | 7 | Set the certificate chain to the configuration using `pal_setCAChain`.        | PAL_SUCCESS |
* | 8 | Set the socket chain to the configuration using `pal_tlsSetSocket`.           | PAL_SUCCESS |
* | 9 | Set a short timeout for the handshake using `pal_setHandShakeTimeOut`.   | PAL_SUCCESS |
* | 10 | Perform a TLS handshake with the server using `pal_handShaket` in a loop. | PAL_ERR_TIMEOUT_EXPIRED |
* | 11 | Uninitialize the TLS context using `pal_freeTLS`.                         | PAL_SUCCESS |
* | 12 | Uninitialize the TLS configuration using `pal_tlsConfigurationFree`.      | PAL_SUCCESS |
* | 13 | Close the UDP socket.                                                   | PAL_SUCCESS |
*/
TEST(pal_tls, tlsHandshakeUDPTimeOut)
{
    palStatus_t status = PAL_SUCCESS;
    palTLSConfHandle_t palTLSConf = NULLPTR;
    palTLSHandle_t palTLSHandle = NULLPTR;
    palTLSTransportMode_t transportationMode = PAL_DTLS_MODE;
    palSocketAddress_t socketAddr = { 0 };
    palSocketLength_t addressLength = 0;
    palX509_t pubKey = { (const void*)g_pubKey,sizeof(g_pubKey) };
    palPrivateKey_t prvKey = { (const void*)g_prvKey,sizeof(g_prvKey) };
    palTLSSocket_t tlsSocket = { g_socket, &socketAddr, 0, transportationMode };
    palX509_t caCert = { (const void*)pal_test_cas,sizeof(pal_test_cas) };
    uint64_t curTimeInSec;
    const uint64_t minSecSinceEpoch = PAL_MIN_SEC_FROM_EPOCH + 1; //At least 47 years passed from 1.1.1970 in seconds      
    
    /*#1*/
    status = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, 0, &g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#2*/
    status = pal_getAddressInfo(PAL_TLS_TEST_SERVER_ADDRESS, &socketAddr, &addressLength);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    tlsSocket.addressLength = addressLength;
    tlsSocket.socket = g_socket;
    /*#3*/
    status = pal_setSockAddrPort(&socketAddr, DTLS_SERVER_PORT_TIMEOUT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    status = pal_initTLSConfiguration(&palTLSConf, transportationMode);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#5*/
    status = pal_initTLS(palTLSConf, &palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    //status = pal_sslDebugging(true);
    //TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#6*/
    status = pal_setOwnCertAndPrivateKey(palTLSConf, &pubKey, &prvKey);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#7*/
    status = pal_setCAChain(palTLSConf, &caCert, NULL);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#8*/
    status = pal_tlsSetSocket(palTLSConf, &tlsSocket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#9*/
    status = pal_setHandShakeTimeOut(palTLSConf, 100);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    status = pal_osSetTime(minSecSinceEpoch);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status); // More than current epoch time -> success    
    /*#10*/
    do
    {
        status = pal_handShake(palTLSHandle, palTLSConf);
    }
    while (PAL_ERR_TLS_WANT_READ == status || PAL_ERR_TLS_WANT_WRITE == status);

    curTimeInSec = pal_osGetTime();
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_TIMEOUT_EXPIRED, status);
    TEST_ASSERT_TRUE(curTimeInSec - minSecSinceEpoch <= 1); //less than one second             
    /*#11*/
    status = pal_freeTLS(&palTLSHandle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#12*/
    status = pal_tlsConfigurationFree(&palTLSConf);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#13*/
    status = pal_close(&g_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
}
