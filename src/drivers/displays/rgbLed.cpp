#include "../devices/device.h"

#ifdef HAS_RGB_LED

#include <Arduino.h>
#include "rgbLed.h"
#include "monitor.h"
#include "../storage/storage.h"

extern TSettings Settings;
extern monitor_data mMonitor;
extern String current_block;
extern volatile uint32_t g_acceptedShares;
extern volatile uint32_t g_blockFoundEvents;
extern bool g_usingFallback;

// Common anode: each colour pin is active low. Driven by PWM so any colour
// and brightness works. LEDC channels 2-4 (timers 1 and 2); the backlight
// has channel 0 on timer 0, and ledcSetup on channel 1 would retime it.
static const uint8_t kPins[3] = {LED_PIN, LED_PIN_G, LED_PIN_B};
static const uint8_t kFirstChannel = 2;

static uint32_t s_shown = 0xFFFFFFFF; // duties on the pins now, packed; none yet

// Reactive event pulses
enum RgbFx
{
  FX_NONE = 0,
  FX_BLOCK,     // Rainbow strobe explosion: valid block solved!
  FX_SHARE,     // Vibrant electric blue flash: pool share accepted
  FX_NET_BLOCK  // Subtle white sparkle: new block on the network
};
static RgbFx s_rgbFx = FX_NONE;
static unsigned long s_rgbFxStart = 0;
static String s_lastBlock;
static uint32_t s_lastAccepted = 0;
static uint32_t s_lastBlockEvents = 0;

// Rough gamma (square) so the brightness setting and mixed colours look
// even; any lit component stays at least 1/255.
static uint32_t level(uint32_t component, uint32_t brightness)
{
  const uint32_t v = component * brightness / 255;
  return (v * v + 254) / 255;
}

static void show(uint32_t rgb)
{
  const uint32_t b = constrain(Settings.ledBrightness, 1, 255);
  const uint32_t r = level((rgb >> 16) & 0xFF, b);
  const uint32_t g = level((rgb >> 8) & 0xFF, b);
  const uint32_t bl = level(rgb & 0xFF, b);
  const uint32_t packed = (r << 16) | (g << 8) | bl;
  if (packed == s_shown)
    return; // only touch the peripheral when something changes
  s_shown = packed;
  ledcWrite(kFirstChannel + 0, 255 - r); // 255 = pin held high = off
  ledcWrite(kFirstChannel + 1, 255 - g);
  ledcWrite(kFirstChannel + 2, 255 - bl);
}

void rgbLedInit(void)
{
  // Off is written after attaching: ledcAttachPin takes the duty it reads back
  // from a channel that has not run yet (0 = pin low = all three lit), so a
  // write before it is lost.
  for (int i = 0; i < 3; i++)
  {
    ledcSetup(kFirstChannel + i, 5000, 8);
    ledcAttachPin(kPins[i], kFirstChannel + i);
    ledcWrite(kFirstChannel + i, 255);
  }
  s_shown = 0xFFFFFFFF; // first rgbLedTick writes whatever it wants, off included
  s_lastAccepted = g_acceptedShares;
  s_lastBlockEvents = g_blockFoundEvents;
}

