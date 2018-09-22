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
#include "pal_plat_TLS.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "stdlib.h"
#include "string.h"

#define SSL_LIB_SUCCESS 0
PAL_PRIVATE PAL_INLINE palStatus_t translateTLSErrToPALError(int32_t error) 
{
	palStatus_t status;
	switch(error) 
	{
		case SSL_LIB_SUCCESS: 
			status = PAL_ERR_END_OF_FILE;
			break;
		case MBEDTLS_ERR_SSL_WANT_READ:
			status = PAL_ERR_TLS_WANT_READ;
			break;
		case MBEDTLS_ERR_SSL_WANT_WRITE:
			status = PAL_ERR_TLS_WANT_WRITE;
			break;
		case MBEDTLS_ERR_SSL_TIMEOUT:
			status = PAL_ERR_TIMEOUT_EXPIRED;
			break;
		case MBEDTLS_ERR_SSL_BAD_INPUT_DATA:
			status = PAL_ERR_TLS_BAD_INPUT_DATA;
			break;
		case MBEDTLS_ERR_SSL_CLIENT_RECONNECT:
			status = PAL_ERR_TLS_CLIENT_RECONNECT;
			break;
		case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
			status = PAL_ERR_TLS_PEER_CLOSE_NOTIFY;
			break;
		case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
			status = PAL_ERR_X509_CERT_VERIFY_FAILED;
			break;
		default:
			{
				status = PAL_ERR_GENERIC_FAILURE;
			}
	};
	return status;

}
	

#if defined(MBEDTLS_DEBUG_C)
//! Add forward declaration for the function from mbedTLS
void mbedtls_debug_set_threshold( int threshold );
#endif

typedef mbedtls_ssl_context platTlsContext;
typedef mbedtls_ssl_config platTlsConfiguraionContext;

PAL_PRIVATE mbedtls_entropy_context *g_entropy = NULL;
PAL_PRIVATE bool g_entropyInitiated = false;

typedef struct palTimingDelayContext
{
	uint64_t						start_ticks;
	uint32_t						int_ms;
	uint32_t						fin_ms;
} palTimingDelayContext_t;

//! the full structures will be defined later in the implemetation.
typedef struct palTLSConf{
	platTlsConfiguraionContext*  confCtx;
	palTLSSocketHandle_t palIOCtx; // which will be used as bio context for mbedTLS
	uint32_t tlsIndex; // to help us to get the index of the containing palTLS_t in the array. will be updated in the init
					   // maybe we need to make this an array, since index can be shared for more than one TLS context
	mbedtls_ctr_drbg_context ctrDrbg;
	palTimingDelayContext_t timerCtx;
	mbedtls_x509_crt owncert;
	mbedtls_pk_context pkey;
	mbedtls_x509_crt cacert;  
	bool hasKeys;
	bool hasChain;
	int cipherSuites[PAL_MAX_ALLOWED_CIPHER_SUITES+1];  // The +1 is for the Zero Termination required by mbedTLS
}palTLSConf_t;

//! the full structures will be defined later in the implemetation.
typedef struct palTLS{
	platTlsContext tlsCtx;
	palTLSConf_t* palConfCtx;
	bool tlsInit;
	uint32_t tlsIndex;
	char* psk; //NULL terminated
	char* identity; //NULL terminated
	bool wantReadOrWrite;
}palTLS_t;

//! Forward declaration
PAL_PRIVATE int palBIORecv_timeout(palTLSSocketHandle_t socket, unsigned char *buf, size_t len, uint32_t timeout);
PAL_PRIVATE int palBIORecv(palTLSSocketHandle_t socket, unsigned char *buf, size_t len);
PAL_PRIVATE int palBIOSend(palTLSSocketHandle_t socket, const unsigned char *buf, size_t len);
PAL_PRIVATE void palDebug(void *ctx, int debugLevel, const char *fileName, int line, const char *message);
int pal_plat_entropySourceTLS( void *data, unsigned char *output, size_t len, size_t *olen );
PAL_PRIVATE int palTimingGetDelay( void *data );
PAL_PRIVATE void palTimingSetDelay( void *data, uint32_t intMs, uint32_t finMs );
//! This is the array to hold the TLS context
PAL_PRIVATE palTLS_t *g_palTLSContext = NULL;

