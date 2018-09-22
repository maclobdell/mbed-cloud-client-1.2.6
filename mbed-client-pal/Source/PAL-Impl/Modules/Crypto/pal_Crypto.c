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
#include "pal_plat_Crypto.h"


palStatus_t pal_initAes(palAesHandle_t *aes)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == aes)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_initAes(aes);
    return status;
}

palStatus_t pal_freeAes(palAesHandle_t *aes)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULL == aes || (uintptr_t)NULL == *aes)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_freeAes(aes);
    return status;
}

palStatus_t pal_setAesKey(palAesHandle_t aes, const unsigned char* key, uint32_t keybits, palAesKeyType_t keyTarget)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == aes || NULL == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_setAesKey(aes, key, keybits, keyTarget);
    return status;
}

palStatus_t pal_aesCTR(palAesHandle_t aes, const unsigned char* input, unsigned char* output, size_t inLen, unsigned char iv[16])
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == aes || NULL == input || NULL == output || NULL == iv)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_aesCTR(aes, input, output, inLen, iv, false);
    return status;
}

palStatus_t pal_aesCTRWithZeroOffset(palAesHandle_t aes, const unsigned char* input, unsigned char* output, size_t inLen, unsigned char iv[16])
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == aes || NULL == input || NULL == output || NULL == iv)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_aesCTR(aes, input, output, inLen, iv, true);
    return status;
}

palStatus_t pal_aesECB(palAesHandle_t aes, const unsigned char input[PAL_CRYPT_BLOCK_SIZE], unsigned char output[PAL_CRYPT_BLOCK_SIZE], palAesMode_t mode)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == aes || NULL == input || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_aesECB(aes, input, output, mode);
    return status;
}

palStatus_t pal_sha256(const unsigned char* input, size_t inLen, unsigned char* output)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULL == input || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    
    status = pal_plat_sha256(input, inLen, output);
    return status;
}

palStatus_t pal_x509Initiate(palX509Handle_t* x509Cert)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == x509Cert)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509Initiate(x509Cert);
    return status;
}

palStatus_t pal_x509CertParse(palX509Handle_t x509Cert, const unsigned char* input, size_t inLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509Cert || NULL == input)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CertParse(x509Cert, input, inLen);
    return status;
}

palStatus_t pal_x509CertGetAttribute(palX509Handle_t x509Cert, palX509Attr_t attr, void* output, size_t outLenBytes, size_t* actualOutLenBytes)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509Cert || NULL == output || NULL == actualOutLenBytes)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CertGetAttribute(x509Cert, attr, output, outLenBytes, actualOutLenBytes);
    return status;
}

palStatus_t pal_x509CertVerify(palX509Handle_t x509Cert, palX509Handle_t x509CertChain)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509Cert)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CertVerify(x509Cert, x509CertChain);
    return status;
}

palStatus_t pal_x509Free(palX509Handle_t* x509Cert)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULLPTR == x509Cert || NULLPTR == *x509Cert)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509Free(x509Cert);
    return status;
}

palStatus_t pal_mdInit(palMDHandle_t* md, palMDType_t mdType)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == md)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdInit(md, mdType);
    return status;
}

palStatus_t pal_mdUpdate(palMDHandle_t md, const unsigned char* input, size_t inLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == md || NULL == input)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdUpdate(md, input, inLen);
    return status;
}

palStatus_t pal_mdGetOutputSize(palMDHandle_t md, size_t* bufferSize)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == md || NULL == bufferSize)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdGetOutputSize(md, bufferSize);
    return status;
}

palStatus_t pal_mdFinal(palMDHandle_t md, unsigned char* output)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == md || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdFinal(md, output);
    return status;
}

palStatus_t pal_mdFree(palMDHandle_t* md)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == md || NULLPTR == *md)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdFree(md);
    return status;
}

palStatus_t pal_verifySignature(palX509Handle_t x509, palMDType_t mdType, const unsigned char *hash, size_t hashLen, const unsigned char *sig, size_t sigLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509 || NULL == hash || NULL == sig)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_verifySignature(x509, mdType, hash, hashLen, sig, sigLen);
    return status;
}
 
palStatus_t pal_ASN1GetTag(unsigned char **position, const unsigned char *end, size_t *len, uint8_t tag )
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == position || NULL == end || NULL == len)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    
    status = pal_plat_ASN1GetTag(position, end, len, tag);
    return status;
}

palStatus_t pal_CCMInit(palCCMHandle_t* ctx)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == ctx)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CCMInit(ctx);
    return status;
}

palStatus_t pal_CCMFree(palCCMHandle_t* ctx)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == ctx || NULLPTR == *ctx)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CCMFree(ctx);
    return status;
}