void rgbLedTick(void)
{
  uint32_t rgb = 0;
  const unsigned long now = millis();

  // Event detection: Valid block solved or pool share accepted
  if (g_blockFoundEvents != s_lastBlockEvents)
  {
    s_lastBlockEvents = g_blockFoundEvents;
    s_rgbFx = FX_BLOCK;
    s_rgbFxStart = now;
  }
  else if (s_lastBlock.length() && current_block.length() && current_block != s_lastBlock)
  {
    s_lastBlock = current_block;
    if (s_rgbFx == FX_NONE)
    {
      s_rgbFx = FX_NET_BLOCK;
      s_rgbFxStart = now;
    }
  }
  else
  {
    s_lastBlock = current_block;
  }

  uint32_t acc = g_acceptedShares;
  if (acc != s_lastAccepted)
  {
    s_lastAccepted = acc;
    if (s_rgbFx == FX_NONE || s_rgbFx == FX_NET_BLOCK)
    {
      s_rgbFx = FX_SHARE;
      s_rgbFxStart = now;
    }
  }

  // Active event FX override background pattern
  if (s_rgbFx == FX_BLOCK)
  {
    unsigned long t = now - s_rgbFxStart;
    if (t < 1800)
    {
      // Rainbow Strobe Explosion!
      int step = (t / 60) % 8;
      const uint32_t rainbow[8] = {
        0xFF0000, // Vibrant Red
        0xFFA500, // Amber / Orange
        0xFFFF00, // Golden Yellow
        0x00FF00, // Emerald Green
        0x00FFFF, // Neon Cyan
        0x0033FF, // Deep Royal Blue
        0xFF00FF, // Magenta Purple
        0xFFFFFF  // Strobe White
      };
      // Strobe gap of 15ms every 60ms cycle
      rgb = ((t % 60) > 45) ? 0 : rainbow[step];
    }
    else
    {
      s_rgbFx = FX_NONE;
    }
  }
  else if (s_rgbFx == FX_SHARE)
  {
    unsigned long t = now - s_rgbFxStart;
    if (t < 250)
    {
      // Vibrant electric blue flash!
      rgb = 0x0066FF;
    }
    else
    {
      s_rgbFx = FX_NONE;
    }
  }
  else if (s_rgbFx == FX_NET_BLOCK)
  {
    unsigned long t = now - s_rgbFxStart;
    if (t < 400)
    {
      bool on = (t < 100) || (t >= 200 && t < 300);
      rgb = on ? 0xFFFFFF : 0;
    }
    else
    {
      s_rgbFx = FX_NONE;
    }
  }

  // If no event FX active, evaluate background mode
  if (s_rgbFx == FX_NONE)
  {
    switch (Settings.ledMode)
    {
    case LED_MODE_OFF:
      rgb = 0;
      break;

    case LED_MODE_SHARE_PULSE:
      // Dark stealth mining: only glow when fallback pool warning
      if (g_usingFallback)
      {
        uint32_t t = now % 1200;
        int pulse = (t < 600) ? (t * 255 / 600) : ((1200 - t) * 255 / 600);
        int bVal = 20 + (pulse * 150) / 255;
        rgb = (((255 * bVal) / 255) << 16) | (((140 * bVal) / 255) << 8);
      }
      else
      {
        rgb = 0;
      }
      break;

    case LED_MODE_STEADY:
      rgb = Settings.ledColor;
      break;

    case LED_MODE_STATUS: // AMBIENT
      switch (mMonitor.NerdStatus)
      {
      case NM_waitingConfig:
        rgb = 0xFF0000; // red steady
        break;
      case NM_Connecting:
        rgb = 0x0000FF; // blue connecting
        break;
      default: // NM_hashing: calm ambient sine breathing
        {
          uint32_t t = now % 2400;
          int p = (t < 1200) ? (t * 255 / 1200) : ((2400 - t) * 255 / 1200);
          p = (p * p) / 255;
          int bVal = 40 + (p * 215) / 255;
          uint32_t base = Settings.ledColor ? Settings.ledColor : 0x00FF88;
          rgb = (((((base >> 16) & 0xFF) * bVal) / 255) << 16) |
                (((((base >> 8) & 0xFF) * bVal) / 255) << 8) |
                ((((base & 0xFF) * bVal) / 255));
        }
        break;
      }
      break;

    case LED_MODE_REACTIVE: // Reactive Stratum Glow
    default:
      switch (mMonitor.NerdStatus)
      {
      case NM_waitingConfig:
        rgb = 0xFF0000; // red, steady: starting up or setup portal
        break;

      case NM_Connecting:
        {
          // Pulsing Amber when connecting / reconnecting
          uint32_t t = now % 1000;
          int p = (t < 500) ? (t * 255 / 500) : ((1000 - t) * 255 / 500);
          int bVal = 30 + (p * 225) / 255;
          rgb = (((255 * bVal) / 255) << 16) | (((140 * bVal) / 255) << 8);
        }
        break;

      case NM_hashing:
      default:
        {
          if (g_usingFallback)
          {
            // Pulsing Amber when operating on secondary fallback pool!
            uint32_t t = now % 1200;
            int pulse = (t < 600) ? (t * 255 / 600) : ((1200 - t) * 255 / 600);
            int bVal = 25 + (pulse * 230) / 255;
            rgb = (((255 * bVal) / 255) << 16) | (((140 * bVal) / 255) << 8);
          }
          else
          {
            // Breathing Green/Cyan: Normal steady mining at ~1.0 MH/s
            uint32_t t = now % 2560;
            int breath = (t < 1280) ? (t * 255 / 1280) : ((2560 - t) * 255 / 1280);
            breath = (breath * breath) / 255; // Quadratic ease
            int bVal = 35 + (breath * 220) / 255;
            int greenVal = 220 + (breath * 35) / 255;
            int blueVal = 25 + (breath * 225) / 255;
            rgb = (((greenVal * bVal) / 255) << 8) | ((blueVal * bVal) / 255);
          }
        }
        break;
      }
      break;
    }
  }

  if (Settings.ledScreenOff && ledcRead(0) == 0)
    rgb = 0; // screen switched or timed out off (backlight is LEDC channel 0)

  show(rgb);
}

#endif // HAS_RGB_LED
