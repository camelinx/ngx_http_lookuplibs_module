#include "ngx_http_lookuplibs_module.h"

uint32_t
ngx_http_lklb_uint32_htonl( uint32_t transforms, uint32_t data ) {
    if( NGX_HTTP_LKLB_TRANSFORM_HTONL & transforms ) {
        data = htonl( data );
    }

    return data;
}

uint32_t *
ngx_http_lklb_uint128_htonl( uint32_t transforms, uint32_t *data ) {
    if( NULL == data ) {
        return NULL;
    }

    if( NGX_HTTP_LKLB_TRANSFORM_HTONL & transforms ) {
        data[ 0 ] = htonl( data[ 0 ] );
        data[ 1 ] = htonl( data[ 1 ] );
        data[ 2 ] = htonl( data[ 2 ] );
        data[ 3 ] = htonl( data[ 3 ] );
    }

    return data;
}

uint8_t *
ngx_http_lklb_str_transform( uint32_t transforms, uint8_t *data, size_t len ) {
    uint8_t *s, *e;

    if( NULL == data ) {
        return NULL;
    }

    if( !( ( NGX_HTTP_LKLB_TRANSFORM_TOLOWER | NGX_HTTP_LKLB_TRANSFORM_REVERSE ) & transforms ) ) {
        return data;
    }

    s = ( uint8_t * )data;
    e = ( uint8_t * )( data + len - 1 );

    while( s < e ) {
        if( NGX_HTTP_LKLB_TRANSFORM_TOLOWER & transforms ) {
            *s = ngx_tolower( *s );
            *e = ngx_tolower( *e );
        }

        if( NGX_HTTP_LKLB_TRANSFORM_REVERSE & transforms ) {
            *s = *s ^ *e;
            *e = *s ^ *e;
            *s = *s ^ *e;
        }

        s++;
        e--;
    }

    if( s == e ) {
        *s = ngx_tolower( *s );
    }

    return data;
}
