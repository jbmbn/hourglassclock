//read charger state
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "driver/spi_slave.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "charger.h"


#define CHARGER_WAKE 23
#define CHARGER_MOSI 36
#define CHARGER_SCK  35
#define CHARGER_CS   34

#define CHARGER_DATA_DELAY (100/portTICK_RATE_MS)
#define CHARGER_ENQUEUE_DELAY (10/portTICK_RATE_MS)

#define CHARGER_SPI SPI3_HOST
#define CHARGER_SPI_DMA 1
#define CHARGER_SPI_QUEUE_SIZE 2
//packet size is actually 8 but the SPI slave of the ESP32 does not
//(always) read the last 32 bits. So we add 4 more bytes as workaround
#define CHARGER_PACKET_SIZE 12
struct spi_slave_transaction_t charger_transaction[CHARGER_SPI_QUEUE_SIZE];
uint8_t charger_rx_buffer[CHARGER_SPI_QUEUE_SIZE][CHARGER_PACKET_SIZE];

RTC_DATA_ATTR battery_info_t battery_info = { 
    .state = 0, .mode = 1, 
    .b1 = -1, .b2 = -1, .b3 = -1,
    .v1 = 0, .v2 = 0, .v3 = 0, .v4 = 0, .v5 = 0, 
    .missed_count = 0 };

battery_info_t *charger_enabled_state(void) {
    //read gpio34 and gpio35
    //11 not charging
    //10 charging1 
    //01 charging2 
    //00 charging  done
    gpio_set_direction((gpio_num_t)CHARGER_SCK, GPIO_MODE_INPUT);
    gpio_set_direction((gpio_num_t)CHARGER_MOSI, GPIO_MODE_INPUT);
    int r = 0;
    if(!gpio_get_level((gpio_num_t)CHARGER_MOSI)) r|=0x01;
    if(!gpio_get_level((gpio_num_t)CHARGER_SCK))  r|=0x02;
    gpio_reset_pin((gpio_num_t)CHARGER_MOSI);
    gpio_reset_pin((gpio_num_t)CHARGER_SCK);
    //result 0 ==> Not charging
    //result 1 ==> Charging1
    //result 2 ==> Charging2
    //result 3 ==> Charging done
    battery_info.state = r;
    return &battery_info;
}

//Called after transaction is sent/received. We use this to set the handshake line low.
static void my_post_trans_cb(spi_slave_transaction_t *trans) {
    //TODO: log packet received
}

