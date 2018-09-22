// ----------------------------------------------------------------------------
// Copyright 2016-2017 ARM Ltd.
//
// SPDX-License-Identifier: Apache-2.0
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

#include <string.h>
#include "include/uriqueryparser.h"

char* query_string(char* uri)
{
    char* query = strchr((char*)uri, '?');
    if (query != NULL) {
        query++;
        if (*query == '\0') {
            return NULL;
        } else {
            return query;
        }
    } else {
        return NULL;
    }
}

int8_t query_param_count(char* query)
{
    int param_count = 0;
    while (NULL != (query = strchr(query, '='))) {
        param_count++;
        ++query;
    }
    return param_count;
}

bool uri_query_parameters(char* query, char *uri_query_parameters[], int index)
{
    if (query == NULL || *query == '\0' || index < 0) {
        return false;
    }

    uri_query_parameters[index++] = query;
    while (NULL != (query = strchr(query, '&'))) {
        *query = '\0';
        uri_query_parameters[index] = ++query;
        index++;
    }

    return true;
}
