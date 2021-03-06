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

#ifndef _PAL_RTOS_TEST_UTILS_H
#define _PAL_RTOS_TEST_UTILS_H

#include "pal_types.h"
#include "pal_rtos.h"
#include "pal_test_main.h"
#if 0    //MUST MOVE TO PLATFORM SPECIFIC HEADER
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"

#include "pin_mux.h"
#include "clock_config.h"


#define MUTEX_UNITY_TEST 1
#define SEMAPHORE_UNITY_TEST 1
#endif   // MUST MOVE TO PLATFORM SPECIFIC HEADER
#define PAL_RANDOM_TEST_LOOP 100000
#define PAL_RANDOM_ARRAY_TEST_SIZE 100
#define PAL_RANDOM_BUFFER_ARRAY_TEST_SIZE 60
#define PAL_TIME_TO_WAIT_MS	5000 //in [ms]
#define PAL_TIME_TO_WAIT_SHORT_MS	300 //in [ms]
#define PAL_TIMER_TEST_TIME_TO_WAIT_MS_SHORT 40 //in [ms]
#define PAL_TIMER_TEST_TIME_TO_WAIT_MS_LONG 130 //in [ms]

typedef struct threadsArgument{
    uint32_t arg1;
    uint32_t arg2;
    uint32_t arg3;
    uint32_t arg4;
    uint32_t arg5;
    uint32_t arg6;
    uint32_t arg7;
	uint8_t threadCounter;
}threadsArgument_t;


extern threadsArgument_t g_threadsArg;

extern palThreadLocalStore_t g_threadStorage;

void palThreadFunc1(void const *argument);
void palThreadFunc2(void const *argument);
void palThreadFunc3(void const *argument);
void palThreadFunc4(void const *argument);
void palThreadFunc5(void const *argument);
void palThreadFunc6(void const *argument);


typedef struct timerArgument{
    uint32_t ticksBeforeTimer;
    uint32_t ticksInFunc1;
    uint32_t ticksInFunc2;
}timerArgument_t;

extern timerArgument_t g_timerArgs;

void palTimerFunc1(void const *argument);
void palTimerFunc2(void const *argument);
void palTimerFunc3(void const *argument);
void palTimerFunc4(void const *argument);
void palTimerFunc5(void const *argument);


void palThreadFuncCustom1(void const *argument);
void palThreadFuncCustom2(void const *argument);
void palThreadFuncCustom3(void const *argument);
void palThreadFuncCustom4(void const *argument);
void palThreadFuncWaitForEverTest(void const *argument);

void RecursiveLockThread(void const *param);
typedef struct palRecursiveMutexParam{
	palMutexID_t mtx;
	palSemaphoreID_t sem;
	size_t count;
    palThreadID_t higherPriorityThread;
    palThreadID_t lowerPriorityThread;
    palThreadID_t activeThread;
} palRecursiveMutexParam_t;

#define MEMORY_POOL1_BLOCK_SIZE 32
#define MEMORY_POOL1_BLOCK_COUNT 5
#define MEMORY_POOL2_BLOCK_SIZE 12
#define MEMORY_POOL2_BLOCK_COUNT 4

extern palMutexID_t mutex1;
extern palMutexID_t mutex2;

extern palSemaphoreID_t semaphore1;

#endif //_PAL_RTOS_TEST_UTILS_H
