#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "bitmaps.h"
#include "font.h"

#ifdef CONFIG_IDF_TARGET_ESP32
#define EPD_HOST    HSPI_HOST
#define DMA_CHAN    2

#elif defined CONFIG_IDF_TARGET_ESP32S2
#define EPD_HOST    SPI2_HOST
#define DMA_CHAN    EPD_HOST
#endif

#define EINK_SPI_CS   26
#define EINK_DC       27
#define EINK_RST       5

#define TESTING 0
#if TESTING
#define EINK_BUSY     36
#else
#define EINK_BUSY      4
#endif

#define EINK_SPI_MOSI 12
#define EINK_SPI_CLK  25

#define SPI_FREQUENCY 8000000

#define EINK_RESET_DELAY 10

static spi_device_handle_t spi;

static const char* TAG = "Epd driver";

//Wave from for partial update for Heltec 1.54 200x200 pixels
static const unsigned char WF_PARTIAL[159] = {
0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x80,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x40,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x80,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x6,0x0,0x0,0x0,0x0,0x0,0x1,
0x1,0x1,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x0,0x0,0x0,0x0,0x0,0x0,0x0,
0x22,0x22,0x22,0x22,0x22,0x22,0x0,0x0,0x0,
0x02,0x17,0x41,0xB0,0x32,0x28
};	

static const unsigned char WF_PARTIAL_MODE[10] = {
    0x00,0x00,0x00,0x00,0x00,
    0x40,0x00,0x00,0x00,0x00
};

static void eink_init_io(void) {
    //printf("MOSI: %d CLK: %d\nSPI_CS: %d DC: %d RST: %d BUSY: %d\n\n",
    //    EINK_SPI_MOSI, EINK_SPI_CLK, EINK_SPI_CS,EINK_DC,EINK_RST,EINK_BUSY);
    //Initialize GPIOs direction & initial states
    gpio_reset_pin((gpio_num_t)EINK_BUSY);
    gpio_reset_pin((gpio_num_t)EINK_SPI_MOSI);
    gpio_reset_pin((gpio_num_t)EINK_SPI_CLK);
    gpio_reset_pin((gpio_num_t)EINK_DC);
    gpio_reset_pin((gpio_num_t)EINK_SPI_CS);
    gpio_set_direction((gpio_num_t)EINK_SPI_CS, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)EINK_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)EINK_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)EINK_BUSY, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)EINK_BUSY, GPIO_PULLUP_ONLY);

    gpio_set_level((gpio_num_t)EINK_SPI_CS, 1);
    gpio_set_level((gpio_num_t)EINK_DC, 1);
    gpio_set_level((gpio_num_t)EINK_RST, 1);
    
    esp_err_t ret;
    // MISO not used, only Master to Slave
    spi_bus_config_t buscfg={
        .mosi_io_num=EINK_SPI_MOSI,
        .miso_io_num = -1,
        .sclk_io_num=EINK_SPI_CLK,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=4094
    };
    //Config Frequency and SS GPIO
    spi_device_interface_config_t devcfg={
        .mode=0,  //SPI mode 0
        .clock_speed_hz=SPI_FREQUENCY,
        .input_delay_ns=0,
        .spics_io_num=EINK_SPI_CS,
        .flags = (SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE),
        .queue_size=5
    };
    // DISABLED Callbacks pre_cb/post_cb. SPI does not seem to behave the same
    // CS / DC GPIO states the usual way

    //Initialize the SPI bus
    ret=spi_bus_initialize(EPD_HOST, &buscfg, DMA_CHAN);
    ESP_ERROR_CHECK(ret);

    //Attach the EPD to the SPI bus
    ret=spi_bus_add_device(EPD_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
    
    //printf("EpdSpi::init() Debug enabled. SPI master at frequency:%d  MOSI:%d CLK:%d CS:%d DC:%d RST:%d BUSY:%d\n",
    //  SPI_FREQUENCY, EINK_SPI_MOSI, EINK_SPI_CLK, EINK_SPI_CS,
    //  EINK_DC,EINK_RST,EINK_BUSY);
}

void eink_shutdown_io(void)
{
    //ESP_ERROR_CHECK(spi_bus_remove_device(spi));
    //ESP_ERROR_CHECK(spi_bus_free(EPD_HOST));
    //gpio_reset_pin((gpio_num_t)EINK_BUSY);
    //gpio_reset_pin((gpio_num_t)EINK_SPI_CS);
    //gpio_reset_pin((gpio_num_t)EINK_SPI_MOSI);
    //gpio_reset_pin((gpio_num_t)EINK_SPI_CLK);
    //gpio_reset_pin((gpio_num_t)EINK_DC);
}