palStatus_t pal_plat_initTLSLibrary(void)
{
	palStatus_t status = PAL_SUCCESS;

	g_palTLSContext = NULL;

	g_entropy = (mbedtls_entropy_context*)malloc(sizeof(mbedtls_entropy_context));
	if (NULL == g_entropy)
	{
		status = PAL_ERR_NO_MEMORY;
	}
	else
	{
		mbedtls_entropy_init(g_entropy);
		g_entropyInitiated = false;
	}

	return status;
}


palStatus_t pal_plat_cleanupTLS(void)
{
	mbedtls_entropy_free(g_entropy);
	g_entropyInitiated = false;
	free(g_entropy);
	g_entropy = NULL;
	if (g_palTLSContext)
	{
		free((void*)g_palTLSContext);
		g_palTLSContext = NULL;
	}
	return PAL_SUCCESS;
}


palStatus_t pal_plat_addEntropySource(palEntropySource_f entropyCallback)
{
	palStatus_t status = PAL_SUCCESS;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULL == entropyCallback)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	if (!g_entropyInitiated)
	{
		platStatus = mbedtls_entropy_add_source(g_entropy, entropyCallback, NULL, PAL_INITIAL_RANDOM_SIZE, MBEDTLS_ENTROPY_SOURCE_STRONG );
		if (SSL_LIB_SUCCESS != platStatus)
		{
			status = PAL_ERR_TLS_CONFIG_INIT;
		}
		else
		{
			g_entropyInitiated = true;
		}
		
	}

	return status;
}


palStatus_t pal_plat_initTLSConf(palTLSConfHandle_t* palConfCtx, palTLSTransportMode_t transportVersion, palDTLSSide_t methodType)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = NULL;
	int32_t platStatus = SSL_LIB_SUCCESS;
	int32_t endpoint = 0;
	int32_t transport = 0;

	if (NULLPTR == palConfCtx)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	localConfigCtx = (palTLSConf_t*)malloc(sizeof(palTLSConf_t));
	if (NULL == localConfigCtx)
	{
		status = PAL_ERR_NO_MEMORY;
		goto finish;
	}

	localConfigCtx->confCtx = (platTlsConfiguraionContext*)malloc(sizeof(platTlsConfiguraionContext));
	if (NULL == localConfigCtx->confCtx)
	{
		status = PAL_ERR_NO_MEMORY;
		goto finish;
	}
	localConfigCtx->tlsIndex = 0;
	localConfigCtx->hasKeys = false;
	localConfigCtx->hasChain = false;
	memset(localConfigCtx->cipherSuites, 0,(sizeof(int)* (PAL_MAX_ALLOWED_CIPHER_SUITES+1)) );	
	mbedtls_ssl_config_init(localConfigCtx->confCtx);

	if (PAL_TLS_IS_CLIENT == methodType)
	{
		endpoint = MBEDTLS_SSL_IS_CLIENT;
	}
	else
	{
		endpoint = MBEDTLS_SSL_IS_SERVER;
	}

	if (PAL_TLS_MODE == transportVersion)
	{
		transport = MBEDTLS_SSL_TRANSPORT_STREAM;
	}
	else
	{
		transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
	}
	platStatus = mbedtls_ssl_config_defaults(localConfigCtx->confCtx, endpoint, transport, MBEDTLS_SSL_PRESET_DEFAULT);
	if (SSL_LIB_SUCCESS != platStatus)
	{
		PAL_LOG(ERR, "TLS Init conf status %" PRId32 ".", platStatus);
		status = PAL_ERR_TLS_CONFIG_INIT;
		goto finish;
	}								

	mbedtls_ctr_drbg_init(&localConfigCtx->ctrDrbg);
	status = pal_plat_addEntropySource(pal_plat_entropySourceTLS);
	if (PAL_SUCCESS != status)
	{
		goto finish;
	}								

	platStatus = mbedtls_ctr_drbg_seed(&localConfigCtx->ctrDrbg, mbedtls_entropy_func, g_entropy, NULL, 0); //Custom data can be defined in 
																						  //pal_TLS.h header and to be defined by 
																						  //Service code. But we need to check if other platform support this 
																						  //input!
	if (SSL_LIB_SUCCESS != platStatus)
	{
		status = PAL_ERR_TLS_CONFIG_INIT;
		goto finish;
	}
	
	mbedtls_ssl_conf_rng(localConfigCtx->confCtx, mbedtls_ctr_drbg_random, &localConfigCtx->ctrDrbg);
	*palConfCtx = (uintptr_t)localConfigCtx;
	
