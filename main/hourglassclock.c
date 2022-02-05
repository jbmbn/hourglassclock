/* hourglass clock
 *
 */
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "wifi.h"
#include "eink.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "setup.h"
#include "esp_ota_ops.h"

#include "rotate.h"
#include "charger.h"

#include "ulp_utils.h"



static const char *TAG = "hourglassclock";

/*
 * Counter of unexpected resets
 * This counter is use to prevent reboot loops
 * and unwanted activating of the motors
 * When the CPU battery is almost empty, the NTP sync will fail first
 * because the wifi needs a lot of current. This will cause a brownout reset.
 */
RTC_NOINIT_ATTR int    crashCount;
RTC_NOINIT_ATTR int    syncCrashed;
RTC_NOINIT_ATTR int    lastChargerState;
RTC_NOINIT_ATTR int    coldStart;
RTC_NOINIT_ATTR time_t lastSyncTime;

/* Variable holding number of times ESP32 restarted since first boot.
 * It is placed into RTC memory using RTC_DATA_ATTR and
 * maintains its value when ESP32 wakes from deep sleep.
 */
RTC_DATA_ATTR int boot_count = 0;

//a timestamp before 1-1-2020 is too old for a clock
//so time is not valid
#define INVALID_TIME (24*60*60*(4*365+1)*((2020-1970)/4))

static void obtain_time(void);
static void initialize_sntp(void);

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static uint64_t millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000ULL + (tv.tv_usec / 1000ULL));
}

