/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include "ngx_http_lookuplibs_module.h"
#include "ngx_http_lookuplibs_transforms.h"
#include "ngx_http_lookuplib_radix_tree.h"

static void *NGX_HTTP_LKLB_RADIX_NO_VALUE = ( void * )( -1 );

struct ngx_http_lklb_radix_s {
    ngx_http_lklb_radix_node_t        *root;
    ngx_http_lklb_radix_node_t        *free;

    char                              *start;
    size_t                             size;
    ngx_uint_t                         npages;
    ngx_uint_t                         nnodes;

    ngx_uint_t                         transforms;

    ngx_pool_t                        *pool;
    void                              *mem_ctx;
    ngx_http_lklb_radix_calloc_pt      calloc_fnpt;
    ngx_http_lklb_radix_free_pt        free_fnpt;

    void                              *lock_ctx;
    ngx_http_lklb_radix_rlock_pt       rlock_fnpt;
    ngx_http_lklb_radix_wlock_pt       wlock_fnpt;
    ngx_http_lklb_radix_unlock_pt      unlock_fnpt;
};

static void
ngx_http_lklb_radix_rlock( ngx_http_lklb_radix_t *tree ) {
    if( tree->rlock_fnpt ) {
        tree->rlock_fnpt( tree->lock_ctx );
    }
}

static void
ngx_http_lklb_radix_wlock( ngx_http_lklb_radix_t *tree ) {
    if( tree->wlock_fnpt ) {
        tree->wlock_fnpt( tree->lock_ctx );
    }
}

static void
ngx_http_lklb_radix_unlock( ngx_http_lklb_radix_t *tree ) {
    if( tree->unlock_fnpt ) {
        tree->unlock_fnpt( tree->lock_ctx );
    }
}

struct ngx_http_lklb_radix_node_s {
    ngx_http_lklb_radix_node_t  *right;
    ngx_http_lklb_radix_node_t  *left;
    ngx_http_lklb_radix_node_t  *parent;
    void                        *value;
};
 
static void
ngx_http_lklb_radix_init_children( ngx_http_lklb_radix_node_t *node ) {
    node->right = node->left = NULL;
}

static uint32_t
ngx_http_lklb_radix_is_leaf_node( ngx_http_lklb_radix_node_t *node ) {
    return( ( NULL == node->right ) && ( NULL == node->left ) );
}

#define NGX_HTTP_LKLB_RADIX_NODE_IS_ROOT( __node )    ( NULL == ( __node )->parent )

static ngx_http_lklb_radix_node_t *
ngx_http_lklb_radix_alloc( ngx_http_lklb_radix_t *tree );

