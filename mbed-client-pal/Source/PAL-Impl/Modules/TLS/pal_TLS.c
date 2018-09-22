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


#include "pal_TLS.h"
#include "pal_plat_TLS.h"
#include "pal_configuration.h"

palStatus_t pal_initTLS(palTLSConfHandle_t palTLSConf, palTLSHandle_t* palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_initTLS(palTLSConf, palTLSHandle);
	return status;
}


palStatus_t pal_freeTLS(palTLSHandle_t* palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_freeTLS(palTLSHandle);
	return status;
}


palStatus_t pal_initTLSConfiguration(palTLSConfHandle_t* palTLSConf, palTLSTransportMode_t transportationMode)
{
	palStatus_t status = PAL_SUCCESS;
	
	status = pal_plat_initTLSConf(palTLSConf, transportationMode, PAL_TLS_IS_CLIENT);
	if (PAL_SUCCESS != status)
	{
		goto finish;
	}
	
	status = pal_plat_setAuthenticationMode(*palTLSConf, PAL_TLS_VERIFY_REQUIRED);
	if (PAL_SUCCESS != status)
	{
		goto finish;
	}
//#define PAL_TLS_SUPPORT_ALL_AVAILABLE_SUITES 1
#if (PAL_TLS_CIPHER_SUITE & PAL_TLS_PSK_WITH_AES_128_CBC_SHA256_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_PSK_WITH_AES_128_CBC_SHA256);
#elif (PAL_TLS_CIPHER_SUITE & PAL_TLS_PSK_WITH_AES_128_CCM_8_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_PSK_WITH_AES_128_CCM_8);
#elif (PAL_TLS_CIPHER_SUITE & PAL_TLS_PSK_WITH_AES_256_CCM_8_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_PSK_WITH_AES_256_CCM_8);
#elif (PAL_TLS_CIPHER_SUITE & PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8);
#elif (PAL_TLS_CIPHER_SUITE & PAL_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8);
#elif (PAL_TLS_CIPHER_SUITE & PAL_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384_SUITE)
	status = pal_plat_setCipherSuites(*palTLSConf, PAL_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8);
#elif PAL_TLS_SUPPORT_ALL_AVAILABLE_SUITES
	status = PAL_SUCCESS;
#else
	#error : No CipherSuite was defined!
#endif
	if (PAL_SUCCESS != status)
	{
		goto finish;
	}
finish:
	return status;
}


palStatus_t pal_tlsConfigurationFree(palTLSConfHandle_t* palTLSConf)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_tlsConfigurationFree(palTLSConf);
	return status;
}


palStatus_t pal_addEntropySource(palEntropySource_f entropyCallback)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_addEntropySource(entropyCallback);
	return status;
}

palStatus_t pal_setOwnCertAndPrivateKey(palTLSConfHandle_t palTLSConf, palX509_t* ownCert, palPrivateKey_t* privateKey)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_setOwnCertAndPrivateKey(palTLSConf, ownCert, privateKey);
	return status;
}


palStatus_t pal_setCAChain(palTLSConfHandle_t palTLSConf, palX509_t* caChain, palX509CRL_t* caCRL)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_setCAChain(palTLSConf, caChain, caCRL);
	return status;
}


palStatus_t pal_setPSK(palTLSConfHandle_t palTLSConf, const unsigned char *identity, uint32_t maxIdentityLenInBytes, const unsigned char *psk, uint32_t maxPskLenInBytes)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_setPSK(palTLSConf, identity, maxIdentityLenInBytes, psk, maxPskLenInBytes);
	return status;
}


palStatus_t pal_tlsSetSocket(palTLSConfHandle_t palTLSConf, palTLSSocket_t* socket)
{	//palSocket_t depend on the library (socket or bio pointer)
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_tlsSetSocket(palTLSConf, socket);
	return status;
}


palStatus_t pal_handShake(palTLSHandle_t palTLSHandle, palTLSConfHandle_t palTLSConf)
{
	palStatus_t status = PAL_SUCCESS;
	
	status = pal_plat_sslSetup(palTLSHandle, palTLSConf);
	if (PAL_SUCCESS == status)
	{
		status = pal_plat_handShake(palTLSHandle);
	} 
	return status;
}


palStatus_t pal_sslGetVerifyResult(palTLSHandle_t palTLSHandle)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_sslGetVerifyResult(palTLSHandle);
	return status;
}


palStatus_t pal_setHandShakeTimeOut(palTLSConfHandle_t palTLSConf, uint32_t timeoutInMilliSec)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_setHandShakeTimeOut(palTLSConf, timeoutInMilliSec);
	return status;
}


palStatus_t pal_sslRead(palTLSHandle_t palTLSHandle, void *buffer, uint32_t len, uint32_t* actualLen)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_sslRead(palTLSHandle, buffer, len, actualLen);
	return status;
}


palStatus_t pal_sslWrite(palTLSHandle_t palTLSHandle, const void *buffer, uint32_t len, uint32_t *bytesWritten)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_sslWrite(palTLSHandle, buffer, len, bytesWritten);
	return status;
}

palStatus_t pal_sslDebugging(uint8_t turnOn)
{
	palStatus_t status = PAL_SUCCESS;
	status = pal_plat_sslDebugging(turnOn);
	return status;
}

