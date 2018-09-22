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
#include "fsl_debug_console.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lwip/sys.h"

#ifndef PAL_TEST_MAIN_THREAD_STACK_SIZE
#define PAL_TEST_MAIN_THREAD_STACK_SIZE (1024*8)
#endif

bool runProgram(testMain_t mainTestFunc, pal_args_t * args)
{
    //Init Unit testing thread
	xTaskCreate((TaskFunction_t)mainTestFunc, "unity_main", (uint16_t)PAL_TEST_THREAD_STACK_SIZE, args, tskIDLE_PRIORITY + 1, NULL);

	//Start OS
	vTaskStartScheduler();

	return true;
}


bool initPlatform(void)
{
	//Init Board
	boardInit();

	//Init FileSystem
	xTaskCreate((TaskFunction_t)fileSystemMountDrive, "FileSystemInit", 1024*4, NULL, tskIDLE_PRIORITY + 3, NULL);

	//Init DHCP thread
    sys_thread_new("networkInit", networkInit, NULL, 1024, tskIDLE_PRIORITY + 2);

	return true;
}


#ifndef __CC_ARM          /* ARM Compiler */
/*This is a Hardfault handler to use in debug for more info please read -
 * http://www.freertos.org/Debugging-Hard-Faults-On-Cortex-M-Microcontrollers.html */
/* The prototype shows it is a naked function - in effect this is just an
assembly function. */
void HardFault_Handler( void ) __attribute__( ( naked ) );

/* The fault handler implementation calls a function called
prvGetRegistersFromStack(). */
void HardFault_Handler(void)
{
    __asm volatile
    (
        " tst lr, #4                                                \n"
        " ite eq                                                    \n"
        " mrseq r0, msp                                             \n"
        " mrsne r0, psp                                             \n"
        " ldr r1, [r0, #24]                                         \n"
        " ldr r2, handler2_address_const                            \n"
        " bx r2                                                     \n"
        " handler2_address_const: .word prvGetRegistersFromStack    \n"
    );
}


void prvGetRegistersFromStack( uint32_t *pulFaultStackAddress )
{
/* These are volatile to try and prevent the compiler/linker optimising them
away as the variables never actually get used.  If the debugger won't show the
values of the variables, make them global my moving their declaration outside
of this function. */
volatile uint32_t r0;
volatile uint32_t r1;
volatile uint32_t r2;
volatile uint32_t r3;
volatile uint32_t r12;
volatile uint32_t lr; /* Link register. */
volatile uint32_t pc; /* Program counter. */
volatile uint32_t psr;/* Program status register. */

    r0 = pulFaultStackAddress[ 0 ];
    r1 = pulFaultStackAddress[ 1 ];
    r2 = pulFaultStackAddress[ 2 ];
    r3 = pulFaultStackAddress[ 3 ];

    r12 = pulFaultStackAddress[ 4 ];
    lr = pulFaultStackAddress[ 5 ];
    pc = pulFaultStackAddress[ 6 ];
    psr = pulFaultStackAddress[ 7 ];

    /* When the following line is hit, the variables contain the register values. */
    for( ;; );
}

#endif
// This is used by unity for output. The make file must pass a definition of the following form
// -DUNITY_OUTPUT_CHAR=unity_output_char
void unity_output_char(int c)
{
	PUTCHAR(c);
}


