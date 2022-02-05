//PB0, PB6, PB7 are the relay control lines
//PB5 is the SCK line, PB3 is MOSI.
//PB4 is MISO and available to the programmer (as are PB3 and PB5 and RST)
//PB2 is CS for the ESP SPI slave
//    CS is high active because it's connected to a JFet on the ESP side
//    with a pull down. High impedance will block the JFet and result in a
//    high level on the ESP SPI Slave CS input pin. A 1 will cause the
//    ESP input pin to go low.
//PB1 is connected to a jumper for switching between 3 and 5 battery mode
//
//PD2 is connected to the ESP. When the ESP powers up the motor drivers,
//PD2 is pulled low. This is the signal to measure the battery voltages
//and send the data to the ESP using SPI (MOSI=PB3, SCK=PB5 and CS=PB2)
//When PD2 is low, ESP requests for charger data using SPI
//When PD2 is high, charger uses PB5 and PB3 to signal charger state
//                 Using High impedance for 0.
//                 The lines are connected to a JFet with a pull down
//                 so high impedance blocks the JFet and results in a
//                 high signal on the ESP. 1 will make the JFet conduct
//                 and the ESP input will go low.
//                 ZZ  Charger not connected
//                 Z1  Charging mode1 (bat1, bat2, bat3)
//                 1Z  Charging mode2 (bat1, bat4, bat5)
//                 11  Charging done
//PD7 is low when the power supply is connected
//PD0, PD1, PD3, PD4, PD5 and PD6 are connected to the CHARGE en STANDBY
//outputs of the TP4056 chips. The TP4056 will pull them low when charging
//or when charging is done.
//
//PC0 turns the voltage dividers for measuring the battery voltages on/off
//PC1, PC2, PC4 and PC5 are connected to switchable voltage dividers and to
//each of the individual motor batteries (B2,B3,B4 and B5)
//Vcc is connected directly to B1 so to measure the voltage of B1, we
//use the bandgap and Vcc as reference.
//To measure the voltages of the other batteries, we need to make PC0 high
//Then the voltage can be measured using the bandgap as reference
//PC3 is not connected
//
//Relay debounce times were detemined using a simple test setup.
//Worst case was about 250usec
//Here we'll be using 2msec delay to prevent any problems

#include <avr/io.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

//watchdog counter to allow checking for a stuck loop and force a reset
#define WDT_TIMEOUT 1000
uint16_t wdt=WDT_TIMEOUT; //1000 times the watchdog timer is about 30 seconds

//To measure battery1 using the ADC, measure the bandgap using Vcc as voltage reference
#define BAT2 ((1<<REFS1)|(1<<REFS0)|0x02)
#define BAT2_FACTOR 45
#define BAT3 ((1<<REFS1)|(1<<REFS0)|0x04)
#define BAT3_FACTOR 85
#define BAT4 ((1<<REFS1)|(1<<REFS0)|0x01)
#define BAT4_FACTOR 45
#define BAT5 ((1<<REFS1)|(1<<REFS0)|0x05)
#define BAT5_FACTOR 85


#define STATE_IDLE       0
#define STATE_CHARGING1  1
#define STATE_CHARGING2  2
#define STATE_TRICKLE1   3
#define STATE_TRICKLE2   4

//2ms delay time after releasing relays before asuming contact are stable
#define RELAY_DEBOUNCE_TIME 2
//Between measurements, the voltage should not decrease to much (0.3 volts)
#define MAX_DELTA_V 6

//keep track of charge state of the batteries
uint8_t  batteriesState=0; //not charged
#define CHARGING_DONE_DETECT 700
#define CHARGING_DONE_EXPIRE 40000
uint16_t charger1timer=0;
uint16_t charger2timer=0;
uint16_t charger3timer=0;

//counter for psu connect time.
//count down from 0xFFFF to indicate psu connected for
//less than half an hour when>0. When 0, at least half an hour connected
//only valid when state!=STATE_IDLE
uint16_t psuConnected=0;

//keep track of PORTD state
uint8_t signals=0xFF;

//espRequest is set to 1 by INT0 interrupt
//the INT0 interrupt itself is the disabled to prevent and INT0 avalange
uint8_t espRequest=0;


