#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_bms_ota_handle_http_request(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
