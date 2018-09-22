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

#include "PlatIncludes.h"
#include "pal_test_main.h"


#include "mbed_trace.h"


pal_args_t g_args; // defiend as global so it could persist 
                   // during task execution on FreeRTOS


#ifdef DEBUG
#define	PAL_TESTS_LOG_LEVEL ((uint8_t)((TRACE_MASK_LEVEL & TRACE_ACTIVE_LEVEL_ALL) | (TRACE_MASK_CONFIG & TRACE_CARRIAGE_RETURN)))
#else
#define	PAL_TESTS_LOG_LEVEL ((uint8_t)((TRACE_MASK_LEVEL & TRACE_ACTIVE_LEVEL_ERROR) | (TRACE_MASK_CONFIG & TRACE_CARRIAGE_RETURN)))
#endif


int main(int argc, char * argv[])
{
	bool ret = false;
	g_args.argc = argc;
	g_args.argv = argv;

	mbed_trace_init();
	mbed_trace_config_set(PAL_TESTS_LOG_LEVEL);

	ret = initPlatform();
	if(ret == true)
	{
		ret = runProgram(&palTestMain, &g_args);
	}
	mbed_trace_free();
	return (int)ret;
}
