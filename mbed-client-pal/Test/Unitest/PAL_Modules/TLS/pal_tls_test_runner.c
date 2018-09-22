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

TEST_GROUP_RUNNER(pal_tls)
{
  RUN_TEST_CASE(pal_tls, tlsConfiguration);
  RUN_TEST_CASE(pal_tls, tlsInitTLS);
  RUN_TEST_CASE(pal_tls, tlsPrivateAndPublicKeys);
  RUN_TEST_CASE(pal_tls, tlsCACertandPSK);
	RUN_TEST_CASE(pal_tls, tlsHandshakeUDPTimeOut);
  RUN_TEST_CASE(pal_tls, tlsHandshakeTCP_nonBlocking);  
  RUN_TEST_CASE(pal_tls, tlsHandshakeTCP);  
  RUN_TEST_CASE(pal_tls, tlsHandshakeUDP); 
  RUN_TEST_CASE(pal_tls, tlsHandshakeUDP_NonBlocking); 
}


