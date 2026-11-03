#include "bootInfo.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>

// RTC slow memory survives every reset except a power loss (power-on resets it
// to garbage: hence the magic word).
#define BOOT_RTC_MAGIC 0x424F4F54u // "BOOT"
RTC_NOINIT_ATTR static uint32_t s_rtcMagic;
RTC_NOINIT_ATTR static uint32_t s_rtcUptimeS;
RTC_NOINIT_ATTR static uint32_t s_rtcMinHeap;

static uint32_t s_boot = 0;
static uint8_t s_reason = 0;
static uint32_t s_prevUp = 0, s_prevHeap = 0;
static BootRec s_hist[BOOT_HIST_N];
static int s_histN = 0;

const char *bootReasonName(uint8_t r)
{
  switch ((esp_reset_reason_t)r)
  {
  case ESP_RST_POWERON: return "power-on";
  case ESP_RST_EXT: return "reset pin";
  case ESP_RST_SW: return "software";
  case ESP_RST_PANIC: return "crash";
  case ESP_RST_INT_WDT: return "int watchdog";
  case ESP_RST_TASK_WDT: return "task watchdog";
  case ESP_RST_WDT: return "watchdog";
  case ESP_RST_DEEPSLEEP: return "deep sleep";
  case ESP_RST_BROWNOUT: return "brownout";
  case ESP_RST_SDIO: return "sdio";
  default: return "unknown";
  }
}

void bootInfoInit(void)
{
  s_reason = (uint8_t)esp_reset_reason();
  const bool rtcOk = s_rtcMagic == BOOT_RTC_MAGIC && s_reason != ESP_RST_POWERON;
  s_prevUp = rtcOk ? s_rtcUptimeS : 0;
  s_prevHeap = rtcOk ? s_rtcMinHeap : 0;
  s_rtcMagic = BOOT_RTC_MAGIC;
  s_rtcUptimeS = 0;
  s_rtcMinHeap = 0;

  Preferences p;
  if (p.begin("cyd_boot", false))
  {
    s_boot = p.getUInt("n", 0) + 1;
    p.putUInt("n", s_boot);
    BootRec h[BOOT_HIST_N];
    size_t got = p.getBytes("h", h, sizeof(h));
    int n = (got % sizeof(BootRec)) ? 0 : (int)(got / sizeof(BootRec));
    if (n > BOOT_HIST_N) n = BOOT_HIST_N;
    s_hist[0] = BootRec{s_boot, s_reason, s_prevUp, s_prevHeap};
    for (int i = 0; i < n && i + 1 < BOOT_HIST_N; i++) s_hist[i + 1] = h[i];
    s_histN = n + 1 > BOOT_HIST_N ? BOOT_HIST_N : n + 1;
    p.putBytes("h", s_hist, s_histN * sizeof(BootRec));
    p.end();
  }
  Serial.printf("[BOOT] #%lu reason %s (%d), previous boot ran %lus, min heap %lu\n", (unsigned long)s_boot,
                bootReasonName(s_reason), (int)s_reason, (unsigned long)s_prevUp, (unsigned long)s_prevHeap);
}

void bootInfoTick(uint32_t uptimeS)
{
  s_rtcUptimeS = uptimeS;
  s_rtcMinHeap = (uint32_t)esp_get_minimum_free_heap_size();
}

uint32_t bootCount(void) { return s_boot; }
uint8_t bootReason(void) { return s_reason; }
uint32_t bootPrevUptimeS(void) { return s_prevUp; }
uint32_t bootPrevMinHeap(void) { return s_prevHeap; }
int bootHistory(BootRec *out, int maxN)
{
  int n = s_histN < maxN ? s_histN : maxN;
  for (int i = 0; i < n; i++) out[i] = s_hist[i];
  return n;
}

void bootInfoJson(char *buf, size_t len)
{
  size_t o = snprintf(buf, len,
                      "{\"count\":%lu,\"reason\":\"%s\",\"reason_code\":%d,\"prev_uptime_s\":%lu,\"prev_min_heap\":%lu,"
                      "\"min_heap\":%lu,\"history\":[",
                      (unsigned long)s_boot, bootReasonName(s_reason), (int)s_reason, (unsigned long)s_prevUp,
                      (unsigned long)s_prevHeap, (unsigned long)esp_get_minimum_free_heap_size());
  for (int i = 0; i < s_histN && o < len; i++)
    o += snprintf(buf + o, len - o, "%s{\"boot\":%lu,\"reason\":\"%s\",\"prev_uptime_s\":%lu,\"prev_min_heap\":%lu}",
                  i ? "," : "", (unsigned long)s_hist[i].boot, bootReasonName(s_hist[i].reason),
                  (unsigned long)s_hist[i].prevUptimeS, (unsigned long)s_hist[i].prevMinHeap);
  if (o < len) snprintf(buf + o, len - o, "]}");
}