palStatus_t pal_CCMSetKey(palCCMHandle_t ctx, const unsigned char *key, uint32_t keybits, palCipherID_t id)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CCMSetKey(ctx, id, key, keybits);
    return status;
}

palStatus_t pal_CCMDecrypt(palCCMHandle_t ctx, unsigned char* input, size_t inLen, 
							unsigned char* iv, size_t ivLen, unsigned char* add, 
							size_t addLen, unsigned char* tag, size_t tagLen, 
							unsigned char* output)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == input || NULL == iv || NULL == add || NULL == tag || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CCMDecrypt(ctx, input, inLen, iv, ivLen, add, addLen, tag, tagLen, output);
    return status;
}

palStatus_t pal_CCMEncrypt(palCCMHandle_t ctx, unsigned char* input, 
							size_t inLen, unsigned char* iv, size_t ivLen, 
							unsigned char* add, size_t addLen, unsigned char* output, 
							unsigned char* tag, size_t tagLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == input || NULL == iv || NULL == add || NULL == tag || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CCMEncrypt(ctx, input, inLen, iv, ivLen, add, addLen, output, tag, tagLen);
    return status;
}

palStatus_t pal_CtrDRBGInit(palCtrDrbgCtxHandle_t* ctx, const void* seed, size_t len)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == ctx || NULL == seed)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CtrDRBGInit(ctx);
    if (PAL_SUCCESS == status)
    {
        status = pal_plat_CtrDRBGSeed(*ctx, seed, len);
    }

    return status;
}

palStatus_t pal_CtrDRBGGenerate(palCtrDrbgCtxHandle_t ctx, unsigned char* out, size_t len)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == out)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CtrDRBGGenerate(ctx, out, len);
    return status;
}

palStatus_t pal_CtrDRBGFree(palCtrDrbgCtxHandle_t* ctx)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == ctx || NULLPTR == *ctx)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CtrDRBGFree(ctx);
    return status;
}

palStatus_t pal_cipherCMAC(const unsigned char *key, size_t keyLenInBits, const unsigned char *input, size_t inputLenInBytes, unsigned char *output)
{
    palStatus_t status = PAL_SUCCESS;
    if (NULL == key || NULL == input || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_cipherCMAC(key, keyLenInBits, input, inputLenInBytes, output);
    return status;
}

palStatus_t pal_CMACStart(palCMACHandle_t *ctx, const unsigned char *key, size_t keyLenBits, palCipherID_t cipherID)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CMACStart(ctx, key, keyLenBits, cipherID);
    return status;
}

palStatus_t pal_CMACUpdate(palCMACHandle_t ctx, const unsigned char *input, size_t inLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULL == input)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CMACUpdate(ctx, input, inLen);
    return status;
}

palStatus_t pal_CMACFinish(palCMACHandle_t *ctx, unsigned char *output, size_t* outLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == ctx || NULLPTR == *ctx || NULL == output || NULL == outLen)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_CMACFinish(ctx, output, outLen);
    return status;
}

palStatus_t pal_mdHmacSha256(const unsigned char *key, size_t keyLenInBytes, const unsigned char *input, size_t inputLenInBytes, unsigned char *output, size_t* outputLenInBytes)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == key || NULL == input || NULL == output)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_mdHmacSha256(key, keyLenInBytes, input, inputLenInBytes, output, outputLenInBytes);
    return status;
}

palStatus_t pal_ECCheckKey(palCurveHandle_t grp, palECKeyHandle_t key, uint32_t type, bool *verified)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == grp || NULLPTR == key || NULL == verified)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECCheckKey(grp, key, type, verified);
    return status;
}

palStatus_t pal_ECKeyNew(palECKeyHandle_t* key)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECKeyNew(key);
    return status;
}

palStatus_t pal_ECKeyFree(palECKeyHandle_t* key)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULL == key || NULLPTR == *key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECKeyFree(key);
    return status;
}

palStatus_t pal_parseECPrivateKeyFromDER(const unsigned char* prvDERKey, size_t keyLen, palECKeyHandle_t key)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == prvDERKey || NULLPTR == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    } 

    status = pal_plat_parseECPrivateKeyFromDER(prvDERKey, keyLen, key);
    return status;
}

palStatus_t pal_parseECPublicKeyFromDER(const unsigned char* pubDERKey, size_t keyLen, palECKeyHandle_t key)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == pubDERKey || NULLPTR == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    } 

    status = pal_plat_parseECPublicKeyFromDER(pubDERKey, keyLen, key);
    return status;
}

palStatus_t pal_writePrivateKeyToDer(palECKeyHandle_t key, unsigned char* derBuffer, size_t bufferSize, size_t* actualSize)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == key || NULL == derBuffer || NULL == actualSize)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_writePrivateKeyToDer(key, derBuffer, bufferSize, actualSize);
    return status;
}