finish:	
	if (PAL_SUCCESS != status && NULL != localConfigCtx)
	{
		if (NULL != localConfigCtx->confCtx)
		{
			free(localConfigCtx->confCtx);			
		}
		free(localConfigCtx);
		*palConfCtx = NULLPTR;
	}
	return status;
}


palStatus_t pal_plat_tlsConfigurationFree(palTLSConfHandle_t* palTLSConf)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = NULL;

	if (NULLPTR == palTLSConf || NULLPTR == *palTLSConf)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	localConfigCtx = (palTLSConf_t*)*palTLSConf;

	if (true == localConfigCtx->hasKeys)
	{
		mbedtls_pk_free(&localConfigCtx->pkey);
		mbedtls_x509_crt_free(&localConfigCtx->owncert);
	}

	if (true == localConfigCtx->hasChain)
	{
		mbedtls_x509_crt_free(&localConfigCtx->cacert);
	}

	mbedtls_ssl_config_free(localConfigCtx->confCtx);
	mbedtls_ctr_drbg_free(&localConfigCtx->ctrDrbg);

	free(localConfigCtx->confCtx);

	memset(localConfigCtx, 0, sizeof(palTLSConf_t));
	free(localConfigCtx);
	*palTLSConf = NULLPTR;
	return status;
}


palStatus_t pal_plat_initTLS(palTLSConfHandle_t palTLSConf, palTLSHandle_t* palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	uint32_t firstAvailableCtxIndex = PAL_MAX_NUM_OF_TLS_CTX;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	

	if (NULLPTR == palTLSConf || NULLPTR == palTLSHandle)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}
	
	if (NULL == g_palTLSContext) //We allocate the entire array only for the first time
	{
		g_palTLSContext = (palTLS_t*)malloc(PAL_MAX_NUM_OF_TLS_CTX * sizeof(palTLS_t));
		if (NULL == g_palTLSContext)
		{
			status = PAL_ERR_TLS_RESOURCE;
			goto finish;
		}
		memset((void*)g_palTLSContext, 0 ,PAL_MAX_NUM_OF_TLS_CTX * sizeof(palTLS_t));
	}

	for (uint32_t i=0 ; i < PAL_MAX_NUM_OF_TLS_CTX ; ++i)
	{
		if (false == g_palTLSContext[i].tlsInit)
		{
			firstAvailableCtxIndex = i;
			break;
		}
	}

	if (firstAvailableCtxIndex >= PAL_MAX_NUM_OF_TLS_CTX)
	{
        status = PAL_ERR_TLS_RESOURCE;
		goto finish;
	}
	memset(&g_palTLSContext[firstAvailableCtxIndex], 0 , sizeof(palTLS_t));
	mbedtls_ssl_init(&g_palTLSContext[firstAvailableCtxIndex].tlsCtx);
	localConfigCtx->tlsIndex = firstAvailableCtxIndex;
	g_palTLSContext[firstAvailableCtxIndex].palConfCtx = localConfigCtx;
	g_palTLSContext[firstAvailableCtxIndex].tlsIndex = firstAvailableCtxIndex;
	g_palTLSContext[firstAvailableCtxIndex].tlsInit = true;
	mbedtls_ssl_set_timer_cb(&g_palTLSContext[firstAvailableCtxIndex].tlsCtx, &localConfigCtx->timerCtx, palTimingSetDelay, palTimingGetDelay);
	*palTLSHandle = (palTLSHandle_t)&g_palTLSContext[firstAvailableCtxIndex];

finish:
	return status;
}


palStatus_t pal_plat_freeTLS(palTLSHandle_t* palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	palTLS_t* localTLSCtx = NULL;
	bool foundActiveTLSCtx = false;
	
	if (NULLPTR == palTLSHandle || NULLPTR == *palTLSHandle)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	localTLSCtx = (palTLS_t*)*palTLSHandle;
	if (false == localTLSCtx->tlsInit)
	{
		status = PAL_ERR_TLS_CONTEXT_NOT_INITIALIZED;
		goto finish;
	}

	g_palTLSContext[localTLSCtx->tlsIndex].tlsInit = false;

	mbedtls_ssl_free(&localTLSCtx->tlsCtx);
	memset(localTLSCtx, 0, sizeof(palTLS_t));
	*palTLSHandle = NULLPTR;

	for (uint32_t i=0 ; i < PAL_MAX_NUM_OF_TLS_CTX ; ++i) //lets see if we need to release the global array
	{
		if (true == g_palTLSContext[i].tlsInit)
		{
			foundActiveTLSCtx = true;
			break;
		}
	}	

	if (false == foundActiveTLSCtx) // no more contexts, no need to hold the entire ctx array
	{
		free((void*)g_palTLSContext);
		g_palTLSContext = NULL;
	}
	
finish:
	return status;
}


