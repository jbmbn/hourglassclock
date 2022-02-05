/* rotate.c
 * Implements the task that rotates the rings of the clock
 * to the required position.
 * When done, the task deletes itself and ends
 * The task can not survive deep sleep so there's no need
 * to keep it running
 * The task engages the motors and monitors the position sensors
 * when the requested position has been reached, the task stops
 * the motors.
 * When both motors are switched off, the motor driver is shutdown
 * and the power circuits are disabled.
 *
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "tmc2209.h"

RTC_DATA_ATTR static int8_t current_hour = 0;

static int rotate_task_state=0;

#define VCC2_ENABLE  2
#define CHARGER_DISABLE 23

#define ROTATE_SENSOR1 22
#define ROTATE_SENSOR2 21
#define ROTATE_SENSOR3 13
#define ROTATE_SENSOR4 14

//read sensors of the hours ring
static int get_hours_ring_sensors(void) {
    //read all sensors at once
    //note that sensors are low active
    int sensors=0;
    if(!gpio_get_level(ROTATE_SENSOR1)) sensors|=0x01;
    if(!gpio_get_level(ROTATE_SENSOR2)) sensors|=0x02;
    if(!gpio_get_level(ROTATE_SENSOR3)) sensors|=0x04;
    return sensors;
}

//mapping two subsequent position of the hours ring
//to the actual hour on the ring
//when rotating counter clockwise
//0 means invalid
static int8_t sensorsMappingCC[] = {
//   0   1   2   3   4   5   6   7   new/cur
     0,  0,  0,  0,  0,  0,  0,  0,     //0
     0,  0,  0,  0,  0,  0,  0,  6,     //1
     0,  1,  0,  0, 10,  0,  0,  0,     //2
     0,  0,  0,  0,  0,  0,  0,  4,     //3
     0,  0,  0,  0,  0,  3,  0,  0,     //4
     0,  0,  8,  0,  0,  0,  0, 12,     //5
     0,  0,  0,  0,  0,  7,  0,  0,     //6
     0,  0,  5, 11,  0,  0,  2,  9      //7
};


//mapping two subsequent position of the hours ring
//to the actual hour on the ring
//when rotating clockwise
//0 means invalid
static int8_t sensorsMappingCW[] = {
//   0   1   2   3   4   5   6   7   new/cur
     0,  0,  0,  0,  0,  0,  0,  0,     //0
     0,  0,  8,  0,  0,  0,  0,  0,     //1
     0,  0,  0,  0,  0,  3,  0, 12,     //2
     0,  0,  0,  0,  0,  0,  0,  6,     //3
     0,  0,  5,  0,  0,  0,  0,  0,     //4
     0,  0,  0,  0, 10,  0,  2,  0,     //5
     0,  0,  0,  0,  0,  0,  0,  9,     //6
     0,  1,  0, 11,  0,  7,  0,  4      //7
};

//mapping hour to sensor value
//list extends to 19 to make allow determining the sensor value
//for the next and the previous position on the ring
//i.e. +5 and -5 = (-5+12=+7) hours
//position 0 is not used. max index=12-5+12=19
static uint8_t expectMap[] = {
//0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16  17  18  19
  0,  1,  6,  5,  7,  2,  7,  5,  2,  7,  4,  3,  7,  1,  6,  5,  7,  2,  7,  5
};

volatile static int hours_ring_prev=0;
volatile static int hours_ring_current=0;
volatile static int hours_ring_current_threshold=0;
volatile static int hours_ring_expect=0;
volatile static int hours_ring_direction=0;
volatile static int hours_ring_hour=0;
volatile static int hours_ring_zero_threshold=0;
volatile static int hours_ring_velocity=0;

//at full speed the hours ring moves about 1cm in 80msec
//full speed has a velocity of 0x18000
//0x3C0000 is about 0.5 cm movement (maybe)
//So half a centimeter past the magnets zero level is detected
//Using a little lower value
#define HOURS_RING_ZERO_THRESHOLD 0x3C0000
//Magnets are 1cm in diameter so try to detect at center of the magnets
//Using a little lower value to compensate for overshoot
#define HOURS_RING_CURRENT_THRESHOLD 0x270000

static void read_sensors_timer(void* arg)
{
    //this method is only invoked when hour ring is moving
    //get sensors for hours ring
    int sensors = get_hours_ring_sensors();
    if(sensors==0) {
        //none of the sensors is currently active
        hours_ring_current_threshold=0;
        hours_ring_zero_threshold+=hours_ring_velocity;
        if(hours_ring_current!=0) {
            //still debouncing zero sensor state
            hours_ring_zero_threshold+=hours_ring_velocity;
            if(hours_ring_zero_threshold>HOURS_RING_ZERO_THRESHOLD) {
                //just debounce zero sensors state
                //deterine current position of hours ring
                int h=0;
                if(hours_ring_direction) {
                    h=sensorsMappingCW[hours_ring_prev+hours_ring_current];
                    hours_ring_expect=expectMap[h+7];
                } else {
                    h=sensorsMappingCC[hours_ring_prev+hours_ring_current];
                    hours_ring_expect=expectMap[h+5];
                }
                if(h==0) {
                     //don't know where we are
                     hours_ring_expect=0;
                     hours_ring_hour=0;
                }
                hours_ring_prev=hours_ring_current*8;
                hours_ring_current=0;
            }
        }
    } else {
        //sensors are not 0 so try to get the correct sensor value
        //unfortunately not all sensors trigger at the same time
        //so debounce and try to find the center position of the sensors
        hours_ring_zero_threshold=0;
        hours_ring_current_threshold+=hours_ring_velocity;
        if(hours_ring_current_threshold>HOURS_RING_CURRENT_THRESHOLD) {
            //detected a non zero value long enough
            if(hours_ring_current<sensors) hours_ring_current = sensors;
            if(hours_ring_current==hours_ring_expect) {
                //detected the expected value. Assume this is the
                //correct value for the current position
                //So update current hour
                //We're not expecting a new sensor value yet
                //so leave that as is
                if(hours_ring_direction) {
                    hours_ring_hour=sensorsMappingCW[hours_ring_prev+hours_ring_current];
                } else {
                    hours_ring_hour=sensorsMappingCC[hours_ring_prev+hours_ring_current];
                }
            }
        }
    }
}

//threshold value to detect sensor active for hourglass ring
#define HOURGLASS_SENSOR_THRESHOLD   0x48000

static int hourglass_sensor_threshold=0;

//read sensor of the hourglass ring and debounce
//take rotation speed into account
//when velocity is 0 return actual sensor value as the rings are not turning
//the trheshold will never be reached and the value will not change
static int get_hourglass_ring_sensor(int velocity) {
    if(gpio_get_level(ROTATE_SENSOR4)) {
        //sensor not active. Clear threshold
        hourglass_sensor_threshold = 0;
    } else {
        if(velocity==0) return 1;
        hourglass_sensor_threshold+=velocity;
        if(hourglass_sensor_threshold>=HOURGLASS_SENSOR_THRESHOLD) {
            hourglass_sensor_threshold=HOURGLASS_SENSOR_THRESHOLD;
            return 1; //sensor is active
        }
    }
    return 0; //sensor is inactive
}


static int8_t rotate_task_target=1;

void rotate_rings_task(void *params) {
    //Note: charging should be disabled already
    //Power up motor driver
    gpio_set_direction((gpio_num_t)VCC2_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)VCC2_ENABLE, 1);
    //wait a little bit to let vcc2 staiblize
    vTaskDelay(50 / portTICK_RATE_MS);

    //configure position sensors as input
    gpio_set_direction((gpio_num_t)ROTATE_SENSOR1, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)ROTATE_SENSOR1, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)ROTATE_SENSOR2, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)ROTATE_SENSOR2, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)ROTATE_SENSOR3, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)ROTATE_SENSOR3, GPIO_PULLUP_ONLY);
    gpio_set_direction((gpio_num_t)ROTATE_SENSOR4, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)ROTATE_SENSOR4, GPIO_PULLUP_ONLY);

    tmc2209_init();

    //create timer to determine position of hours ring
    const esp_timer_create_args_t sensors_timer_args = {
        .callback = &read_sensors_timer,
        /* name is optional, but may help identify the timer when debugging */
        .name = "sensors"
    };
    esp_timer_handle_t sensors_timer;
    ESP_ERROR_CHECK(esp_timer_create(&sensors_timer_args, &sensors_timer));
    /* The timer has been created but is not running yet */


    //Handle rotating the rings.
    //Check position sensors every 10 msec to see if the target is reached
    int cnt=0;
    //determine sensor actual sensors value
    int sensors = get_hours_ring_sensors();
    hours_ring_expect = 0;
    hours_ring_current=0;
    hours_ring_prev = 0;
    hours_ring_direction = 0; //default rotate counter clockwise 
    hours_ring_velocity=0x5000;
    //make sure we detect zero state first
    hours_ring_current_threshold=INT_MIN;
    hours_ring_zero_threshold=0;
    if(!(current_hour>0 && current_hour<=12 && expectMap[current_hour]==sensors)) {
        //sensor position is not what we expect
        //or we don't know what to expect
        hours_ring_hour=0;
    } else {
        //sensor value is what we expect
        hours_ring_hour=current_hour;
        //set expect to the next hour
        if(current_hour==1 && rotate_task_target==3) {
            //Handle DST change winter to summer time
            //reverse direction and preload the sensor position info
            //when the zero state is detected, it'll set the position
            //correct when detecting the next one automatically
            //So it'll be in time to find the 3 o'clock position
            hours_ring_direction=1;
            hours_ring_prev=7*8;         //prev sensor value must have been 7
            hours_ring_current=sensors;  //set current sensor value
        } else if(current_hour==2 && rotate_task_target==2) {
            //Handle DST change summer to winter time
            //the hours ring does not need to turn at all.
            hours_ring_velocity=0;
            rotate_task_state&=~0x01;
        } else {
            //We're probably at the expected location
            //In that case we need to rotate 5 positions
            //We need 2 positions to determine the correct location
            //so don't preload. We'll detect the correct position
            //in time without issues
        }
    }
    //start 1msec periodic timer when required
    if(rotate_task_state&0x01) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(sensors_timer, 1000));