//Signal charger module to stop charging and make sure batteries
//are connected to the motor driver. The charger module will respond
//by sending to 64 bit packages using SPI where the charger module
//is the master and the ESP is the slave.
//Therefore, this function first configures the SPI in slave mode
//and enqueues some transactions to receive the packages.
//Then it signals the charger module.
void charger_disable(void) {
    esp_err_t ret;

    //setup spi in slave mode
    spi_bus_config_t buscfg={
        .mosi_io_num=CHARGER_MOSI,
        .miso_io_num=-1,
        .sclk_io_num=CHARGER_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    spi_slave_interface_config_t slvcfg={
        .mode=0,
        .spics_io_num=CHARGER_CS,
        .queue_size=CHARGER_SPI_QUEUE_SIZE,
        .flags=0,
        .post_setup_cb=NULL,
        .post_trans_cb=my_post_trans_cb
    };
    ret=spi_slave_initialize(CHARGER_SPI, &buscfg, &slvcfg, CHARGER_SPI_DMA);
    assert(ret==ESP_OK);

    //fill spi slave queue
    for(uint8_t i=0; i<CHARGER_SPI_QUEUE_SIZE; i++) {
        charger_transaction[i].length=CHARGER_PACKET_SIZE*8;
        charger_transaction[i].tx_buffer=NULL;
        charger_transaction[i].rx_buffer=charger_rx_buffer[i];
        charger_transaction[i].user=NULL;
        ret=spi_slave_queue_trans(CHARGER_SPI,
                &charger_transaction[i], CHARGER_ENQUEUE_DELAY);
        assert(ret==ESP_OK);
    }

    //signal charger module to disable charging and send battery details
    gpio_reset_pin(CHARGER_WAKE);
    gpio_set_direction(CHARGER_WAKE, GPIO_MODE_OUTPUT);
    gpio_set_level(CHARGER_WAKE, 1);
}

//when rotating the rings is done and the motor drivers
//do not need the batteries anymore, free the charger module
//It can then resume charging the batteries when the PSU is connected
//or it can go back to sleep.
void charger_free(void) {
    //tell charger module we're done
    gpio_set_level(CHARGER_WAKE, 0);
    gpio_reset_pin(CHARGER_WAKE);
    //free the SPI resources
    spi_slave_free(CHARGER_SPI);
}


//After invoking charger_disable(), this method needs to be used
//to retrieve the data from the charger module
//The charger module returns two packets so this method needs
//to be called twice. However, it must be called from one task only
//because the result is not kept in between.
//So only one of the rotate tasks should call this and then
//when the result is received, the second rotate task can be started.
//Note: this function should only be called after invoking charger_disable()
//      and before invoking charger_free()
battery_info_t *charger_get_battery_state(void) {
    spi_slave_transaction_t *trans;
    if(spi_slave_get_trans_result(CHARGER_SPI, &trans, CHARGER_DATA_DELAY)==ESP_OK) {
        //Received the data from the charger module. Handle it.
        uint8_t *msg = (uint8_t *)(trans->rx_buffer);
        ESP_LOGW("Charger", "Charger: %02X, %02X, %02X, %02X, %02X, %02X, %02X, %02X", msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6], msg[7]);
        battery_info.missed_count = 0;
        battery_info.state = (int)(msg[1]&0x07);
        if(battery_info.state>3) battery_info.state = 3;
        battery_info.mode=(msg[1]&0x08)?1:0;
        battery_info.v1 = ((int)(msg[2]))*5;
        battery_info.v2 = ((int)(msg[3]))*5;
        battery_info.v3 = ((int)(msg[4]))*5;
        battery_info.v4 = ((int)(msg[5]))*5;
        battery_info.v5 = ((int)(msg[6]))*5;

        //detrmine values for b1 (ESP32)
        if(battery_info.v1>370) battery_info.b1=3;
        else if(battery_info.v1>355) battery_info.b1=2;
        else if(battery_info.v1>340) battery_info.b1=1;
        else if(battery_info.v1>330) battery_info.b1=0;
        else battery_info.b1=-1;

        //detrmine values for b2
        if(battery_info.v3>740) battery_info.b2=3;
        else if(battery_info.v3>720) battery_info.b2=2;
        else if(battery_info.v3>700) battery_info.b2=1;
        else if(battery_info.v3>600) battery_info.b2=0;
        else battery_info.b2=-1;
        battery_info.v3-=battery_info.v2;
        if(battery_info.v2<=300) battery_info.b2=-1;
        else if(battery_info.v3<=300) battery_info.b2=-1;
        else if(battery_info.v2<=350) battery_info.b2=0;
        else if(battery_info.v3<=350) battery_info.b2=0;
 
        //detrmine values for b3
        if(battery_info.mode) {
            battery_info.b3=-1;
            battery_info.v5-=battery_info.v4;
        } else {
            if(battery_info.v5>740) battery_info.b3=3;
            else if(battery_info.v5>720) battery_info.b3=2;
            else if(battery_info.v5>700) battery_info.b3=1;
            else if(battery_info.v5>600) battery_info.b3=0;
            else battery_info.b3=-1;
            battery_info.v5-=battery_info.v4;
            if(battery_info.v4<=300) battery_info.b3=-1;
            else if(battery_info.v5<=300) battery_info.b3=-1;
            else if(battery_info.v4<=350) battery_info.b3=0;
            else if(battery_info.v5<=350) battery_info.b3=0;
        }
    } else {
        battery_info.missed_count++;
        if(battery_info.missed_count>3) {
            battery_info.missed_count=99;
            battery_info.state=0;
            battery_info.b1=-1;
            battery_info.b2=-1;
            battery_info.b3=-1;
        }
    }
    return &battery_info;
}

