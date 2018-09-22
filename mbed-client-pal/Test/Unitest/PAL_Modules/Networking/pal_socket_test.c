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

#include "pal.h"
#include "pal_network.h"
#include "unity.h"
#include "unity_fixture.h"
#include "PlatIncludes.h"
#include "pal_test_main.h"
#include "string.h"
#ifdef __LINUX__
#include <netdb.h>
#define test_getAddressInfo getAddressInfoIPv4
#else
#define test_getAddressInfo pal_getAddressInfo
#endif

TEST_GROUP(pal_socket);

//Sometimes you may want to get local data in a module,
//for example if you need to pass a reference.
//However, you should usually avoid this.
//extern int Counter;

#define PAL_NET_SUPPORT_LWIP 1
#define PAL_NET_TEST_SERVER_NAME   "www.arm.com"
#define PAL_NET_TEST_SERVER_NAME_UDP   "8.8.8.8"


#define PAL_NET_TEST_SERVER_HTTP_PORT 80
#define PAL_NET_TEST_SERVER_UDP_PORT 53
#define PAL_NET_TEST_INCOMING_PORT 8002
#define PAL_NET_TEST_INCOMING_PORT2 8989

#define PAL_NET_TEST_LOCAL_LOOPBACK_IF_INDEX 0
PAL_PRIVATE void * g_networkInterface = NULL;
PAL_PRIVATE uint32_t g_interfaceCTXIndex = 0;
PAL_PRIVATE uint32_t s_callbackcounter = 0;

#define PAL_NET_TEST_SOCKETS 4
PAL_PRIVATE palSocket_t g_testSockets[PAL_NET_TEST_SOCKETS] = {0,0,0,0};

#define PAL_NET_TEST_GOOGLE_CDN_HOST "ajax.googleapis.com" /*! CDN host server */
#define PAL_NET_TEST_GOOGLE_CDN_HOST_PORT 80 /*! CDN host port */
#define PAL_NET_TEST_GOOGLE_CDN_REQUEST "GET /ajax/libs/jquery/3.2.1/jquery.js HTTP/1.0\r\nHost:" PAL_NET_TEST_GOOGLE_CDN_HOST "\r\n\r\n" /*! HTTP get request */
#define PAL_NET_TEST_BUFFERED_TCP_BUF_SIZE_SMALL 4
#define PAL_NET_TEST_BUFFERED_TCP_BUF_SIZE_LARGE 1024
#define PAL_NET_TEST_BUFFERED_UDP_BUF_SIZE_SMALL 64
#define PAL_NET_TEST_BUFFERED_UDP_BUF_SIZE_LARGE 512
#define PAL_NET_TEST_BUFFERED_UDP_PORT 2606
#define PAL_NET_TEST_BUFFERED_UDP_MESSAGE_SIZE (1024 * 256)
PAL_PRIVATE uint8_t *g_testRecvBuffer = NULLPTR;
PAL_PRIVATE uint8_t *g_testSendBuffer = NULLPTR;

typedef struct pal_udp_test_data /*! structure used to hold state in UDP buffered tests */
{
    const size_t messageSize;
    const size_t bufferSize;
    const uint8_t startValue;
    palNetInterfaceInfo_t interfaceInfo;
    uint8_t currentValue;
    size_t totalSize;
    size_t chunkSize;
} pal_udp_test_data_t;

TEST_SETUP(pal_socket)
{
    uint32_t i = 0;
    palStatus_t status = PAL_SUCCESS;
    //This is run before *each test*
    pal_init();
    if (g_networkInterface == NULL)
    {
        g_networkInterface = palTestGetNetWorkInterfaceContext();
        status = pal_registerNetworkInterface(g_networkInterface , &g_interfaceCTXIndex);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }

    for (i = 0; i < PAL_NET_TEST_SOCKETS; i++)
    {
        g_testSockets[i] = 0;
    }
}

TEST_TEAR_DOWN(pal_socket)
{
    uint32_t i = 0;
    for (i = 0; i < PAL_NET_TEST_SOCKETS; i++)
    {
        if (g_testSockets[i] != 0)
        {
            pal_close(&(g_testSockets[i]));
        }
    }

    if (g_testRecvBuffer != NULLPTR)
    {
        free(g_testRecvBuffer);
        g_testRecvBuffer = NULLPTR;
    }
    if (g_testSendBuffer != NULLPTR)
    {
        free(g_testSendBuffer);
        g_testSendBuffer = NULLPTR;
    }

    pal_destroy();
}

#define PAL_TEST_BUFFER_SIZE 50
PAL_PRIVATE void socketCallback1( void * arg)
{
    s_callbackcounter++;
}

