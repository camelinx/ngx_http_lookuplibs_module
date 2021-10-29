#ifndef _NGX_HTTP_LOOKUPLIBS_INTERNAL_H_INCLUDED_
#define _NGX_HTTP_LOOKUPLIBS_INTERNAL_H_INCLUDED_

#include "ngx_http_lookuplibs_module.h"
#include "ngx_http_lookuplib_radix_tree.h"
#include "ngx_http_lookuplibs_lua.h"

typedef struct ngx_http_lklb_main_conf_s ngx_http_lklb_main_conf_t;
typedef struct ngx_http_lklb_ctx_s ngx_http_lklb_ctx_t;

typedef enum {
    NGX_HTTP_LKLB_TYPE_RADIX    = 0,
    /* Add newer types here */

    NGX_HTTP_LKLB_TYPE_MAX
} ngx_http_lklb_type_e;

typedef struct {
    ngx_atomic_t             rwlock;
    ngx_http_lklb_radix_t   *tree;
} ngx_http_lklb_radix_ctx_t;

struct ngx_http_lklb_ctx_s {
#define ngx_http_lklb_ctx_type( __ctx )         ( __ctx )->type
#define ngx_http_lklb_ctx_is_radix( __ctx )     ( NGX_HTTP_LKLB_TYPE_RADIX == ngx_http_lklb_ctx_type( __ctx ) )
    ngx_http_lklb_type_e             type;

    ngx_uint_t                       transforms;

#define ngx_http_lklb_ctx_type_ctx( __ctx )     ( __ctx )->type_ctx
#define ngx_http_lklb_ctx_radix( __ctx )        ( ngx_http_lklb_ctx_type_ctx( __ctx ) ).radix_ctx
    union {
        ngx_http_lklb_radix_ctx_t   *radix_ctx;
    } type_ctx;

    ngx_slab_pool_t                 *shpool;
    ngx_http_lklb_main_conf_t       *lklbmcf;
};

typedef struct {
    ngx_shm_zone_t          *zone;
    ngx_http_lklb_ctx_t     *ctx;
} ngx_http_lklb_shared_t;

struct ngx_http_lklb_main_conf_s {
    ngx_array_t             *shared_libs;
};

#define NGX_HTTP_LKLB_MCF_KEY   "__ngx_lklb_mcf"

#endif /* _NGX_HTTP_LOOKUPLIBS_INTERNAL_H_INCLUDED_ */
