/*
 * pal_memory.c
 *
 *  Created on: Jun 26, 2017
 *      Author: pal
 */

#ifdef PAL_MEMORY_STATISTICS
#include "stdio.h"
#include "mbed-trace/mbed_trace.h"
#include "mbed_stats.h"

TRACE_GROUP "PAL_MEMORY"
void printMemoryStats(void)
{
	mbed_stats_heap_t heap_stats;
	mbed_stats_heap_get(&heap_stats);

	mbed_stats_stack_t stack_stats;
	mbed_stats_stack_get(&stack_stats);
	tr_info("--- heap stats ---\n");

	tr_info("heap max size: %ld\n", heap_stats.max_size);
	tr_info("heap reserved size: %ld\n", heap_stats.reserved_size);
	tr_info("heap alloc cnt: %ld\n", heap_stats.alloc_cnt);
	tr_info("heap alloc fail cnt: %ld\n", heap_stats.alloc_fail_cnt);

	tr_info("--- stack stats ---\n");
	tr_info("stack max size: %ld\n", stack_stats.max_size);
	tr_info("stack reserved size: %ld\n", stack_stats.reserved_size);
	tr_info("stack stack cnt: %ld\n", stack_stats.stack_cnt);

}
#endif