/*! \brief Test socket creation, destruction and modification, as well as getting address infromation and checking the blocking status of sockets.
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Register a net interface using `pal_registerNetworkInterface`.                          | PAL_SUCCESS |
* | 2 | Register a net interface using `pal_registerNetworkInterface`, and check that the ID is the same as the previous step.  | PAL_SUCCESS |
* | 3 | Get the interface address using `pal_getNetInterfaceInfo`.                       | PAL_SUCCESS |
* | 4 | Create a blocking UDP socket using `pal_socket`.                                        | PAL_SUCCESS |
* | 5 | Create a blocking UDP socket using `pal_socket`.                                        | PAL_SUCCESS |
* | 6 | Create a non-blocking UDP socket using `pal_socket`.                                    | PAL_SUCCESS |
* | 7 | Create a blocking asynchronous TCP socket with `socketCallback1` as callback.           | PAL_SUCCESS |
* | 8 | Check the number of net interfaces registered using `pal_getNetInterfaceInfo`.           | PAL_SUCCESS |
* | 9 | Set the socket receive timeout using `pal_setSocketOptions`.                              | PAL_SUCCESS |
* | 10 | Check that the sockets return the correct blocking status using `pal_isNonBlocking`.      | PAL_SUCCESS |
* | 11 | Check the `pal_getAddressInfo` function with an invalid address.                       | PAL_ERR_SOCKET_DNS_ERROR |
* | 12 | Close all sockets.                                                              | PAL_SUCCESS |
*/
TEST(pal_socket, socketUDPCreationOptionsTest)
{
    palStatus_t result = PAL_SUCCESS;
    uint32_t numInterface = 0;
    palNetInterfaceInfo_t interfaceInfo;
    uint32_t interfaceIndex = 0;
    uint32_t interfaceIndex2 = 0;
    uint32_t sockOptVal = 5000;
    uint32_t sockOptLen = sizeof(sockOptVal);
    palSocketAddress_t address = { 0 };
    palSocketLength_t addrlen = 0;
    bool isNonBlocking = false;

    memset(&interfaceInfo,0,sizeof(interfaceInfo));
    // Check that re-adding the network interface returns the same index
    /*#1*/
    result = pal_registerNetworkInterface(g_networkInterface, &interfaceIndex);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#2*/
    result = pal_registerNetworkInterface(g_networkInterface, &interfaceIndex2);
    TEST_ASSERT_EQUAL_HEX(interfaceIndex, interfaceIndex2);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#3*/
    result = pal_getNetInterfaceInfo(interfaceIndex, &interfaceInfo);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_PRINTF("Default interface address: %u %u %u %u \r\n",
        (unsigned char)interfaceInfo.address.addressData[2],
        (unsigned char)interfaceInfo.address.addressData[3],
        (unsigned char)interfaceInfo.address.addressData[4],
        (unsigned char)interfaceInfo.address.addressData[5]);;


    //Blocking
    /*#4*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, interfaceIndex, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    /*#5*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, interfaceIndex, &g_testSockets[1]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    //Non-blocking
    /*#6*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, true, interfaceIndex, &g_testSockets[3]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
#if    PAL_NET_ASYNCHRONOUS_SOCKET_API
    /*#7*/
    result = pal_asynchronousSocket(PAL_AF_INET, PAL_SOCK_STREAM, false, interfaceIndex, socketCallback1, &g_testSockets[2]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
#endif // PAL_NET_ASYNCHRONOUS_SOCKET_API

    /*#8*/
    result = pal_getNumberOfNetInterfaces(&numInterface);
    TEST_ASSERT_NOT_EQUAL(numInterface, 0);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#9*/
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &sockOptVal, sockOptLen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#10*/
    result = pal_isNonBlocking(g_testSockets[0],&isNonBlocking);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(isNonBlocking, false);

    result = pal_isNonBlocking(g_testSockets[3], &isNonBlocking);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(isNonBlocking, true);

    /*#11*/
    result = pal_getAddressInfo("0.0.0.0", &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX(PAL_ERR_SOCKET_DNS_ERROR, result);

    /*#12*/
#if    PAL_NET_ASYNCHRONOUS_SOCKET_API
    result = pal_close(&g_testSockets[2]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

#endif // PAL_NET_ASYNCHRONOUS_SOCKET_API

    result = pal_close(&g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    result = pal_close(&g_testSockets[1]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    result = pal_close(&g_testSockets[3]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    result = pal_close(&g_testSockets[3]); //double close - should succeed
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
}


/*! \brief Test TCP socket creation, connection, send and receive with a test server.
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a blocking TCP socket using `pal_socket`.                                         | PAL_SUCCESS |
* | 2 | Look up the IP address of the test server using `pal_getAddressInfo`.                          | PAL_SUCCESS |
* | 3 | Set the port to a test port in the address structure using `pal_setSockAddrPort` and set timeout. | PAL_SUCCESS |
* | 4 | Connect the socket to the test server using `pal_connect`.                                     | PAL_SUCCESS |
* | 5 | Send a test message (short HTTP request) to the test server using `pal_send`.                  | PAL_SUCCESS |
* | 6 | Receive (blocking) the server's response using `pal_recv` and check it is HTTP.          | PAL_SUCCESS |
* | 7 | Close the socket.                                                                        | PAL_SUCCESS |
*/
TEST(pal_socket, basicTCPclientSendRecieve)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    const char message[] = "GET / HTTP/1.0\r\n\r\n";
    size_t sent = 0;
    char buffer[100] = { 0 };
    size_t read = 0;
    palSocketLength_t addrlen = 0;
    int timeout = 1000;

    /*#1*/

    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#2*/
    result = pal_getAddressInfo(PAL_NET_TEST_SERVER_NAME, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_SERVER_HTTP_PORT);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);


    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_SNDTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);


    /*#4*/
    result = pal_connect(g_testSockets[0], &address, 16);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#5*/
    result = pal_send(g_testSockets[0], message, sizeof(message) - 1, &sent);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#6*/
    result = pal_recv(g_testSockets[0], buffer, 99, &read);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    TEST_ASSERT(read >= 4);
    TEST_ASSERT(buffer[0] == 'H' && buffer[1] == 'T'&& buffer[2] == 'T' && buffer[3] == 'P');

    /*#7*/
    pal_close(&g_testSockets[0]);

}

/*! \brief Test UDP socket creation, connection, send and recieve with a test server.
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a blocking UDP socket using `pal_socket`.                                     | PAL_SUCCESS |
* | 2 | Look up the IP address of the test server using `pal_getAddressInfo`.                      | PAL_SUCCESS |
* | 3 | Set the port to a test port in the address structure using `pal_setSockAddrPort`.            | PAL_SUCCESS |
* | 4 | Connect the socket to the test server using `pal_connect`.                                 | PAL_SUCCESS |
* | 5 | Send a test message (short DNS request) to the test server using `pal_send`.                | PAL_SUCCESS |
* | 6 | Receive (blocking) the server's response using `pal_recv`.                           | PAL_SUCCESS |
* | 7 | Close the socket.                                                                    | PAL_SUCCESS |
*/
TEST(pal_socket, basicUDPclientSendRecieve)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    palSocketAddress_t address2 = { 0 };
    uint8_t buffer[33] = { 0x8e, 0xde, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x61, 0x72, 0x73, 0x74, 0x65, 0x63, 0x68, 0x6e, 0x69, 0x63, 0x61, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01 };
    uint8_t buffer_in[10];
    size_t sent = 0;
    size_t read = 0;
    size_t socket_timeout_ms = 5000;
    palSocketLength_t addrlen = 0;

    /*#1*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#2*/
    result = pal_getAddressInfo(PAL_NET_TEST_SERVER_NAME_UDP, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_SERVER_UDP_PORT);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#4*/
    //We set a timeout for receiving so we won't get stuck in the test
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &socket_timeout_ms, sizeof(socket_timeout_ms));
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#5*/
    result = pal_sendTo(g_testSockets[0], buffer, sizeof(buffer), &address, 16, &sent);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL(sent, sizeof(buffer));

    /*#6*/
    result = pal_receiveFrom(g_testSockets[0], buffer_in, 10, &address2, &addrlen, &read);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL(read, 10);

    /*#7*/
    pal_close(&g_testSockets[0]);
}




// This is an example showing how to check for a socket that has been closed remotely.
#if 0
PAL_PRIVATE void basicSocketScenario3Callback(void * arg)
{
    char buffer[400];
    size_t read = 0;
    palStatus_t result;


    s_callbackcounter++;
    result = pal_recv(g_testSockets[0], buffer, 999, &read);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    // If 0 bytes are read it means that the peer has performed an orderly shutdown so we must close the socket
    // to avoid ppoll from checking it. Checking a socket whose other end has been shut down causes ppoll to immediately return
    // with events == 0x1.
    if(read == 0)
    {
        pal_close(&g_testSockets[0]);
    }
    else
    {
        buffer[read] = '\0';
        if(s_callbackcounter == 0)
        {
            TEST_ASSERT(read >= 4);
            TEST_ASSERT(buffer[0] == 'H' && buffer[1] == 'T'&& buffer[2] == 'T' && buffer[3] == 'P');
        }
    }

}
#endif
palSemaphoreID_t s_semaphoreID = NULLPTR;

PAL_PRIVATE void socketCallback2(void * arg)
{
    palStatus_t result;
    if(s_callbackcounter == 0)
    {
        result = pal_osSemaphoreRelease(s_semaphoreID);
        TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    }
    s_callbackcounter++;

}

static int s_secondCallbackCounter = 0;
PAL_PRIVATE void socketCallbackErr(void * arg)
{
    s_secondCallbackCounter++;
}

/*! \brief Test asynchronous socket callbacks.
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Look up the IP address of the test server using `pal_getAddressInfo`.                      | PAL_SUCCESS |
* | 2 | Create a blocking asynchronous TCP socket with `socketCallback2` as callback.        | PAL_SUCCESS |
* | 3 | Set port to a test port in the address structure using `pal_setSockAddrPort`.            | PAL_SUCCESS |
* | 4 | Connect the socket to the test server using `pal_connect`.                                 | PAL_SUCCESS |
* | 5 | Send a test message (short HTTP request) to the test server using `pal_send`.               | PAL_SUCCESS |
* | 6 | Wait for a callback to release the semaphore when the response arrives.                    | PAL_SUCCESS |
* | 7 | Receive (blocking) the server's response using `pal_recv` and check that the response is HTTP.| PAL_SUCCESS |
* | 8 | Close the socket.                                                                    | PAL_SUCCESS |
*/
TEST(pal_socket, basicSocketScenario3)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    const char* message = "GET / HTTP/1.0\r\nHost:10.45.48.68:8000\r\n\r\n";
    size_t sent = 0;
    char buffer[100] = { 0 };
    size_t read = 0;
    s_callbackcounter = 0;
    palSocketLength_t addrlen = 0;
    int32_t countersAvailable;

    result = pal_osSemaphoreCreate(1, &s_semaphoreID);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    result = pal_osSemaphoreWait(s_semaphoreID, 40000, &countersAvailable);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#1*/
    result = pal_getAddressInfo(PAL_NET_TEST_SERVER_NAME, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);


#if PAL_NET_ASYNCHRONOUS_SOCKET_API
    /*#2*/
    result = pal_asynchronousSocketWithArgument(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, socketCallback2, "socketCallback2Arg", &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    s_secondCallbackCounter = 0;
    result = pal_asynchronousSocketWithArgument(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, socketCallbackErr, "socketCallback2Arg", &g_testSockets[1]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    TEST_ASSERT_EQUAL_HEX(0, s_secondCallbackCounter);
    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_SERVER_HTTP_PORT);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#4*/
    result = pal_connect(g_testSockets[0], &address, 16);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    TEST_ASSERT_EQUAL_HEX(0, s_secondCallbackCounter);
    /*#5*/
    result = pal_send(g_testSockets[0], message, strlen(message), &sent);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    TEST_ASSERT_EQUAL_HEX(0, s_secondCallbackCounter);
    // Give a chance for the callback to be called.
    /*#6*/
    result=pal_osSemaphoreWait(s_semaphoreID, 40000,  &countersAvailable);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    result=pal_osSemaphoreDelete(&s_semaphoreID);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#7*/
    result = pal_recv(g_testSockets[0], buffer, 99, &read);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT(read >= 4);
    TEST_ASSERT(buffer[0] == 'H' && buffer[1] == 'T'&& buffer[2] == 'T' && buffer[3] == 'P');
    TEST_ASSERT(s_callbackcounter > 0);


    TEST_ASSERT_EQUAL_HEX(0, s_secondCallbackCounter);
    /*#8*/
    pal_close(&g_testSockets[0]);
#endif // PAL_NET_ASYNCHRONOUS_SOCKET_API
}


/*! \brief Tests two main secenarios:
* 1. Use `pal_socketMiniSelect` to detect incoming traffic.
* 2. Use `pal_socketMiniSelect` to check if a non-blocking socket has finished connecting.
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a blocking TCP socket using `pal_socket`.                                        | PAL_SUCCESS |
* | 2 | Create a blocking UDP socket using `pal_socket`.                                        | PAL_SUCCESS |
* | 3 | Look up the IP address of the `www.arm.com` server using `pal_getAddressInfo`.                  | PAL_SUCCESS |
* | 4 | Set a port to the test port in the address structure using `pal_setSockAddrPort`.               | PAL_SUCCESS |
* | 5 | Connect a socket to the test server using `pal_connect`.                                    | PAL_SUCCESS |
* | 6 | Send a test message (short HTTP request) to the test server using `pal_send`.                  | PAL_SUCCESS |
* | 7 | Call `socketMiniSelect` with a timeout of 5 seconds, and check for correct socket state. Check `select` again when the data arrives.| PAL_SUCCESS |
* | 8 | Receive (blocking) the server's response using `pal_recv` and check that the response is HTTP.   | PAL_SUCCESS |
* | 9 | Close the socket.                                                                       | PAL_SUCCESS |
* | 10 | Call `socketMiniSelect` with a timeout of 1 second and check for the correct socket state.| PAL_SUCCESS |
* | 11 | Close the socket.                                                                      | PAL_SUCCESS |
* | 12 | Create a non-blocking TCP socket using `pal_socket`.                                   | PAL_SUCCESS |
* | 13 | Look up the IP address `192.0.2.0` (invalid IP address) using `pal_getAddressInfo`.     | PAL_SUCCESS |
* | 14 | Set the port to the test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | 15 | Connect to an invalid address and call `select`; check that the socket is not writable.  | PAL_SUCCESS |
* | 16 | Close the socket.                                                                      | PAL_SUCCESS |
* | 17 | Create a non-blocking TCP socket using `pal_socket`.                                   | PAL_SUCCESS |
* | 18 | Look up the IP address of the `www.arm.com` server using `pal_getAddressInfo`.                 | PAL_SUCCESS |
* | 19 | Set the port to the test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | 20 | Connect and call `select` with a timeout of 2 seconds, and check that the socket is writable.   | PAL_SUCCESS |
* | 21 | Close the socket.                                                                      | PAL_SUCCESS |
*/
TEST(pal_socket, basicSocketScenario4)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    const char* message = "GET / HTTP/1.0\r\n\r\n";
    size_t sent = 0;
    char buffer[100] = { 0 };
    size_t read = 0;
    palSocketLength_t addlen = 0;
    uint32_t numSockets = 0;
    palSocket_t socketsToCheck[2] = { 0 };
    pal_timeVal_t tv = {0};
    uint8_t palSocketStatus[2] = { 0 };

    /*#1*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#2*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, 0, &g_testSockets[1]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#3*/
    result = pal_getAddressInfo("www.arm.com", &address, &addlen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#4*/
    result = pal_setSockAddrPort(&address, 80);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#5*/
    result = pal_connect(g_testSockets[0], &address, 16);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#6*/
    result = pal_send(g_testSockets[0], message, strlen(message), &sent);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#7*/
    socketsToCheck[0] = g_testSockets[0];
    socketsToCheck[1] = g_testSockets[1];
    tv.pal_tv_sec = 5;
    result = pal_socketMiniSelect(socketsToCheck, 2, &tv, palSocketStatus, &numSockets); // Data is expected to arrive during select
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    if (numSockets == 0) // Clean up to prevent resource leak.
    {
    pal_close(&g_testSockets[0]);
    pal_close(&g_testSockets[1]);
    }
    TEST_ASSERT( 0 <  numSockets); 
    TEST_ASSERT(0< palSocketStatus[0] );
    TEST_ASSERT(PAL_NET_SELECT_IS_TX(palSocketStatus, 0) || PAL_NET_SELECT_IS_RX(palSocketStatus, 0) || PAL_NET_SELECT_IS_ERR(palSocketStatus, 0));
    TEST_ASSERT((palSocketStatus[1] & (PAL_NET_SOCKET_SELECT_RX_BIT | PAL_NET_SOCKET_SELECT_ERR_BIT)) ==    0);
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_RX(palSocketStatus,1)));
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_ERR(palSocketStatus, 1)));


    palSocketStatus[0] = 0;
    palSocketStatus[1] = 0;
    result = pal_socketMiniSelect(socketsToCheck, 2, &tv, palSocketStatus, &numSockets); // Check what happens when you call `select` when the data has already arrived.
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    if (numSockets == 0) // Clean up to prevent resource leak.
    {
        pal_close(&g_testSockets[0]);
        pal_close(&g_testSockets[1]);
    }
    TEST_ASSERT(0 <  numSockets);
    TEST_ASSERT(0< palSocketStatus[0]);
    TEST_ASSERT((palSocketStatus[1] & (PAL_NET_SOCKET_SELECT_RX_BIT | PAL_NET_SOCKET_SELECT_ERR_BIT)) == 0);
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_RX(palSocketStatus, 1)));
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_ERR(palSocketStatus, 1)));



    /*#8*/

    result = pal_recv(g_testSockets[0], buffer, 99, &read);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    TEST_ASSERT(read >= 4);
    TEST_ASSERT(buffer[0] == 'H' && buffer[1] == 'T'&& buffer[2] == 'T' && buffer[3] == 'P');

    /*#9*/
    pal_close(&g_testSockets[0]);

    /*#10*/
    numSockets = 0;
    palSocketStatus[0] =0;
    palSocketStatus[1] =0;
    socketsToCheck[0] = g_testSockets[1];
    socketsToCheck[1] = 0;
    tv.pal_tv_sec = 1;

    result = pal_socketMiniSelect(socketsToCheck, 1, &tv, palSocketStatus, &numSockets);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT((palSocketStatus[0] & (PAL_NET_SOCKET_SELECT_RX_BIT | PAL_NET_SOCKET_SELECT_ERR_BIT)) ==    0);
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_RX(palSocketStatus, 1)));
    TEST_ASSERT_FALSE((PAL_NET_SELECT_IS_ERR(palSocketStatus, 1)));

    /*#11*/
    pal_close(&g_testSockets[1]);

    // Non-responsive socket connection
    /*#12*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, true, 0, &g_testSockets[2]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    numSockets = 0;
    palSocketStatus[0] =0;
    palSocketStatus[1] =0;
    socketsToCheck[0] = g_testSockets[2];
    socketsToCheck[1] = 0;
    tv.pal_tv_sec = 1;

    /*#13*/
     result = pal_getAddressInfo("192.0.2.0", &address, &addlen); // Address intended for testing (not a real address); we don't expect a connection.
     TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

     /*#14*/
     result = pal_setSockAddrPort(&address, 80);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

     result = pal_connect(g_testSockets[2], &address, 16);
     //TEST_ASSERT_EQUAL_HEX( PAL_ERR_SOCKET_IN_PROGRES, result); // Comment back in when a non-blocking connection is enabled on mbed OS

     /*#15*/
    result = pal_socketMiniSelect(socketsToCheck, 1, &tv, palSocketStatus, &numSockets);

    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT( 0 ==  numSockets);
    TEST_ASSERT(0 == palSocketStatus[0] );

    /*#16*/
    pal_close(&g_testSockets[2]);

    /*#17*/
     result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, true, 0, &g_testSockets[2]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    numSockets = 0;
    palSocketStatus[0] =0;
    palSocketStatus[1] =0;
    socketsToCheck[0] = g_testSockets[2];
    socketsToCheck[1] = 0;
    tv.pal_tv_sec = 2;

    /*#18*/
     result = pal_getAddressInfo("www.arm.com", &address, &addlen); // Address intended for testing (not a real address); we don't expect a connection.
     TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

     /*#19*/
     result = pal_setSockAddrPort(&address, 80);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

     result = pal_connect(g_testSockets[2], &address, 16);
     //TEST_ASSERT_EQUAL_HEX( PAL_ERR_SOCKET_IN_PROGRES, result);   // Comment back in when a non-blocking connection is enabled on mbed OS

     /*#20*/
    result = pal_socketMiniSelect(socketsToCheck, 1, &tv, palSocketStatus, &numSockets);
    /*#21*/
    pal_close(&g_testSockets[2]);

    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT( 1 ==  numSockets);
    TEST_ASSERT( PAL_NET_SOCKET_SELECT_TX_BIT ==  (palSocketStatus[0]& ( PAL_NET_SOCKET_SELECT_TX_BIT) ));
    TEST_ASSERT((PAL_NET_SELECT_IS_TX(palSocketStatus, 0)));
}


