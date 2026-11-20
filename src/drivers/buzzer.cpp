#include "buzzer.h"
#include "storage/storage.h"

extern TSettings Settings;
extern volatile uint32_t g_shareFoundEvents;
extern volatile uint32_t g_blockFoundEvents;

struct Note {
  uint16_t freq;   // frequency in Hz (0 = silent pause)
  uint16_t durMs;  // duration in milliseconds
};

// Super Mario Coin Chime: B5 (988 Hz) -> E6 (1319 Hz)
static const Note kMelodyCoin[] = {
  { 988,  70 },  // B5
  { 1319, 320 }, // E6
};

// Victory Fanfare: Retro 8-bit victory fanfare
static const Note kMelodyFanfare[] = {
  { 523, 130 }, // C5
  { 523, 130 }, // C5
  { 523, 130 }, // C5
  { 523, 350 }, // C5
  { 415, 280 }, // G#4 / Ab4
  { 466, 280 }, // A#4 / Bb4
  { 523, 150 }, // C5
  { 0,    40 }, // pause
  { 466, 120 }, // A#4 / Bb4
  { 523, 500 }  // C5
};

// Quick confirmation tap/chirp when unmuting
static const Note kMelodyClick[] = {
  { 1046, 35 } // High C
};

static const Note* s_curMelody = nullptr;
static uint8_t s_curLen = 0;
static uint8_t s_curIdx = 0;
static unsigned long s_noteStartMs = 0;
static bool s_buzzerInit = false;

static uint32_t s_lastShareSound = 0;
static uint32_t s_lastBlockSound = 0;

static void startMelody(const Note* melody, uint8_t len)
{
  if (!Settings.buzzerEnabled)
    return;
  s_curMelody = melody;
  s_curLen = len;
  s_curIdx = 0;
  s_noteStartMs = millis();
  if (melody[0].freq > 0)
  {
    ledcSetup(BUZZER_CHANNEL, melody[0].freq, 8);
    ledcWrite(BUZZER_CHANNEL, 128); // 50% duty square wave
  }
  else
  {
    ledcWrite(BUZZER_CHANNEL, 0);
  }
}

void buzzerInit(void)
{
  ledcSetup(BUZZER_CHANNEL, 2000, 8);
  ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
  ledcWrite(BUZZER_CHANNEL, 0);
  s_curMelody = nullptr;
  s_curLen = 0;
  s_curIdx = 0;
  s_lastShareSound = g_shareFoundEvents;
  s_lastBlockSound = g_blockFoundEvents;
  s_buzzerInit = true;
}

void buzzerPlayCoin(void)
{
  startMelody(kMelodyCoin, sizeof(kMelodyCoin) / sizeof(kMelodyCoin[0]));
}

void buzzerPlayFanfare(void)
{
  startMelody(kMelodyFanfare, sizeof(kMelodyFanfare) / sizeof(kMelodyFanfare[0]));
}

void buzzerPlayClick(void)
{
  startMelody(kMelodyClick, sizeof(kMelodyClick) / sizeof(kMelodyClick[0]));
}

void buzzerTick(void)
{
  if (!s_buzzerInit)
    buzzerInit();

  const unsigned long now = millis();

  // Check for mining events
  if (g_blockFoundEvents != s_lastBlockSound)
  {
    s_lastBlockSound = g_blockFoundEvents;
    s_lastShareSound = g_shareFoundEvents;
    buzzerPlayFanfare();
  }
  else if (g_shareFoundEvents != s_lastShareSound)
  {
    s_lastShareSound = g_shareFoundEvents;
    buzzerPlayCoin();
  }

  if (!s_curMelody || !Settings.buzzerEnabled)
  {
    if (s_curMelody && !Settings.buzzerEnabled)
    {
      s_curMelody = nullptr;
      ledcWrite(BUZZER_CHANNEL, 0);
    }
    return;
  }

  if (now - s_noteStartMs >= s_curMelody[s_curIdx].durMs)
  {
    s_curIdx++;
    if (s_curIdx >= s_curLen)
    {
      // Melody complete
      s_curMelody = nullptr;
      ledcWrite(BUZZER_CHANNEL, 0);
    }
    else
    {
      s_noteStartMs = now;
      uint16_t f = s_curMelody[s_curIdx].freq;
      if (f > 0)
      {
        ledcSetup(BUZZER_CHANNEL, f, 8);
        ledcWrite(BUZZER_CHANNEL, 128);
      }
      else
      {
        ledcWrite(BUZZER_CHANNEL, 0);
      }
    }
  }
}