//switch betwee 3 and 5 batteries mode
//is determined from PB1 when charging starts
static uint8_t mode = 1;

//charger state
uint8_t state=STATE_IDLE;
//timer for trickle charge states. When timer reaches 0, switch trickle charge state
uint16_t trickle=0;
//debounce timer (about 3 sec) before detecting charger connected
#define DEBOUNCE_TIMEOUT 100
uint8_t debounce=DEBOUNCE_TIMEOUT;
//watchdog on timer (about 4 sec) before detecting charger disconnected
#define WATCHDOG_TIMEOUT 130
uint8_t watchdogOn=0;
uint8_t watchdogEnabled=0;

static uint8_t adcReadVcc(void)
{
    uint16_t temp = 0;
    uint16_t r = 0;
    
    // initialize ADC and set MUX to the requested input pin
    ADMUX  = (1<<REFS0)|0x0E; //Measure bandgap using vcc as Vref
    ADCSRA = (1<<ADEN)|(1<<ADPS1)|(1<<ADPS0);  //set ADC prescaler to, 1MHz/8=125kHz

    //do a dummy readout first
    ADCSRA |= (1<<ADSC);        // do single conversion
    while(!(ADCSRA & 0x10));    // wait for conversion done, ADIF flag active
        
    for(uint8_t i=0;i<4;i++)            // do the ADC conversion 4 times for better accuracy 
    {
        ADCSRA |= (1<<ADSC);        // do single conversion
        while(!(ADCSRA & 0x10));    // wait for conversion done, ADIF flag active
        
	// get 10 bit ADC value
	temp = ADCL;
	temp += (ADCH<<8);
        r += temp;      // accumulate result (4 samples) for later averaging
    }
    ADCSRA &= ~(1<<ADEN);      // disable the ADC
    //convert to voltage in 0.05V steps
    r=45056/((r+1)>>1);
    return (uint8_t)r;
}

static uint8_t adcRead(uint8_t adcInput, uint8_t conversionFactor)
{
    uint16_t temp = 0;
    uint16_t r = 0;
    
    // initialize ADC and set MUX to the requested input pin
    ADMUX  = adcInput;   // select voltage reference and adc input
    ADCSRA = (1<<ADEN)|(1<<ADPS1)|(1<<ADPS0);  //set ADC prescaler to, 1MHz/8=125kHz

    //do a dummy readout first
    ADCSRA |= (1<<ADSC);        // do single conversion
    while(!(ADCSRA & 0x10));    // wait for conversion done, ADIF flag active
        
    for(uint8_t i=0;i<4;i++)            // do the ADC conversion 4 times for better accuracy 
    {
        ADCSRA |= (1<<ADSC);        // do single conversion
        while(!(ADCSRA & 0x10));    // wait for conversion done, ADIF flag active
        
	// get 10 bit ADC value
	temp = ADCL;
	temp += (ADCH<<8);
        r += temp;      // accumulate result (4 samples) for later averaging
    }
    ADCSRA &= ~(1<<ADEN);      // disable the ADC
    //convert to voltage in 0.05V steps
    r=((((r+4)>>3)*conversionFactor)+128)>>8;
    return (uint8_t)r;
}

static void sendPacket(uint8_t *packet) {
    //calculate checksum and store in packet[7]
    //make CS line high (will be low on ESP side)
    PORTB|=(1<<PB2);
    uint8_t checksum=0;
    for(uint8_t i=0; i<7; i++) {
        //send next byte
        SPDR=~packet[i]; 
        //wait for SPI transmit done
        while(!(SPSR&(1<<SPIF)));
        //data line is inverted. So invert data on sending to make it right on receipt
        checksum+=packet[i];
    }
    SPDR=checksum; //checksum should be inverted but line is also inverted
    //wait for SPI transmit done
    while(!(SPSR&(1<<SPIF)));
    //send 4 more bytes because ESP seems to loose the last 4 bytes
    //of the second packet. Just sending a few dummy bytes to make
    //sure all relevant data is received properly
    for(uint8_t i=0; i<4; i++) {
        SPDR=0xAC; 
        while(!(SPSR&(1<<SPIF)));
    }
    //make CS line low (will be high on ESP side)
    PORTB&=~(1<<PB2);
}

