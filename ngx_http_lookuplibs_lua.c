#include "ngx_http_lookuplibs_module.h"
#include "ngx_http_lookuplibs_internal.h"
#include "ngx_http_lookuplib_radix_tree.h"
#include "ngx_http_lookuplibs_lua.h"

static void *ngx_http_lklb_shmem_calloc( void *shpool, size_t size );
static void  ngx_http_lklb_shmem_free( void *shpool, void *ptr );
static void  ngx_http_lklb_tree_rlock( void *lock_ctx );
static void  ngx_http_lklb_tree_wlock( void *lock_ctx );
static void  ngx_http_lklb_tree_unlock( void *lock_ctx );

typedef ngx_int_t ( *lklb_ctx_init_pt )( ngx_http_lklb_ctx_t * );
typedef ngx_int_t ( *lklb_ctx_get_pt )( ngx_http_lklb_ctx_t * );
typedef ngx_int_t ( *lklb_ctx_set_pt )( ngx_http_lklb_ctx_t * );
typedef ngx_int_t ( *lklb_ctx_copy_pt )( ngx_http_lklb_ctx_t *, ngx_http_lklb_ctx_t * );

typedef struct {
    lklb_ctx_init_pt    init_handler;
    lklb_ctx_get_pt     get_handler;
    lklb_ctx_set_pt     set_handler;
    lklb_ctx_copy_pt    copy_handler;
} ngx_http_lklb_ctx_handlers_t;

static ngx_int_t ngx_http_lklb_init_radix_ctx( ngx_http_lklb_ctx_t *ctx );
static ngx_int_t ngx_http_lklb_get_radix_ctx( ngx_http_lklb_ctx_t *ctx );
static ngx_int_t ngx_http_lklb_set_radix_ctx( ngx_http_lklb_ctx_t *ctx );
static ngx_int_t ngx_http_lklb_copy_radix_ctx( ngx_http_lklb_ctx_t *ctx, ngx_http_lklb_ctx_t *octx );

ngx_http_lklb_ctx_handlers_t ctx_handlers[ NGX_HTTP_LKLB_TYPE_MAX ] = {
    /* NGX_HTTP_LKLB_TYPE_RADIX */
    { ngx_http_lklb_init_radix_ctx,
      ngx_http_lklb_get_radix_ctx,
      ngx_http_lklb_set_radix_ctx,
      ngx_http_lklb_copy_radix_ctx }
};

static ngx_int_t
ngx_http_lklb_init_radix_ctx( ngx_http_lklb_ctx_t *ctx ) {
    ngx_http_lklb_radix_ctx_t   *radix_ctx;

    radix_ctx = ngx_slab_calloc( ctx->shpool, sizeof( ngx_http_lklb_radix_ctx_t ) );
    if( NULL == radix_ctx ) {
        return NGX_ERROR;
    }

    radix_ctx->tree = ngx_http_lklb_radix_create( NULL, ctx->shpool, ctx->transforms,
                                                  ngx_http_lklb_shmem_calloc,
                                                  ngx_http_lklb_shmem_free );
    if( NULL == radix_ctx->tree ) {
        return NGX_ERROR;
    }

    ngx_http_lklb_radix_set_lock_functions( radix_ctx->tree, radix_ctx,
                                            ngx_http_lklb_tree_rlock,
                                            ngx_http_lklb_tree_wlock,
                                            ngx_http_lklb_tree_unlock );


    ngx_http_lklb_ctx_radix( ctx ) = radix_ctx;
    return NGX_OK;
}

static ngx_int_t
ngx_http_lklb_get_radix_ctx( ngx_http_lklb_ctx_t *ctx ) {
    ngx_http_lklb_ctx_radix( ctx ) = ctx->shpool->data;
    return NGX_OK;
}

static ngx_int_t
ngx_http_lklb_set_radix_ctx( ngx_http_lklb_ctx_t *ctx ) {
    ctx->shpool->data = ngx_http_lklb_ctx_radix( ctx );
    return NGX_OK;
}

