#include "pal.h"
#define PAL_DEVICE_KEY_SIZE_IN_BYTES 16

//THIS CODE IS FOR TESTING PURPOSES ONLY. DO NOT USE IN PRODUCTION ENVIRONMENTS. REPLACE WITH A PROPER IMPLEMENTATION BEFORE USE
palStatus_t  __attribute__((weak)) pal_plat_osGetRoT128Bit(uint8_t *keyBuf, size_t keyLenBytes)

{
    #if defined (__CC_ARM)          /* ARM compiler. */
    #warning("PAL_INSECURE- You are using insecure Root Of Trust implementation, DO NOT USE IN PRODUCTION ENVIRONMENTS. REPLACE WITH A PROPER IMPLEMENTATION BEFORE USE")
    #else
    #pragma message ("You are using insecure Root Of Trust implementation, DO NOT USE IN PRODUCTION ENVIRONMENTS. REPLACE WITH A PROPER IMPLEMENTATION BEFORE USE")
    #endif

	PAL_LOG(WARN, "You are using insecure Root Of Trust implementation");

    if (keyLenBytes < PAL_DEVICE_KEY_SIZE_IN_BYTES)
	{
		return PAL_ERR_BUFFER_TOO_SMALL;
	}

	if (NULL == keyBuf)
	{
        return PAL_ERR_NULL_POINTER;
	}

    for (int i=0; i < PAL_DEVICE_KEY_SIZE_IN_BYTES; i++)
    {
        keyBuf[i] = i;
    }
	return PAL_SUCCESS;
}