palStatus_t pal_writePublicKeyToDer(palECKeyHandle_t key, unsigned char* derBuffer, size_t bufferSize, size_t* actualSize)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == key || NULL == derBuffer || NULL == actualSize)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_writePublicKeyToDer(key, derBuffer, bufferSize, actualSize);
    return status;
}
palStatus_t pal_ECGroupInitAndLoad(palCurveHandle_t* grp, palGroupIndex_t index)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == grp)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECGroupInitAndLoad(grp, index);
    return status;
}

palStatus_t pal_ECGroupFree(palCurveHandle_t* grp)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == grp || NULLPTR == *grp)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECGroupFree(grp);
    return status;
}

palStatus_t pal_ECKeyGenerateKey(palGroupIndex_t grpID, palECKeyHandle_t key)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == key)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }  

    status = pal_plat_ECKeyGenerateKey(grpID, key);
    return status;
}

palStatus_t pal_ECKeyGetCurve(palECKeyHandle_t key, palGroupIndex_t* grpID)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == key || NULL == grpID)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECKeyGetCurve(key, grpID);
    return status;
}

palStatus_t pal_x509CSRInit(palx509CSRHandle_t *x509CSR)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == x509CSR)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRInit(x509CSR);
    return status;
}

palStatus_t pal_x509CSRSetSubject(palx509CSRHandle_t x509CSR, const char* subjectName)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509CSR || NULL == subjectName)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRSetSubject(x509CSR, subjectName);
    return status;
}

palStatus_t pal_x509CSRSetKey(palx509CSRHandle_t x509CSR, palECKeyHandle_t pubKey, palECKeyHandle_t prvKey)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULLPTR == x509CSR || NULLPTR == pubKey)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRSetKey(x509CSR, pubKey, prvKey);
    return status;
}

palStatus_t pal_x509CSRSetMD(palx509CSRHandle_t x509CSR, palMDType_t mdType)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509CSR)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRSetMD(x509CSR, mdType);
    return status;
}

palStatus_t pal_x509CSRSetKeyUsage(palx509CSRHandle_t x509CSR, uint32_t keyUsage)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509CSR)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRSetKeyUsage(x509CSR, keyUsage);
    return status;
}

palStatus_t pal_x509CSRSetExtension(palx509CSRHandle_t x509CSR,const char* oid, size_t oidLen, const unsigned char* value, size_t valueLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509CSR || NULL == oid || NULL == value)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRSetExtension(x509CSR, oid, oidLen, value, valueLen);
    return status;
}

palStatus_t pal_x509CSRWriteDER(palx509CSRHandle_t x509CSR, unsigned char* derBuf, size_t derBufLen, size_t* actualDerLen)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == x509CSR || NULL == derBuf)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    } 

    status = pal_plat_x509CSRWriteDER(x509CSR, derBuf, derBufLen, actualDerLen);
    return status;
}

palStatus_t pal_x509CSRFree(palx509CSRHandle_t *x509CSR)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULL == x509CSR || NULLPTR == *x509CSR)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_x509CSRFree(x509CSR);
    return status;
}

palStatus_t pal_ECDHComputeKey(const palCurveHandle_t grp, const palECKeyHandle_t peerPublicKey, 
                            const palECKeyHandle_t privateKey, palECKeyHandle_t outKey)
{
    palStatus_t status = PAL_SUCCESS;

    if (NULLPTR == grp || NULLPTR == peerPublicKey || NULLPTR == privateKey || NULLPTR == outKey)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }

    status = pal_plat_ECDHComputeKey(grp, peerPublicKey, privateKey, outKey);
    return status;
}

palStatus_t pal_ECDSASign(palCurveHandle_t grp, palMDType_t mdType, palECKeyHandle_t prvKey, unsigned char* dgst, 
							uint32_t dgstLen, unsigned char *sig, size_t *sigLen)
{
    palStatus_t status = PAL_SUCCESS;
        
    if (NULLPTR == grp || NULLPTR == prvKey || NULL == dgst || NULL == sig || NULL == sigLen)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    
    status = pal_plat_ECDSASign(grp, mdType, prvKey, dgst, dgstLen, sig, sigLen);
    return status;
}

palStatus_t pal_ECDSAVerify(palECKeyHandle_t pubKey, unsigned char* dgst, uint32_t dgstLen, 
                            unsigned char* sig, size_t sigLen, bool* verified)
{
    palStatus_t status = PAL_SUCCESS;
    
    if (NULLPTR == pubKey || NULL == dgst || NULL == sig || NULL == verified)
    {
        return PAL_ERR_INVALID_ARGUMENT;
    }
    
    status = pal_plat_ECDSAVerify(pubKey, dgst, dgstLen, sig, sigLen, verified);
    return status;
}