palStatus_t pal_plat_setAuthenticationMode(palTLSConfHandle_t sslConf, palTLSAuthMode_t authMode)
{
	palStatus_t status = PAL_SUCCESS;
	int32_t platAuthMode;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)sslConf;

	if (NULLPTR == sslConf)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	switch(authMode)
	{
		case PAL_TLS_VERIFY_NONE:
			platAuthMode = MBEDTLS_SSL_VERIFY_NONE;
			break;			
		case PAL_TLS_VERIFY_OPTIONAL:
			platAuthMode = MBEDTLS_SSL_VERIFY_OPTIONAL;
			break;			
		case PAL_TLS_VERIFY_REQUIRED:
			platAuthMode = MBEDTLS_SSL_VERIFY_REQUIRED;
			break;			
		default:
			status = PAL_ERR_INVALID_ARGUMENT;
			goto finish;
	};
	mbedtls_ssl_conf_authmode(localConfigCtx->confCtx, platAuthMode );

finish:
	return status;	
}

palStatus_t pal_plat_setCipherSuites(palTLSConfHandle_t sslConf, palTLSSuites_t palSuite)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)sslConf;

	if (NULLPTR == sslConf)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	switch(palSuite)
	{
		case PAL_TLS_PSK_WITH_AES_128_CBC_SHA256:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_PSK_WITH_AES_128_CBC_SHA256;
			break;
    	case PAL_TLS_PSK_WITH_AES_128_CCM_8:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_PSK_WITH_AES_128_CCM_8;
			break;
		case PAL_TLS_PSK_WITH_AES_256_CCM_8:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_PSK_WITH_AES_256_CCM_8;
			break;
		case PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8;
			break;
		case PAL_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256;
			break;
		case PAL_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384:
			localConfigCtx->cipherSuites[0] = MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384;
			break;
		default:
			localConfigCtx->cipherSuites[0] = 0;
			status = PAL_ERR_TLS_INVALID_CIPHER;
			goto finish;
	}

	mbedtls_ssl_conf_ciphersuites(localConfigCtx->confCtx, localConfigCtx->cipherSuites);
finish:
	return status;
}


palStatus_t pal_plat_sslGetVerifyResult(palTLSHandle_t palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	palTLS_t* localTLSCtx = (palTLS_t*)palTLSHandle;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULLPTR == palTLSHandle)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}
	
	platStatus = mbedtls_ssl_get_verify_result(&localTLSCtx->tlsCtx);
	if (SSL_LIB_SUCCESS != platStatus)
	{
		//This errors handling must be expanded to all possible 
		//return values from the mbedtls_ssl_get_verify_result()
		PAL_LOG(ERR, "SSL Verify result error %" PRId32 ".", platStatus);
		status = PAL_ERR_GENERIC_FAILURE;
	}
	return status;
}


palStatus_t pal_plat_sslRead(palTLSHandle_t palTLSHandle, void *buffer, uint32_t len, uint32_t* actualLen)
{
	palStatus_t status = PAL_SUCCESS;
	int32_t platStatus = SSL_LIB_SUCCESS;
	palTLS_t* localTLSCtx = (palTLS_t*)palTLSHandle;

	if (NULLPTR == palTLSHandle || NULL == buffer || NULL == actualLen)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	platStatus = mbedtls_ssl_read(&localTLSCtx->tlsCtx, (unsigned char*)buffer, len);
	if (platStatus > SSL_LIB_SUCCESS)
	{
		*actualLen = platStatus;
	}
	else
	{
		status = translateTLSErrToPALError(platStatus);
		PAL_LOG(ERR, "SSL Read return code %" PRId32 ".", platStatus);
	}
		
	return status;
}


palStatus_t pal_plat_sslWrite(palTLSHandle_t palTLSHandle, const void *buffer, uint32_t len, uint32_t *bytesWritten)
{
	palStatus_t status = PAL_SUCCESS;
	int32_t platStatus = SSL_LIB_SUCCESS;
	palTLS_t* localTLSCtx = (palTLS_t*)palTLSHandle;

	if (NULLPTR == palTLSHandle || NULL == buffer || NULL == bytesWritten)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	platStatus = mbedtls_ssl_write(&localTLSCtx->tlsCtx, (unsigned char*)buffer, len);
	if (platStatus > SSL_LIB_SUCCESS)
	{
		*bytesWritten = platStatus;
	}
	else
	{
		status = translateTLSErrToPALError(platStatus);
		PAL_LOG(ERR, "SSL Write platform return code %" PRId32 ".", platStatus);
	}

	return status;
}


