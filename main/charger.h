#ifndef _CHARGER_H
#define _CHARGER_H

typedef struct {
    int state;
    int mode;
    int b1;
    int b2;
    int b3;
    int v1;
    int v2;
    int v3;
    int v4;
    int v5;
    int missed_count;
} battery_info_t;

battery_info_t *charger_enabled_state(void);
battery_info_t *charger_get_battery_state(void);
void charger_disable(void);
void charger_free(void);

#endif
