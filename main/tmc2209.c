#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define BAUD_RATE    230400
#define MOTOR1_EN    17
#define MOTOR1_TXD   19
#define MOTOR1_RXD   UART_PIN_NO_CHANGE
#define MOTOR2_EN    16
#define MOTOR2_TXD   18
#define MOTOR2_RXD   UART_PIN_NO_CHANGE


static uint8_t tmc2209_calc_crc(uint8_t datagram[], int len) {
    uint8_t crc=0;
    for(int i=0; i<len; i++) {
        uint8_t b=datagram[i];
        for(int j=0; j<8; j++) {
            if((crc>>7)^(b&0x01)) crc=(crc<<1)^0x07;
            else crc=(crc<<1);
            b>>=1;
        }
    }
    return crc;
}

static void tmc2209_write(int motorId, uint8_t reg, uint32_t val) {
    uint8_t datagram[8];

    datagram[0] = 0x05;
    datagram[1] = 0x00;
    datagram[2] = 0x80 | reg;
    datagram[3] = (uint8_t)((val>>24)&0xFF);
    datagram[4] = (uint8_t)((val>>16)&0xFF);
    datagram[5] = (uint8_t)((val>>8)&0xFF);
    datagram[6] = (uint8_t)(val&0xFF);
    datagram[7] = tmc2209_calc_crc(datagram, sizeof(datagram)-1);

    uart_write_bytes((motorId==1)?UART_NUM_1:UART_NUM_2, 
                 (const char *) datagram, sizeof(datagram));
    //TODO: log error when uart_write_bytes does not send all bytes
}

static void tmc2209_chip_init(int motorId) {
    tmc2209_write(motorId, 0x00, 0x000001C1);
    tmc2209_write(motorId, 0x01, 0x00000001);
    tmc2209_write(motorId, 0x10, 0x00011F1F); //max power
    tmc2209_write(motorId, 0x6C, 0x10020053);
    tmc2209_write(motorId, 0x70, 0xC10D0024);
}

void tmc2209_stop(int motorId) {
    //tell driver to stop the motor
    if(motorId==1) {
        tmc2209_write(UART_NUM_1, 0x22, 0);
        //disable motor driver
        gpio_set_level(MOTOR1_EN, 1);
    } else {
        tmc2209_write(UART_NUM_2, 0x22, 0);
        //disable motor driver
        gpio_set_level(MOTOR2_EN, 1);
    }
}

void tmc2209_rotate_cw(int motorId, int32_t velocity) {
    if(velocity==0) tmc2209_stop(motorId);
    //enable motor driver
    if(motorId==1) {
        gpio_set_level(MOTOR1_EN, 0);
        //send vactual command
        tmc2209_write(UART_NUM_1, 0x22, velocity);
    } else {
        gpio_set_level(MOTOR2_EN, 0);
        //send vactual command
        tmc2209_write(UART_NUM_2, 0x22, velocity);
    }
}

void tmc2209_rotate_cc(int motorId, int32_t velocity) {
    tmc2209_rotate_cw(motorId, 0-velocity);
}


void tmc2209_init(void) {
    //TODO: initialize UART_NUM_1 and UART_NUM_2
    uart_config_t uart_config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_FIFO_LEN*2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, MOTOR1_TXD, MOTOR1_RXD,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    //Initialize enable pin and disable motor driver
    gpio_pad_select_gpio(MOTOR1_EN);
    gpio_set_direction(MOTOR1_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(MOTOR1_EN, 1);
    tmc2209_chip_init(1);

    //init UART_NUM_2
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, UART_FIFO_LEN*2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, MOTOR2_TXD, MOTOR2_RXD,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    //Initialize enable pin and disable motor driver
    gpio_pad_select_gpio(MOTOR2_EN);
    gpio_set_direction(MOTOR2_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(MOTOR2_EN, 1);
    tmc2209_chip_init(2);
}

void tmc2209_shutdown(void) {
    //stop motors and make sure the drivers are not enabled
    tmc2209_stop(1);
    tmc2209_stop(2);
    //delete the uart drivers
    ESP_ERROR_CHECK(uart_driver_delete(UART_NUM_1));
    ESP_ERROR_CHECK(uart_driver_delete(UART_NUM_2));
    //disconnect the txd and rxd pins 
    gpio_reset_pin(MOTOR1_TXD);
    if(MOTOR1_RXD!=UART_PIN_NO_CHANGE) gpio_reset_pin(MOTOR1_RXD);
    gpio_reset_pin(MOTOR1_EN);
    gpio_reset_pin(MOTOR2_TXD);
    if(MOTOR2_RXD!=UART_PIN_NO_CHANGE) gpio_reset_pin(MOTOR2_RXD);
    gpio_reset_pin(MOTOR2_EN);
}