typedef struct palNetTestThreadData{
    palSemaphoreID_t sem1;
    palSemaphoreID_t sem2;
    uint16_t port;
} palNetTestThreadData_t;

char s_rcv_buffer[20] = {0};
char s_rcv_buffer2[50]  = {0};

void palNetClientFunc(void const *argument)
{
    palStatus_t result = PAL_SUCCESS;
    int32_t tmp = 0;
    size_t sent = 0;
    size_t read = 0;
    palNetTestThreadData_t* dualSem = (palNetTestThreadData_t*)argument;
    palSocketLength_t addrlen = 16;
    //palSocketAddress_t address = { 0 };
    palNetInterfaceInfo_t interfaceInfo;
    const char* message = "GET / HTTP/1.0\r\n\r\n";

    /*#C1*/
    result = pal_osSemaphoreWait(dualSem->sem1, 500, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C2*/
    result = pal_getNetInterfaceInfo(PAL_NET_TEST_LOCAL_LOOPBACK_IF_INDEX, &interfaceInfo);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C3*/
    uint16_t incoming_port = dualSem->port;
    TEST_PRINTF("client port = %u", incoming_port);
    result = pal_setSockAddrPort(&(interfaceInfo.address), incoming_port);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C4*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, &g_testSockets[2]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C5*/
    result = pal_connect(g_testSockets[2], &(interfaceInfo.address), addrlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C6*/
    result = pal_send(g_testSockets[2], message, 18, &sent);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#C7*/
    result = pal_recv(g_testSockets[2], s_rcv_buffer, 15, &read);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_PRINTF(s_rcv_buffer);

    /*#C8*/
    pal_close(&g_testSockets[2]);

    result = pal_osSemaphoreRelease(dualSem->sem2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
}

/*! \brief /b ServerSocketScenario tests a TCP client-server scenario using device loopback.
*
* \note The test steps are divided into those in the server main thread (S1..S13) and those in the client thread (C1..C8).
* The sequence below is an approximation of the actual order of execution.
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | S1 | Create a blocking TCP server socket using `pal_socket`.                                | PAL_SUCCESS |
* | S2 | Create a blocking TCP socket using `pal_socket`.                                       | PAL_SUCCESS |
* | S3 | Look up the IP address of loopback using `pal_getAddressInfo`.                           | PAL_SUCCESS |
* | S4 | Set the port to test port in address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | S5 | Bind the server socket to the port and address using `pal_bind`.                             | PAL_SUCCESS |
* | S6 | Create synchronization sepmaphores and set count to 0.                             | PAL_SUCCESS |
* | S7 | Create a client thread with `BelowNormal` priority running `palNetClientFunc`.           | PAL_SUCCESS |
* | C1 | Client thread blocks on client sepmaphore s1.                                      | PAL_SUCCESS |
* | S8 | Listen to the server port using `pal_listen`.                                            | PAL_SUCCESS |
* | S9 | Release the client sepmahore s1.                                                       | PAL_SUCCESS |
* | S10 | Call `accept` (blocking) to accept a new connection (retry in case of failure).     | PAL_SUCCESS |
* | C2 | Look up the IP address of the loopback using `pal_getAddressInfo`.                           | PAL_SUCCESS |
* | C3 | Set the port to test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | C4 | Create a blocking TCP socket using `pal_socket`.                                       | PAL_SUCCESS |
* | C5 | Connect to the server using `pal_connect`.                                               | PAL_SUCCESS |
* | C6 | Send data to server.                                                               | PAL_SUCCESS |
* | S11 | Receive data from the client.                                                         | PAL_SUCCESS |
* | S12 | Send data to the client.                                                              | PAL_SUCCESS |
* | C7 | Receive data from the server.                                                          | PAL_SUCCESS |
* | C8 | Client thread cleanup - close the socket and release the semaphore.                        | PAL_SUCCESS |
* | S13 | Cleanup: close sockets and delete semaphores.                                     | PAL_SUCCESS |
*/

TEST(pal_socket, ServerSocketScenario)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address2 = { 0 };
    const char* messageOut = "HTTP/1.0 200 OK";
    size_t sent = 0;
    size_t read = 0;
    palSocketLength_t addrlen = 16;

    palSemaphoreID_t semaphoreID = NULLPTR;
    palSemaphoreID_t semaphoreID2 = NULLPTR;
    palNetTestThreadData_t dualSem = {0};
    palThreadID_t threadID1 = NULLPTR;
    int32_t tmp = 0;
    palNetInterfaceInfo_t interfaceInfo;
    memset(&interfaceInfo,0,sizeof(interfaceInfo));


    /*#S1*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM_SERVER, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S2*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, &g_testSockets[1]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S3*/
    result = pal_getNetInterfaceInfo(PAL_NET_TEST_LOCAL_LOOPBACK_IF_INDEX, &interfaceInfo);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

        TEST_PRINTF("interface addr: %u %u %u %u \r\n",
        (unsigned char)interfaceInfo.address.addressData[2],
        (unsigned char)interfaceInfo.address.addressData[3],
        (unsigned char)interfaceInfo.address.addressData[4],
        (unsigned char)interfaceInfo.address.addressData[5]);;
    /*#S4*/
    uint32_t rand_number = 0;
    uint16_t incoming_port;

    for (int i=0; i<5; i++) {
        pal_osRandom32bit(&rand_number);
        incoming_port = (uint16_t)(35400 + (rand_number % (40000 - 35400)));
        TEST_PRINTF("server port = %u", incoming_port);

        result = pal_setSockAddrPort(&(interfaceInfo.address), incoming_port);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

        /*#S5*/
        result = pal_bind(g_testSockets[0], &(interfaceInfo.address), interfaceInfo.addressSize);

        if (PAL_SUCCESS == result) {
            TEST_PRINTF("bind succeeded on port %u", incoming_port);
            break;
        } else {
            TEST_PRINTF("bind failed on port %u", incoming_port);
        }
    }
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S6*/

    // start client thread to connect to the server.
    result = pal_osSemaphoreCreate(1 ,&semaphoreID);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    result = pal_osSemaphoreWait(semaphoreID, 1000, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);


    result = pal_osSemaphoreCreate(1 ,&semaphoreID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    result = pal_osSemaphoreWait(semaphoreID2, 1000, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    dualSem.sem1 = semaphoreID;
    dualSem.sem2 = semaphoreID2;
    dualSem.port = incoming_port;

    /*#S7*/
    result = pal_osThreadCreateWithAlloc(palNetClientFunc, &dualSem , PAL_osPriorityBelowNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &threadID1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S8*/
    result = pal_listen(g_testSockets[0], 10);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S9*/
    result = pal_osSemaphoreRelease(dualSem.sem1);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);


    TEST_PRINTF("waiting for connection:\r\n");
    /*#S10*/
    result = pal_accept(g_testSockets[0], &address2, &addrlen, &g_testSockets[1]);
    TEST_PRINTF("after accept: %" PRIu32 "\r\n", result);
    if (PAL_SUCCESS != result )
    {
         result = pal_accept(g_testSockets[0], &address2, &addrlen, &g_testSockets[1]);
         TEST_PRINTF("after accept: %" PRIu32 "\r\n",result);
    }
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#S11*/
    result = pal_recv(g_testSockets[1], s_rcv_buffer2, 49, &read);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_PRINTF(s_rcv_buffer2);

    /*#S12*/
    result = pal_send(g_testSockets[1], messageOut, 15, &sent);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);


//cleanup

/*#S13*/
    pal_close(&g_testSockets[1]);
    pal_close(&g_testSockets[0]);

    result = pal_osSemaphoreWait(semaphoreID2, 5000, &tmp);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    pal_osDelay(2000);
       pal_osThreadTerminate(&threadID1);
    result = pal_osSemaphoreDelete(&semaphoreID);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(NULL, semaphoreID);

    result = pal_osSemaphoreDelete(&semaphoreID2);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(NULL, semaphoreID2);
}



PAL_PRIVATE volatile uint32_t s_callbackCounterNonBlock = 0;

PAL_PRIVATE void nonBlockCallback(void * arg)
{
    s_callbackCounterNonBlock++;
}

#define PAL_NET_TEST_HTTP_HEADER_LEN 5

/*! \brief /b nonBlockingAsyncTest checks the asynchronous- nonblocking socket scenario.
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Look up the IP address of the test server using `pal_getAddressInfo`.                        | PAL_SUCCESS |
* | 2 | Create an asynchronous non-blocking TCP socket with `nonBlockCallback` as callback.     | PAL_SUCCESS |
* | 3 | Set the port to test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | 4 | Connect the socket.                                                                    | PAL_SUCCESS or PAL_ERR_SOCKET_IN_PROGRES |
* | 5 | Send a test message to the test server using `pal_send` (repeat until success).           | PAL_SUCCESS or PAL_ERR_SOCKET_IN_PROGRES |
* | 6 | Wait for the callback and receive server response using `pal_recv` (repeat until success). | PAL_SUCCESS or PAL_ERR_SOCKET_WOULD_BLOCK|
* | 7 | Close the socket.                                                                      | PAL_SUCCESS |
*/
TEST(pal_socket, nonBlockingAsyncTest)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    const char* message = "GET / HTTP/1.0\r\nHost:10.45.48.68:8000\r\n\r\n";
    size_t sent = 0;
    char buffer[100] = { 0 };
    size_t read = 0;
    s_callbackcounter = 0;
    palSocketLength_t addrlen = 0;
    int32_t waitIterations = 0;

    /*#1*/
    result = pal_getAddressInfo(PAL_NET_TEST_SERVER_NAME, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

#if PAL_NET_ASYNCHRONOUS_SOCKET_API
    /*#2*/
    result = pal_asynchronousSocketWithArgument(PAL_AF_INET, PAL_SOCK_STREAM, true, 0, nonBlockCallback, "non-blockSocketCallbackArg", &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_SERVER_HTTP_PORT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#4*/
    result = pal_connect(g_testSockets[0], &address, 16);
    if (PAL_ERR_SOCKET_IN_PROGRES == result)
    {
        result = pal_connect(g_testSockets[0], &address, 16);
        if ((result != PAL_SUCCESS) && (result != PAL_ERR_SOCKET_ALREADY_CONNECTED) && (result != PAL_ERR_SOCKET_IN_PROGRES) && (result != PAL_ERR_SOCKET_WOULD_BLOCK)) // check expected result codes.(connection should either be in progress or connected already)
        {
            TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
        }
        pal_osDelay(400);
    }
    else
    {
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    }
    s_callbackCounterNonBlock = 0;

    /*#5*/
    result = pal_send(g_testSockets[0], message, strlen(message), &sent);

    while (PAL_ERR_SOCKET_IN_PROGRES == result)
    {
        pal_osDelay(100);
        result = pal_send(g_testSockets[0], message, strlen(message), &sent);
    }
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#6*/
    result = pal_recv(g_testSockets[0], buffer, PAL_NET_TEST_HTTP_HEADER_LEN, &read); // may block
    while ((PAL_ERR_SOCKET_WOULD_BLOCK == result) && (10 > waitIterations ))
    {
        s_callbackCounterNonBlock = 0;
        while (s_callbackCounterNonBlock == 0)
        {
            waitIterations++;
            pal_osDelay(100);
        }
        result = pal_recv(g_testSockets[0], buffer, PAL_NET_TEST_HTTP_HEADER_LEN, &read); // shouldnt block
    }

    /*#7*/
    pal_close(&g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT(read >= 4);
    TEST_ASSERT(buffer[0] == 'H' && buffer[1] == 'T'&& buffer[2] == 'T' && buffer[3] == 'P');
    TEST_ASSERT(s_callbackCounterNonBlock > 0);

#endif // PAL_NET_ASYNCHRONOUS_SOCKET_API
}

/*! \brief /b tProvUDPTest tests UDP socket send/receive and checks that we get the correct error for receive timeout.
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a blocking UDP socket using `pal_socket`.                                       | PAL_SUCCESS |
* | 2 | Look up the IP address of the test server using `pal_getAddressInfo`.                        | PAL_SUCCESS |
* | 3 | Set the port to test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | 4 | Set socket timeouts using `pal_setSocketOptions`.                                    | PAL_SUCCESS |
* | 5 | Send a test message (short HTTP request) to test the server using `pal_send`.                 | PAL_SUCCESS |
* | 6 | Receive the (blocking) server response using `pal_recv`.                                 | PAL_SUCCESS |
* | 7 | Receive  the (blocking) server response again using `pal_recv` and fail.                  | PAL_ERR_SOCKET_WOULD_BLOCK |
* | 8 | Close the socket.                                                                      | PAL_SUCCESS |
*/
TEST(pal_socket, tProvUDPTest)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0,{0} };
    uint8_t buffer[100] = { 0 };
    uint8_t buffer_dns[33] = { 0x8e, 0xde, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x61, 0x72, 0x73, 0x74, 0x65, 0x63, 0x68, 0x6e, 0x69, 0x63, 0x61, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01 };
    size_t sent = 0;
    size_t read = 0;
    palSocketLength_t addrlen = 16;
    int timeout = 1000;

    /*#1*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#2*/
    result = pal_getAddressInfo(PAL_NET_TEST_SERVER_NAME_UDP, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_SERVER_UDP_PORT);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#4*/
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_SNDTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    timeout = 1000;
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);

    /*#5*/
    result = pal_sendTo(g_testSockets[0], buffer_dns, sizeof(buffer_dns), &address, addrlen, &sent);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(sent, sizeof(buffer_dns));

    /*#6*/
    result = pal_receiveFrom(g_testSockets[0], buffer, 16, NULL, NULL, &read);
    TEST_ASSERT_EQUAL_HEX( PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL(read, 16);

    /*#7*/
    result = pal_receiveFrom(g_testSockets[0], buffer, 100, NULL, NULL, &read); //  should get timeout
    TEST_ASSERT_EQUAL_HEX(result, PAL_ERR_SOCKET_WOULD_BLOCK);

    /*#8*/
    pal_close(&g_testSockets[0]);
}


void socket_event_handler(void* arg)
{

}



#define PAL_COAP_NET_TEST_SERVER_NAME "coap-integration-lab.dev.mbed.com"
#define PAL_COAP_NET_TEST_SERVER_HTTP_PORT 5684
#define WAIT_TIME_ASYNC_SEC 0
#define RETRY_COUNT 10
/*! \brief /b PalTestAPPTestt tests TCP async connection to COAP server.
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a blocking UDP socket using `pal_socket`.                                       | PAL_SUCCESS |
* | 2 | Look up the IP address of the test server using `pal_getAddressInfo`.                        | PAL_SUCCESS |
* | 3 | Set the port to test port in the address structure using `pal_setSockAddrPort`.              | PAL_SUCCESS |
* | 4 | Get the local unit IP using `pal_getSockAddrIPV4Addr`.                                   | PAL_SUCCESS |
* | 5 | Get the number of connected interfaces using `pal_getNumberOfNetInterfaces`.         | PAL_SUCCESS |
* | 6 | Get the interface info using `pal_getNetInterfaceInfo`.                                 | PAL_SUCCESS |
* | 7 | Set the async socket `pal_asynchronousSocket`.                      | PAL_SUCCESS |
* | 8| Connect to the socket.                                                                 | PAL_SUCCESS  Or PAL_ERR_SOCKET_IN_PROGRES|
* | 9| If step 10 failed, check if socket was connected using `pal_socketMiniSelect`.       | PAL_SUCCESS |
* | 10| If mini select passes, try sending data to socket.                               | PAL_SUCCESS |
* | 11 | Close the socket.                                                                     | PAL_SUCCESS |
*/
TEST(pal_socket, PalMiniSelectNoBlockingTcpConnection)
{
    palStatus_t status = PAL_SUCCESS;
    palSocketLength_t _socket_address_len = 0;
    palSocketAddress_t _socket_address = { 0 };
    palSocket_t _socket = 0;
    palIpV4Addr_t  interface_address4 = {0};
    palIpV6Addr_t  interface_address6 = {0};
    uint32_t interface_count;
    palNetInterfaceInfo_t interface_info;
    palSocketAddress_t bind_address;
    pal_timeVal_t zeroTime = {WAIT_TIME_ASYNC_SEC, 0};
    uint32_t socketsSet = 0;
    uint8_t socketStatus[1] = { 0 };
    int i = 0;

    /*#1*/
    memset(&interface_info,0,sizeof(interface_info));
    memset(&bind_address, 0, sizeof(palSocketAddress_t));

    /*#2*/
    status = pal_getAddressInfo(PAL_COAP_NET_TEST_SERVER_NAME, &_socket_address, &_socket_address_len);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#3*/
    status = pal_setSockAddrPort(&_socket_address, PAL_COAP_NET_TEST_SERVER_HTTP_PORT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    /*#4*/
    if (PAL_AF_INET == _socket_address.addressType) //if address is IPV4 extract ipv4 address.
    {
        status = pal_getSockAddrIPV4Addr(&_socket_address, interface_address4);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    else  if (PAL_AF_INET6 == _socket_address.addressType){ // address is IPV6 - extract IPV6 address.
        status = pal_getSockAddrIPV6Addr(&_socket_address, interface_address6);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
    }
    else
    {
        // unexpected address type  ---> error 
        TEST_ASSERT(((PAL_AF_INET6 == _socket_address.addressType) || (PAL_AF_INET == _socket_address.addressType)));
    }

    /*#5*/
    status = pal_getNumberOfNetInterfaces(&interface_count);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#6*/
    status = pal_getNetInterfaceInfo(g_interfaceCTXIndex, &interface_info);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#7*/
    status = pal_asynchronousSocket((palSocketDomain_t)_socket_address.addressType, (palSocketType_t)PAL_SOCK_STREAM, true, (uint32_t)g_interfaceCTXIndex, (palAsyncSocketCallback_t)&socket_event_handler, &_socket);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#8*/
    status = pal_connect(_socket, &_socket_address, _socket_address_len);
    pal_osDelay(300);

    /*#9*/
    if(status != PAL_SUCCESS)
    {
        for(i = 0; i < RETRY_COUNT ; i++)
        {
            status = pal_socketMiniSelect(&_socket, 1, &zeroTime, socketStatus, &socketsSet);
            TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);
            if(socketsSet > 0)
            {
                TEST_ASSERT_EQUAL_HEX(socketsSet >= 1, 1);
                break;
            }
            pal_osDelay(100);
        }
    }
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    /*#10*/
    {
        uint8_t message[] = { 0x16 ,0x03 ,0x01 ,0x00 ,0x79 ,0x01 ,0x00 ,0x00 ,0x75 ,0x03 ,0x03 ,0x59 ,0x11 ,0xae ,0xef ,0x4f
                ,0x6f ,0x5f ,0xd7 ,0x0f ,0x50 ,0x7c ,0x05 ,0x37 ,0xe3 ,0xd9 ,0x47 ,0x82 ,0x8e ,0x64 ,0x75 ,0x3c
                ,0xa4 ,0xca ,0xef ,0x45 ,0x25 ,0x4e ,0x36 ,0xdf ,0x5d ,0xbf ,0x96 ,0x00 ,0x00 ,0x04 ,0xc0 ,0xac
                ,0x00 ,0xff ,0x01 ,0x00 ,0x00 ,0x48 ,0x00 ,0x0d ,0x00 ,0x16 ,0x00 ,0x14 ,0x06 ,0x03 ,0x06 ,0x01
                ,0x05 ,0x03 ,0x05 ,0x01 ,0x04 ,0x03 ,0x04 ,0x01 ,0x03 ,0x03 ,0x03 ,0x01 ,0x02 ,0x03 ,0x02 ,0x01
                ,0x00 ,0x0a ,0x00 ,0x18 ,0x00 ,0x16 ,0x00 ,0x19 ,0x00 ,0x1c ,0x00 ,0x18 ,0x00 ,0x1b ,0x00 ,0x17
                ,0x00 ,0x16 ,0x00 ,0x1a ,0x00 ,0x15 ,0x00 ,0x14 ,0x00 ,0x13 ,0x00 ,0x12 ,0x00 ,0x0b ,0x00 ,0x02
                ,0x01 ,0x00 ,0x00 ,0x16 ,0x00 ,0x00 ,0x00 ,0x17 ,0x00 ,0x00 ,0x00 ,0x23 ,0x00 ,0x00};

        size_t sent = 0;
        for(i = 0; i < RETRY_COUNT; i++)
        {
            status = pal_send(_socket, message, sizeof(message), &sent);
            if(status == PAL_SUCCESS)
            {
                break;
            }
            pal_osDelay(100);
        }
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, status);

    }

    /*#10*/
    pal_close(&_socket);
}

PAL_PRIVATE void fillUDPTestBuffer(pal_udp_test_data_t *data, uint8_t* buffer)
{
    memset(buffer, 0, data->bufferSize);
    data->chunkSize = (data->messageSize - data->totalSize > data->bufferSize) ? data->bufferSize : (data->messageSize - data->totalSize);
    memset(buffer, ++(data->currentValue), data->chunkSize);
    data->totalSize += data->chunkSize;
}

// UDP test sender thread function.
PAL_PRIVATE void socketUDPBufferedTestSender(const void *arg)
{
    palStatus_t result = PAL_SUCCESS;
    pal_udp_test_data_t *data = (pal_udp_test_data_t*)arg; // cast from const to non-const
    size_t sent = 0, totalSent = 0;

    g_testSendBuffer = (uint8_t*)malloc(sizeof(uint8_t) * data->bufferSize);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, g_testSendBuffer);

    data->totalSize = 0;
    data->chunkSize = 0;
    data->currentValue = data->startValue;
    while (totalSent != data->messageSize)
    {
        fillUDPTestBuffer(data, g_testSendBuffer);
        result = pal_sendTo(g_testSockets[0], g_testSendBuffer, data->chunkSize, &(data->interfaceInfo.address), data->interfaceInfo.addressSize, &sent);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
        totalSent += sent;
        pal_osDelay(5); // allow some time for the RX bits to be set
    };

    free(g_testSendBuffer);
    g_testSendBuffer = NULLPTR;
}

/*! \brief Test UDP socket read in chunks
*
* \note The test generates data and calculates its hash, then this data is re-generated from a dedicated thread and 
* received on the current thread which calculates the received data hash and compares it to the original hash
*
* @param[in]    bufSize - the read buffer size
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Initialize the MD context.                                                           | PAL_SUCCESS |
* | 2 | Allocate buffer.                                                                     | PAL_SUCCESS |
* | 3 | Generate data incrementally and update the MD context.                               | PAL_SUCCESS |
* | 4 | Get the hash output size and validate it.                                            | PAL_SUCCESS |
* | 5 | Get the calculated hash.                                                             | PAL_SUCCESS |
* | 6 | Free the MD context resources.                                                       | PAL_SUCCESS |
* | 7 | Get the interface address.                                                           | PAL_SUCCESS |
* | 8 | Create a (blocking) UDP socket.                                                      | PAL_SUCCESS |
* | 9 | Set the socket port and set send/receive timeouts.                                   | PAL_SUCCESS |
* | 10 | Bind the socket.                                                                    | PAL_SUCCESS |
* | 11 | Initialize the MD context.                                                          | PAL_SUCCESS |
* | 12 | Launch the data sender thread.                                                      | PAL_SUCCESS |
* | 13 | Read data from the socket until there's no more data or all data has been received. | PAL_SUCCESS |
* | 14 | Update the MD context.                                                              | PAL_SUCCESS |
* | 15 | Terminate the sending thread.                                                       | PAL_SUCCESS |
* | 16 | Close the socket.                                                                   | PAL_SUCCESS |
* | 17 | Get the hash output size and validate it.                                           | PAL_SUCCESS |
* | 18 | Get the calculated hash and compare it.                                             | PAL_SUCCESS |
* | 19 | Free the MD context resources.                                                      | PAL_SUCCESS |
* | 20 | Free allocated buffer.                                                              | PAL_SUCCESS |
*/
PAL_PRIVATE void socketUDPBuffered(size_t bufSize)
{
    palStatus_t result = PAL_SUCCESS;
    pal_udp_test_data_t data = { PAL_NET_TEST_BUFFERED_UDP_MESSAGE_SIZE, bufSize, 0 };
    uint8_t expectedHash[PAL_SHA256_SIZE] = { 0 }, actualHash[PAL_SHA256_SIZE] = { 0 };
    size_t read = 0, totalRead = 0, hashlen = 0;
    int timeout = 1000;
    palMDHandle_t handle = NULLPTR;
    palThreadID_t thread = NULLPTR;
    
    /*#1*/
    result = pal_mdInit(&handle, PAL_SHA256);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, handle);

    /*#2*/
    g_testRecvBuffer = (uint8_t*)malloc(sizeof(uint8_t) * bufSize);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, g_testRecvBuffer);

    /*#3*/
    data.totalSize = data.chunkSize = 0;
    data.currentValue = data.startValue;
    while (data.totalSize != data.messageSize)
    {
        fillUDPTestBuffer(&data, g_testRecvBuffer);
        result = pal_mdUpdate(handle, g_testRecvBuffer, data.chunkSize);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    };

    /*#4*/
    result = pal_mdGetOutputSize(handle, &hashlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(PAL_SHA256_SIZE, hashlen);

    /*#5*/
    result = pal_mdFinal(handle, expectedHash);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#6*/
    result = pal_mdFree(&handle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#7*/
    memset(&(data.interfaceInfo), 0, sizeof(data.interfaceInfo));
    result = pal_getNetInterfaceInfo(0, &(data.interfaceInfo));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#8*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_DGRAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#9*/
    result = pal_setSockAddrPort(&(data.interfaceInfo.address), PAL_NET_TEST_BUFFERED_UDP_PORT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_SNDTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#10*/
    result = pal_bind(g_testSockets[0], &(data.interfaceInfo.address), data.interfaceInfo.addressSize);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#11*/
    handle = NULLPTR;
    result = pal_mdInit(&handle, PAL_SHA256);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, handle);

    /*#12*/
    result = pal_osThreadCreateWithAlloc(socketUDPBufferedTestSender, &data, PAL_osPriorityNormal, PAL_TEST_THREAD_STACK_SIZE, NULL, &thread);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, thread);

    /*#13*/
    do
    {
        read = 0;
        memset(g_testRecvBuffer, 0, data.bufferSize);
        result = pal_receiveFrom(g_testSockets[0], g_testRecvBuffer, data.bufferSize, &(data.interfaceInfo.address), &(data.interfaceInfo.addressSize), &read);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
        /*#14*/
        result = pal_mdUpdate(handle, g_testRecvBuffer, read);
        TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
        totalRead += read;
    } while (read > 0 && totalRead < data.messageSize);

    /*#15*/
    result = pal_osThreadTerminate(&thread);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#16*/
    result = pal_close(&g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#17*/
    hashlen = 0;
    result = pal_mdGetOutputSize(handle, &hashlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(PAL_SHA256_SIZE, hashlen);

    /*#18*/
    result = pal_mdFinal(handle, actualHash);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_MEMORY(expectedHash, actualHash, PAL_SHA256_SIZE);

    /*#19*/
    result = pal_mdFree(&handle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#20*/
    free(g_testRecvBuffer);
    g_testRecvBuffer = NULLPTR;
}

/*! \brief Test function UDP socket read in small chunks
*
** \test
*/
TEST(pal_socket, socketUDPBufferedSmall)
{
    socketUDPBuffered(PAL_NET_TEST_BUFFERED_UDP_BUF_SIZE_SMALL);
}

/*! \brief Test function UDP socket read in large chunks
*
** \test
*/
TEST(pal_socket, socketUDPBufferedLarge)
{
    socketUDPBuffered(PAL_NET_TEST_BUFFERED_UDP_BUF_SIZE_LARGE);
}

#ifdef __LINUX__ // Linux CI tests for socketTCPBufferedSmall & socketTCPBufferedLarge must use an ipv4 address in order to connect to the external host
PAL_PRIVATE palStatus_t getAddressInfoIPv4(char const *url, palSocketAddress_t *address, palSocketLength_t* addressLength)
{
    struct addrinfo *info = NULLPTR;
    struct addrinfo hints = { 0 };
    struct sockaddr_in *sockAddress = NULLPTR;
    palIpV4Addr_t ipV4Address = { 0 };
    int ret;
    palStatus_t result;

    hints.ai_family = AF_INET;
    ret = getaddrinfo(url, NULL, &hints, &info);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, info);
    TEST_ASSERT_EQUAL(AF_INET, info->ai_family);

    sockAddress = (struct sockaddr_in*)info->ai_addr;
    memcpy(ipV4Address, &(sockAddress->sin_addr), PAL_IPV4_ADDRESS_SIZE);
    freeaddrinfo(info);

    result = pal_setSockAddrIPV4Addr(address, ipV4Address);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    *addressLength = sizeof(struct sockaddr_in);
    return result;
}
#endif

/*! \brief Test TCP socket read in chunks
*
* \note The test attempts to perform an HTTP get request to a google (jquery) CDN, read the file in chunks (ignoring HTTP headers) and compare its hash to a pre-known hash using SHA256.
*
* @param[in]    bufSize - the read buffer size
*
** \test
* | # |    Step                        |   Expected  |
* |---|--------------------------------|-------------|
* | 1 | Create a (blocking) TCP socket.                                                                     | PAL_SUCCESS |
* | 2 | Look up the IP address of the CDN server.                                                           | PAL_SUCCESS |
* | 3 | Set the port to the CDN server's HTTP port and set send/receive timeouts.                           | PAL_SUCCESS |
* | 4 | Connect the socket to the CDN server.                                                               | PAL_SUCCESS |
* | 5 | Send an HTTP get request to the CDN server.                                                         | PAL_SUCCESS |
* | 6 | Initialize the MD context.                                                                          | PAL_SUCCESS |
* | 7 | Allocate HTTP response buffer.                                                                      | PAL_SUCCESS |
* | 8 | Read the server's response until there's no more data to read.                                      | PAL_SUCCESS |
* | 9 | If we're done dealing with the HTTP headers then update the MD context.                             | PAL_SUCCESS |
* | 10 | Locate the end of the HTTP headers in the server's response (HTTP headers end with a double CRLF). | PAL_SUCCESS |
* | 11 | Update the MD context.                                                                             | PAL_SUCCESS |
* | 12 | Close the socket.                                                                                  | PAL_SUCCESS |
* | 13 | Get the hash output size and validate it.                                                          | PAL_SUCCESS |
* | 14 | Get the calculated hash and compare it to the pre-known hash.                                      | PAL_SUCCESS |
* | 15 | Free the MD context resources.                                                                     | PAL_SUCCESS |
* | 16 | Free HTTP response buffer.                                                                         | PAL_SUCCESS |
*/
PAL_PRIVATE void socketTCPBuffered(size_t bufSize)
{
    palStatus_t result = PAL_SUCCESS;
    palSocketAddress_t address = { 0 };
    palSocketLength_t addrlen = 0;
    int timeout = 5000;
    uint8_t next = '\r', state = 0;
    size_t read = 0, sent = 0, hashlen = 0;
    bool body = false;
    palMDHandle_t handle = NULLPTR;
    uint8_t actualHash[PAL_SHA256_SIZE] = { 0 };
    const uint8_t expectedHash[] = // pre-calculated jquery.js 3.2.1 SHA256
    {
        0x0d, 0x90, 0x27, 0x28, 0x9f, 0xfa, 0x5d, 0x9f, 0x6c, 0x8b, 0x4e, 0x07, 0x82, 0xbb, 0x31, 0xbb, 
        0xff, 0x2c, 0xef, 0x5e, 0xe3, 0x70, 0x8c, 0xcb, 0xcb, 0x7a, 0x22, 0xdf, 0x91, 0x28, 0xbb, 0x21
    };
      
    /*#1*/
    result = pal_socket(PAL_AF_INET, PAL_SOCK_STREAM, false, 0, &g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#2*/    
    result = test_getAddressInfo(PAL_NET_TEST_GOOGLE_CDN_HOST, &address, &addrlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#3*/
    result = pal_setSockAddrPort(&address, PAL_NET_TEST_GOOGLE_CDN_HOST_PORT);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_SNDTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    result = pal_setSocketOptions(g_testSockets[0], PAL_SO_RCVTIMEO, &timeout, sizeof(timeout));
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#4*/
    result = pal_connect(g_testSockets[0], &address, addrlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#5*/
    result = pal_send(g_testSockets[0], PAL_NET_TEST_GOOGLE_CDN_REQUEST, sizeof(PAL_NET_TEST_GOOGLE_CDN_REQUEST) - 1, &sent);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#6*/
    result = pal_mdInit(&handle, PAL_SHA256);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, handle);
    
    /*#7*/
    g_testRecvBuffer = (uint8_t*)malloc(sizeof(uint8_t) * bufSize + 1);
    TEST_ASSERT_NOT_EQUAL(NULLPTR, g_testRecvBuffer);
    
    /*#8*/
    do
    {
        read = 0;
        memset(g_testRecvBuffer, 0, bufSize + 1);
        result = pal_recv(g_testSockets[0], g_testRecvBuffer, bufSize, &read);
        TEST_ASSERT_TRUE((PAL_SUCCESS == result && read > 0) || (PAL_ERR_SOCKET_CONNECTION_CLOSED == result && read == 0));

        /*#9*/
        if (body)
        {
            result = pal_mdUpdate(handle, g_testRecvBuffer, read);
            TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
            continue;
        }

        /*#10*/
        for (size_t i = 0; i < bufSize; i++) // dealing with the HTTP headers - headers end on a double CRLF
        {
            if (g_testRecvBuffer[i] == next)
            {
                next = (next == '\r') ? '\n' : '\r';
                state = state | (state + 1);
                if (state == 0xf)
                {
                    /*#11*/
                    body = true;
                    result = pal_mdUpdate(handle, (g_testRecvBuffer + i + 1), strlen(((char*)g_testRecvBuffer) + i + 1));
                    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
                    break;
                }
            }
            else if (state != 0)
            {
                next = '\r';
                state = 0;
            }
        }
    } while (read > 0);

    /*#12*/
    result = pal_close(&g_testSockets[0]);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);

    /*#13*/
    result = pal_mdGetOutputSize(handle, &hashlen);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_HEX(PAL_SHA256_SIZE, hashlen);

    /*#14*/
    result = pal_mdFinal(handle, actualHash);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);
    TEST_ASSERT_EQUAL_MEMORY(expectedHash, actualHash, PAL_SHA256_SIZE);

    /*#15*/
    result = pal_mdFree(&handle);
    TEST_ASSERT_EQUAL_HEX(PAL_SUCCESS, result);    

    /*#16*/
    free(g_testRecvBuffer);
    g_testRecvBuffer = NULLPTR;
}

/*! \brief Test function TCP socket read in small chunks
*
** \test
*/
TEST(pal_socket, socketTCPBufferedSmall)
{
    socketTCPBuffered(PAL_NET_TEST_BUFFERED_TCP_BUF_SIZE_SMALL);
}

/*! \brief Test function TCP socket read in large chunks
*
** \test
*/
TEST(pal_socket, socketTCPBufferedLarge)
{
    socketTCPBuffered(PAL_NET_TEST_BUFFERED_TCP_BUF_SIZE_LARGE);
}