static void enableWatchdog(void) {
    //start watchdog timer at 30msec intervals in interrupt mode
    cli();
    wdt_reset();
    MCUCR &= ~(1<<WDRF);
    WDTCSR |= (1<<WDCE)|(1<<WDE);
    WDTCSR = (1<<WDP0);
    WDTCSR |= (1<<WDIE);
    sei();
    watchdogEnabled=1;
}

static void disableWatchdog(void) {
    //stop WDT
    wdt_reset();
    wdt_disable();
    watchdogEnabled=0;
}

static uint8_t checkVoltage(uint8_t v1, uint8_t v2, uint8_t cnt) {
    if(cnt==0) return 1; //debounce delay is large enough. So we're done
    if((v1+MAX_DELTA_V)>=v2) return 1; //voltage within expected range
    _delay_ms(RELAY_DEBOUNCE_TIME);
}


static uint8_t packet[7];

static void handleEspRequest(void) {
    //ESP has requested charger state and battery voltages (i.e. PD2 is low)
    //This function sends the requested information and waits till PD2 is high again
    uint8_t v;
    //Make sure we stop charging first
    PORTC|=(1<<PC3); //debug
    PORTB&=~(1<<PB6);
    PORTB&=~(1<<PB7);
    PORTB&=~(1<<PB0); //Disable PB0 last. It connects the motor batteries to ground
    PRR=0xEA;   //Disable all peripherials except SPI and ADC

    //initialize packet
    packet[0]=0x5A;
    //calculate flags.
    //bit 0-2 current state
    //bit 3   mode
    //bit 4-6 psu connected timer msb
    //bit 7   psu connected
    packet[1]=0;
    if(state!=STATE_IDLE) {
        packet[1]=((~psuConnected)>>9)&0x70;
        packet[1]|=state&0x07;
    }
    if(signals&(1<<PD7)) packet[1]|=0x80;
    if(mode) packet[1]|=0x08;
    //Start ADC and measure Vcc
    PORTC|=(1<<PC0); //enable voltage dividers on ADC inputs
    adcReadVcc();    //do a dummy read for more consitent results
    packet[2]=adcReadVcc();
    //on initial packet, use previously measured battery voltages

    //initialize SPI
    PORTB|=(1<<PB5);  //make sure CLK is high
    _delay_us(8);
    PORTB&=~(1<<PB2); //make sure CS is inactive
    DDRB|=(1<<PB2); //CS
    DDRB|=(1<<PB3); //MOSI
    DDRB|=(1<<PB5); //SCK
    SPCR=(1<<SPE)|(1<<MSTR)|(1<<SPR0)|(1<<CPOL); //clock signal is inverted
    _delay_us(8);
    //send packet with current charger state and Vcc
    sendPacket(packet);

    //Measure voltages of all batteries
    packet[0]=0x5B;
    _delay_ms(RELAY_DEBOUNCE_TIME); 
    packet[2]=adcReadVcc();
    //wait till relays are stable
    uint8_t cnt=4;
    while(1) {
        cnt--;
        v=adcRead(BAT2, BAT2_FACTOR);
        if(!checkVoltage(v, packet[3], cnt)) continue;
        packet[3]=v;
        v=adcRead(BAT3, BAT3_FACTOR);
        if(!checkVoltage(v, packet[4], cnt)) continue;
        packet[4]=v;
        if(mode) {
            packet[5]=packet[3];
            packet[6]=packet[4];
        } else {
            v=adcRead(BAT4, BAT4_FACTOR);
            if(!checkVoltage(v, packet[5], cnt)) continue;
            packet[5]=v;
            v=adcRead(BAT5, BAT5_FACTOR);
            if(!checkVoltage(v, packet[6], cnt)) continue;
            packet[6]=v;
        }
        //reaching this point means all voltages have been checked
        break;
    }
    //Done with the ADC so turn voltage dividers off
    PORTC&=~(1<<PC0);
    //send packet with current charger state and all battery voltages
    sendPacket(packet);

    //Make cs (PB2) high-Z
    _delay_us(10);
    PORTB&=~(1<<PB2);
    DDRB&=~(1<<PB2);
    //Disable SPI
    SPCR=0x00;
    //Currently not charging so make both PB5 and PB3 high Z
    PORTB&=~(1<<PB5);
    DDRB&=~(1<<PB5);
    PORTB&=~(1<<PB5);
    DDRB&=~(1<<PB3);
    PRR=0xEF;   //Disable all peripherials
    //wait till PD2 goes high again
    if(!watchdogEnabled) enableWatchdog();
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    while(!(PIND&(1<<PD2))) {
        //Sleep. Wakeup by the watchdog every 30 msec for polling PD2
	sleep_mode();
    }
    //Done. will go back to the state machine now
    PORTC&=~(1<<PC3); //debug
}

