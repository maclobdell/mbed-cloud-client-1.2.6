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
#include "PlatIncludes.h"
#include "pal_test_main.h"
#include "unity_fixture.h"

extern bool dhcp_done;
void TEST_pal_all_GROUPS_RUNNER(void);

void palTestMain(pal_args_t *args)
{
	const char * myargv[] = {"app","-v"};

	while (dhcp_done == 0)
	{
		pal_osDelay(1000);
	}
	UnityPrint("*****PAL_TEST_START*****");
    UNITY_PRINT_EOL();
	UnityMain(sizeof(myargv)/sizeof(myargv[0]), myargv, TEST_pal_all_GROUPS_RUNNER);
	UnityPrint("*****PAL_TEST_END*****");
    UNITY_PRINT_EOL();
}


void TEST_pal_all_GROUPS_RUNNER(void)
{
	PRINT_MEMORY_STATS
#if PAL_TEST_RTOS
	TEST_pal_rtos_GROUP_RUNNER();
#endif
	PRINT_MEMORY_STATS
#if PAL_TEST_NETWORK
	TEST_pal_socket_GROUP_RUNNER();
#endif
	PRINT_MEMORY_STATS
#if PAL_TEST_CRYPTO
	TEST_pal_crypto_GROUP_RUNNER();
#endif

#if PAL_TEST_FS
	TEST_pal_fileSystem_GROUP_RUNNER();
#endif

#if PAL_TEST_UPDATE
	TEST_pal_update_GROUP_RUNNER();
#endif

#if PAL_TEST_TLS
	TEST_pal_tls_GROUP_RUNNER();
#endif
	PRINT_MEMORY_STATS

#if PAL_TEST_FLASH
	TEST_pal_internalFlash_GROUP_RUNNER();
#endif

}

