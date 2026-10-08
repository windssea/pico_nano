#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
typedef void *httpd_handle_t;
typedef enum {HTTP_GET,HTTP_POST,HTTP_PUT,HTTP_DELETE} httpd_method_t;
typedef struct httpd_req {httpd_handle_t handle;int method;const char *uri;size_t content_len;void *user_ctx,*aux;} httpd_req_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
typedef struct {unsigned stack_size,max_req_hdr_len,max_uri_len,server_port,ctrl_port,max_open_sockets,max_uri_handlers,max_resp_headers,recv_wait_timeout,send_wait_timeout;void *uri_match_fn,*global_user_ctx;void (*global_user_ctx_free_fn)(void *);esp_err_t (*open_fn)(httpd_handle_t,int);} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_RESP_USE_STR -1
#define HTTPD_SOCK_ERR_FAIL -1
#define HTTPD_SOCK_ERR_TIMEOUT -3
typedef int (*httpd_recv_func_t)(httpd_handle_t,int,char *,size_t,int);
esp_err_t httpd_sess_set_recv_override(httpd_handle_t,int,httpd_recv_func_t);
void *httpd_get_global_user_ctx(httpd_handle_t);
extern void *httpd_uri_match_wildcard;
esp_err_t httpd_start(httpd_handle_t *,const httpd_config_t *);
esp_err_t httpd_stop(httpd_handle_t);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
size_t httpd_req_get_hdr_value_len(httpd_req_t *,const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_send(httpd_req_t *,const char *,int);