palStatus_t pal_plat_setHandShakeTimeOut(palTLSConfHandle_t palTLSConf, uint32_t timeoutInMilliSec)
{
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	uint32_t minTimeout = PAL_DTLS_PEER_MIN_TIMEOUT;
	uint32_t maxTimeout = timeoutInMilliSec >> 1; //! faster dividing by 2
	//! Since mbedTLS algorithm for UDP handshake algorithm is as follow:
	//! wait 'minTimeout' ..=> 'minTimeout = 2*minTimeout' while 'minTimeout < maxTimeout'
	//! if 'minTimeout >= maxTimeout' them wait 'maxTimeout'.
	//! The whole waiting time is the sum of the different intervals waited.
	//! Therefore we need divide the 'timeoutInMilliSec' by 2 to give a close approximation of the desired 'timeoutInMilliSec'
	//! 1 + 2 + ... + 'timeoutInMilliSec/2' ~= 'timeoutInMilliSec'

	if (NULLPTR == palTLSConf || 0 == timeoutInMilliSec)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	if (maxTimeout < PAL_DTLS_PEER_MIN_TIMEOUT)
	{
		minTimeout = (timeoutInMilliSec+1) >> 1; //to prevent 'minTimeout == 0'
		maxTimeout = timeoutInMilliSec;
	}

	mbedtls_ssl_conf_handshake_timeout(localConfigCtx->confCtx, minTimeout, maxTimeout);

	return PAL_SUCCESS;
}


palStatus_t pal_plat_sslSetup(palTLSHandle_t palTLSHandle, palTLSConfHandle_t palTLSConf)
{
	palStatus_t status = PAL_SUCCESS;
	palTLS_t* localTLSCtx = (palTLS_t*)palTLSHandle;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULLPTR == palTLSConf || NULLPTR == palTLSHandle)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	if (!localTLSCtx->wantReadOrWrite)
	{
		platStatus = mbedtls_ssl_setup(&localTLSCtx->tlsCtx, localConfigCtx->confCtx);
		if (SSL_LIB_SUCCESS != platStatus)
		{
			if (MBEDTLS_ERR_SSL_ALLOC_FAILED == platStatus)
			{
				status = PAL_ERR_NO_MEMORY;
				goto finish;
			}
			PAL_LOG(ERR, "SSL setup return code %" PRId32 ".", platStatus);
			status = PAL_ERR_GENERIC_FAILURE;
			goto finish;
		}

		localTLSCtx->palConfCtx = localConfigCtx;
		localConfigCtx->tlsIndex = localTLSCtx->tlsIndex;		
	}
finish:
	return status;
}


palStatus_t pal_plat_handShake(palTLSHandle_t palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	palTLS_t* localTLSCtx = (palTLS_t*)palTLSHandle;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULLPTR == palTLSHandle)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}
	
	platStatus = mbedtls_ssl_handshake(&localTLSCtx->tlsCtx);
	switch(platStatus)
	{
		case SSL_LIB_SUCCESS:
			status = PAL_SUCCESS;
			localTLSCtx->wantReadOrWrite = false;
			break;
		case MBEDTLS_ERR_SSL_WANT_READ:
			status = PAL_ERR_TLS_WANT_READ;
			localTLSCtx->wantReadOrWrite = true;
			break;
		case MBEDTLS_ERR_SSL_WANT_WRITE:
			status = PAL_ERR_TLS_WANT_WRITE;
			localTLSCtx->wantReadOrWrite = true;
			break;
		case MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED:
			status = PAL_ERR_TLS_HELLO_VERIFY_REQUIRED;
			break;
		case MBEDTLS_ERR_SSL_TIMEOUT:
			status = PAL_ERR_TIMEOUT_EXPIRED;
			break;
		case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
			status = PAL_ERR_TLS_PEER_CLOSE_NOTIFY;
			break;
		case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
			status = PAL_ERR_X509_CERT_VERIFY_FAILED;
			break;
		default:
			{
				PAL_LOG(ERR, "SSL handshake return code %" PRId32 ".", platStatus);
				status = PAL_ERR_GENERIC_FAILURE;
			}
	};

	return status;
}


