/*
 * ULP program that waits for the busy line to go low
 * and then sends the deep sleep command to the eink display
 */

#include "soc/soc_ulp.h"      // for WRITE_RTC_REG
#include "soc/rtc_io_reg.h"   // for RTC_GPIO_*
#include "soc/rtc_cntl_reg.h" // for RC_CNTL_*


.global entry                // ulp start address used by main core program

.set BUSY_PIN, 10             // The busy pin is gpio pin 4, i.e. rtc pin 10
.set CS_PIN,    7             // The cs pin is gpio pin 26, i.e. rtc pin 7
.set DC_PIN,   17             // The dc pin is gpio pin 27, i.e. rtc pin 17
.set CLK_PIN,   6             // The clk pin is gpio pin 25, i.e. rtc pin 6
.set DIO_PIN,  15             // The dio pin is gpio pin 12, i.e. rtc pin 15

.text

entry:
  //initialize CS PIN (GPIO26)
  // cs pin shall be output, not input
  WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S+CS_PIN, 1, 1)
  //make sure CS is high (i.e. inactive)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CS_PIN, 1, 1)

  //initialize CLK PIN (GPIO25)
  // clk pin shall be output, not input
  WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S+CLK_PIN, 1, 1)
  //make sure CLK is low (i.e. idle)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)

  //initialize DC PIN (GPIO27)
  // use digital function, not rtc function for dc pin
  WRITE_RTC_REG(RTC_IO_TOUCH_PAD7_REG, RTC_IO_TOUCH_PAD7_MUX_SEL_S, 1, 1)
  // dc pin shall be output, not input
  WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S+DC_PIN, 1, 1)

  //initialize DIO PIN (GPIO12)
  // use digital function, not rtc function for dio pin
  WRITE_RTC_REG(RTC_IO_TOUCH_PAD5_REG, RTC_IO_TOUCH_PAD5_MUX_SEL_S, 1, 1)
  // led pin shall be output, not input
  WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S+DIO_PIN, 1, 1)

  //initialize LED PIN (GPIO??)
  // use digital function, not rtc function for led pin
  //WRITE_RTC_REG(RTC_IO_TOUCH_PAD0_REG, RTC_IO_TOUCH_PAD0_MUX_SEL_S, 1, 1)
  // led pin shall be output, not input
  //WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TS_REG, RTC_GPIO_ENABLE_W1TS_S+LED_PIN, 1, 1)

  stage_rst

  //turn led on
  //WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+LED_PIN, 1, 1)

  //wait for busy to go high
wait_busy:
  //on timeout turn off led and exit
  stage_inc 1              // increment stage counter for counting timeout
  jumps done, 250, ge      // halt after about 8 seconds
  wait  64000              // wait 64000 clock ticks at 8MHz -> 8ms
  wait  64000              // wait 64000 clock ticks at 8MHz -> 8ms
  wait  64000              // wait 64000 clock ticks at 8MHz -> 8ms
  wait  64000              // wait 64000 clock ticks at 8MHz -> 8ms
  READ_RTC_REG(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT_S+BUSY_PIN, 1)
  AND   r0, r0, 0x0001
  jump not_busy, EQ
  jump wait_busy
not_busy:
  //busy pin is now low
  //now bitbang deep sleep command to eink display
  //set clk, cs and dc low
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+DC_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CS_PIN, 1, 1)
  //command 0x10
  //bit 7
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+DIO_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 6
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 5
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 4
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+DIO_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 3
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+DIO_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 2
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 1
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 0
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)

  //data 0x01
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+DC_PIN, 1, 1)
  //bit 7
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 6
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 5
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 4
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 3
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 2
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 1
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //bit 0
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+DIO_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)


  //add some delay by keeping clk low
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+CLK_PIN, 1, 1)
  //deactivate CS
  WRITE_RTC_REG(RTC_GPIO_OUT_W1TS_REG, RTC_GPIO_OUT_DATA_S+CS_PIN, 1, 1)

done:
  //Done. Make sure led is off
  //WRITE_RTC_REG(RTC_GPIO_OUT_W1TC_REG, RTC_GPIO_OUT_DATA_S+LED_PIN, 1, 1)
  //disable output pins
  //WRITE_RTC_REG(RTC_GPIO_ENABLE_W1TC_REG, RTC_GPIO_ENABLE_W1TC_S+LED_PIN, 1, 1)
  //TODO: reset IO
  // make sure the ULP is not restarted automatically after halt
  /* Set RTC_CNTL_ULP_CP_SLP_TIMER_EN field of RTC_CNTL_STATE0_REG to 0 */
  WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)

  halt

