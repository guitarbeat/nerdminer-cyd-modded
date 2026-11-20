#ifndef _BUZZER_H_
#define _BUZZER_H_

#include <Arduino.h>

#define PIN_BUZZER 26
#define BUZZER_CHANNEL 6

void buzzerInit(void);
void buzzerTick(void);
void buzzerPlayCoin(void);
void buzzerPlayFanfare(void);
void buzzerPlayClick(void);

#endif // _BUZZER_H_
