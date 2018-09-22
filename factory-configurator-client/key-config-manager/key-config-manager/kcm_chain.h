#define KCM_MAX_NUMBER_OF_CERTITICATES_IN_CHAIN 5

typedef enum kcm_chain_operation_type_ {
    KCM_CHAIN_OP_TYPE_CREATE,
    KCM_CHAIN_OP_TYPE_OPEN,
    KCM_CHAIN_OP_TYPE_MAX
} kcm_chain_operation_type_e;

/** The chain context used internally only and should not be changed by user.
*/
typedef struct kcm_cert_chain_context_ {
    uint8_t *chain_name;                      //!< The name of certificate chain.
    size_t  chain_name_len;                //!< The size of certificate chain name.
    uint32_t num_of_certificates_in_chain;    //!< The number of certificate in the chain.
    esfs_file_t current_cert_file_descriptor; //!< Current certificate descriptor iterator.
    uint32_t current_cert_descriptor_index;   //!< Current descriptor certificate iterator.
    uint32_t current_cert_index;              //!< Current certificate iterator.
    kcm_chain_operation_type_e operation_type;//!< Type of Current operation.
} kcm_chain_context_s;



/** The API initializes chain context for write chain operation,
*   This API should be called prior to kcm_cert_chain_add_certificate API.
*
*    @param[out] kcm_chain_context                pointer to certificate chain context.
*    @param[in] kcm_chain_name                    pointer to certificate chain name.
*    @param[in] kcm_chain_name_len                length of certificate name buffer.
*    @param[in] kcm_chain_len                     number of certificates in the chain.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_create(kcm_chain_context_s *kcm_chain_context,
                                   const uint8_t *kcm_chain_name,
                                   size_t kcm_chain_name_len,
                                   uint32_t kcm_chain_len);

/** The API initializes chain context for read chain operation.
*   This API should be called prior to kcm_cert_chain_get_next_certificate_size and kcm_cert_chain_get_next_certificate_data APIs
*
*    @param[out] kcm_chain_context                pointer to certificate chain context.
*    @param[in] kcm_chain_name                    pointer to certificate chain name.
*    @param[in] kcm_chain_name_len                size of certificate name buffer.
*    @param[out] kcm_chain_len                    length of certificate chain.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_open(kcm_chain_context_s *kcm_chain_context,
                                 const uint8_t *kcm_chain_name,
                                 size_t kcm_chain_name_len,
                                 uint32_t *kcm_chian_len);

/** This API adds next certificate of chain to the storage.
*  The certificates should be added in the order from root of chain, followed by the certificates it signs and so on.
*
*    @param[in] kcm_chain_context                pointer to certificate chain context.
*    @param[in] kcm_cert_data                    pointer to certificate data in DER format.
*    @param[in] kcm_cert_data_size               size of certificate data buffer.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_add_next(kcm_chain_context_s *kcm_chain_context,
                                     const uint8_t *kcm_cert_data,
                                     size_t kcm_cert_data_size);

/** The API deletes all certificates of the chain from the storage.
*
*    @param[in] kcm_chain_name                pointer to certificate chain name.
*    @param[in] kcm_chain_name_len            length of certificate chain name.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_delete(const uint8_t *kcm_chain_name,
                                   size_t kcm_chain_name_len);

/** The API returns size of the next certificate in the chain.
*  This API should be called prior to kcm_cert_chain_get_next_data.
*  This operation does not increase chain's context iterator.
*
*    @param[in] kcm_chain_name           pointer to certificate chain context.
*    @param[out] kcm_cert_data_size      pointer size of next certificate.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_get_next_size(kcm_chain_context_s *kcm_chain_context,
        size_t *kcm_cert_data_size);

/** The API returns data of the next certificate in the chain.
*   To get exact size of a next certificate use kcm_cert_chain_get_next_certificate_size.
*   In the end of get data operation, chain context points to the next certificate of current chain.
*
*    @param[in] kcm_chain_context                   pointer to certificate chain context.
*    @param[in/out] kcm_cert_data                   pointer to certificate data in DER format.
*    @param[in] kcm_max_cert_data_size              max size of certificate data buffer.
*    @param[out] kcm_actual_cert_data_size          actual size of certificate data.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_get_next_data(kcm_chain_context_s *kcm_chain_context,
        uint8_t *kcm_cert_data,
        size_t kcm_max_cert_data_size,
        size_t *kcm_actual_cert_data_size);


/** The API releases the context and frees allocated resources.
*   When operation type is creation--> if total number of added/stored certificates is not equal to number
*   of certificates in the chain, the API will return an error.
*
*    @param[in] kcm_chain_context                pointer to certificate chain context.
*
*    @returns
*        KCM_STATUS_SUCCESS in case of success or one of the `::kcm_status_e` errors otherwise.
*/
kcm_status_e kcm_cert_chain_close(kcm_chain_context_s *kcm_chain_context);



/*Example flow
To create a new chain –
kcm_cert_chain_create(context, “name”, strlen(“name”), 3);
kcm_cert_chain_add_next(context, der_cert1, sizeof(der_cert1));
kcm_cert_chain_add_next(context, der_cert2, sizeof(der_cert2));
kcm_cert_chain_add_next(context, der_cert3, sizeof(der_cert3));
kcm_cert_chain_finilize(context);

To open and read an existing chain with size retrieving –
kcm_cert_chain_open(context, “name”, strlen(“name”));
kcm_cert_chain_get_next_size(context, &out_size);
out_data1 = fcc_malloc(out_size);
kcm_cert_chain_get_next_data(context, out_data1, out_size, &actual_out_size);
kcm_cert_chain_get_next_size(context, &out_size);
out_data2 = fcc_malloc(out_size);
kcm_cert_chain_get_next_data(context, out_data2, out_size, &actual_out_size);
kcm_cert_chain_get_next_size(context, &out_size);
out_data3 = fcc_malloc(out_size);
kcm_cert_chain_get_next_data(context, out_data3, out_size, &actual_out_size);
kcm_cert_chain_finilize(context);

To open and read an existing chain without size retrieving (predefined max length) –
uint8_t out_data1[1024];
uint8_t out_data2[1024];
uint8_t out_data3[1024];
kcm_cert_chain_open(context, “name”, strlen(“name”));
kcm_cert_chain_get_next_data(context, out_data1, sizeof(out_data1), &actual_out_size);
kcm_cert_chain_get_next_data(context, out_data2, sizeof(out_data2), &actual_out_size);
kcm_cert_chain_get_next_data(context, out_data3, sizeof(out_data3), &actual_out_size);
kcm_cert_chain_finilize(context);

To delete –
kcm_cert_chain_delete(“name”, strlen(“name”));
*/
