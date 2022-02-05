#ifndef _EINK_H
#define _EINK_H

void eink_start(void);
//void eink_display_test1(void);
//void eink_display_test2(void);
void eink_init(int fullUpdate);
void eink_display_number(uint8_t num, int b1, int b2, int b3, int sync, int chargeState, int fullUpdate);
void eink_display_setup(char *version, uint32_t ipaddress, char *ssid);
void eink_update(int fullUpdate);
void eink_stop(void);
void eink_shutdown_io(void);

#endif

