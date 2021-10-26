#ifndef _NGX_HTTP_LOOKUPLIBS_TRANSFORMS_H_INCLUDED_
#define _NGX_HTTP_LOOKUPLIBS_TRANSFORMS_H_INCLUDED_

#include "ngx_http_lookuplibs_module.h"

uint32_t
ngx_http_lklb_uint32_htonl( uint32_t transforms, uint32_t data );

uint32_t *
ngx_http_lklb_uint128_htonl( uint32_t transforms, uint32_t *data );

uint8_t *
ngx_http_lklb_str_transform( uint32_t transforms, uint8_t *data, size_t len );

#endif /* _NGX_HTTP_LOOKUPLIBS_TRANSFORMS_H_INCLUDED_*/