static void eink_cmd(const uint8_t cmd)
{
    //printf("C %x\n",cmd);

    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Command is 8 bits
    t.tx_buffer=&cmd;               //The data is the cmd itself 
    // No need to toogle CS when spics_io_num is defined in SPI config struct
    //gpio_set_level((gpio_num_t)EINK_SPI_CS, 0);
    gpio_set_level((gpio_num_t)EINK_DC, 0);
    ret=spi_device_polling_transmit(spi, &t);

    assert(ret==ESP_OK);
    gpio_set_level((gpio_num_t)EINK_DC, 1);
}

static void eink_data(uint8_t data)
{
    //printf("D %x\n",data);
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Data is 8 bits
    t.tx_buffer=&data;              //Use the data itself
    ret=spi_device_polling_transmit(spi, &t);
    
    assert(ret==ESP_OK);
}

static void eink_dataBuffer(const uint8_t *data, int len)
{
    if (len==0) return; 
    esp_err_t ret;
    spi_transaction_t t;
                
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    ret=spi_device_polling_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

static void eink_waitBusy(const char* message){
  ESP_LOGI(TAG, "_waitBusy for %s", message);
  int64_t time_since_boot = esp_timer_get_time();

  while (1){
    // On low is not busy anymore
    if (gpio_get_level((gpio_num_t)EINK_BUSY) == 0) break;
    vTaskDelay(10 / portTICK_RATE_MS);
    if (esp_timer_get_time()-time_since_boot>7000000)
    {
      ESP_LOGI(TAG, "Busy Timeout");
      break;
    }
  }
}

static void eink_init_partial(void) {
    eink_cmd(0x32); //set LUT
    eink_dataBuffer(WF_PARTIAL, 153);
    eink_waitBusy("load LUT");
    eink_cmd(0x3F); //gate voltage
    eink_data(WF_PARTIAL[153]);
    eink_cmd(0x03); //gate voltage
    eink_data(WF_PARTIAL[154]);
    eink_cmd(0x04); //source voltage
    eink_data(WF_PARTIAL[155]);
    eink_data(WF_PARTIAL[156]);
    eink_data(WF_PARTIAL[157]);
    eink_cmd(0x2C); //vcom
    eink_data(WF_PARTIAL[158]);
    eink_cmd(0x37); //active waveform
    eink_dataBuffer(WF_PARTIAL_MODE, 10);
    eink_cmd(0x3C); //border
    eink_data(0x80);
}


void eink_reset(void) {
    gpio_set_level((gpio_num_t)EINK_RST, 0);
    vTaskDelay(EINK_RESET_DELAY / portTICK_RATE_MS);
    gpio_set_level((gpio_num_t)EINK_RST, 1);
    vTaskDelay(EINK_RESET_DELAY / portTICK_RATE_MS);
}

void eink_start(void) {
    //TODO: power on eink display
    eink_init_io();
    eink_reset();
    eink_waitBusy("epd_reset");
}

void eink_init(int fullUpdate) {
    if(!fullUpdate) {
        eink_init_partial();
        return;
    }
    //TODO: initialize display
    //send power on sequence to diplay
    eink_cmd(0x12); // soft reset
    eink_waitBusy("epd_wakeup_power:ON");

    eink_cmd(0x01); // Driver output control
    eink_data(0xC7);
    eink_data(0x00);
    eink_data(0x00);

    eink_cmd(0x1A); // set temperatur to 25 degrees to select apropriate LUT
    eink_data(0x19);
    eink_data(0x00);

    eink_cmd(0x22);
    eink_data(0x91);
    eink_cmd(0x20);
    eink_waitBusy("PowerOn");
}

void eink_stop(void) {
    eink_waitBusy("Power off");
    //goto deep sleep
    eink_cmd(0x10);
    eink_data(0x01);
    //eink_shutdown_io();
    //TODO: gpio to minimal
}

void static eink_set_rampointer(int top, int right, int width, int height) {
    //top in lines, left in multiples of 8 pixels rounded down
    //set ram area
    eink_cmd(0x11);
    eink_data(0x03);
    eink_cmd(0x44);
    eink_data(top);
    eink_data(top+height-1); //200px = 25 * 8. ie 0-24
    eink_cmd(0x45);
    eink_data(right);
    eink_data(0);
    eink_data(right+width-1); //200 lines. ie 0-199
    eink_data(0);
    //set ram pointer
    eink_cmd(0x4E);
    eink_data(top);
    eink_cmd(0x4F);
    eink_data(right);
    eink_data(0);
    eink_waitBusy("set ram");
}


static uint8_t imageBuffer[5000]; //200 x 200px = 200 x 25 = 5000 bytes
void static eink_draw_bitmap(const uint8_t *bitmap, int top, int right, int width, int height) {
    //buffer is filled with vertical lines of height bytes (i.e. buffer is 90 degrees rotate)
    //writing the lines is from right to left
    //each line is written top to botttom
    //bitmaps must be rotated and must consist of full bytes. No bit shifting
    //Bytes in the image buffer not covered by the bitmap are untouched
    //first skip right*height bytes to move to required start positon
    int p = right*height;
    uint8_t bitmapWidth =  *bitmap++;
    uint8_t bitmapHeight = *bitmap++;
    for(uint8_t x=0; x<bitmapWidth; x++) {
        //for each (vertical) line skip top bytes
        p+=top;
        for(uint8_t y=0; y<bitmapHeight; y++) {
            //now copy one line of the bitmap into the imageBuffer
            if(p>=sizeof(imageBuffer)) return; // out of buffer space
            imageBuffer[p++]=*bitmap++;
        }
        p+=height-top-bitmapHeight;
    }
}

void eink_draw_text(char *text, int len, int top, int left, int width, int height) {
    //draw max len character aligned from the LEFT
    int i=0;
    if(len>8) len=8; //can't draw more than 8 characters on a line
    while(i<len) {
        if(*text=='\0') break; //encountered end of string.
        left-=25; //calculate right side of the first character
        if(*text>32 && *text<127) {
            //only draw visible characters. Others are space
            eink_draw_bitmap(font[(int)(*text)-33], top, left, width, height);
        }
        text++;
        i++;
    }
}

void eink_invert_block(int top, int right, int blockWidth, int blockHeight, int width, int height) {
    int p = right*height;
    for(uint8_t x=0; x<blockWidth; x++) {
        //for each (vertical) line skip top bytes
        p+=top;
        for(uint8_t y=0; y<blockHeight; y++) {
            //now invert one line of the imageBuffer
            if(p>=sizeof(imageBuffer)) return; // out of buffer space
            imageBuffer[p++]^=0xFF;
        }
        p+=height-top-blockHeight;
    }
}

static void eink_batteries(int b1, int b2, int b3) {
    //batteries are only drawn in full update
    if(b3<0) {
         //show just two batteries. (b2 left and b1 right)
         if(b2>=0 && b2<=3) eink_draw_bitmap(bitmaps[BITMAP_BAT0+b2], 21, 158, 200, 25);
         else eink_draw_bitmap(bitmaps[BITMAP_BATX], 21, 166, 200, 25);
         if(b1>=0 && b1<=3) eink_draw_bitmap(bitmaps[BITMAP_BAT0+b1], 21, 118, 200, 25);
         else eink_draw_bitmap(bitmaps[BITMAP_BATX], 21, 110, 200, 25);
    } else {
         //show three batteries from left to right b2  b3  b1
         if(b2>=0 && b2<=3) eink_draw_bitmap(bitmaps[BITMAP_BAT0+b2], 21, 170, 200, 25);
         else eink_draw_bitmap(bitmaps[BITMAP_BATX], 21, 170, 200, 25);
         if(b3>=0 && b3<=3) eink_draw_bitmap(bitmaps[BITMAP_BAT0+b3], 21, 138, 200, 25);
         else eink_draw_bitmap(bitmaps[BITMAP_BATX], 21, 138, 200, 25);
         if(b1>=0 && b1<=3) eink_draw_bitmap(bitmaps[BITMAP_BAT0+b1], 21, 106, 200, 25);
         else eink_draw_bitmap(bitmaps[BITMAP_BATX], 21, 106, 200, 25);
    }
}

static void eink_sync(int sync, int fullUpdate) {
    //location differs for full and partial updates
    if(fullUpdate) {
        if(sync>=0 && sync<7) eink_draw_bitmap(bitmaps[BITMAP_SYNC0+sync], 20, 10, 200, 25);
        else eink_draw_bitmap(bitmaps[BITMAP_SYNC7], 20, 10, 200, 25);
    } else {
        if(sync>=0 && sync<7) eink_draw_bitmap(bitmaps[BITMAP_SYNC0+sync], 18, 2, 84, 23);
        else eink_draw_bitmap(bitmaps[BITMAP_SYNC7], 18, 2, 84, 23);
    }
}

static void eink_charge(int chargeState, int fullUpdate) {
    //chargeState<0 means charge state has not changed
    //this function is never invoked when chargeState<0
    //charge state==0 means don't draw any icon. section should already be blank
    //there are 4 charge state symbols. only use two lowest bits
    //this allows us to use BITMAP_CHARGING0 by using chargeState==4
    if(chargeState==0) return;
    chargeState&=3;
    //location differs for full and partial updates
    if(fullUpdate) {
        eink_draw_bitmap(bitmaps[BITMAP_CHARGING0+chargeState], 20, 50, 200, 25);
    } else {
        eink_draw_bitmap(bitmaps[BITMAP_CHARGING0+chargeState], 18, 42, 84, 23);
    }
}

void eink_display_number(uint8_t num, int b1, int b2, int b3, int sync, int chargeState, int fullUpdate) {
    //set ram pointer
    uint8_t ones = num%10;
    uint8_t tens = (num/10)%10;

    if(fullUpdate) {
        //write screen data
        memset(imageBuffer, 0xFF, 5000); //clear entire buffer
        eink_draw_bitmap(digits[ones], 2, 8, 200, 25);
        eink_draw_bitmap(digits[tens], 2, 108, 200, 25);
        eink_batteries(b1, b2, b3);
        eink_sync(sync, fullUpdate);
        eink_charge(chargeState, fullUpdate);
        
        eink_set_rampointer(0, 0, 200, 25); //full frame 200 x (25*8) pixels starting at 0,0 (top,right)
        eink_cmd(0x24);
        eink_dataBuffer(imageBuffer, 5000);
        eink_set_rampointer(0, 0, 200, 25); //full frame 200 x (25*8) pixels starting at 0,0 (top,right)
        eink_cmd(0x26);
        eink_dataBuffer(imageBuffer, 5000);
    } else {
        //init partial update
        //write screen data
        if(chargeState<0) {
            //No change in charging indicator so we only need to replace the ones
            eink_draw_bitmap(digits[ones], 0, 0, 84, 17);
            eink_set_rampointer(2, 8, 84, 17); //full frame 84 x (17*8) pixels starting at 2*8=  16,8 (top,right)
            eink_cmd(0x24);
            eink_dataBuffer(imageBuffer, 1428);
        } else {
            //new value for charging indicator so update ones and the charging and sync indicators
            memset(imageBuffer, 0xFF, 1932); //clear entire buffer
            eink_draw_bitmap(digits[ones], 0, 0, 84, 23);
            eink_sync(sync, fullUpdate);
            eink_charge(chargeState, fullUpdate);
            eink_set_rampointer(2, 8, 84, 23); //full frame 84 x (23*8) pixels starting at 2*8=  16,8 (top,right)
            eink_cmd(0x24);
            eink_dataBuffer(imageBuffer, 1932);
        }
    }
}

static void eink_draw_ip_for_setup(uint32_t ipaddress) {
    int top=0;
    int right=0;
    for(int i=0; i<4; i++) {
        uint8_t v = (uint8_t)(ipaddress&0xFF);
        ipaddress>>=8;
        do {
            eink_draw_bitmap(font[(v%10)+'0'-33], top, right, 200, 25);
            v/=10;
            right+=23;
        } while(v>0);
        right = 0;
        top += 5; //go down one line
    }
}

//draw bitmap on the specified location
//always using partial updates
//the clock has initialized the display anyway
//ghosting of the display is no big issue
//It's just meant to show setup is active
//and where to find it.
void eink_display_setup(char *version, uint32_t ipaddress, char *ssid) {
    memset(imageBuffer, 0xFF, 5000); //clear entire buffer
    eink_draw_bitmap(bitmaps[BITMAP_SETUP1], 0, 72, 200, 25);
    eink_draw_text(version, 5, 15, 200, 200, 25);
    eink_invert_block(15, 72, 128, 5, 200, 25);
    eink_draw_text(ssid, 8, 20, 200, 200, 25);
    if(ipaddress!=0) eink_draw_ip_for_setup(ipaddress);
    eink_set_rampointer(0, 0, 200, 25); //full frame 200 x (25*8) pixels starting at 0,0 (top,right)
    eink_cmd(0x24);
    eink_dataBuffer(imageBuffer, 5000);
    eink_set_rampointer(0, 0, 200, 25); //full frame 200 x (25*8) pixels starting at 0,0 (top,right)
    eink_cmd(0x26);
    eink_dataBuffer(imageBuffer, 5000);
}

void eink_update(int fullUpdate) {
    eink_cmd(0x22);
    eink_data(fullUpdate?0xC7:0xCF);
    eink_cmd(0x20);
}

