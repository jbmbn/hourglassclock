#ifndef _OTA_H
#define _OTA_H

esp_err_t ota_update(httpd_req_t *req);
char * ota_get_app_version(void);

#endif
