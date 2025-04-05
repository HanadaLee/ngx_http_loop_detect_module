
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define NGX_HTTP_LOOP_CHECK_VARIABLE_CURRENT_LOOPS          0
#define NGX_HTTP_LOOP_CHECK_VARIABLE_PROXY_ADD_CDN_LOOP     1


typedef struct {
    ngx_flag_t       enable;
    ngx_str_t        cdn_id;
    ngx_uint_t       status_code;
    ngx_int_t        max_allow_loops;

    ngx_int_t        http_cdn_loop_index;
} ngx_http_loop_check_conf_t;


typedef struct {
    ngx_str_t        extra_cdn_loop;
    ngx_int_t        current_loops;
} ngx_http_loop_check_ctx_t;


static ngx_int_t ngx_http_loop_check_parse_cdn_info(u_char *item_start,
    u_char *item_last, ngx_str_t cdn_id, ngx_int_t *current_loops);
static ngx_int_t ngx_http_loop_check_parse_cdn_loop(ngx_http_request_t *r,
    ngx_http_loop_check_ctx_t *ctx);
static ngx_int_t ngx_http_loop_check_handler(ngx_http_request_t *r);
static void *ngx_http_loop_check_create_conf(ngx_conf_t *cf);
static char *ngx_http_loop_check_merge_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_loop_check_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_loop_check_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);
static ngx_int_t ngx_http_loop_check_init(ngx_conf_t *cf);


static ngx_conf_num_bounds_t  ngx_http_loop_check_status_bounds = {
    ngx_conf_check_num_bounds, 400, 599
};


static ngx_str_t  ngx_http_cdn_loop_headers = ngx_string("http_cdn_loop");


