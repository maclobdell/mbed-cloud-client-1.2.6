#ifndef MBED_CLIENT_PAL_TEST_MAINTEST_H_
#define MBED_CLIENT_PAL_TEST_MAINTEST_H_
#include "pal.h"
#include "pal_rtos.h"



#ifndef PAL_TEST_RTOS
#define PAL_TEST_RTOS 0
#endif // PAL_TEST_RTOS

#ifndef PAL_TEST_NETWORK
#define PAL_TEST_NETWORK 0
#endif // PAL_TEST_NETWORK

#ifndef PAL_TEST_TLS
#define PAL_TEST_TLS 0
#endif // PAL_TEST_TLS

#ifndef PAL_TEST_CRYPTO
#define PAL_TEST_CRYPTO 0
#endif // PAL_TEST_CRYPTO

#ifndef PAL_TEST_FS
#define PAL_TEST_FS 0
#endif // PAL_TEST_FS

#ifndef PAL_TEST_UPDATE
#define PAL_TEST_UPDATE 0
#endif // PAL_TEST_UPDATE

#ifndef PAL_TEST_FLASH
#define PAL_TEST_FLASH 1
#endif // PAL_TEST_FLASH

#define TEST_PRINTF(ARGS...) PAL_PRINTF(ARGS)

#ifdef PAL_LINUX
#define PAL_TEST_THREAD_STACK_SIZE 16*1024*sizeof(uint32_t)
#else
#define PAL_TEST_THREAD_STACK_SIZE 512*sizeof(uint32_t)
#endif


typedef struct {
	int argc;
	char **argv;
} pal_args_t;


#ifdef __cplusplus
extern "C" {
#endif


typedef void (*testMain_t)(pal_args_t *);
void palTestMain(pal_args_t * args);

/*! \brief This function initialized the platform (BSP , file system ....)
*
* @param None
*
* \return void
*
*/
bool initPlatform(void);


/*! \brief This function is called from the main function
*			and calls the startup sequence for the board tests
*
* @param[in] mainTestFunc - callback function for the main test runner
* @param[in] args - structure the contains argv and argc received from the main function
*
* \return void
*
*/
bool runProgram(testMain_t mainTestFunc, pal_args_t * args);


#if 1
    void TEST_pal_rtos_GROUP_RUNNER(void);
#endif

#if PAL_TEST_NETWORK
    void TEST_pal_socket_GROUP_RUNNER(void);
#endif

#if PAL_TEST_TLS
    void TEST_pal_tls_GROUP_RUNNER(void);
#endif

#if PAL_TEST_CRYPTO
    void TEST_pal_crypto_GROUP_RUNNER(void);
#endif

#if PAL_TEST_FS
    void TEST_pal_fileSystem_GROUP_RUNNER(void);
#endif

#if PAL_TEST_UPDATE
    void TEST_pal_update_GROUP_RUNNER(void);
#endif

#if PAL_TEST_FLASH
  void TEST_pal_internalFlash_GROUP_RUNNER(void);
#endif


#ifdef __cplusplus
}
#endif

#endif /* MBED_CLIENT_PAL_TEST_MAINTEST_H_ */
