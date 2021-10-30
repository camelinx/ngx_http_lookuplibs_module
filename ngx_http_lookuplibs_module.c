#include "ngx_http_lookuplibs_module.h"
#include "ngx_http_lookuplib_radix_tree.h"
#include "ngx_http_lookuplibs_lua.h"
#include "ngx_http_lookuplibs_internal.h"

static ngx_conf_enum_t ngx_http_lklb_types[ ] = {
    { ngx_string( "radix" ), NGX_HTTP_LKLB_TYPE_RADIX }
    /* Add newer types here */
};

static ngx_conf_enum_t ngx_http_lklb_transforms[ ] = {
    { ngx_string( "htonl" ), NGX_HTTP_LKLB_TRANSFORM_HTONL },
    { ngx_string( "tolower" ), NGX_HTTP_LKLB_TRANSFORM_TOLOWER },
    { ngx_string( "reverse" ), NGX_HTTP_LKLB_TRANSFORM_REVERSE }
    /* Add newer transforms here */
};

static char *
ngx_http_lklb_lua_shared_lookuplib( ngx_conf_t *cf, ngx_command_t *cmd, void *conf );

static ngx_int_t
ngx_http_lklb_post_config_init( ngx_conf_t *cf );

static void *
ngx_http_lklb_create_main_conf(ngx_conf_t *cf );

static ngx_command_t  ngx_http_lookuplibs_commands[ ] = {
    { ngx_string( "lua_shared_lookup" ),
      NGX_HTTP_MAIN_CONF | NGX_CONF_2MORE,
      ngx_http_lklb_lua_shared_lookuplib,
      0,
      0,
      NULL },

    ngx_null_command
};

static ngx_http_module_t  ngx_http_lookuplibs_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_http_lklb_post_config_init,         /* postconfiguration */

    ngx_http_lklb_create_main_conf,         /* create main configuration */
    NULL,                                   /* init main configuration */

    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */

    NULL,                                   /* create location configuration */
    NULL                                    /* merge location configuration */
};