static ngx_command_t  ngx_http_loop_check_commands[] = {

    { ngx_string("loop_check"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_loop_check_conf_t, enable),
      NULL },

    { ngx_string("loop_check_cdn_id"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_loop_check_conf_t, cdn_id),
      NULL },

    { ngx_string("loop_check_status"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_loop_check_conf_t, status_code),
      &ngx_http_loop_check_status_bounds },

    { ngx_string("loop_check_max_allow_loops"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_loop_check_conf_t, max_allow_loops),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_loop_check_module_ctx = {
    ngx_http_loop_check_add_variables,  /* preconfiguration */
    ngx_http_loop_check_init,           /* postconfiguration */
    NULL,                               /* create main configuration */
    NULL,                               /* init main configuration */
    NULL,                               /* create server configuration */
    NULL,                               /* merge server configuration */
    ngx_http_loop_check_create_conf,    /* create location configuration */
    ngx_http_loop_check_merge_conf      /* merge location configuration */
};


ngx_module_t  ngx_http_loop_check_module = {
    NGX_MODULE_V1,
    &ngx_http_loop_check_module_ctx,    /* module context */
    ngx_http_loop_check_commands,       /* module directives */
    NGX_HTTP_MODULE,                    /* module type */
    NULL,                               /* init master */
    NULL,                               /* init module */
    NULL,                               /* init process */
    NULL,                               /* init thread */
    NULL,                               /* exit thread */
    NULL,                               /* exit process */
    NULL,                               /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_variable_t  ngx_http_loop_check_vars[] = {

    { ngx_string("loop_check_current_loops"), NULL,
      ngx_http_loop_check_variable,
      NGX_HTTP_LOOP_CHECK_VARIABLE_CURRENT_LOOPS,
      0, 0 },

    { ngx_string("loop_check_proxy_add_cdn_loop"), NULL,
      ngx_http_loop_check_variable,
      NGX_HTTP_LOOP_CHECK_VARIABLE_PROXY_ADD_CDN_LOOP,
      0, 0 },

      ngx_http_null_variable
};


static ngx_int_t
ngx_http_loop_check_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t  *var, *v;

    for (v = ngx_http_loop_check_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}


static void *
ngx_http_loop_check_create_conf(ngx_conf_t *cf)
{
    ngx_http_loop_check_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_loop_check_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     clcf->cdn_id = { 0, NULL };
     */

    conf->enable = NGX_CONF_UNSET;
    conf->status_code = NGX_CONF_UNSET_UINT;
    conf->max_allow_loops = NGX_CONF_UNSET;
    conf->http_cdn_loop_index = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_loop_check_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_loop_check_conf_t *prev = parent;
    ngx_http_loop_check_conf_t *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_uint_value(conf->status_code, prev->status_code, 508);
    ngx_conf_merge_value(conf->max_allow_loops, prev->max_allow_loops, 10);
    ngx_conf_merge_str_value(conf->cdn_id, prev->cdn_id, "openresty");

    conf->http_cdn_loop_index = ngx_http_get_variable_index(cf,
        &ngx_http_cdn_loop_headers);
    if (conf->http_cdn_loop_index == NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
            "loop_check: could not get variable index for $http_cdn_loop");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_loop_check_parse_cdn_info(u_char *item_start, u_char *item_last, ngx_str_t cdn_id,
    ngx_int_t *current_loops)
{
    u_char         *pos;
    ngx_int_t       loops;

    loops = 0;

    pos = ngx_strlchr(item_start, item_last, ';');
    if (pos == NULL || pos == item_start || pos == item_last) {
        return NGX_ERROR;
    }

    if ((size_t) (pos - item_start) != cdn_id.len
        || ngx_strncasecmp(item_start, cdn_id.data, (size_t) (pos - item_start)) != 0)
    {
        return NGX_ERROR;
    }

    pos++;

    if (item_last - pos < 7) {
        return NGX_ERROR;
    }

    if (ngx_strncasecmp(pos, (u_char *) " loops=", 7) != 0) {
        return NGX_ERROR;
    }

    pos += 7;

    if (pos >= item_last) {
        return NGX_ERROR;
    }

    while (pos < item_last) {
        if (*pos < '0' || *pos > '9') {
            return NGX_ERROR;
        }

        loops = loops * 10 + (*pos - '0');

        pos++;
    }

    *current_loops = loops;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_loop_check_parse_cdn_loop(ngx_http_request_t *r,
    ngx_http_loop_check_ctx_t *ctx)
{
    ngx_http_loop_check_conf_t  *conf;
    ngx_http_variable_value_t   *value;
    ngx_str_t                    cdn_loop_value;
    ngx_int_t                    current_loops;
    u_char                      *start, *last, *pos;
    u_char                      *comma, *item_start, *item_last;
    u_char                      *new_str;
    size_t                       new_len;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_loop_check_module);

    value = ngx_http_get_indexed_variable(r, conf->http_cdn_loop_index);

    if (value == NULL || value->not_found) {
        ctx->current_loops = 0;
        ctx->extra_cdn_loop.len = 0;
        ctx->extra_cdn_loop.data = NULL;

        return NGX_OK;

    }

    cdn_loop_value.len = value->len;
    cdn_loop_value.data = value->data;

    current_loops = 0;
    new_len = 0;
    new_str = NULL;
    start = cdn_loop_value.data;
    last = start + cdn_loop_value.len;
    pos = start;

    while (pos < last) {
        comma = ngx_strlchr(pos, last, ',');
        if (comma == NULL) {
            comma = last;
        }

        item_start = pos;
        item_last = comma;

        while (item_start < item_last && *item_start == ' ') {
            item_start++;
        }

        while (item_last > item_start && item_last[-1] == ' ') {
            item_last--;
        }

        if (item_start >= item_last) {
            pos = comma + 1;
            continue;
        }

        if (ngx_http_loop_check_parse_cdn_info(item_start, item_last,
            conf->cdn_id, &current_loops) == NGX_OK)
        {
            new_len = (item_start - start) + (last - (comma + 1));

            new_str = ngx_palloc(r->pool, new_len + 1);
            if (!new_str) {
                return NGX_ERROR;
            }

            ngx_memcpy(new_str, start, item_start - start);

            ngx_memcpy(new_str + (item_start - start), comma + 1, last - (comma + 1));

            new_str[new_len] = '\0';

            break;
        }

        pos = comma + 1;
    }

    ctx->current_loops = current_loops;
    ctx->extra_cdn_loop.len = new_len;
    ctx->extra_cdn_loop.data = new_str;

    return NGX_OK;
}


static ngx_int_t 
ngx_http_loop_check_variable(ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data)
{
    ngx_http_loop_check_ctx_t    *ctx;
    ngx_http_loop_check_conf_t   *conf;
    u_char                       *proxy_add_cdn_loop, *p;

    ctx = ngx_http_get_module_ctx(r, ngx_http_loop_check_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_loop_check_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_loop_check_module);

        if (ngx_http_loop_check_parse_cdn_loop(r, ctx) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (data == NGX_HTTP_LOOP_CHECK_VARIABLE_CURRENT_LOOPS) {

        v->data = ngx_palloc(r->pool, NGX_INT_T_LEN);
        if (v->data == NULL) {
            return NGX_ERROR;
        }

        v->len = ngx_sprintf(v->data, "%i", ctx->current_loops) - v->data;

    } else if (data == NGX_HTTP_LOOP_CHECK_VARIABLE_PROXY_ADD_CDN_LOOP) {

        conf = ngx_http_get_module_loc_conf(r, ngx_http_loop_check_module);

        if (ctx->extra_cdn_loop.len > 0) {
            proxy_add_cdn_loop = ngx_pnalloc(r->pool, conf->cdn_id.len
                                                  + sizeof("; loops=") - 1
                                                  + NGX_INT_T_LEN
                                                  + sizeof(", ") - 1
                                                  + ctx->extra_cdn_loop.len
                                                  + 1);

        } else {
            proxy_add_cdn_loop = ngx_pnalloc(r->pool, conf->cdn_id.len
                                                  + sizeof("; loops=") - 1
                                                  + NGX_INT_T_LEN
                                                  + 1);
        }

        if (proxy_add_cdn_loop == NULL) {
            return NGX_ERROR;
        }

        if (ctx->extra_cdn_loop.len > 0) {
            p = ngx_sprintf(proxy_add_cdn_loop, "%V; loops=%i, %V",
                    &conf->cdn_id, ctx->current_loops + 1,
                    &ctx->extra_cdn_loop);

        } else {
            p = ngx_sprintf(proxy_add_cdn_loop, "%V; loops=%i", &conf->cdn_id,
                    ctx->current_loops + 1);
        }

        v->len = p - proxy_add_cdn_loop;
        v->data = proxy_add_cdn_loop;
    }

    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}


static ngx_int_t
ngx_http_loop_check_handler(ngx_http_request_t *r)
{
    ngx_http_loop_check_conf_t  *conf;
    ngx_http_loop_check_ctx_t   *ctx;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_loop_check_module);

    if (!conf->enable) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_loop_check_module);
    if (ctx == NULL) {
        ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_loop_check_ctx_t));
        if (ctx == NULL) {
            return NGX_ERROR;
        }

        ngx_http_set_ctx(r, ctx, ngx_http_loop_check_module);

        if (ngx_http_loop_check_parse_cdn_loop(r, ctx) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    if (ctx->current_loops > conf->max_allow_loops) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "loop_check: request loops exceeded the limit, "
                      "current_loops: %i, max_allow_loops: %i",
                      ctx->current_loops, conf->max_allow_loops);
        return conf->status_code;
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_loop_check_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_loop_check_handler;

    return NGX_OK;
}