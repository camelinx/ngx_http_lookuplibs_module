#ifndef _NGX_HTTP_LOOKUPLIBS_MODULE_H_INCLUDED_
#define _NGX_HTTP_LOOKUPLIBS_MODULE_H_INCLUDED_

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_string.h>

typedef enum {
    NGX_HTTP_LKLB_ERR              = -1,
    NGX_HTTP_LKLB_MATCH            =  0,
    NGX_HTTP_LKLB_PARTIAL_MATCH    =  1,
    NGX_HTTP_LKLB_DUP              =  2
} ngx_http_lklb_retval_e;

#define NGX_HTTP_LKLB_TRANSFORM_HTONL        1
#define NGX_HTTP_LKLB_TRANSFORM_TOLOWER      2
#define NGX_HTTP_LKLB_TRANSFORM_REVERSE      4

#endif /* _NGX_HTTP_LOOKUPLIBS_MODULE_H_INCLUDED_ */
