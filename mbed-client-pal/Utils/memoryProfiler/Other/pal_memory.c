/*
 * pal_memory.c
 *
 *  Created on: Jun 26, 2017
 *      Author: pal
 */
#ifdef PAL_MEMORY_STATISTICS
#include "stdio.h"
#include "mbed-trace/mbed_trace.h"


#define TRACE_GROUP "PAL_MEMORY"

#define SMALL_BUCKET	32
#define LARGE_BUCKET	4096

typedef enum _memoryBucketSizes
{
	PAL_BUCKET_SIZE_32,
	PAL_BUCKET_SIZE_64,
	PAL_BUCKET_SIZE_128,
	PAL_BUCKET_SIZE_256,
	PAL_BUCKET_SIZE_512,
	PAL_BUCKET_SIZE_1024,
	PAL_BUCKET_SIZE_2048,
	PAL_BUCKET_SIZE_4096,
	PAL_BUCKET_SIZE_LARGE,
	PAL_BUCKET_NUMBER
}memoryBucketSizes;

typedef struct _memoryAllocationData
{
	int32_t totalsize;
	int32_t waterMark;
	int32_t buckets[PAL_BUCKET_NUMBER];
	int32_t waterMarkBuckets[PAL_BUCKET_NUMBER];
}memoryAllocationData;

static memoryAllocationData memoryStats = {0};


static inline memoryBucketSizes getBucketNumber(size_t size)
{
	if (size <= SMALL_BUCKET)
	{
		return PAL_BUCKET_SIZE_32;
	}
	if (size >= LARGE_BUCKET)
	{
		return PAL_BUCKET_SIZE_LARGE;
	}

	uint8_t bucket = 1;
	uint32_t power = 64; // Starting with 32
	while (power < size)
	{
		bucket++;
		power*=2;
	}
	return bucket;
}


void* __wrap_malloc(size_t c)
{
	 void *ptr = __real_malloc(c + sizeof(size_t) + sizeof(size_t));
	if (ptr == NULL)
	{
		return NULL;
	}
	 int32_t currentTotal = pal_osAtomicIncrement((&memoryStats.totalsize),c);
	if (currentTotal > memoryStats.waterMark)
	{
		memoryStats.waterMark = currentTotal; // need to make this thread safe
	}

	*(size_t*)ptr = c;
	ptr = ((size_t*)ptr+1);
	*(size_t*)ptr = (size_t)getBucketNumber(c);
	 int32_t currentBucketTotal = pal_osAtomicIncrement(&(memoryStats.buckets[*(size_t*)ptr]),1);
	if (memoryStats.waterMarkBuckets[*(size_t*)ptr] < currentBucketTotal)
	{
		memoryStats.waterMarkBuckets[*(size_t*)ptr] = currentBucketTotal;
	}
	ptr = ((size_t*)ptr + 1);
	return ptr;
}


void __wrap_free(void* ptr)
{
	if (NULL == ptr)
	{
		return;
	}
	ptr = ((size_t*)ptr-1);
	pal_osAtomicIncrement(&(memoryStats.buckets[*(size_t*)ptr]),-1);
	ptr = ((size_t*)ptr-1);
	pal_osAtomicIncrement((&memoryStats.totalsize),-1*(*(size_t*)ptr));
	__real_free(ptr);
}


void* __wrap_calloc(size_t num, size_t size)
{
	void* ptr = __wrap_malloc(num*size);
	if (NULL != ptr)
	{
		memset(ptr,0,(num*size));
	}
	return (ptr);
}


void printMemoryStats(void)
{
	tr_info("\n*******************************************************\r\n");
	tr_info("water mark size = %ld\r\n",memoryStats.waterMark);
	tr_info("total size = %ld\r\n",memoryStats.totalsize);
	tr_info("bucket 32    allocation number %ld\r\n",memoryStats.buckets[0]);
	tr_info("bucket 64    allocation number %ld\r\n",memoryStats.buckets[1]);
	tr_info("bucket 128   allocation number %ld\r\n",memoryStats.buckets[2]);
	tr_info("bucket 258   allocation number %ld\r\n",memoryStats.buckets[3]);
	tr_info("bucket 512   allocation number %ld\r\n",memoryStats.buckets[4]);
	tr_info("bucket 1024  allocation number %ld\r\n",memoryStats.buckets[5]);
	tr_info("bucket 2048  allocation number %ld\r\n",memoryStats.buckets[6]);
	tr_info("bucket 4096  allocation number %ld\r\n",memoryStats.buckets[7]);
	tr_info("bucket large allocation number %ld\r\n",memoryStats.buckets[8]);


	tr_info("water mark bucket 32    allocation number %ld\r\n",memoryStats.waterMarkBuckets[0]);
	tr_info("water mark bucket 64    allocation number %ld\r\n",memoryStats.waterMarkBuckets[1]);
	tr_info("water mark bucket 128   allocation number %ld\r\n",memoryStats.waterMarkBuckets[2]);
	tr_info("water mark bucket 256   allocation number %ld\r\n",memoryStats.waterMarkBuckets[3]);
	tr_info("water mark bucket 512   allocation number %ld\r\n",memoryStats.waterMarkBuckets[4]);
	tr_info("water mark bucket 1024  allocation number %ld\r\n",memoryStats.waterMarkBuckets[5]);
	tr_info("water mark bucket 2048  allocation number %ld\r\n",memoryStats.waterMarkBuckets[6]);
	tr_info("water mark bucket 4096  allocation number %ld\r\n",memoryStats.waterMarkBuckets[7]);
	tr_info("water mark bucket large allocation number %ld\r\n",memoryStats.waterMarkBuckets[8]);
	tr_info("*******************************************************\r\n");
}
#endif