static void chargerStateIdle(void) {
    //Turn off all relays
    PORTB&=~(1<<PB7);
    PORTB&=~(1<<PB6);
    PORTB&=~(1<<PB0);
    //Tell ESP we're not charging
    PORTB&=~(1<<PB3);
    DDRB&=~(1<<PB3);
    PORTB&=~(1<<PB5);
    DDRB&=~(1<<PB5);
    if(watchdogEnabled && (watchdogOn==0)) {
	disableWatchdog();
    }
    //enable interrupts on PD7 onnly
    PCMSK2=0x80; //PD7
    state = STATE_IDLE;
}

static void chargerStateCharging1(void) {
    //Set relays for charging mode 1
    PORTB|=(1<<PB7);
    PORTB&=~(1<<PB6);
    PORTB|=(1<<PB0);
    //Tell ESP we're charging bat1, bat2 and bat3 (PB3 1, PB5 Z)
    DDRB|=(1<<PB3);
    PORTB|=(1<<PB3);
    PORTB&=~(1<<PB5);
    DDRB&=~(1<<PB5);
    if(state==STATE_IDLE) {
        //enable WDT
        enableWatchdog();
    }
    state = STATE_CHARGING1;
}

static void chargerStateCharging2(void) {
    //Set relays for charging mode 2
    if(trickle>0) {
        //pause charging for 5 minutes. All relays off
        PORTB&=~(1<<PB7);
        PORTB&=~(1<<PB6);
        PORTB&=~(1<<PB0);
    } else {
        PORTB&=~(1<<PB7);
        PORTB|=(1<<PB6);
        PORTB|=(1<<PB0);
    }
    //Tell ESP we're charging bat1, bat4 and bat5 (PB3 Z, PB5 1)
    PORTB&=~(1<<PB3);
    DDRB&=~(1<<PB3);
    DDRB|=(1<<PB5);
    PORTB|=(1<<PB5);
    if(state==STATE_IDLE) {
        //enable WDT
        enableWatchdog();
    }
    state = STATE_CHARGING2;
}

static void chargerStateTrickle1(void) {
    if(trickle>50000) {
        //pause charging for 5 minutes. All relays off
        PORTB&=~(1<<PB7);
        PORTB&=~(1<<PB6);
        PORTB&=~(1<<PB0);
    } else {
        //Set relays for trickle mode 1
        PORTB|=(1<<PB7);
        PORTB&=~(1<<PB6);
        PORTB|=(1<<PB0);
    }
    //Tell ESP we're done charging (PB3 1, PB5 1)
    PORTB|=(1<<PB3);
    DDRB|=(1<<PB3);
    PORTB|=(1<<PB5);
    DDRB|=(1<<PB5);
    if(state==STATE_IDLE) {
        //enable WDT
        enableWatchdog();
    }
    state = STATE_TRICKLE1;
}

static void chargerStateTrickle2(void) {
    if(trickle>50000) {
        //pause charging for 5 minutes. All relays off
        PORTB&=~(1<<PB7);
        PORTB&=~(1<<PB6);
        PORTB&=~(1<<PB0);
    } else {
        //Set relays for trickle mode 2
        PORTB&=~(1<<PB7);
        PORTB|=(1<<PB6);
        PORTB|=(1<<PB0);
    }
    //Tell ESP we're done charging (PB3 1, PB5 1)
    PORTB|=(1<<PB3);
    DDRB|=(1<<PB3);
    PORTB|=(1<<PB5);
    DDRB|=(1<<PB5);
    if(state==STATE_IDLE) {
        //enable WDT
        enableWatchdog();
    }
    state = STATE_TRICKLE2;
}

