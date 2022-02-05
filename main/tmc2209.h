#ifndef _TMC2209_H
#define _TMC2209_H

void tmc2209_init();
void tmc2209_rotate_cc(int motorId, uint32_t velocity);
void tmc2209_rotate_cw(int motorId, uint32_t velocity);
void tmc2209_stop(int motorId);
void tmc2209_shutdown(void);

#endif