palStatus_t pal_plat_setOwnCertAndPrivateKey(palTLSConfHandle_t palTLSConf, palX509_t* ownCert, palPrivateKey_t* privateKey)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	int32_t platStatus = SSL_LIB_SUCCESS;


	if (NULLPTR == palTLSConf || NULL == ownCert || NULL == ownCert)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mbedtls_x509_crt_init(&localConfigCtx->owncert);
	mbedtls_pk_init(&localConfigCtx->pkey);

	
    platStatus = mbedtls_x509_crt_parse_der(&localConfigCtx->owncert, (const unsigned char *)ownCert->buffer, ownCert->size);
	if (SSL_LIB_SUCCESS != platStatus)
	{
		status = PAL_ERR_TLS_FAILED_TO_PARSE_CERT;
		goto finish;
	}

	platStatus = mbedtls_pk_parse_key(&localConfigCtx->pkey, (const unsigned char *)privateKey->buffer, privateKey->size, NULL, 0 );
	if (SSL_LIB_SUCCESS != platStatus)
	{
		status = PAL_ERR_TLS_FAILED_TO_PARSE_KEY;
		goto finish;
	}

	platStatus = mbedtls_ssl_conf_own_cert(localConfigCtx->confCtx, &localConfigCtx->owncert, &localConfigCtx->pkey); 
	if (SSL_LIB_SUCCESS != platStatus)
	{
		status = PAL_ERR_TLS_FAILED_TO_SET_CERT;
	}

	localConfigCtx->hasKeys = true;

finish:
	PAL_LOG(DBG, "TLS set and parse status %" PRIu32 ".", platStatus);
	return status;
}


palStatus_t pal_plat_setCAChain(palTLSConfHandle_t palTLSConf, palX509_t* caChain, palX509CRL_t* caCRL)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULLPTR == palTLSConf || NULL == caChain)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	mbedtls_x509_crt_init(&localConfigCtx->cacert);

	platStatus = mbedtls_x509_crt_parse_der(&localConfigCtx->cacert, (const unsigned char *)caChain->buffer, caChain->size);
	if (SSL_LIB_SUCCESS != platStatus)
	{
		PAL_LOG(ERR, "TLS CA chain status %" PRId32 ".", platStatus);
		status = PAL_ERR_GENERIC_FAILURE;
		goto finish;
	}
	mbedtls_ssl_conf_ca_chain(localConfigCtx->confCtx, &localConfigCtx->cacert, NULL );

	localConfigCtx->hasChain = true;
finish:
	return status;
}

palStatus_t pal_plat_setPSK(palTLSConfHandle_t palTLSConf, const unsigned char *identity, uint32_t maxIdentityLenInBytes, const unsigned char *psk, uint32_t maxPskLenInBytes)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	int32_t platStatus = SSL_LIB_SUCCESS;

	if (NULLPTR == palTLSConf || NULL == identity || NULL == psk)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	platStatus = mbedtls_ssl_conf_psk(localConfigCtx->confCtx, psk, maxPskLenInBytes, identity, maxIdentityLenInBytes);
	if (SSL_LIB_SUCCESS != platStatus)
	{
		if (MBEDTLS_ERR_SSL_ALLOC_FAILED == platStatus)
		{
			status = PAL_ERR_TLS_INIT;
			goto finish;
		}
		PAL_LOG(ERR, "TLS set psk status %" PRId32 ".", platStatus);
		status = PAL_ERR_GENERIC_FAILURE;
	}
finish:
	return status;
}

palStatus_t pal_plat_tlsSetSocket(palTLSConfHandle_t palTLSConf, palTLSSocket_t* socket)
{
	palStatus_t status = PAL_SUCCESS;

	if (NULLPTR == palTLSConf || NULL == socket)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}

	status = pal_plat_sslSetIOCallBacks(palTLSConf, socket, palBIOSend, palBIORecv);
	return status;
}

palStatus_t pal_plat_sslSetIOCallBacks(palTLSConfHandle_t palTLSConf, palTLSSocket_t* palIOCtx, palBIOSend_f palBIOSend, palBIORecv_f palBIORecv)
{
	palStatus_t status = PAL_SUCCESS;
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	bool isNonBlocking = false;

	if (NULLPTR == palTLSConf || NULL == palBIOSend || NULL == palBIORecv)
	{
		return PAL_ERR_INVALID_ARGUMENT;
	}
	localConfigCtx->palIOCtx = palIOCtx;

	status = pal_isNonBlocking(palIOCtx->socket, &isNonBlocking);
	if (PAL_SUCCESS != status)
	{
		return status;
	}

	if (isNonBlocking)
	{
		mbedtls_ssl_set_bio(&g_palTLSContext[localConfigCtx->tlsIndex].tlsCtx, palIOCtx, palBIOSend, palBIORecv, NULL);
	}
	else
	{
		mbedtls_ssl_set_bio(&g_palTLSContext[localConfigCtx->tlsIndex].tlsCtx, palIOCtx, palBIOSend, NULL, palBIORecv_timeout);
	}

	return PAL_SUCCESS;
}

