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

#include "mbed.h"

#ifndef PAL_TEST_K64F_BAUD_RATE
#define PAL_TEST_K64F_BAUD_RATE 115200
#endif

#ifndef PAL_TEST_MAIN_THREAD_STACK_SIZE
#define PAL_TEST_MAIN_THREAD_STACK_SIZE (1024*7)
#endif


extern int initSDcardAndFileSystem(void);

Serial pc(USBTX, USBRX);

bool runProgram(testMain_t func, pal_args_t * args)
{
	Thread thread(osPriorityNormal, PAL_TEST_MAIN_THREAD_STACK_SIZE);
	thread.start(callback(func, args));
	wait(1); // to be on the safe side - sleep for 1sec
	bool result = (thread.join() == osOK);
	return result;
}


bool initPlatform(void)
{ 
	pc.baud (PAL_TEST_K64F_BAUD_RATE);
	int err = initSDcardAndFileSystem();
    if (err < 0)
	{
        return false;
    }
	return true;
}