void app_main(void)
{
    uint64_t wakeuptime = millis();
    int crashDetect=0;
    ++boot_count;
    esp_reset_reason_t resetReason = esp_reset_reason();
    ESP_LOGW(TAG, "Boot count: %d, reason: %d", boot_count, resetReason);

    //check reset reason to prevent boot loops
    //as much as possible
    if(crashCount>5) crashCount=5;
    if(resetReason==ESP_RST_DEEPSLEEP) {
       //running normally. Decrement crash counter
       if(crashCount>0) crashCount--;
       else crashCount=0;
    } else if(resetReason==ESP_RST_POWERON ||
       resetReason==ESP_RST_EXT     ||
       resetReason==ESP_RST_SW) {
       //normal reset. Reset crash counter
       crashCount=0;
       lastSyncTime=0;
       lastChargerState=-1;
       coldStart=-1;
    } else {
       //unexpected reset. increment crash counter
       crashCount++;
       crashDetect=1;
    }

    ESP_ERROR_CHECK( nvs_flash_init() );

    //Check wakeup reason. When EXT1, switch to management mode
    //Can't do this on connecting the charger because it would
    //wakeup continuously af long as the charger is connected
    //as this wakeup is a level interrupt
    if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT1) {
        //start management mode
        ESP_LOGI(TAG, "Switch to management mode");
        setup();
        //this function will not return.
        //It'll end with a reboot
    }

    time_t now;
    time(&now);
    //check current time to find out what to do
    //round minutes up when seconds>=57
    //Do a time sync at 04.57AM UTC
    int fullUpdate = 0;
    int doSync = 0;
    int minutes = ((now+3)/60)%(24*60); 
    if(minutes==297 && (lastSyncTime+86300)<now) doSync=1; //time to resync
    minutes = minutes%60;
    if(lastSyncTime==0) {
        //cold boot so force synchronisation
        if(now<310) { 
            doSync=1;       //try every minute in the first 5 minutes
            fullUpdate=-1;   //force display update
        } else {
            if(minutes==57) {
                doSync=1;       //Sync when minute is 57 of every hour
                fullUpdate=-1;  //force display update
            }
        }
    }
    ESP_LOGW(TAG, "Starting: %ld, %ld, %d, %d, %d, %d", now, lastSyncTime, minutes, doSync, fullUpdate, crashCount);
    if(crashCount>2) doSync=0; //too many consecutive crashes. Don't sync
    if(doSync) {
        syncCrashed = 1;
        ESP_LOGI(TAG, "Time sync required. Connecting to WiFi and getting time over NTP.");
        obtain_time();
        //When we get here time sync did not crash
        //but if it's immediately after a brownout, still indicate the crash
        if(!crashDetect) syncCrashed = 0;
        // update 'now' variable with current time
        time(&now);
        minutes = ((now+3)/60)%60; //increment minutes when seconds>=57
    }

    //start updating the eink display
    int doRotate=0;
    int chargerState=0;
    if(minutes==0) {
        fullUpdate=-1;
        doRotate=1;
    }
    if(coldStart) {
        //first start up. Rings have not rotated yet.
        doRotate=1;
        fullUpdate=-1;
    }
    if(!fullUpdate && (minutes%10)==0) fullUpdate=1;
    
    battery_info_t *battery_info;
    if(crashDetect) doRotate=0; //don't turn on motors after unexpected reset
    if(doRotate) {
        //full hour, need to rotate the rings
        //first disable charger
        charger_disable();

        //now determine current hour
        struct tm timeinfo;
        // Set timezone to Central European Standard Time
        setenv("TZ", "CET-1CEST-2,M3.5.0/2,M10.5.0/3", 1);
        tzset();
        localtime_r(&now, &timeinfo);
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG,"The current date/time in Amsterdam is: %s", strftime_buf);

        //determine current hour in range 1-12
        int hour=timeinfo.tm_hour;
        if(timeinfo.tm_min==59) hour++;
        hour=hour%12;
        if(hour==0) hour=12;

        //get battery voltages and charger state (twice, first old, then new)
        battery_info=charger_get_battery_state();
        battery_info=charger_get_battery_state();
        //check battery levels OK
        if((battery_info->b2>=0)
          &&((battery_info->mode)||(battery_info->b3>=0))) {
            //run rotate task
            ESP_LOGW(TAG, "Start rotate");
            rotate_set_time(hour);
            coldStart=0;
        }
    } else {
        //get current charger status
        battery_info=charger_enabled_state();
    }
    //change charger icon for 3 battery mode 
    //(i.e. 2 icons showing) when charging
    chargerState = battery_info->state;
    if(battery_info->mode && chargerState==1) chargerState=4;
    ESP_LOGI(TAG, "Charger state: %d\n",battery_info->state);

    //start updating the eink display
    uint64_t startEink=millis()-wakeuptime;
    eink_start();
    eink_init(fullUpdate);
    if(!fullUpdate && lastChargerState==chargerState) {
        //not a full display update and charger state has not changed
        //so no need to update
        chargerState=-1; 
    } else {
        //remember current chargerState for next run
        lastChargerState = chargerState;
    }
    int syncState = 3; //Sync is too long ago
    if((now-4*86400)<lastSyncTime) syncState = (int)((now-lastSyncTime)/86400);
    if(syncCrashed) syncState+=4; //reboot during time sync. Show on display
    eink_display_number(minutes,
                        //b1: battery for EPS32 (right symbol)
                        //when detecting low level and sync crashed, it's dead!
                        (syncCrashed&&battery_info->b1==0)?-1: battery_info->b1,
                        battery_info->b2,  //battery for Motor1 or both (left)
                        battery_info->b3,  //battery for none or Motor2 (center)
                        syncState,
                        chargerState,
                        fullUpdate);
    uint64_t displayEink=millis()-wakeuptime;
    eink_update(fullUpdate);
    uint64_t updateEink=millis()-wakeuptime;

    if(doRotate) {
        //wait till rotating the rings is done
        while(rotate_busy()) {
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
        //free charger module
        charger_free();
    }

    //wait for eink update to complete
    //TODO: wait for eink update completion using ULP
    //eink_stop();
    uint64_t stopEink=millis()-wakeuptime;
    //eink_shutdown_io();
    uint64_t shutdownEink=millis()-wakeuptime;
    
    if(boot_count==1) {
        //We've made a full run updating the display
        //and rotating the rings to the correct time
        //mark this version as valid.
        //It needs to be done now, because the next boot
        //which is after deep sleep, it will be rolled back
        //if we don't mark this version as valid.
        if(esp_ota_mark_app_valid_cancel_rollback()==ESP_OK) {
            ESP_LOGI(TAG, "New firmware accepted");
        }
        //ulp_init only needs to be done on "cold" boot
        ulp_init();
    }

    //configure managment gpio pin to wakeup from deep sleep
    esp_sleep_enable_ext1_wakeup(0x8000000000, ESP_EXT1_WAKEUP_ALL_LOW);

    time(&now);
    long deep_sleep_sec = 60-(now%60);
    if(deep_sleep_sec<3) deep_sleep_sec+=60L;
    ESP_LOGI(TAG, "Entering deep sleep for %ld seconds (%d,%d,%d,%d,%d,%d)", deep_sleep_sec, (int)startEink, (int)displayEink, (int)updateEink, (int)stopEink, (int)shutdownEink, (int)(millis()-wakeuptime));

    //TODO: Refactor this section
    //Initialize RTC_IO for the eink display to allow the ULP to
    //bring the eink display into deep sleep mode
    if(gpio_get_level(GPIO_NUM_4)) {
        //eink display is still busy. Let ULP handle eink power down
        eink_shutdown_io();
        rtc_gpio_init(GPIO_NUM_4);
        rtc_gpio_set_direction(GPIO_NUM_4, RTC_GPIO_MODE_INPUT_ONLY);
        gpio_reset_pin(GPIO_NUM_12);
        rtc_gpio_init(GPIO_NUM_12);
        rtc_gpio_set_direction(GPIO_NUM_12, RTC_GPIO_MODE_OUTPUT_ONLY);
        gpio_reset_pin(GPIO_NUM_25);
        rtc_gpio_init(GPIO_NUM_25);
        rtc_gpio_set_direction(GPIO_NUM_25, RTC_GPIO_MODE_OUTPUT_ONLY);
        gpio_reset_pin(GPIO_NUM_27);
        rtc_gpio_init(GPIO_NUM_27);
        rtc_gpio_set_direction(GPIO_NUM_27, RTC_GPIO_MODE_OUTPUT_ONLY);
        gpio_reset_pin(GPIO_NUM_26);
        rtc_gpio_init(GPIO_NUM_26);
        rtc_gpio_set_direction(GPIO_NUM_26, RTC_GPIO_MODE_OUTPUT_ONLY);
        //rtc_gpio_pulldown_dis(GPIO_NUM_4);
        //rtc_gpio_pullup_dis(GPIO_NUM_4);
        //rtc_gpio_hold_en(GPIO_NUM_4);
        //rtc_gpio_isolate(GPIO_NUM_4);
        ulp_start();
        ESP_LOGW(TAG, "ULP_started ????");
    } else {
        ESP_LOGW(TAG, "ULP not started");
    }

    esp_deep_sleep(deep_sleep_sec*1000000l);
}

static void obtain_time(void)
{
    //TODO: determine time correction factor for the RTC
    //      to compensate for the error in the 32KHz xtal frequency
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK( esp_event_loop_create_default() );

    //Connect to wifi here!
    wifi_init_sta();

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    sntp_sync_status_t sync_status;
    while ((sync_status=sntp_get_sync_status())==SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    //not sure these are necessary
    time(&now);
    localtime_r(&now, &timeinfo);

    if(sync_status==SNTP_SYNC_STATUS_COMPLETED) {
        //time sync succesful
        lastSyncTime=now;
    }

    //ESP_ERROR_CHECK( example_disconnect() );
    wifi_stop_sta();
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}