palStatus_t pal_plat_sslDebugging(uint8_t turnOn)
{
	palStatus_t status = PAL_SUCCESS;
	palLogFunc_f func = NULL;
#if defined(MBEDTLS_DEBUG_C)	
	mbedtls_debug_set_threshold(PAL_TLS_DEBUG_THRESHOLD);
#endif

	if (turnOn)
	{
		func = palDebug;
	}

	for (int i=0 ; i < PAL_MAX_NUM_OF_TLS_CTX ; ++i )
	{
		if ((g_palTLSContext != NULL) && (g_palTLSContext[i].tlsInit))
		{
			status = pal_plat_SetLoggingCb((palTLSConfHandle_t)g_palTLSContext[i].palConfCtx, func, NULL);
		}
	}

	return status;
}

palStatus_t pal_plat_SetLoggingCb(palTLSConfHandle_t palTLSConf, palLogFunc_f palLogFunction, void *logContext)
{
	palTLSConf_t* localConfigCtx = (palTLSConf_t*)palTLSConf;
	
	mbedtls_ssl_conf_dbg(localConfigCtx->confCtx, palLogFunction, logContext);
	return PAL_SUCCESS;
}

PAL_PRIVATE uint64_t palTimingGetTimer(uint64_t *start_ticks, int reset)
{
	uint64_t delta_ms;
	uint64_t ticks = pal_osKernelSysTick();

	if (reset)
	{
		*start_ticks = ticks;
		delta_ms = 0;
	}
	else
	{
		delta_ms = pal_osKernelSysMilliSecTick(ticks - *start_ticks);
	}

	return delta_ms;
}


/*
 * Set delays to watch
 */
PAL_PRIVATE void palTimingSetDelay( void *data, uint32_t intMs, uint32_t finMs )
{

    palTimingDelayContext_t *ctx = data;

    ctx->int_ms = intMs;
    ctx->fin_ms = finMs;

    if( finMs != 0 )
	{
		(void) palTimingGetTimer( &ctx->start_ticks, 1 );
	}
}

/*
 * Get number of delays expired
 */
PAL_PRIVATE int palTimingGetDelay( void *data )
{
	int result = 0;
    palTimingDelayContext_t *ctx = data;
    uint64_t elapsed_ms;

    if( ctx->fin_ms == 0 )
	{
		result = -1;
		goto finish;
	}

    elapsed_ms = palTimingGetTimer( &ctx->start_ticks, 0 );

    if( elapsed_ms >= ctx->fin_ms )
	{
		result = 2;
		goto finish;
	}

    if( elapsed_ms >= ctx->int_ms )
	{
		result = 1;
		goto finish;
	}

finish:
    return result;
}


int pal_plat_entropySourceTLS( void *data, unsigned char *output, size_t len, size_t *olen )
{
	palStatus_t status = PAL_SUCCESS;
	(void)data;

	status = pal_osRandomBuffer((uint8_t*) output, len);
	if (PAL_SUCCESS == status)
	{
		if (NULL != olen)
		{
			*olen = len;
		}
		return 0;
	}
	else
	{
		return -1;
	}
}

PAL_PRIVATE int palBIOSend(palTLSSocketHandle_t socket, const unsigned char *buf, size_t len)
{
	palStatus_t status = PAL_SUCCESS;
	size_t sentDataSize = 0;
	palTLSSocket_t* localSocket = (palTLSSocket_t*)socket;

	if (NULLPTR == socket)
	{
		status = -1;
		goto finish;
	}

	if (PAL_TLS_MODE == localSocket->transportationMode)
	{
		status = pal_send(localSocket->socket, buf, len, &sentDataSize);
	}
	else if (PAL_DTLS_MODE == localSocket->transportationMode)
	{
		status = pal_sendTo(localSocket->socket, buf, len, localSocket->socketAddress, localSocket->addressLength, &sentDataSize);
	}
	else
	{
		PAL_LOG(ERR, "TLS BIO send error");
		status = PAL_ERR_GENERIC_FAILURE;
	}
	if (PAL_SUCCESS == status || PAL_ERR_NO_MEMORY == status || PAL_ERR_SOCKET_WOULD_BLOCK == status)
	{
		if (0 != sentDataSize)
		{
            status = sentDataSize;
		}
		else
		{
			status = MBEDTLS_ERR_SSL_WANT_WRITE;
		}
	}
finish:
	return status;
}

