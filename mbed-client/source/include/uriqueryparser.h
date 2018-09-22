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

#ifndef URIQUERYPARSER_H
#define URIQUERYPARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ns_types.h"

/**
 * \brief Returns string containing query parameters.
 * \param uri URL
 * \return Query parameters or NULL if there is no parameters.
 */
char *query_string(char *uri);

/**
 * \brief Return count of query parameters.
 * \param query Query string
 * \return Count of query parameters
 */
int8_t query_param_count(char *query);

/**
 * \brief Extract queries from the query string.
 * \param query Query string
 * \param uri_query_parameters [OUT] List of queries
 * \param index Starting index
 * \return True if parsing success otherwise False.
 */
bool uri_query_parameters(char* query, char *uri_query_parameters[], int index);

#ifdef __cplusplus
}
#endif

#endif // URIQUERYPARSER_H