//Keep track of chargers to determine when batteries are full
static uint8_t chargingDone(void) {
    if(signals&(1<<PD1)) {
        //charging BAT1. Check expiry timer
        if((batteriesState&0x01) && (charger1timer==0)) {
            //battery is currently charging and timer expired. Clear done flag
            batteriesState&=~0x01;
        }
        if(batteriesState&0x10) {
            //reset detection state and timer
            batteriesState&=~0x10;
            cli(); charger1timer=0; sei();
        }
    } else {
        //BAT1 fully charged
        if(batteriesState&0x01) {
            //fully charged already detected. Reset expiry timer
            cli(); charger1timer=CHARGING_DONE_EXPIRE; sei();
        } else {
            if(batteriesState&0x10) {
                //waiting for fully charged state to acknowlegde
                if(charger1timer==0) {
                    batteriesState&=~0x10;
                    batteriesState|=0x01;
                    cli(); charger1timer=CHARGING_DONE_EXPIRE; sei();
                }
            } else {
                //not detected fully charged state yet. Mark it now
                batteriesState|=0x10;
                cli(); charger1timer=CHARGING_DONE_DETECT; sei();
            }
        }
    }
    if(signals&(1<<PD4)) {
        //charging BAT2/4. Check expiry timer
        if((batteriesState&0x02) && (charger2timer==0)) {
            //battery is currently charging and timer expired. Clear done flag
            batteriesState&=~0x02;
        }
        if(batteriesState&0x20) {
            //reset detection state and timer
            batteriesState&=~0x20;
            cli(); charger2timer=0; sei();
        }
    } else {
        //BAT2/4 fully charged
        if(batteriesState&0x02) {
            //fully charged already detected. Reset expiry timer
            cli(); charger2timer=CHARGING_DONE_EXPIRE; sei();
        } else {
            if(batteriesState&0x20) {
                //waiting for fully charged state to acknowlegde
                if(charger2timer==0) {
                    batteriesState&=~0x20;
                    batteriesState|=0x02;
                    cli(); charger2timer=CHARGING_DONE_EXPIRE; sei();
                }
            } else {
                //not detected fully charged state yet. Mark it now
                batteriesState|=0x20;
                cli(); charger2timer=CHARGING_DONE_DETECT; sei();
            }
        }
    }
    if(signals&(1<<PD6)) {
        //charging BAT3/5. Check expiry timer
        if((batteriesState&0x04) && (charger3timer==0)) {
            //battery is currently charging and timer expired. Clear done flag
            batteriesState&=~0x04;
        }
        if(batteriesState&0x40) {
            //reset detection state and timer
            batteriesState&=~0x40;
            cli(); charger3timer=0; sei();
        }
    } else {
        //BAT3/5 fully charged
        if(batteriesState&0x04) {
            //fully charged already detected. Reset expiry timer
            cli(); charger3timer=CHARGING_DONE_EXPIRE; sei();
        } else {
            if(batteriesState&0x40) {
                //waiting for fully charged state to acknowlegde
                if(charger3timer==0) {
                    batteriesState&=~0x40;
                    batteriesState|=0x04;
                    cli(); charger3timer=CHARGING_DONE_EXPIRE; sei();
                }
            } else {
                //not detected fully charged state yet. Mark it now
                batteriesState|=0x40;
                cli(); charger3timer=CHARGING_DONE_DETECT; sei();
            }
        }
    }
    //when in 5 battery mode and BAT1 is not fully charged yet we can
    //switch to charging state2 anyway. BAT1 is charged in both modes
    if((state==STATE_CHARGING1) && (!mode)) return (batteriesState&0x06);
    return (batteriesState==0x07);
}

static void clearChargingDoneFlags(void) {
    cli();
    charger1timer=0;
    charger2timer=0;
    charger3timer=0;
    batteriesState=0;
    sei();
}

