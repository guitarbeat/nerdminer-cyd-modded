#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include <stdint.h>
#include <stddef.h>

// Why the chip last (re)started, kept across reboots. bootInfoInit() runs once
// at the top of setup() (before any miner task exists): it reads
// esp_reset_reason(), bumps an NVS boot counter and keeps a short history.
// bootInfoTick() runs from the monitor task once per second and only stores
// two words into RTC memory (uptime, lowest free heap) so the NEXT boot can
// say how long the previous one lasted and how low its heap got. No NVS
// writes after setup, nothing on any mining path.
#define BOOT_HIST_N 6
struct BootRec
{
  uint32_t boot;        // boot number
  uint8_t reason;       // esp_reset_reason_t
  uint32_t prevUptimeS; // uptime of the boot before (0 = unknown, e.g. power loss)
  uint32_t prevMinHeap; // lowest free heap of the boot before (0 = unknown)
};
void bootInfoInit(void);
void bootInfoTick(uint32_t uptimeS);
uint32_t bootCount(void);
uint8_t bootReason(void);
const char *bootReasonName(uint8_t r); // short: "power-on", "panic", "brownout", ...
int bootHistory(BootRec *out, int maxN); // newest first
uint32_t bootPrevUptimeS(void);
uint32_t bootPrevMinHeap(void);
// JSON object {"count":..,"reason":"..",...} appended by /api/status and /api/stats
void bootInfoJson(char *buf, size_t len);

#endif