static ngx_int_t
ngx_http_lklb_copy_radix_ctx( ngx_http_lklb_ctx_t *ctx, ngx_http_lklb_ctx_t *octx ) {
    if( NGX_HTTP_LKLB_TYPE_RADIX != octx->type ) {
        return NGX_ERROR;
    }

    ngx_http_lklb_ctx_radix( ctx ) = ngx_http_lklb_ctx_radix( octx );

    return NGX_OK;
}

ngx_int_t
ngx_http_lklb_shm_init( ngx_shm_zone_t *shm_zone, void *data ) {
    ngx_http_lklb_ctx_t         *octx, *ctx;

    octx = data;
    ctx  = shm_zone->data;

    if( ctx->type >= NGX_HTTP_LKLB_TYPE_MAX ) {
        return NGX_ERROR;
    }

    if( octx ) {
        ctx->type   = octx->type;
        ctx->shpool = octx->shpool;

        if( NGX_OK != ( ctx_handlers[ ctx->type ] ).copy_handler( ctx, octx ) ) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    ctx->shpool  = ( ngx_slab_pool_t * )shm_zone->shm.addr;

    if( shm_zone->shm.exists ) {
        if( NGX_OK != ( ctx_handlers[ ctx->type ] ).get_handler( ctx ) ) {
            return NGX_ERROR;
        }

        return NGX_OK;
    }

    if( NGX_OK != ( ctx_handlers[ ctx->type ] ).init_handler( ctx ) ) {
        return NGX_ERROR;
    }

    if( NGX_OK != ( ctx_handlers[ ctx->type ] ).set_handler( ctx ) ) {
        return NGX_ERROR;
    }

    return NGX_OK;
}

static void *
ngx_http_lklb_shmem_calloc( void *shpool, size_t size )
{
    return ngx_slab_calloc( ( ngx_slab_pool_t * )shpool, size );
}

static void
ngx_http_lklb_shmem_free( void *shpool, void *ptr )
{
    ngx_slab_free( ( ngx_slab_pool_t * )shpool, ptr );
}

static void
ngx_http_lklb_tree_rlock( void *lock_ctx )
{
    ngx_http_lklb_radix_ctx_t   *radix_ctx = lock_ctx;

    ngx_rwlock_rlock( &radix_ctx->rwlock );
}

static void
ngx_http_lklb_tree_wlock( void *lock_ctx )
{
    ngx_http_lklb_radix_ctx_t   *radix_ctx = lock_ctx;

    ngx_rwlock_wlock( &radix_ctx->rwlock );
}

static void
ngx_http_lklb_tree_unlock( void *lock_ctx )
{
    ngx_http_lklb_radix_ctx_t   *radix_ctx = lock_ctx;

    ngx_rwlock_unlock( &radix_ctx->rwlock );
}

int
ngx_http_lklb_create_lua_module( lua_State *L ) {
    ngx_http_lklb_main_conf_t   *lklbmcf;

    lua_getglobal( L, NGX_HTTP_LKLB_MCF_KEY );
    lklbmcf = lua_touserdata( L, -1 );
    lua_pop( L, 1 );

    if( NULL == lklbmcf ) {
        return -1;
    }

#ifdef NGX_HTTP_LKLB_ENABLE_LUA_FUNCTIONS

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_insert_lua );
    lua_setfield( L, -2, "insert_ipv4" );

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_mask_insert_lua );
    lua_setfield( L, -2, "insert_ipv4_with_mask" );

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_delete_lua );
    lua_setfield( L, -2, "delete_ipv4" );

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_mask_delete_lua );
    lua_setfield( L, -2, "delete_ipv4_with_mask" );

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_find_lua );
    lua_setfield( L, -2, "find_ipv4" );

    lua_pushcfunction( L, ngx_http_lklb_radix_uint32_mask_find_lua );
    lua_setfield( L, -2, "find_ipv4_with_mask" );

#endif /* NGX_HTTP_LKLB_ENABLE_LUA_FUNCTIONS */

    return 1;
}
