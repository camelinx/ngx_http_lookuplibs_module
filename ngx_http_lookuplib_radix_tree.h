/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#ifndef _NGX_HTTP_LOOKUP_LIB_RADIX_TREE_H_INCLUDED_
#define _NGX_HTTP_LOOKUP_LIB_RADIX_TREE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_string.h>

typedef struct ngx_http_lklb_radix_s ngx_http_lklb_radix_t;
typedef struct ngx_http_lklb_radix_node_s ngx_http_lklb_radix_node_t;

typedef void *( *ngx_http_lklb_radix_calloc_pt )( void *, size_t );
typedef void( *ngx_http_lklb_radix_free_pt )( void *, void * );

typedef void( *ngx_http_lklb_radix_rlock_pt )( void * );
typedef void( *ngx_http_lklb_radix_wlock_pt )( void * );
typedef void( *ngx_http_lklb_radix_unlock_pt )( void * );

ngx_http_lklb_radix_t *
ngx_http_lklb_radix_create(
    ngx_pool_t                      *pool,
    void                            *mem_ctx,
    ngx_uint_t                       transforms,
    ngx_http_lklb_radix_calloc_pt    calloc_fnpt,
    ngx_http_lklb_radix_free_pt      free_fnpt
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_set_lock_functions(
    ngx_http_lklb_radix_t         *tree,
    void                          *lock_ctx,
    ngx_http_lklb_radix_rlock_pt   rlock_fn,
    ngx_http_lklb_radix_wlock_pt   wlock_fn,
    ngx_http_lklb_radix_unlock_pt  unlock_fn
);

ngx_uint_t
ngx_http_lklb_radix_get_num_pages( ngx_http_lklb_radix_t *tree );

ngx_uint_t
ngx_http_lklb_radix_get_num_nodes( ngx_http_lklb_radix_t *tree );

/*
 * uint32_t APIs
 * tree:    tree returned from the create API above
 * key:     e.g. IPv4 address
 * mask:    Mask will be applied against
 *          the key to decide how much of it must be used. (key & mask).
 *          Use mask-less api to use the entire key always
 * value:   Optional value associated with key.
 *          Will be returned with delete and find APIs
 * prefix:  Used with find API. If true, a prefix match will return a hit.
 */
ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_insert_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    uint32_t                mask,
    void                   *value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_insert(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                   *value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_delete_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    uint32_t                mask,
    void                  **value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_delete(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                  **value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_find_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    uint32_t                mask,
    void                  **value,
    uint8_t                 prefix
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_find(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                  **value,
    uint8_t                 prefix
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_insert_with_mask(
    ngx_http_lklb_radix_t *tree,
    uint32_t              *key,
    uint32_t              *mask,
    void                  *value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_insert(
    ngx_http_lklb_radix_t *tree,
    uint32_t              *key,
    void                  *value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_delete_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    uint32_t               *mask,
    void                  **value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_delete(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    void                  **value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_find_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    uint32_t               *mask,
    void                  **value,
    uint8_t                 prefix
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_find(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    void                  **value,
    uint8_t                 prefix
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_insert(
    ngx_http_lklb_radix_t  *tree,
    uint8_t                *key,
    size_t                  key_len,
    void                   *value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_delete(
    ngx_http_lklb_radix_t  *tree,
    uint8_t                *key,
    size_t                  key_len,
    void                  **value
);

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_find(
    ngx_http_lklb_radix_t  *tree,
    uint8_t                *key,
    size_t                  key_len,
    void                  **value,
    uint8_t                 prefix
);

#endif /* _NGX_HTTP_LOOKUP_LIB_RADIX_TREE_H_INCLUDED_ */