int main(int argc, char *argv[])
{
    //clear WDT
    MCUSR=0;
    wdt_reset();
    wdt_disable();

    //initialize PORTB. Make sure all relays are off and not signaling to ESP
    PORTB=(1<<PB1);
    DDRB=(1<<PB0)|(1<<PB6)|(1<<PB7);
    //determine charger mode (3 or 5 batteries)
    asm("\tnop\n\tnop\n");
    mode = PINB&(1<<PB1);
    //disable pullup on PB1 to conserve power
    PORTB&=~(1<<PB1);

    //Make sure voltage dividers are off
    PORTC=(1<<PC0);
    DDRC=(1<<PC0);
    DDRC=(1<<PC0)|(1<<PC3); //debug
    //PORTD all input. No pullups.
    //PD2 and PD7 have an external pullup
    //Other pullups are only activated when needed
    PORTD=0;
    DDRD=0;

    //measure battery voltages on startup to initalize data packet
    //Some delay to make sure voltages are stable before measuring.
    _delay_ms(RELAY_DEBOUNCE_TIME*4);
    packet[3]=adcRead(BAT2, BAT2_FACTOR);
    packet[4]=adcRead(BAT3, BAT3_FACTOR);
    if(mode) {
        packet[5]=packet[3];
        packet[6]=packet[4];
    } else {
        packet[5]=adcRead(BAT4, BAT4_FACTOR);
        packet[6]=adcRead(BAT5, BAT5_FACTOR);
    }
    PORTC=0; //voltage dividers off

    //wait till PD2 is inactive (high) to prevent INT0 immediately on reset
    //the ESP is probably not waiting on data anymore anyway.
    //enable the watchdog timer to make sure we're not stuck indefinitely
    wdt=WDT_TIMEOUT;
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    enableWatchdog();
    while(!(PIND&(1<<PD2))) {
        sleep_mode();
    }
    disableWatchdog();

    while(1)
    {
        //main loop
        //first reset the watchdog counter
        //nothing in the main loop should ever take more than 30 seconds
        //except in IDLE state when only a PIN interrupt will wake us up
        cli(); wdt=WDT_TIMEOUT; sei();

        //read PORTD. It contains the current signal state
        //First acctivate pullups
        PORTD=(1<<PD0)|(1<<PD1)|(1<<PD3)|(1<<PD4)|(1<<PD5)|(1<<PD6);
        asm("\tnop\n\tnop\n"); //a bit of time for the pullups to stabilize
        signals = PIND;
        PORTD=0; //disable pullups
        if(espRequest) {
            handleEspRequest();
        }
        //PD2 is high. ESP not querying. Charger state must have changed
        //Handle state machine
        switch(state) {
            case STATE_CHARGING1:
                if(signals&(1<<PD7)) {
                    //Power supply is no longer connected. Go to idle mode
                    chargerStateIdle();
                } else {
                    //power supply still connected. Check if charging is done
                    //for all three chargers
                    if(chargingDone()) {
                        clearChargingDoneFlags();
                        //bat1,bat2 and bat3 have been charged. Switch to mode 2
                        if(mode) {
                            chargerStateTrickle1();
                            cli(); trickle=60000; sei(); //about 30 minutes
                        } else {
                            //pause charging for 5 minutes before
                            //continuing whit the second set of batteries
                            cli(); trickle=10000; sei();
                            chargerStateCharging2();
                        }
                    } else {
                        //stil charging. Stay in this state
                        chargerStateCharging1();
                    }
                }
                break;
            case STATE_CHARGING2:
                if(signals&(1<<PD7)) {
                    //Power supply is no longer connected. Go to idle mode
                    chargerStateIdle();
                } else {
                    //power supply still connected. Check if charging is done
                    //for all three chargers
                    if(chargingDone() && (!mode)) {
                        clearChargingDoneFlags();
                        //bat1, bat4 and bat5 have been charged.
                        //Switch to trickle charge
                        chargerStateTrickle1();
                        cli(); trickle=60000; sei(); //about 30 minutes
                    } else {
                        //stil charging. Stay in this state
                        chargerStateCharging2();
                    }
                }
                break;
            case STATE_TRICKLE1:
            case STATE_TRICKLE2:
                if(signals&(1<<PD7)) {
                    //Power supply is no longer connected. Go to idle mode
                    chargerStateIdle();
                } else {
                    //power supply still connected. Check if charging is done
                    //for all three chargers
                    if(chargingDone() && (trickle==0)) {
                        clearChargingDoneFlags();
                        //switch to other batteries
                        if((state==STATE_TRICKLE1) && (!mode)) chargerStateTrickle2();
                        else chargerStateTrickle1();
                        cli(); trickle=60000; sei(); //about 30 minutes
                    } else {
                        //stil charging. Stay in this state
                        if(state==STATE_TRICKLE1) chargerStateTrickle1();
                        else chargerStateTrickle2();
                    }
                }
                break;
            default:
                //STATE_IDLE
                if(signals&(1<<PD7)) {
                    //Charger is not connected. Stay in IDLE mode.
                    //reset debounce timer.
                    //charging start a few seconds after connecting the psu
                    cli(); debounce=DEBOUNCE_TIMEOUT; sei(); 
                    chargerStateIdle();
                } else {
                    //Power supply has just been connected.
                    //reset watchdogOn timer to keep watchdog running
                    //long enough in idle state
                    cli(); watchdogOn=WATCHDOG_TIMEOUT; sei(); 
                    if(!watchdogEnabled) {
                        //Watchdog not started yet but detected the psu
                        //start the watchdog now to handle debounce timer
                        enableWatchdog();
                    }
                    //Wait for debounce to finish
                    if(debounce) {
                        //stay in state idle while waiting for PSU to stabilize
                        chargerStateIdle();
                    } else {
                        cli(); psuConnected=0xFFFF; sei();
                        //Switch to charging state
                        //determine charger mode (3 or 5 batteries)
                        //enable pullup on PB1
                        PORTB|=(1<<PB1);
                        //a bit of time for the pullup to stabilize
                        asm("\tnop\n\tnop\n");
                        mode = PINB&(1<<PB1);
                        //disable pullup on PB1 to conserve power
                        PORTB&=~(1<<PB1);
                        chargerStateCharging1();
                    }
                }
                break;
        } //end of switch(state)

        //make sure interrupts are enabled
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        cli();
        PCMSK2=0x80; //PD7
        PCICR=(1<<PCIE2);
        EICRA=0;
        EIMSK=(1<<INT0);
        espRequest=0;
        //Goto power down.
        sleep_enable();
        //Instruction after sei() is always executed first
        //even when interrupts are pending.
        //So we're garanteed to go to sleep and wake from interrupt
        //This way we won't mis an interrupt or disable INT0 before
        //we go to sleep
        sei();
        sleep_cpu(); 
        //wake by interrupt
        sleep_disable();
    }
    return 0;
}