ngx_module_t  ngx_http_lookuplibs_module = {
    NGX_MODULE_V1,
    &ngx_http_lookuplibs_module_ctx,       /* module context */
    ngx_http_lookuplibs_commands,          /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

#define NGX_HTTP_LKLB_NAME_IDX          1
#define NGX_HTTP_LKLB_SIZE_IDX          2
#define NGX_HTTP_LKLB_TYPE_IDX          3
#define NGX_HTTP_LKLB_TRANSFORMS_IDX    4

/*
 * Handler for lua_shared_lookup directive. This directive will take 2 or
 * more arguments. The simplest to way to setup is
 *      "lua_shared_lookup <shared segment name> <size>"
 * This will default to a radix backed lookup segment without any transformations.
 *      "lua_shared_lookup <shared segment name> <size> radix"
 * Same as above except radix is stated explicitly
 *      "lua_shared_lookup <shared segment name> <size> radix <list of transforms>
 * Extending configuration to apply transformations on input before storing.
 * Supported transformations are "htonl" - e.g. ip addresses, tolower or reverse
 * Strings can be reversed before being inserted to support domain suffix match lookup
 * use case.
 */
static char *
ngx_http_lklb_lua_shared_lookuplib( ngx_conf_t *cf, ngx_command_t *cmd, void *conf ) {
    ngx_http_lklb_main_conf_t   *lklbmcf = conf;
    ngx_http_lklb_shared_t      *shared_lib;
    ngx_http_lklb_ctx_t         *lklb_ctx;
    ngx_str_t                   *value, type;
    ngx_uint_t                   idx, itype, tflag;
    ssize_t                      size;

    if( NULL == lklbmcf->shared_libs ) {
        lklbmcf->shared_libs = ngx_array_create( cf->pool, 2, sizeof( ngx_http_lklb_shared_t ) );
        if( NULL == lklbmcf->shared_libs ) {
            return NGX_CONF_ERROR;
        }
    }

    value = cf->args->elts;

    if( 0 == value[  NGX_HTTP_LKLB_NAME_IDX] .len ) {
        ngx_conf_log_error( NGX_LOG_EMERG, cf, 0,
                            "invalid shared lookup lib name \"%V\"", &value[ NGX_HTTP_LKLB_NAME_IDX ] );
        return NGX_CONF_ERROR;
    }

    size = ngx_parse_size( &value[ NGX_HTTP_LKLB_SIZE_IDX ] );
    if( size < ( ssize_t )( 8 * ngx_pagesize ) ) {
        ngx_conf_log_error( NGX_LOG_EMERG, cf, 0,
                            "invalid shared lookup lib size \"%v\"", &value[ NGX_HTTP_LKLB_SIZE_IDX ] );
        return NGX_CONF_ERROR;
    }

    tflag = 0;

    /* cf->args->nelts > NGX_HTTP_LKLB_TYPE_IDX means backing implementation is explicitly configured */
    if( cf->args->nelts > NGX_HTTP_LKLB_TYPE_IDX ) {
        if( 0 == value[ NGX_HTTP_LKLB_TYPE_IDX ].len ) {
            ngx_conf_log_error( NGX_LOG_EMERG, cf, 0,
                                "invalid shared lookup lib type \"%V\"", &value[ NGX_HTTP_LKLB_TYPE_IDX ] );
            return NGX_CONF_ERROR;
        }

        type  = value[ NGX_HTTP_LKLB_TYPE_IDX ];
        itype = NGX_HTTP_LKLB_TYPE_MAX;

        for( idx = 0; idx < NGX_HTTP_LKLB_TYPE_MAX; idx++ ) {
            if( ( type.len == ( ngx_http_lklb_types[ idx ] ).name.len ) &&
                ( !ngx_strncasecmp( type.data, ( ngx_http_lklb_types[ idx ] ).name.data, type.len ) ) ) {
                itype = ( ngx_http_lklb_types[ idx ] ).value;
                break;
            }
        }

        if( NGX_HTTP_LKLB_TYPE_MAX == itype ) {
            ngx_conf_log_error( NGX_LOG_EMERG, cf, 0,
                                "unknown shared lookup lib type \"%V\"", &value[ NGX_HTTP_LKLB_TYPE_IDX ] );
            return NGX_CONF_ERROR;
        }

        /* cf->args->nelts > NGX_HTTP_LKLB_TRANSFORMS_IDX means transformations are explicitly configured */
        if( cf->args->nelts > NGX_HTTP_LKLB_TRANSFORMS_IDX ) {
            ngx_uint_t  tidx;
            ngx_str_t   transform;

            for( idx = NGX_HTTP_LKLB_TRANSFORMS_IDX; idx <= cf->args->nelts; idx++ ) {
                for( tidx = 0; tidx < sizeof( ngx_http_lklb_transforms ) / sizeof( ngx_conf_enum_t ); tidx++ ) {
                    transform = ( ngx_http_lklb_transforms[ tidx ] ).name;

                    if( ( ( value[ idx ] ).len == transform.len ) &&
                        ( !ngx_strncasecmp( ( value[ idx ] ).data, transform.data, transform.len ) ) ) {
                        tflag |= ( ngx_http_lklb_transforms[ tidx ] ).value;
                    }
                }
            }
        }
    } else {
        itype = NGX_HTTP_LKLB_TYPE_RADIX;
    }


    shared_lib = ngx_array_push( lklbmcf->shared_libs );
    if( NULL == shared_lib ) {
        return NGX_CONF_ERROR;
    }

    shared_lib->zone = ngx_shared_memory_add( cf, &value[ 2 ], size, &ngx_http_lookuplibs_module );
    if( NULL == shared_lib->zone ) {
        return NGX_CONF_ERROR;
    }

    lklb_ctx = ngx_pcalloc( cf->pool, sizeof( ngx_http_lklb_ctx_t ) );
    if( NULL == lklb_ctx ) {
        return NGX_CONF_ERROR;
    }

    lklb_ctx->type       = itype;
    lklb_ctx->transforms = tflag;
    lklb_ctx->lklbmcf    = lklbmcf;

    shared_lib->zone->init    = ngx_http_lklb_shm_init;
    shared_lib->zone->data    = lklb_ctx;
    shared_lib->zone->noreuse = 1;

    return NGX_CONF_OK;
}

static void *
ngx_http_lklb_create_main_conf(ngx_conf_t *cf ) {
    ngx_http_lklb_main_conf_t   *lklbmcf;

    lklbmcf = ngx_pcalloc( cf->pool, sizeof( ngx_http_lklb_main_conf_t ) );
    if( NULL == lklbmcf ) {
        return NULL;
    }

    return lklbmcf;
}

static ngx_int_t
ngx_http_lklb_post_config_init( ngx_conf_t *cf ) {
    ngx_http_lklb_main_conf_t   *lklbmcf;

    lklbmcf = ngx_http_conf_get_module_main_conf( cf, ngx_http_lookuplibs_module );
    if( NULL == lklbmcf ) {
         return NGX_ERROR;
    }

    if( NGX_OK != ( ngx_http_lua_add_package_preload( cf, "ngxlookuplibs", ngx_http_lklb_create_lua_module ) ) ) {
        return NGX_ERROR;
    }

    return NGX_OK;
}