printf("started timer");
    }
printf("sensors: %d, %d, %d, %d\n", hours_ring_prev, hours_ring_current,
             hours_ring_expect, hours_ring_hour);
    while(rotate_task_state&0x03 && cnt<1500) //rotate max 15 seconds
    {
        if(rotate_task_state&0x01) {
            //handle hours ring iteration
            //get sensors using old velocity value
            //afterall, that was used the last tick anyway
            if((hours_ring_velocity<0x18000) && ((cnt&0x03)==0)) {
                //ramp up every 40msec till max speed
                hours_ring_velocity+=0x1000;
                if(hours_ring_direction) tmc2209_rotate_cw(1, hours_ring_velocity);
                else tmc2209_rotate_cc(1, hours_ring_velocity);
            }
            //stored position is invalid. Clear it
            current_hour=0;
            if(hours_ring_hour==rotate_task_target) {
                //at target position. Stop turning
                current_hour=hours_ring_hour; //Position is valid
                tmc2209_stop(1);
                //clear motor1 task state bit
                rotate_task_state&=~0x01;
                //stop periodic timer
                esp_timer_stop(sensors_timer);
            } else {
                if(hours_ring_hour!=0 && hours_ring_direction==0) {
                    //we know where we are and where we are going
                    //determine rotation direction to get there
                    //the fastest but only change direction when
                    //rotating counter clockwise (i.e. direction==0)
                    //just to make sure we change direction only once
                    int tmp=((rotate_task_target-hours_ring_hour)*5)%12;
                    if(tmp<0) tmp+=12;
                    if(tmp>6) {
                        //changing direction.
                        //stop timer
                        esp_timer_stop(sensors_timer);
                        hours_ring_direction=1;
                        tmc2209_stop(1); //stop rotating
                        hours_ring_velocity=0x5000; //start rampup again
                        //make sure the current position is known
                        //for reversing direction
                        hours_ring_prev=expectMap[hours_ring_hour+5]*8;
                        hours_ring_current=expectMap[hours_ring_hour];
                        hours_ring_expect=hours_ring_current;
                        //ignore current position till sensors go
                        //from non zero back to zero again
                        hours_ring_zero_threshold=INT_MIN;
                        //restart timer
                        ESP_ERROR_CHECK(esp_timer_start_periodic(sensors_timer, 1000));
                    }
                } 
            }
        }

        if(rotate_task_state&0x02) {
            //handle hoursglass iteration
            if(cnt==0) {
                //just start motor. no ramp up necessary
                tmc2209_rotate_cw(2, 0x10000);
            }
            int sensors=get_hourglass_ring_sensor(0x10000);
            if(cnt>100 && sensors==1) {
                //found stop position so we're done
                tmc2209_stop(2);
                rotate_task_state&=~0x02; //clear motor2 task state bit
            }
        }
        cnt++;
        vTaskDelay(10 / portTICK_RATE_MS);
    }
    if(rotate_task_state&0x01) {
        //Hours ring is still on so the timer is too.
        //stop the timer now
        esp_timer_stop(sensors_timer);
    }
    if(hours_ring_velocity>0) {
        //correct overshoot of hours ring by turning back a little
        if(hours_ring_direction) tmc2209_rotate_cc(1, 0x4000);
        else tmc2209_rotate_cw(1, 0x4000);
        vTaskDelay(200 / portTICK_RATE_MS);
    }
    //the rotation loop has ended so either both rings have reached
    //the target position or it has taken to long to get there
    //make sure both motors are off
    tmc2209_stop(1);
    tmc2209_stop(2);

    //destroy sensors timer
    ESP_ERROR_CHECK(esp_timer_delete(sensors_timer));

printf("Rotate task done %d, %d\n", rotate_task_state, cnt);
    //cleanup rotation task
    tmc2209_shutdown();
    gpio_set_level((gpio_num_t)VCC2_ENABLE, 0);
    gpio_set_direction((gpio_num_t)VCC2_ENABLE, GPIO_MODE_INPUT);
    rotate_task_state=0;
    //vTaskDelete(NULL);
    vTaskSuspend(NULL);
}

void rotate_set_time(int hour) {
    //disable charger and get battry voltages
    //enable VCC2
    //initialize motor drivers
    //start task to turn hour ring
    //start task to turn hourglass
    //test motor driver
    //hour actually contains minute while testing
    //minute is always even when we get here.
    //alternate between motors each time this is invoked
    rotate_task_state=7; //handle both rings and be sure the task can complete
    rotate_task_target=hour;
    if(rotate_task_target==0) rotate_task_target=12;
    TaskHandle_t rotateRingsTask;
    xTaskCreate(&rotate_rings_task, "RotateRings",
                2048, NULL, 5, &rotateRingsTask);
}

//return true when rotation tasks are active
int rotate_busy(void) {
   return rotate_task_state;
}