PAL_PRIVATE int palBIORecv(palTLSSocketHandle_t socket, unsigned char *buf, size_t len)
{
	palStatus_t status = PAL_SUCCESS;
	size_t recievedDataSize = 0;
	palTLSSocket_t* localSocket = (palTLSSocket_t*)socket;

	if (NULLPTR == socket)
	{
		status = -1;
		goto finish;
	}

	if (PAL_TLS_MODE == localSocket->transportationMode)
	{
		status = pal_recv(localSocket->socket, buf, len, &recievedDataSize);
		if (PAL_SUCCESS == status)
		{
			status = recievedDataSize;
		}
		else if (PAL_ERR_SOCKET_WOULD_BLOCK == status)
		{
			status = MBEDTLS_ERR_SSL_WANT_READ;
		}
	}
	else if (PAL_DTLS_MODE == localSocket->transportationMode)
	{
		status = pal_receiveFrom(localSocket->socket, buf, len, localSocket->socketAddress, &localSocket->addressLength, &recievedDataSize);
		if (PAL_SUCCESS == status)
		{
			if (0 != recievedDataSize)
			{
				status = recievedDataSize;
			}
			else
			{
				status = MBEDTLS_ERR_SSL_WANT_READ;
			}
		}
		else if (PAL_ERR_SOCKET_WOULD_BLOCK == status)
		{
			status = MBEDTLS_ERR_SSL_WANT_READ;
		}
	}
	else
	{
		PAL_LOG(ERR, "TLS BIO recv error");
		status = PAL_ERR_GENERIC_FAILURE;
	}

finish:
	return status;
}

PAL_PRIVATE int palBIORecv_timeout(palTLSSocketHandle_t socket, unsigned char *buf, size_t len, uint32_t timeout)
{	
	palStatus_t status = PAL_SUCCESS;
	size_t recievedDataSize = 0;
	uint32_t localTimeOut = timeout;
	palTLSSocket_t* localSocket = (palTLSSocket_t*)socket;
	bool isNonBlocking = false;

	if (NULLPTR == socket)
	{
		status = -1;
		goto finish;
	}
	
	status = pal_isNonBlocking(localSocket->socket, &isNonBlocking);
	if (PAL_SUCCESS != status)
	{
		goto finish;
	}

	if (PAL_TLS_MODE == localSocket->transportationMode)
	{
		status = pal_recv(localSocket->socket, buf, len, &recievedDataSize);
		if (PAL_SUCCESS == status)
		{
			status = recievedDataSize;
		}
		else if (PAL_ERR_SOCKET_WOULD_BLOCK == status)
		{
			status = MBEDTLS_ERR_SSL_WANT_READ;
		}
	}
	else if (PAL_DTLS_MODE == localSocket->transportationMode)
	{
		if (false == isNonBlocking) // timeout is relevant only if socket is blocking
		{
			status = pal_setSocketOptions(localSocket->socket, PAL_SO_RCVTIMEO, &localTimeOut, sizeof(localTimeOut));
			if (PAL_SUCCESS != status)
			{
				goto finish;
			}
		}

		status = pal_receiveFrom(localSocket->socket, buf, len, localSocket->socketAddress, &localSocket->addressLength, &recievedDataSize);
		
		if (PAL_SUCCESS == status)
		{
			if (0 != recievedDataSize)
			{
				status = recievedDataSize;
			}
			else
			{
				status = MBEDTLS_ERR_SSL_WANT_READ;
			}
		}
		else if (PAL_ERR_SOCKET_WOULD_BLOCK == status)
		{
			status = MBEDTLS_ERR_SSL_TIMEOUT;
		}
	}
	else
	{
		PAL_LOG(ERR, "TLS BIO recv timeout error");
		status = PAL_ERR_GENERIC_FAILURE;
	}

finish:
	return status;
}

PAL_PRIVATE void palDebug(void *ctx, int debugLevel, const char *fileName, int line, const char *message)
{
	(void)ctx;
	DEBUG_PRINT("%s: %d: %s\r\n", fileName, line, message);
}

