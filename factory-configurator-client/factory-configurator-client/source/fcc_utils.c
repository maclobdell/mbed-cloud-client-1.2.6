// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
//  
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//  
//     http://www.apache.org/licenses/LICENSE-2.0
//  
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------------------------------------------------------

#include "factory_configurator_client.h"
#include "fcc_status.h"
#include "fcc_utils.h"
#include "key_config_manager.h"
#include "pv_error_handling.h"


fcc_status_e fcc_convert_kcm_to_fcc_status(kcm_status_e kcm_result)
{
    fcc_status_e fcc_status = FCC_STATUS_SUCCESS;

    switch (kcm_result) {
        case (KCM_STATUS_SUCCESS):
            fcc_status = FCC_STATUS_SUCCESS;
            break;
        case (KCM_STATUS_ERROR):
        case (KCM_STATUS_INVALID_PARAMETER):
        case (KCM_STATUS_OUT_OF_MEMORY):
        case (KCM_STATUS_INSUFFICIENT_BUFFER):
            fcc_status = FCC_STATUS_KCM_ERROR;
            break;
        case (KCM_STATUS_ITEM_NOT_FOUND):
            fcc_status = FCC_STATUS_ITEM_NOT_EXIST;
            break;
        case (KCM_STATUS_STORAGE_ERROR):
            fcc_status = FCC_STATUS_KCM_STORAGE_ERROR;
            break;
        case (KCM_STATUS_FILE_EXIST):
            fcc_status = FCC_STATUS_KCM_FILE_EXIST_ERROR;
            break;
        case (KCM_CRYPTO_STATUS_UNSUPPORTED_HASH_MODE):
        case (KCM_CRYPTO_STATUS_PARSING_DER_PRIVATE_KEY):
        case (KCM_CRYPTO_STATUS_PARSING_DER_PUBLIC_KEY):
        case (KCM_CRYPTO_STATUS_PRIVATE_KEY_VERIFICATION_FAILED):
        case (KCM_CRYPTO_STATUS_PUBLIC_KEY_VERIFICATION_FAILED):
        case (KCM_CRYPTO_STATUS_UNSUPPORTED_CURVE):
        case (KCM_CRYPTO_STATUS_CERT_EXPIRED):
        case (KCM_CRYPTO_STATUS_CERT_FUTURE):
        case (KCM_CRYPTO_STATUS_CERT_MD_ALG):
        case (KCM_CRYPTO_STATUS_CERT_PUB_KEY_TYPE):
        case (KCM_CRYPTO_STATUS_CERT_PUB_KEY):
        case (KCM_CRYPTO_STATUS_CERT_NOT_TRUSTED):
        case (KCM_CRYPTO_STATUS_INVALID_X509_ATTR):
            fcc_status = FCC_STATUS_KCM_CRYPTO_ERROR;
            break;
        default:
            SA_PV_LOG_INFO("Invalid kcm_result result (%u)!", kcm_result);
            fcc_status = FCC_STATUS_ERROR;
            break;
    }
    return fcc_status;
}
