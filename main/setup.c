#include <stdio.h>
#include <string.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "http_app.h"
#include "esp_netif.h"
#include "ota.h"

#include "eink.h"
#include "bitmaps.h"
#include "charger.h"

extern RTC_NOINIT_ATTR int    coldStart;

extern const char app_html_start[] asm("_binary_app_html_start");
extern const char app_html_end[]   asm("_binary_app_html_end");

extern const char css_start[] asm("_binary_app_css_start");
extern const char css_end[]   asm("_binary_app_css_end");

extern const char js_start[] asm("_binary_app_js_start");
extern const char js_end[]   asm("_binary_app_js_end");

extern const char ota_html_start[] asm("_binary_ota_html_start");
extern const char ota_html_end[]   asm("_binary_ota_html_end");


static const char TAG[] = "setup";

//150 2 second ticks. So 5 minutes
static int rebootWaitCnt = 150;

static char infomessage[2048];

static esp_err_t setup_get_handler(httpd_req_t *req){
        //got a request on the http server. Reset reboot counter
        rebootWaitCnt=150;

        if(strcmp(req->uri, "/info.json") == 0){

                ESP_LOGI(TAG, "Serving page /info.json");
                char *versionStr = ota_get_app_version();
                battery_info_t *battery_info = charger_enabled_state();

                sprintf(infomessage, "{\"version\":\"%s\",\"mode\":%d,\"batteries\":[%d,%d,%d,%d,%d],\"state\":%d,\"missed\":%d}",
                     versionStr, battery_info->mode,
                     battery_info->v1, battery_info->v2, battery_info->v3,
                     battery_info->v4, battery_info->v5,
                     battery_info->state, battery_info->missed_count);

                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "application/json");
                httpd_resp_send(req, infomessage, strlen(infomessage));
        } else if(strcmp(req->uri, "/restart") == 0){
                const char* response = "<html><body><h1>Restarting....</h1></body></html>";

                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "text/html");
                httpd_resp_send(req, response, strlen(response));
                rebootWaitCnt=2; // give a little time to show the response
        } else if(strcmp(req->uri, "/") == 0
                  || strcmp(req->uri, "/app.html") == 0
                  || strcmp(req->uri, "/index.html") == 0){
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "text/html");
                httpd_resp_send(req, app_html_start,
                        app_html_end-app_html_start);
        } else if(strcmp(req->uri, "/app.css") == 0){
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "text/css");
                httpd_resp_send(req, css_start,
                        css_end-css_start);
        } else if(strcmp(req->uri, "/app.js") == 0){
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "text/javascript");
                httpd_resp_send(req, js_start,
                        js_end-js_start);
        } else if(strcmp(req->uri, "/ota") == 0){
                httpd_resp_set_status(req, "200 OK");
                httpd_resp_set_type(req, "text/html");
                httpd_resp_send(req, ota_html_start, 
                        ota_html_end-ota_html_start);
        } else {
                /* send a 404 otherwise */
                httpd_resp_send_404(req);
        }

        return ESP_OK;
}

static esp_err_t setup_post_handler(httpd_req_t *req){
    //got a request on the http server. Reset reboot counter
    //to make sure we won't reboot during the firmware update
    rebootWaitCnt=150;
    if(strcmp(req->uri, "/ota") == 0){
        esp_err_t res = ota_update(req);
        if(res==ESP_OK) {
            //firmware update succesful. Reboot now.
            rebootWaitCnt=2; // give a little time to show the response
        }
    } else {
        /* send a 404 otherwise */
        httpd_resp_send_404(req);
    }
    //always return OK to complete handling the HTTP Post request
    return ESP_OK;
}

static uint32_t ipaddress=0;
void cb_connection_ok(void *pvParameter){
    ESP_LOGI(TAG, "I have a connection!");
    ipaddress=((ip_event_got_ip_t *)pvParameter)->ip_info.ip.addr;
}

void setup(void) {
    //Clear RTC when entering setup
    //This way the clock will have a clean start
    //when it reboots after a crash in the setup module
    //for instance when an ota update fails
    const struct timeval tv = { 0, 0 };
    settimeofday(&tv,NULL);
    //generate a (pseudo) random 8 character password for the AP
    //this password is stored in NVS on the very first boot
    //it does not do anything after the first boot because
    //the password from NVS will be loaded by wifi_manager_start()
    //after generating the random password, thus discarding it.
    //only on the very first boot with a blank NVS, the random password
    //will be used and stored in the NVS
    char *ssid;
    char *versionStr = ota_get_app_version();
    uint32_t rnd = esp_random();
    memset(wifi_settings.ap_pwd, 0, MAX_PASSWORD_SIZE);
    int i=0;
    while(i<8) {
       uint8_t c = (uint8_t)((rnd&0x0F)+'0');
       if(c>'9') c+='A'-'9'-1;
       wifi_settings.ap_pwd[i]=c;
       rnd>>=4;
       i++;
    }
    
    uint32_t ap_ip = esp_ip4addr_aton(DEFAULT_AP_IP);
    wifi_manager_start();
    int waitForIp=15; //max 30 seconds
    if(!wifi_manager_fetch_wifi_sta_config()) {
        //no ssid configured yet so show AP details
        //set ipaddress
        //set ssid sid in version string (showing 5 letters on the display)
        versionStr=(char *)(wifi_settings.ap_ssid);
        //set AP password in the ssid field showing it on the bottom line
        ssid = (char *)(wifi_settings.ap_pwd); 
        ipaddress = ap_ip;
        waitForIp=0;
    } else {
        //wait for ip address
        ssid="";
        wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
    }
    eink_start();
    eink_init(1);
    eink_display_setup(versionStr, ipaddress, ssid);
    eink_update(1);
    http_app_set_handler_hook(HTTP_GET, &setup_get_handler);
    http_app_set_handler_hook(HTTP_POST, &setup_post_handler);
    //wait 5 minutes till reboot
    while(rebootWaitCnt>0) {
        vTaskDelay(2000/portTICK_PERIOD_MS);
        rebootWaitCnt--;
        if(waitForIp>0) {
            waitForIp--;
            if(waitForIp==0) {
                //no ip address. fall back to AP details
                ssid = (char *)(wifi_settings.ap_ssid);
                ipaddress = ap_ip;
                eink_init(1);
                //show ap_ssid in version number field and the ap password
                //on the bottom line of the display
                eink_display_setup((char *)ssid, ipaddress, 
                            (char *)(wifi_settings.ap_pwd));
                eink_update(1);
            } else {
                if(ipaddress!=0) {
                    //ip address has been set
                    ssid = (char *)(wifi_manager_get_wifi_sta_config()->sta.ssid);
                    eink_init(1);
                    eink_display_setup(versionStr, ipaddress, ssid);
                    eink_update(1);
                    waitForIp=0;
                }
            }
        }
    }
    //clear the current time
    settimeofday(&tv,NULL);
    coldStart = -1;
    //cleanup
    //For some reason wifi_manager_destroy causes a panic
    //It'll get cleaned up on restart anyway
    //wifi_manager_destroy();
    eink_stop();
    //restart
    esp_restart();
}