//Pin change interrupt. Just return from interrupt
ISR(PCINT2_vect, ISR_NAKED)
{	
    asm volatile("reti");
}

//INT0 interrupt. Disable INT0 interrupt so it won't fire again immediately
ISR(INT0_vect)
{	
    espRequest=1;
    EIMSK&=~(1<<INT0);
    //PORTC|=(1<<PC3); //debug
}

//Watchdog timer interrupt
//Used for time keeping when in charging mode.
//The watchdog is stopped when the PSU is not connected
//Used for switching between batteries when trickle charging
//Also used for debouncing PD7.
//PD7 needs to be low for about 500msec to return to IDLE state
ISR(WDT_vect)
{
    wdt_reset();
    //watchdog counter within watchdog interrupt
    //wdt should never reach 0.
    //if it does, we're stuck somewhere and we'll force a reset
    wdt--; 
    if(wdt==0) {
        //Force watchdog timer to reset the device
        MCUCR &= ~(1<<WDRF);
        WDTCSR |= (1<<WDCE)|(1<<WDE);
        WDTCSR = (1<<WDE);
        while(1);
    }
    //do some time keeping
    if(trickle>0) trickle--;
    if(debounce>0) debounce--;
    //only decrement the watchdog on timer in IDLE state
    if((state==STATE_IDLE) && (watchdogOn>0)) watchdogOn--;
    if(charger1timer>0) charger1timer--;
    if(charger2timer>0) charger3timer--;
    if(charger3timer>0) charger3timer--;
    if(psuConnected>0) psuConnected--;
}

