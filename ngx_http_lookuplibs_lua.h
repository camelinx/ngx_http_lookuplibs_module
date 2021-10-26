#ifndef _NGX_HTTP_LOOKUPLIBS_LUA_H_INCLUDED_
#define _NGX_HTTP_LOOKUPLIBS_LUA_H_INCLUDED_

#include <lauxlib.h>
#include "ngx_http_lua_api.h"

int ngx_http_lklb_create_lua_module( lua_State *L );

ngx_int_t ngx_http_lklb_shm_init( ngx_shm_zone_t *shm_zone, void *data );

#endif /* _NGX_HTTP_LOOKUPLIBS_LUA_H_INCLUDED_ */
