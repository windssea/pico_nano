#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int psa_status_t;
typedef struct {uint8_t *data;size_t size;} psa_hash_operation_t;
#define PSA_HASH_OPERATION_INIT {0}
#define PSA_ALG_SHA_256 1
#define PSA_SUCCESS 0
psa_status_t psa_crypto_init(void);
psa_status_t psa_hash_setup(psa_hash_operation_t *,int);
psa_status_t psa_hash_update(psa_hash_operation_t *,const uint8_t *,size_t);
psa_status_t psa_hash_finish(psa_hash_operation_t *,uint8_t *,size_t,size_t *);
psa_status_t psa_hash_abort(psa_hash_operation_t *);