ngx_http_lklb_radix_t *
ngx_http_lklb_radix_create(
    ngx_pool_t                    *pool,
    void                          *mem_ctx,
    ngx_uint_t                     transforms,
    ngx_http_lklb_radix_calloc_pt  calloc_fnpt,
    ngx_http_lklb_radix_free_pt    free_fnpt
) {
    ngx_http_lklb_radix_t  *tree = NULL;

    if( ( NULL == pool ) && ( NULL == calloc_fnpt ) ) {
        return NULL;
    }
    
    if( calloc_fnpt ) {
        tree = calloc_fnpt( mem_ctx, sizeof( ngx_http_lklb_radix_t ) );
    } else if( pool ) {
        tree = ngx_pcalloc( pool, sizeof( ngx_http_lklb_radix_t ) );
    }

    if( NULL == tree ) {
        return NULL;
    }

    tree->pool        = pool;
    tree->mem_ctx     = mem_ctx;
    tree->calloc_fnpt = calloc_fnpt;
    tree->free_fnpt   = free_fnpt;

    if( !( tree->root = ngx_http_lklb_radix_alloc( tree ) ) ) {
        return NULL;
    }

    tree->transforms = transforms;

    ngx_http_lklb_radix_init_children( tree->root );
    tree->root->parent = NULL;
    tree->root->value  = NGX_HTTP_LKLB_RADIX_NO_VALUE;

    return tree;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_set_lock_functions(
    ngx_http_lklb_radix_t         *tree,
    void                          *lock_ctx,
    ngx_http_lklb_radix_rlock_pt   rlock_fnpt,
    ngx_http_lklb_radix_wlock_pt   wlock_fnpt,
    ngx_http_lklb_radix_unlock_pt  unlock_fnpt
) {
    tree->lock_ctx     = lock_ctx;
    tree->rlock_fnpt   = rlock_fnpt;
    tree->wlock_fnpt   = wlock_fnpt;
    tree->unlock_fnpt  = unlock_fnpt;

    return NGX_HTTP_LKLB_MATCH;
}

ngx_uint_t
ngx_http_lklb_radix_get_num_pages( ngx_http_lklb_radix_t *tree ) {
    return( ( tree ) ? tree->npages : 0 );
}

ngx_uint_t
ngx_http_lklb_radix_get_num_nodes( ngx_http_lklb_radix_t *tree ) {
    return( ( tree ) ? tree->nnodes : 0 );
}

#define NGX_HTTP_LKLB_RADIX_UINT32_MSB      ( 1 << 31 )

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_insert_with_mask(
    ngx_http_lklb_radix_t *tree,
    uint32_t               key,
    uint32_t               mask,
    void                  *value
) {
    uint32_t                     bit;
    ngx_http_lklb_radix_node_t  *node, *next;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    bit = NGX_HTTP_LKLB_RADIX_UINT32_MSB;

    key  = ngx_http_lklb_uint32_htonl( tree->transforms, key );
    mask = ngx_http_lklb_uint32_htonl( tree->transforms, mask );

    ngx_http_lklb_radix_wlock( tree );

    node = tree->root;
    next = tree->root;

    while( bit & mask ) {
        if( key & bit ) {
            next = node->right;
        } else {
            next = node->left;
        }

        if( NULL == next ) {
            break;
        }

        bit  >>= 1;
        node   = next;
    }

    if( next ) {
        if( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_DUP;
        }

        node->value = value;
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_MATCH;
    }

    while( bit & mask ) {
        if( !( next = ngx_http_lklb_radix_alloc( tree ) ) ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_ERR;
        }

        ngx_http_lklb_radix_init_children( next );
        next->parent = node;
        next->value  = NGX_HTTP_LKLB_RADIX_NO_VALUE;

        if( key & bit ) {
            node->right = next;
        } else {
            node->left = next;
        }

        bit  >>= 1;
        node   = next;
    }

    node->value = value;
    ngx_http_lklb_radix_unlock( tree );
    return NGX_HTTP_LKLB_MATCH;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_insert(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                   *value
) {
    return ngx_http_lklb_radix_uint32_insert_with_mask( tree, key, ( uint32_t )( -1 ), value ); 
}

static ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_find_node(
    ngx_http_lklb_radix_t       *tree,
    uint32_t                     key,
    uint32_t                     mask,
    uint8_t                      prefix,
    ngx_http_lklb_radix_node_t **result
) {
    uint32_t                     bit;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc = NGX_HTTP_LKLB_ERR;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    bit  = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
    node = tree->root;

    while( ( node ) && ( bit & mask ) ) {
        if( ( prefix ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
            rc = NGX_HTTP_LKLB_PARTIAL_MATCH;
            break;
        }

        if( key & bit ) {
            node = node->right;
        } else {
            node = node->left;
        }

        bit >>= 1;
    }

    if( ( node ) && ( NGX_HTTP_LKLB_ERR == rc ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
        rc = NGX_HTTP_LKLB_MATCH;
    }

    if( result ) {
        *result = node;
    }

    return rc;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_delete_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    uint32_t                mask,
    void                  **result
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    key  = ngx_http_lklb_uint32_htonl( tree->transforms, key );
    mask = ngx_http_lklb_uint32_htonl( tree->transforms, mask );

    ngx_http_lklb_radix_wlock( tree );

    rc = ngx_http_lklb_radix_uint32_find_node( tree, key, mask, 0, &node );
    if( ( NULL == node ) || ( NGX_HTTP_LKLB_MATCH != rc ) ) {
        goto ldone;
    }

    value = node->value;

    if( !( ngx_http_lklb_radix_is_leaf_node( node ) ) ) {
        if( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) {
            node->value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
        }

        goto ldone;
    }

    while( 1 ) {
        if( node == node->parent->right ) {
            node->parent->right = NULL;
        } else {
            node->parent->left = NULL;
        }

        node->right = tree->free;
        tree->free  = node;

        node = node->parent;

        if( !( ngx_http_lklb_radix_is_leaf_node( node ) )   ||
            ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ||
            ( NGX_HTTP_LKLB_RADIX_NODE_IS_ROOT( node ) )
          ) {
            break;
        }
    }

ldone:
    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return( ( NGX_HTTP_LKLB_RADIX_NO_VALUE != value ) ? NGX_HTTP_LKLB_MATCH : NGX_HTTP_LKLB_ERR );
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_delete(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                  **result
) {
    return ngx_http_lklb_radix_uint32_delete_with_mask( tree, key, ( uint32_t )( -1 ), result );
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_find_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    uint32_t                mask,
    void                  **result,
    uint8_t                 prefix
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    key  = ngx_http_lklb_uint32_htonl( tree->transforms, key );
    mask = ngx_http_lklb_uint32_htonl( tree->transforms, mask );

    ngx_http_lklb_radix_rlock( tree );

    rc = ngx_http_lklb_radix_uint32_find_node( tree, key, mask, prefix, &node );
    if( NGX_HTTP_LKLB_ERR == rc ) {
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_ERR;
    }

    if( node ) {
        value = node->value;
    }

    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return rc;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint32_find(
    ngx_http_lklb_radix_t  *tree,
    uint32_t                key,
    void                  **result,
    uint8_t                 prefix
) {
    return ngx_http_lklb_radix_uint32_find_with_mask( tree, key, ( uint32_t )( -1 ), result, prefix );
}

#define NGX_HTTP_LKLB_RADIX_UINT128_DEFAULT_MASK { ( uint32_t )-1, ( uint32_t )-1, ( uint32_t )-1, ( uint32_t )-1 }

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_insert_with_mask(
    ngx_http_lklb_radix_t *tree,
    uint32_t              *key,
    uint32_t              *mask,
    void                  *value
) {
    uint32_t                     lkey[ 4 ], lmask[ 4 ];
    uint32_t                     bit, idx;
    ngx_http_lklb_radix_node_t  *node, *next;

    if( ( NULL == tree ) || ( NULL == key ) || ( NULL == mask ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    bit = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
    idx = 0;

    ngx_memcpy( &lkey[ 0 ], key, 4 * sizeof( uint32_t ) );
    ngx_memcpy( &lmask[ 0 ], mask, 4 * sizeof( uint32_t ) );

    ngx_http_lklb_uint128_htonl( tree->transforms, &lkey[ 0 ] );
    ngx_http_lklb_uint128_htonl( tree->transforms, &lmask[ 0 ] );
 
    ngx_http_lklb_radix_wlock( tree );

    node = tree->root;
    next = tree->root;

    while( ( idx < 4 ) && ( bit & lmask[ idx ] ) ) {
        if( lkey[ idx ] & bit ) {
            next = node->right;
        } else {
            next = node->left;
        }

        if( NULL == next ) {
            break;
        }

        bit  >>= 1;
        node   = next;

        if( 0 == bit ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
            idx++;
        }
    }

    if( next ) {
        if( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_DUP;
        }

        node->value = value;
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_MATCH;
    }

    while( ( idx < 4 ) && ( bit & lmask[ idx ] ) ) {
        if( !( next = ngx_http_lklb_radix_alloc( tree ) ) ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_ERR;
        }

        ngx_http_lklb_radix_init_children( next );
        next->parent = node;
        next->value  = NGX_HTTP_LKLB_RADIX_NO_VALUE;

        if( lkey[ idx ] & bit ) {
            node->right = next;
        } else {
            node->left = next;
        }

        bit  >>= 1;
        node   = next;

        if( 0 == bit ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
            idx++;
        }
    }

    node->value = value;
    ngx_http_lklb_radix_unlock( tree );
    return NGX_HTTP_LKLB_MATCH;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_insert(
    ngx_http_lklb_radix_t *tree,
    uint32_t              *key,
    void                  *value
) {
    uint32_t    mask[ 4 ] = NGX_HTTP_LKLB_RADIX_UINT128_DEFAULT_MASK;

    return ngx_http_lklb_radix_uint128_insert_with_mask( tree, key, &mask[ 0 ], value );
}

static ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_find_node(
    ngx_http_lklb_radix_t       *tree,
    uint32_t                    *key,
    uint32_t                    *mask,
    uint8_t                      prefix,
    ngx_http_lklb_radix_node_t **result
) {
    uint32_t                     bit, idx;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc = NGX_HTTP_LKLB_ERR;

    if( ( NULL == tree ) || ( NULL == key ) || ( NULL == mask ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    bit  = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
    idx  = 0;
    node = tree->root;

    while( ( idx < 4 ) && ( node ) && ( bit & mask[ idx ] ) ) {
        if( ( prefix ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
            rc = NGX_HTTP_LKLB_PARTIAL_MATCH;
            break;
        }

        if( key[ idx ] & bit ) {
            node = node->right;
        } else {
            node = node->left;
        }

        bit >>= 1;

        if( 0 == bit ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT32_MSB;
            idx++;
        }
    }

    if( ( node ) && ( NGX_HTTP_LKLB_ERR == rc ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
        rc = NGX_HTTP_LKLB_MATCH;
    }

    if( result ) {
        *result = node;
    }

    return rc;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_delete_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    uint32_t               *mask,
    void                  **result
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    uint32_t                     lkey[ 4 ], lmask[ 4 ];
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( ( NULL == tree ) || ( NULL == key ) || ( NULL == mask ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    ngx_memcpy( &lkey[ 0 ], key, 4 * sizeof( uint32_t ) );
    ngx_memcpy( &lmask[ 0 ], mask, 4 * sizeof( uint32_t ) );

    ngx_http_lklb_uint128_htonl( tree->transforms, &lkey[ 0 ] );
    ngx_http_lklb_uint128_htonl( tree->transforms, &lmask[ 0 ] );

    ngx_http_lklb_radix_wlock( tree );
 
    rc = ngx_http_lklb_radix_uint128_find_node( tree, &lkey[ 0 ], &lmask[ 0 ], 0, &node );

    if( ( NULL == node ) || ( NGX_HTTP_LKLB_MATCH != rc ) ) {
        goto ldone;
    }

    value = node->value;

    if( !( ngx_http_lklb_radix_is_leaf_node( node ) ) ) {
        if( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) {
            node->value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
        }

        goto ldone;
    }

    while( 1 ) {
        if( node == node->parent->right ) {
            node->parent->right = NULL;
        } else {
            node->parent->left = NULL;
        }

        node->right = tree->free;
        tree->free  = node;

        node = node->parent;

        if( !( ngx_http_lklb_radix_is_leaf_node( node ) )   ||
            ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ||
            ( NGX_HTTP_LKLB_RADIX_NODE_IS_ROOT( node ) )
          ) {
            break;
        }
    }

ldone:
    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return( ( NGX_HTTP_LKLB_RADIX_NO_VALUE != value ) ? NGX_HTTP_LKLB_MATCH : NGX_HTTP_LKLB_ERR );
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_delete(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    void                  **result
) {
    uint32_t    mask[ 4 ] = NGX_HTTP_LKLB_RADIX_UINT128_DEFAULT_MASK;

    return ngx_http_lklb_radix_uint128_delete_with_mask( tree, key, &mask[ 0 ], result );
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_find_with_mask(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    uint32_t               *mask,
    void                  **result,
    uint8_t                 prefix
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    uint32_t                     lkey[ 4 ], lmask[ 4 ];
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( ( NULL == tree ) || ( NULL == key ) || ( NULL == mask ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    ngx_memcpy( &lkey[ 0 ], key, 4 * sizeof( uint32_t ) );
    ngx_memcpy( &lmask[ 0 ], mask, 4 * sizeof( uint32_t ) );

    ngx_http_lklb_uint128_htonl( tree->transforms, &lkey[ 0 ] );
    ngx_http_lklb_uint128_htonl( tree->transforms, &lmask[ 0 ] );

    ngx_http_lklb_radix_rlock( tree );
 
    rc = ngx_http_lklb_radix_uint128_find_node( tree, &lkey[ 0 ], &lmask[ 0 ], prefix, &node );
    if( NGX_HTTP_LKLB_ERR == rc ) {
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_ERR;
    }

    if( node ) {
        value = node->value;
    }

    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return rc;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_uint128_find(
    ngx_http_lklb_radix_t  *tree,
    uint32_t               *key,
    void                  **result,
    uint8_t                 prefix
) {
    uint32_t    mask[ 4 ] = NGX_HTTP_LKLB_RADIX_UINT128_DEFAULT_MASK;

    return ngx_http_lklb_radix_uint128_find_with_mask( tree, key, &mask[ 0 ], result, prefix );
}

#define NGX_HTTP_LKLB_RADIX_UINT8_MSB    ( 1 << 7 )

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_insert(
    ngx_http_lklb_radix_t  *tree,
    uint8_t                *key,
    size_t                  key_len,
    void                   *value
) {
    uint8_t                      bit;
    uint32_t                     idx;
    ngx_http_lklb_radix_node_t  *node, *next;

    if( ( NULL == tree ) || ( NULL == key ) || ( 0 == key_len ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    idx = 0;
    bit = NGX_HTTP_LKLB_RADIX_UINT8_MSB;

    key = ngx_http_lklb_str_transform( tree->transforms, key, key_len );

    ngx_http_lklb_radix_wlock( tree );

    node = tree->root;
    next = tree->root;

    while( idx < key_len ) {
        if( key[ idx ] & bit ) {
            next = node->right;
        } else {
            next = node->left;
        }

        if( next == NULL ) {
            break;
        }

        bit  >>= 1;
        node   = next;

        if( bit == 0 ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT8_MSB;
            idx++;
        }
    }

    if( next ) {
        if( node->value != NGX_HTTP_LKLB_RADIX_NO_VALUE ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_DUP;
        }

        node->value = value;
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_MATCH;
    }

    while( idx < key_len ) {
        if( !( next = ngx_http_lklb_radix_alloc( tree ) ) ) {
            ngx_http_lklb_radix_unlock( tree );
            return NGX_HTTP_LKLB_ERR;
        }

        ngx_http_lklb_radix_init_children( next );
        next->parent = node;
        next->value  = NGX_HTTP_LKLB_RADIX_NO_VALUE;

        if( key[ idx ] & bit ) {
            node->right = next;
        } else {
            node->left = next;
        }

        bit  >>= 1;
        node   = next;

        if( bit == 0 ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT8_MSB;
            idx++;
        }
    }

    node->value = value;
    ngx_http_lklb_radix_unlock( tree );
    return NGX_HTTP_LKLB_MATCH;
}

static ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_find_node(
    ngx_http_lklb_radix_t       *tree,
    uint8_t                     *key,
    size_t                       key_len,
    uint8_t                      prefix,
    ngx_http_lklb_radix_node_t **result
) {
    uint8_t                      bit;
    uint32_t                     idx;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc = NGX_HTTP_LKLB_ERR;

    if( ( NULL == tree ) || ( NULL == key ) || ( 0 == key_len ) ) {
        return NGX_HTTP_LKLB_ERR;
    }

    idx  = 0;
    bit  = NGX_HTTP_LKLB_RADIX_UINT8_MSB;

    node = tree->root;
    while( idx < key_len ) {
        if( key[ idx ] & bit ) {
            node = node->right;
        } else {
            node = node->left;
        } 

        if( NULL == node ) {
            break;
        }

        if( ( prefix ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
            rc = NGX_HTTP_LKLB_PARTIAL_MATCH;
            break;
        }

        bit >>= 1;

        if( bit == 0 ) {
            bit  = NGX_HTTP_LKLB_RADIX_UINT8_MSB;
            idx++;
        }
    }

    if( ( node ) && ( NGX_HTTP_LKLB_ERR == rc ) && ( NGX_HTTP_LKLB_RADIX_NO_VALUE != node->value ) ) {
        rc = NGX_HTTP_LKLB_MATCH;
    }

    if( result ) {
        *result = node;
    }

    return rc;
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_delete(
    ngx_http_lklb_radix_t   *tree,
    uint8_t                 *key,
    size_t                   key_len,
    void                   **result
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    key = ngx_http_lklb_str_transform( tree->transforms, key, key_len );

    ngx_http_lklb_radix_wlock( tree );

    rc = ngx_http_lklb_radix_str_find_node( tree, key, key_len, 0, &node );

    if( ( NULL == node ) || ( NGX_HTTP_LKLB_MATCH != rc ) ) {
        goto ldone;
    }

    value = node->value;

    if( !( ngx_http_lklb_radix_is_leaf_node( node ) ) ) {
        if( node->value != NGX_HTTP_LKLB_RADIX_NO_VALUE ) {
            node->value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
        }

        goto ldone;
    }

    while( 1 ) {
        if( node == node->parent->right ) {
            node->parent->right = NULL;
        } else {
            node->parent->left = NULL;
        }

        node->right = tree->free;
        tree->free  = node;

        node = node->parent;

        if( !( ngx_http_lklb_radix_is_leaf_node( node ) )   ||
            ( node->value != NGX_HTTP_LKLB_RADIX_NO_VALUE ) ||
            ( NGX_HTTP_LKLB_RADIX_NODE_IS_ROOT( node ) )
          ) {
            break;
        }
    }

ldone:
    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return( ( NGX_HTTP_LKLB_RADIX_NO_VALUE != value ) ? NGX_HTTP_LKLB_MATCH : NGX_HTTP_LKLB_ERR );
}

ngx_http_lklb_retval_e
ngx_http_lklb_radix_str_find(
    ngx_http_lklb_radix_t   *tree,
    uint8_t                 *key,
    size_t                   key_len,
    void                   **result,
    uint8_t                  prefix
) {
    void                        *value = NGX_HTTP_LKLB_RADIX_NO_VALUE;
    ngx_http_lklb_radix_node_t  *node;
    ngx_http_lklb_retval_e       rc;

    if( NULL == tree ) {
        return NGX_HTTP_LKLB_ERR;
    }

    key = ngx_http_lklb_str_transform( tree->transforms, key, key_len );

    ngx_http_lklb_radix_rlock( tree );

    rc = ngx_http_lklb_radix_str_find_node( tree, key, key_len, prefix, &node );

    if( NGX_HTTP_LKLB_ERR == rc ) {
        ngx_http_lklb_radix_unlock( tree );
        return NGX_HTTP_LKLB_ERR;
    }

    if( node ) {
        value = node->value;
    }

    ngx_http_lklb_radix_unlock( tree );

    if( result ) {
        *result = value;
    }

    return rc;
}

static ngx_http_lklb_radix_node_t *
ngx_http_lklb_radix_alloc( ngx_http_lklb_radix_t *tree ) {
    ngx_http_lklb_radix_node_t *new_node;

    if( tree->free ) {
        new_node   = tree->free;
        tree->free = tree->free->right;
        goto lret;
    }

    if( tree->size < sizeof( ngx_http_lklb_radix_node_t ) ) {
        tree->start = NULL;

        if( tree->calloc_fnpt ) {
            tree->start = tree->calloc_fnpt( tree->mem_ctx, ngx_pagesize );
        } else if( tree->pool ) {
            tree->start = ngx_pmemalign( tree->pool, ngx_pagesize, ngx_pagesize );
        }
        
        if( NULL == tree->start ) {
            return NULL;
        }

        tree->size = ngx_pagesize;
        tree->npages++;
    }

    new_node = ( ngx_http_lklb_radix_node_t * )tree->start;
    tree->nnodes++;

    tree->start += sizeof( ngx_http_lklb_radix_node_t );
    tree->size  -= sizeof( ngx_http_lklb_radix_node_t );

lret:
    return new_node;
}
