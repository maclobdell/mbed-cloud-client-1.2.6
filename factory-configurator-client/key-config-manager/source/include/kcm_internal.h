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

#ifndef KEYS_CONFIG_MANAGER_INTERNAL_H
#define KEYS_CONFIG_MANAGER_INTERNAL_H

#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include "esfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* === Definitions and Prototypes === */

typedef enum kcm_meta_data_type_ {
    KCM_LOCAL_ACL_MD_TYPE,
    KCM_REMOTE_ACL_MD_TYPE,
    KCM_AUDIT_MD_TYPE,
    KCM_NAME_MD_TYPE,
    KCM_USAGE_MD_TYPE,
    KCM_MD_TYPE_MAX_SIZE
} kcm_meta_data_type_e;

typedef struct kcm_meta_data_ {
    kcm_meta_data_type_e type;
    size_t data_size;
    uint8_t *data;
} kcm_meta_data_s;

typedef struct kcm_meta_data_list_ {
    // allocate a single meta data for each type
    kcm_meta_data_s meta_data[KCM_MD_TYPE_MAX_SIZE];
    size_t meta_data_count;
} kcm_meta_data_list_s;

typedef struct kcm_ctx_ {
    esfs_file_t esfs_file_h;
    kcm_meta_data_list_s list;
    uint16_t access_flags; // owner, signed, encrypted, factory, extended ACL bit mask
    size_t file_size;
    bool is_file_size_checked;
} kcm_ctx_s;

#ifdef __cplusplus
}
#endif

#endif //KEYS_CONFIG_MANAGER_INTERNAL_H

