#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <nvs.h>
//#include "ShaTests/nerdSHA256.h"
#include "ShaTests/nerdSHA256plus.h"
#include "stratum.h"
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "timeconst.h"
#include "drivers/displays/display.h"
#include "drivers/storage/storage.h"
#include "wManager.h"
#include <mutex>
#include <list>
#include <stddef.h> // offsetof (saved stats record)
#include <string.h>
#include <map>
#include <memory>
#include "mbedtls/sha256.h"
#include "i2c_master.h"
#include <esp_timer.h>
#include <math.h>
 
//10 Jobs per second
#define NONCE_PER_JOB_SW 4096
// Chunk size the classic-ESP32 pipelined path re-enters C for. Bumped 16x
// from the original 16*1024: profiling against BitsyMiner (which never
// returns to C mid-job at all) showed the redundant per-chunk SW midstate/
// bake precompute plus periph_module_reset() was costing real throughput
// at the old size, dwarfing the actual hash time of a 16384-nonce chunk
// (~25ms at ~650KH/s). 256*1024 chunks take ~280ms, well inside the 900s
// mining watchdog timeout, and job-change latency at that granularity is
// imperceptible against how often stratum jobs actually change. Tried
// pushing this to 1024*1024 (~1.5s/chunk) expecting a further gain from
// amortizing the per-chunk cost even more; measured *worse* (~653KH/s
// steady vs ~680KH/s here) instead, most likely because this pool's very
// low share difficulty means job/share churn is frequent enough that the
// longer stale-job latency at 1M costs more than the extra amortization
// saves. 256*1024 is the empirically-best point found so far.
#define NONCE_PER_JOB_HW 256*1024

//#define I2C_SLAVE

//#define SHA256_VALIDATE
//#define RANDOM_NONCE
#define RANDOM_NONCE_MASK 0xFFFFC000

#ifdef HARDWARE_SHA265
#include <sha/sha_dma.h>
#include <hal/sha_hal.h>
#include <hal/sha_ll.h>

#if defined(CONFIG_IDF_TARGET_ESP32)
#include <sha/sha_parallel_engine.h>
#include <driver/periph_ctrl.h>
#include <hal/efuse_hal.h>
#endif

#endif

nvs_handle_t stat_handle;

uint32_t templates = 0;
uint32_t hashes = 0;
uint32_t Mhashes = 0;
uint32_t totalKHashes = 0;
uint32_t elapsedKHs = 0;
uint64_t upTime = 0;

volatile uint64_t g_currentBlockHashes = 0;
volatile uint64_t g_lastBlockHashes = 0;
volatile uint32_t g_blockStartTimeSec = 0;
volatile uint32_t g_lastBlockDurationSec = 0;
double g_blockBestDiff = 0.0;
static String s_activePrevBlockHash = "";

// Per-path attempt counters, only used to print a HW vs SW hashrate
// breakdown under DEBUG_MINING so real hardware can be A/B tested
// (see FORCE_SW_MINING in mining.h). Each miner task only ever touches
// its own counter, so no locking is needed.
volatile uint32_t debugHashesHw = 0;
volatile uint32_t debugHashesSw[2] = {0, 0};
// Diagnostics for the pipelined-asm HW path. Every candidate the loop reports
// (low 16 digest bits zero, ~1 in 65536 nonces) is recomputed in software:
//   hits     candidates reported by the hardware loop
//   real     ...that software confirms
//   mismatch ...that it doesn't, i.e. hashes the hardware got wrong
//   accepted confirmed candidates that were also shares
// mismatch/hits estimates the fraction of ALL hashes computed wrong (silently
// lost shares); hits * 65536 / seconds is the true hashrate, independent of
// the hash counters.
volatile uint32_t debugPipelinedHits = 0;
volatile uint32_t debugPipelinedHwSwMismatch = 0;
volatile uint32_t debugPipelinedAccepted = 0;
volatile uint32_t debugPipelinedRealHits = 0;
// Diagnostic for the classic-ESP32 chunk-boundary "dead time" theory: total
// microseconds spent between finishing one HW job chunk (mutex released)
// and starting the next (mutex reacquired, first candidate about to hash),
// and how many such gaps were measured. debugChunkGapUs / debugChunkCount
// is the average per-chunk overhead outside the hot loop -- job-queue pop,
// shared_ptr alloc/free, memcpy, engine lock/unlock, and (rarely) blocking
// on g_hwShaMutex against a concurrent pool-stats HTTPS/TLS fetch.
volatile uint32_t debugChunkGapUs = 0;
volatile uint32_t debugChunkCount = 0;
// Same idea, but for the cost INSIDE a chunk: every early-reject candidate
// (~1/65536 nonces) exits the asm entirely, and the SHA hardware sits
// completely idle while the CPU does a full SW SHA256d reverify + debug
// bookkeeping + periph_module_reset(). debugChunkGapUs above can't see this
// -- it only spans time BETWEEN chunks, and a hit happens INSIDE one.
volatile uint32_t debugHitOverheadUs = 0;
volatile uint32_t debugHitOverheadCount = 0;

volatile uint32_t shares; // 32-bit shares (difficulty >= 1) the pool ACCEPTED
volatile uint32_t shares32Rejected = 0; // 32-bit shares the pool refused (see mining.h)
volatile uint32_t valids; // increased if blockhash <= target

// Track best diff
// Best hash found by THIS miner, whatever the pool difficulty. It used to be
// updated only when the pool acknowledged a submitted share, and a share is
// only submitted above the pool difficulty: with a pool that sets (or keeps)
// a high difficulty, nothing ever got there and the value never moved. The
// miners now feed every hash they check (16-bit prefilter) to updateBestDiff().
double best_diff = 0.0;
static portMUX_TYPE s_bestDiffMux = portMUX_INITIALIZER_UNLOCKED;

void updateBestDiff(double diff)
{
  if (!(diff > best_diff)) // unlocked look first: improvements are very rare
    return;
  portENTER_CRITICAL(&s_bestDiffMux);
  if (diff > best_diff)
    best_diff = diff;
  portEXIT_CRITICAL(&s_bestDiffMux);
}

// A double is two 32-bit words here: read it under the lock so a write from
// a miner core can't be seen half done.
double getBestDiff()
{
  portENTER_CRITICAL(&s_bestDiffMux);
  double v = best_diff;
  portEXIT_CRITICAL(&s_bestDiffMux);
  return v;
}

// Stratum ping latency & live network stats
volatile uint32_t g_stratumPing = 0;
volatile uint32_t g_lastJobMs = 0;   // millis() of the last mining.notify (0 = none yet)
// Bumped on every pool-accepted submit (drives the CYD RGB "share" pulse).
volatile uint32_t g_acceptedShares = 0;
double g_networkDifficulty = 0.0;
bool g_usingFallback = false;

// ---- Protocol screen hash samples (display-only; must not move the needle) --
uint16_t g_hashSamples[HASH_SAMPLE_N];
volatile uint8_t g_hashSampleHead = 0;
volatile uint8_t g_hashSampleCount = 0;
volatile uint16_t g_hashSampleLatest = 0;

// Bitcoin LE / nerd digest / mbedtls BE: numeric MSB lives at byte 31.
static inline uint16_t hashLog2Q8FromDigest(const uint8_t h[32])
{
  for (int i = 31; i >= 0; --i)
  {
    uint8_t b = h[i];
    if (!b)
      continue;
    int msb = 31 - __builtin_clz((unsigned)b); // 0..7 within byte
    int bit = i * 8 + msb;                     // 0..255
    // Next 8 bits below the leading 1 for Q8 fraction.
    uint32_t win = (uint32_t)b << 16;
    if (i > 0)
      win |= (uint32_t)h[i - 1] << 8;
    if (i > 1)
      win |= (uint32_t)h[i - 2];
    // Shift so the leading 1 sits at bit 23; bits 22..15 are the fraction.
    win <<= (7 - msb);
    uint8_t frac = (uint8_t)((win >> 15) & 0xFFu);
    return (uint16_t)((bit << 8) | frac);
  }
  return 0;
}

void hashSamplePushDigest(const uint8_t hash[32])
{
  uint16_t v = hashLog2Q8FromDigest(hash);
  if (v == 0)
    return;
  uint8_t i = g_hashSampleHead;
  g_hashSamples[i] = v;
  g_hashSampleHead = (uint8_t)((i + 1u) % HASH_SAMPLE_N);
  if (g_hashSampleCount < HASH_SAMPLE_N)
    g_hashSampleCount++;
  g_hashSampleLatest = v;
  // Every plotted sample is a real hash: keep best_diff >= any plotted point
  // so BEST is always the top of the scatter (diff = 2^(224 - log2 hash)).
  int bit = (int)(v >> 8);
  if (bit <= 224 && bit > 0)
  {
    double d = ldexp(1.0, 224 - bit) / (1.0 + (double)(v & 0xFF) / 256.0);
    if (d > best_diff && d < 1e16)
      best_diff = d;
  }
}

uint16_t hashLog2Q8FromDifficulty(double difficulty)
{
  if (!(difficulty > 0.0))
    return 0xFF00; // near top of plot
  // truediffone = 0xFFFF * 2^208 → log2 ≈ 223.999978314
  const double log2True = 223.999978314;
  int exp = 0;
  double m = frexp(difficulty, &exp); // difficulty = m * 2^exp, m in [0.5, 1)
  // log2(m) for m in [0.5,1): use a short polynomial on (2m-1) in [0,1)
  double t = 2.0 * m - 1.0;
  // log2(1+t) ≈ (t - t^2/2 + t^3/3) / ln(2) for t in [0,1) where 1+t = 2m
  // But m in [0.5,1) → 2m in [1,2) → log2(m) = log2(2m) - 1
  double u = 2.0 * m; // [1,2)
  double z = u - 1.0; // [0,1)
  double log2m = -1.0 + (z - 0.5 * z * z + (z * z * z) / 3.0) / 0.69314718056;
  double log2d = (double)exp + log2m;
  double log2h = log2True - log2d;
  if (log2h < 0.0)
    log2h = 0.0;
  if (log2h > 255.996)
    log2h = 255.996;
  return (uint16_t)(log2h * 256.0);
}




// Variables to hold data from custom textboxes
//Track mining stats in non volatile memory
extern TSettings Settings;

static bool volatile isMinerSuscribed = false;

// Pool settings saved on the LAN settings page (webConfig.cpp). The page
// doesn't write them into Settings itself: the stratum task reads the pool
// address while it connects, holding its c_str() through a blocking DNS
// lookup. They wait here until the stratum loop swaps them in between
// connections (takePoolChange). Guarded by g_settingsMutex.
static bool s_poolChangePending = false;
static TSettings s_poolChange; // only the pool fields are used

static void copyPoolSettings(TSettings &to, const TSettings &from)
{
  to.PoolAddress = from.PoolAddress;
  to.PoolPort = from.PoolPort;
  memcpy(to.BtcWallet, from.BtcWallet, sizeof(to.BtcWallet));
  to.BtcWallet[sizeof(to.BtcWallet) - 1] = 0;
  memcpy(to.PoolPassword, from.PoolPassword, sizeof(to.PoolPassword));
  to.PoolPassword[sizeof(to.PoolPassword) - 1] = 0;
  to.FallbackPoolAddress = from.FallbackPoolAddress;
  to.FallbackPoolPort = from.FallbackPoolPort;
  memcpy(to.FallbackPoolPassword, from.FallbackPoolPassword, sizeof(to.FallbackPoolPassword));
  to.FallbackPoolPassword[sizeof(to.FallbackPoolPassword) - 1] = 0;
}

// Backup pool (see mining.h). Only the stratum task changes these.
static volatile bool s_onBackup = false;
static uint8_t s_poolFailures = 0;   // failed attempts in a row on the pool in use
static uint32_t s_backupSince = 0;   // millis() of the switch to the backup, or of the last look at the main pool

static bool backupPoolSet(void)
{
  return !Settings.FallbackPoolAddress.isEmpty() && Settings.FallbackPoolPort > 0;
}

bool onBackupPool(void)
{
  return s_onBackup;
}

const String &activePoolAddress(void)
{
  return s_onBackup ? Settings.FallbackPoolAddress : Settings.PoolAddress;
}

int activePoolPort(void)
{
  return s_onBackup ? Settings.FallbackPoolPort : Settings.PoolPort;
}

const char *activePoolPassword(void)
{
  return (s_onBackup && Settings.FallbackPoolPassword[0]) ? Settings.FallbackPoolPassword : Settings.PoolPassword;
}

// While on the backup: does the main pool take a connection again? Blocks
// the stratum task for a few seconds at most; the miners keep working on the
// backup's current job meanwhile.
static bool mainPoolAnswers(void)
{
  IPAddress ip;
  if (!WiFi.hostByName(Settings.PoolAddress.c_str(), ip))
    return false;
  WiFiClient probe;
  const bool ok = probe.connect(ip, Settings.PoolPort, 3000);
  probe.stop();
  return ok;
}

// For /api/status: counted by the stratum task...
static volatile uint32_t s_sharesAccepted = 0;
volatile uint32_t g_shareFoundEvents = 0; // see mining.h
volatile uint32_t g_blockFoundEvents = 0;
static volatile uint32_t s_sharesRejected = 0;
static volatile double s_poolDifficulty = DEFAULT_DIFFICULTY;

// ---- Share history (see mining.h) -----------------------------------------
static ShareRec s_shareHist[SHARE_HIST_N];
static uint8_t s_shareHead = 0, s_shareCount = 0; // next write slot, entries used
static volatile uint32_t s_shareSeq = 0;
static portMUX_TYPE s_shareMux = portMUX_INITIALIZER_UNLOCKED;
static void saveShareHistory(void); // NVS, below

// Last error the pool answered outside a share (authorize refused, ...),
// shown on /api/stats and the screens when no job arrives.
static char s_poolError[96] = "";
static portMUX_TYPE s_poolErrMux = portMUX_INITIALIZER_UNLOCKED;
static void notePoolError(const String &line)
{
  StaticJsonDocument<512> j;
  if (deserializeJson(j, line))
    return;
  JsonVariant e = j["error"];
  if (e.isNull())
    return;
  char msg[96];
  if (e.is<JsonArray>() && e.size() >= 2)
    snprintf(msg, sizeof(msg), "%d: %s", e[0].as<int>(), e[1].as<const char *>() ? e[1].as<const char *>() : "?");
  else if (e.is<const char *>())
    snprintf(msg, sizeof(msg), "%s", e.as<const char *>());
  else
    return;
  for (char *c = msg; *c; c++) // ASCII only (screen font, JSON)
    if ((unsigned char)*c < 32 || (unsigned char)*c > 126 || *c == '"' || *c == '\\')
      *c = ' ';
  portENTER_CRITICAL(&s_poolErrMux);
  strcpy(s_poolError, msg);
  portEXIT_CRITICAL(&s_poolErrMux);
  Serial.printf("[STRATUM] Pool error: %s\n", msg);
}
static void clearPoolError(void)
{
  if (!s_poolError[0])
    return;
  portENTER_CRITICAL(&s_poolErrMux);
  s_poolError[0] = 0;
  portEXIT_CRITICAL(&s_poolErrMux);
}
void getPoolError(char *out, size_t n)
{
  portENTER_CRITICAL(&s_poolErrMux);
  strncpy(out, s_poolError, n - 1);
  portEXIT_CRITICAL(&s_poolErrMux);
  out[n - 1] = 0;
}
static void loadShareHistory(void);

static void recordShare(double diff, bool accepted)
{
  ShareRec r;
  memset(&r, 0, sizeof(r));
  r.diff = diff;
  time_t utc = 0;
  r.utc = getUtcTime(utc, false) && utc > 1600000000 ? (uint32_t)utc : 0;
  r.accepted = accepted ? 1 : 0;
  portENTER_CRITICAL(&s_shareMux);
  s_shareHist[s_shareHead] = r;
  s_shareHead = (uint8_t)((s_shareHead + 1) % SHARE_HIST_N);
  if (s_shareCount < SHARE_HIST_N)
    s_shareCount++;
  s_shareSeq++;
  portEXIT_CRITICAL(&s_shareMux);
  saveShareHistory();
}

int getShareHistory(ShareRec *out, int max)
{
  portENTER_CRITICAL(&s_shareMux);
  int n = s_shareCount < max ? s_shareCount : max;
  for (int i = 0; i < n; i++)
    out[i] = s_shareHist[(s_shareHead + SHARE_HIST_N - n + i) % SHARE_HIST_N];
  portEXIT_CRITICAL(&s_shareMux);
  return n;
}

uint32_t getShareHistorySeq(void) { return s_shareSeq; }
double getPoolDifficulty(void) { return s_poolDifficulty; }

void clearShareHistory(void)
{
  portENTER_CRITICAL(&s_shareMux);
  s_shareHead = s_shareCount = 0;
  s_shareSeq++;
  portEXIT_CRITICAL(&s_shareMux);
  saveShareHistory();
}
// ...and the kH counter, sampled once a second by the monitor loop.
#define STATUS_RATE_SAMPLES 11 // 10 s window
static uint32_t s_rateKh[STATUS_RATE_SAMPLES];
static uint32_t s_rateMs[STATUS_RATE_SAMPLES];
static volatile uint8_t s_rateHead = 0, s_rateCount = 0;
static uint32_t s_bootKh = 0, s_bootMs = 0;

static void statusRateSample(uint32_t kh, uint32_t ms)
{
  const uint8_t i = (s_rateHead + 1) % STATUS_RATE_SAMPLES;
  s_rateKh[i] = kh;
  s_rateMs[i] = ms;
  s_rateHead = i;
  if (s_rateCount < STATUS_RATE_SAMPLES)
    s_rateCount++;
}

MinerStatus getMinerStatus(void)
{
  MinerStatus s{};
  const uint8_t head = s_rateHead, n = s_rateCount;
  if (n >= 2)
  {
    const uint8_t oldest = (head + STATUS_RATE_SAMPLES - (n - 1)) % STATUS_RATE_SAMPLES;
    const uint32_t ms = s_rateMs[head] - s_rateMs[oldest];
    if (ms)
      s.hashrateKHs = (s_rateKh[head] - s_rateKh[oldest]) * 1000.0f / ms;
    const uint32_t bootMs = s_rateMs[head] - s_bootMs;
    if (bootMs)
      s.avgHashrateKHs = (s_rateKh[head] - s_bootKh) * 1000.0f / bootMs;
  }
  s.accepted = s_sharesAccepted;
  s.rejected = s_sharesRejected;
  s.subscribed = isMinerSuscribed;
  s.poolDifficulty = s_poolDifficulty;
  s.hwLoop = activeHwShaLoopName();
  return s;
}

void requestPoolChange(const TSettings &s)
{
  std::lock_guard<std::mutex> lock(g_settingsMutex);
  copyPoolSettings(s_poolChange, s);
  s_poolChangePending = true;
}

void readPoolSettings(TSettings &s)
{
  std::lock_guard<std::mutex> lock(g_settingsMutex);
  copyPoolSettings(s, s_poolChangePending ? s_poolChange : Settings);
}

static bool takePoolChange(void)
{
  std::lock_guard<std::mutex> lock(g_settingsMutex);
  if (!s_poolChangePending)
    return false;
  const TSettings &n = s_poolChange;
  const bool mainChanged = n.PoolAddress != Settings.PoolAddress || n.PoolPort != Settings.PoolPort ||
                           strcmp(n.BtcWallet, Settings.BtcWallet) != 0 ||
                           strcmp(n.PoolPassword, Settings.PoolPassword) != 0;
  const bool backupChanged = n.FallbackPoolAddress != Settings.FallbackPoolAddress ||
                             n.FallbackPoolPort != Settings.FallbackPoolPort ||
                             strcmp(n.FallbackPoolPassword, Settings.FallbackPoolPassword) != 0;
  copyPoolSettings(Settings, s_poolChange);
  s_poolChangePending = false;
  if (!mainChanged && !(s_onBackup && backupChanged))
    return false;
  if (mainChanged)
    s_poolError[0] = 0;
  if (mainChanged)
    g_networkDifficulty = 0.0; // new pool may mine another chain: wait for its first job's nbits
  s_onBackup = s_onBackup && !mainChanged && backupPoolSet();
  g_usingFallback = s_onBackup;
  s_poolFailures = 0;
  s_backupSince = millis();
  return true;
}

IPAddress serverIP(1, 1, 1, 1); //Temporally save poolIPaddres
static String s_resolvedPoolHost = "";

//Global work data 
static WiFiClient client;
static miner_data mMiner; //Global miner data (Create a miner class TODO)
mining_subscribe mWorker;
mining_job mJob;
monitor_data mMonitor;
unsigned long mLastTXtoPool = millis();

int saveIntervals[7] = {5 * 60, 15 * 60, 30 * 60, 1 * 3600, 3 * 3600, 6 * 3600, 12 * 3600};
// Longest gap between two saves. After ~11 h of uptime the schedule above
// saved only every 12 h, so a restart (flashing, unplugging, a crash) could
// lose up to 12 h of shares / best diff. A board can cap it from
// platformio.ini; NVS spreads the writes, so even every few minutes is far
// within the flash's endurance.
#ifndef STATS_SAVE_MAX_s
#define STATS_SAVE_MAX_s (12 * 3600)
#endif
// Stats are only written once they have been read back at boot: saving the
// still-empty in-RAM counters before that would wipe the saved ones.
static volatile bool s_statsLoaded = false;
int saveIntervalsSize = sizeof(saveIntervals)/sizeof(saveIntervals[0]);
int currentIntervalIndex = 0;

bool checkPoolConnection(void) {
  
  if (client.connected()) {
    return true;
  }
  
  isMinerSuscribed = false;

  const String &targetPool = activePoolAddress();
  const int targetPort = activePoolPort();

  Serial.printf("Client not connected, trying to connect to %s:%d...\n", targetPool.c_str(), targetPort);

  // Resolve DNS if host changed or not resolved yet
  if (s_resolvedPoolHost != targetPool || serverIP == IPAddress(1,1,1,1)) {
    IPAddress resolved;
    if (WiFi.hostByName(targetPool.c_str(), resolved)) {
      serverIP = resolved;
      s_resolvedPoolHost = targetPool;
      Serial.printf("Resolved DNS for %s -> %s\n", targetPool.c_str(), serverIP.toString().c_str());
    } else {
      Serial.printf("Failed to resolve DNS for %s\n", targetPool.c_str());
      return false;
    }
  }

  // Try connecting pool IP and measure initial TCP connect RTT
  unsigned long t0 = millis();
  if (!client.connect(serverIP, targetPort)) {
    Serial.println("Impossible to connect to: " + targetPool);
    s_resolvedPoolHost = ""; // Force re-resolve on next try
    return false;
  }

  uint32_t connRtt = millis() - t0;
  if (g_stratumPing == 0) {
    g_stratumPing = connRtt;
  }
  return true;
}

//Implements a socketKeepAlive function and 
//checks if pool is not sending any data to reconnect again.
//Even connection could be alive, pool could stop sending new job NOTIFY
unsigned long mStart0Hashrate = 0;
bool checkPoolInactivity(unsigned int keepAliveTime, unsigned long inactivityTime){ 

    unsigned long currentKHashes = (Mhashes*1000) + hashes/1000;
    unsigned long elapsedKHs = currentKHashes - totalKHashes;

    uint32_t time_now = millis();

    // If no shares sent to pool
    // send something to pool to hold socket oppened
    if (time_now < mLastTXtoPool) //32bit wrap
      mLastTXtoPool = time_now;
    if ( time_now > mLastTXtoPool + keepAliveTime)
    {
      mLastTXtoPool = time_now;
      Serial.println("  Sending  : KeepAlive suggest_difficulty");
      //if (client.print("{}\n") == 0) {
      tx_suggest_difficulty(client, DEFAULT_DIFFICULTY);
      /*if(tx_suggest_difficulty(client, DEFAULT_DIFFICULTY)){
        Serial.println("  Sending keepAlive to pool -> Detected client disconnected");
        return true;
      }*/
    }

    if(elapsedKHs == 0){
      //Check if hashrate is 0 during inactivityTIme
      if(mStart0Hashrate == 0) mStart0Hashrate  = time_now; 
      if((time_now-mStart0Hashrate) > inactivityTime) { mStart0Hashrate=0; return true;}
      return false;
    }

  mStart0Hashrate = 0;
  return false;
}

struct JobRequest
{
  uint32_t id;
  uint32_t nonce_start;
  uint32_t nonce_count;
  double difficulty;
  uint8_t sha_buffer[128];
  uint32_t midstate[8];
  uint32_t bake[16];
};

struct JobResult
{
  uint32_t id;
  uint32_t nonce;
  uint32_t nonce_count;
  double difficulty;
  uint8_t hash[32];
};

static std::mutex s_job_mutex;
std::mutex g_hwShaMutex;  // see mining.h for why this exists
#if defined(PIPELINED_S3_MINING)
std::atomic<int> g_hwShaWanted{0}; // see mining.h
#endif

// ---------------------------------------------------------------------------
// Telemetry & Profiler State
// ---------------------------------------------------------------------------
volatile MiningCycleProfile g_cycleProfile = {0};
volatile uint32_t g_dmaTransferCount = 0;
CryptoAuditTelemetry g_cryptoAudit = {0};
static std::mutex s_cryptoAuditMutex;

void pushCryptoAuditSample(uint32_t nonce, double diff, const uint8_t hash[32])
{
  std::lock_guard<std::mutex> lock(s_cryptoAuditMutex);
  uint8_t idx = g_cryptoAudit.sample_count % CRYPTO_AUDIT_SAMPLE_N;
  g_cryptoAudit.samples[idx].nonce = nonce;
  g_cryptoAudit.samples[idx].difficulty = diff;
  memcpy(g_cryptoAudit.samples[idx].hash, hash, 32);
  if (g_cryptoAudit.sample_count < CRYPTO_AUDIT_SAMPLE_N)
    g_cryptoAudit.sample_count++;
}

TaskStackMetrics getTaskStackMetrics(void)
{
  TaskStackMetrics m = {0};
  TaskHandle_t hMonitor = xTaskGetHandle("Monitor");
  TaskHandle_t hStratum = xTaskGetHandle("Stratum");
  TaskHandle_t hHw = xTaskGetHandle("MinerHw-0");
  TaskHandle_t hSw = xTaskGetHandle("MinerSw-1");
  TaskHandle_t hTouch = xTaskGetHandle("cydTouch");

  if (hMonitor) m.monitor_stack_hwm = uxTaskGetStackHighWaterMark(hMonitor);
  if (hStratum) m.stratum_stack_hwm = uxTaskGetStackHighWaterMark(hStratum);
  if (hHw) m.miner_hw_stack_hwm = uxTaskGetStackHighWaterMark(hHw);
  if (hSw) m.miner_sw_stack_hwm = uxTaskGetStackHighWaterMark(hSw);
  if (hTouch) m.touch_stack_hwm = uxTaskGetStackHighWaterMark(hTouch);

  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  m.free_heap_internal = info.total_free_bytes;
  m.min_free_heap = info.minimum_free_bytes;
  m.max_alloc_heap = info.largest_free_block;
  m.allocated_blocks = info.allocated_blocks;
  m.free_blocks = info.free_blocks;
  return m;
}

// ---------------------------------------------------------------------------
// Zero-Allocation Static Object Pools for Hot Mining Loop
// ---------------------------------------------------------------------------
#define POOL_REQ_CAP 16
#define POOL_RES_CAP 64

static JobRequest s_pooled_reqs[POOL_REQ_CAP];
static volatile uint8_t s_pooled_req_used[POOL_REQ_CAP] = {0};

static std::shared_ptr<JobRequest> acquireJobRequest()
{
  for (int i = 0; i < POOL_REQ_CAP; ++i)
  {
    if (!s_pooled_req_used[i])
    {
      s_pooled_req_used[i] = 1;
      return std::shared_ptr<JobRequest>(&s_pooled_reqs[i], [i](JobRequest *) {
        s_pooled_req_used[i] = 0;
      });
    }
  }
  return std::make_shared<JobRequest>();
}

static JobResult s_pooled_results[POOL_RES_CAP];
static volatile uint8_t s_pooled_res_used[POOL_RES_CAP] = {0};

static std::shared_ptr<JobResult> acquireJobResult()
{
  for (int i = 0; i < POOL_RES_CAP; ++i)
  {
    if (!s_pooled_res_used[i])
    {
      s_pooled_res_used[i] = 1;
      return std::shared_ptr<JobResult>(&s_pooled_results[i], [i](JobResult *) {
        s_pooled_res_used[i] = 0;
      });
    }
  }
  return std::make_shared<JobResult>();
}

std::list<std::shared_ptr<JobRequest>> s_job_request_list_sw;
#ifdef HARDWARE_SHA265
std::list<std::shared_ptr<JobRequest>> s_job_request_list_hw;
#endif
std::list<std::shared_ptr<JobResult>> s_job_result_list;
static volatile uint8_t s_working_current_job_id = 0xFF;
#if defined(PIPELINED_ASM_MINING) && defined(CONFIG_IDF_TARGET_ESP32)
// What the pipelined HW loop works from (layout is known to the asm, see
// src/pipelined_hw_sha_classic_v2.S).
struct pl_ctx_t
{
  uint32_t hdr[20];       // header words as the SHA engine wants them (byte-swapped)
  uint32_t nonce;         // in: first nonce to try (swapped space); out: next one
  uint32_t budget;        // in: nonces to try; out: nonces not tried
  volatile uint32_t run;  // the asm reads it every nonce, so clearing it from the
                          // stratum task stops a stale-job chunk at once
  uint32_t pad;           // 0x80000000
  uint32_t chk;           // bench builds only
};
static pl_ctx_t s_pl_ctx;
#endif

static void JobPush(std::list<std::shared_ptr<JobRequest>> &job_list,  uint32_t id, uint32_t nonce_start, uint32_t nonce_count, double difficulty,
                    const uint8_t* sha_buffer, const uint32_t* midstate, const uint32_t* bake)
{
  std::shared_ptr<JobRequest> job = acquireJobRequest();
  job->id = id;
  job->nonce_start = nonce_start;
  job->nonce_count = nonce_count;
  job->difficulty = difficulty;
  memcpy(job->sha_buffer, sha_buffer, sizeof(job->sha_buffer));
  memcpy(job->midstate, midstate, sizeof(job->midstate));
  memcpy(job->bake, bake, sizeof(job->bake));
  job_list.push_back(job);
}


struct Submition
{
  double diff;
  bool is32bit;
  bool isValid;
  unsigned long submitTime;
};

static void MiningJobStop(uint32_t &job_pool, std::map<uint32_t, std::shared_ptr<Submition>> & submition_map)
{
  {
    std::lock_guard<std::mutex> lock(s_job_mutex);
    s_job_result_list.clear();
    s_job_request_list_sw.clear();
    #ifdef HARDWARE_SHA265
    s_job_request_list_hw.clear();
    #endif
  }
  s_working_current_job_id = 0xFF;
  job_pool = 0xFFFFFFFF;
  submition_map.clear();
  mMonitor.NerdStatus = NM_Connecting; // back to NM_hashing with the next job
}

#ifdef RANDOM_NONCE
uint64_t s_random_state = 1;
static uint32_t RandomGet()
{
    s_random_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = s_random_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

#endif

void runStratumWorker(void *name) {

// TEST: https://bitcoin.stackexchange.com/questions/22929/full-example-data-for-scrypt-stratum-client

  Serial.println("");
  Serial.printf("\n[WORKER] Started. Running %s on core %d\n", (char *)name, xPortGetCoreID());

  #ifdef DEBUG_MEMORY
  Serial.printf("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
  #endif

  std::map<uint32_t, std::shared_ptr<Submition>> s_submition_map;

#ifdef I2C_SLAVE
  std::vector<uint8_t> i2c_slave_vector;

  //scan for i2c slaves
  if (i2c_master_start() == 0)
    i2c_slave_vector = i2c_master_scan(0x0, 0x80);
  Serial.printf("Found %d slave workers\n", i2c_slave_vector.size());
  if (!i2c_slave_vector.empty())
  {
    Serial.print("  Workers: ");
    for (size_t n = 0; n < i2c_slave_vector.size(); ++n)
      Serial.printf("0x%02X,", (uint32_t)i2c_slave_vector[n]);
    Serial.println("");
  }
#endif

  // connect to pool  
  double currentPoolDifficulty = DEFAULT_DIFFICULTY;
  uint32_t nonce_pool = 0;
  uint32_t job_pool = 0xFFFFFFFF;
  uint32_t last_job_time = millis();

  // Drops the pool and its jobs; the loop below looks up activePoolAddress()
  // and connects to it.
  auto dropPool = [&]() {
    client.stop();
    isMinerSuscribed = false;
    MiningJobStop(job_pool, s_submition_map);
    #if defined(PIPELINED_ASM_MINING) && defined(CONFIG_IDF_TARGET_ESP32)
    s_pl_ctx.run = 0; // don't finish the old job's chunk
    #endif
    currentPoolDifficulty = DEFAULT_DIFFICULTY;
    s_poolDifficulty = currentPoolDifficulty;
    serverIP = IPAddress(1, 1, 1, 1);
    s_resolvedPoolHost = "";
    poolSettingsChanged(); // monitor: pool stats URL and data
  };

  auto switchPool = [&](bool backup, const char *why) {
    s_onBackup = backup;
    g_usingFallback = backup;
    s_poolFailures = 0;
    s_backupSince = millis();
    Serial.printf("[STRATUM] %s, switching to the %s pool %s:%d\n", why, backup ? "backup" : "main",
                  activePoolAddress().c_str(), activePoolPort());
    dropPool();
  };

  // A failed attempt on the pool in use; true if it moved to the other pool.
  auto poolFailed = [&]() {
    if (s_poolFailures < 255)
      s_poolFailures++;
    if (s_poolFailures < POOL_FAILOVER_TRIES || !backupPoolSet())
      return false;
    switchPool(!s_onBackup, s_onBackup ? "Backup pool not answering" : "Pool not answering");
    return true;
  };

  while(true) {

    if(takePoolChange()){
      // New pool settings from the LAN settings page: drop the old pool and
      // its jobs, look the new address up and connect to it below.
      Serial.printf("[STRATUM] Pool settings changed, switching to %s:%d\n", activePoolAddress().c_str(), activePoolPort());
      dropPool();
    }
      
    if(WiFi.status() != WL_CONNECTED){
      // WiFi is disconnected, so reconnect now
      mMonitor.NerdStatus = NM_Connecting;
      MiningJobStop(job_pool, s_submition_map);
      WiFi.reconnect();
      vTaskDelay(5000 / portTICK_PERIOD_MS);
      continue;
    } 

    if(!checkPoolConnection()){
      MiningJobStop(job_pool, s_submition_map);
      if (poolFailed())
        continue; // try the other pool straight away
      // If server is not reachable add random delay for connection retries
      // A second at a time, so a pool fixed on the settings page is tried
      // straight away instead of after the rest of the wait.
      for (int s = 1 + rand() % 60; s > 0; s--) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        if (s_poolChangePending) break;
      }
      continue;
    }

    if(!isMinerSuscribed)
    {
      // Shares / best / blocks are kept per pool (and coin).
      selectPoolStats(activePoolAddress(), activePoolPort());
      //Stop miner current jobs
      mWorker = init_mining_subscribe();

      // STEP 1: Pool server connection (SUBSCRIBE)
      if(!tx_mining_subscribe(client, mWorker)) { 
        client.stop();
        MiningJobStop(job_pool, s_submition_map);
        poolFailed();
        continue; 
      }
      
      strcpy(mWorker.wName, Settings.BtcWallet);
      strcpy(mWorker.wPass, activePoolPassword());
      // STEP 2: Pool authorize work (Block Info)
      tx_mining_auth(client, mWorker.wName, mWorker.wPass); //Don't verifies authoritzation, TODO
      //tx_mining_auth2(client, mWorker.wName, mWorker.wPass); //Don't verifies authoritzation, TODO

      // STEP 3: Suggest pool difficulty
      tx_suggest_difficulty(client, currentPoolDifficulty);

      isMinerSuscribed=true;
      uint32_t time_now = millis();
      mLastTXtoPool = time_now;
      last_job_time = time_now;
    }

    //Check if pool is down for almost 5minutes and then restart connection with pool (1min=600000ms)
    if(checkPoolInactivity(KEEPALIVE_TIME_ms, POOLINACTIVITY_TIME_ms)){
      //Restart connection
      Serial.println("  Detected more than 2 min without data form stratum server. Closing socket and reopening...");
      client.stop();
      isMinerSuscribed=false;
      MiningJobStop(job_pool, s_submition_map);
      poolFailed();
      continue; 
    }

    {
      uint32_t time_now = millis();
      if (time_now < last_job_time) //32bit wrap
        last_job_time = time_now;
      if (time_now >= last_job_time + 10*60*1000)  //10minutes without job
      {
        client.stop();
        isMinerSuscribed=false;
        MiningJobStop(job_pool, s_submition_map);
        poolFailed();
        continue;
      }
    }

    // On the backup: go back to the main pool once it answers again.
    if (s_onBackup && millis() - s_backupSince >= POOL_FAILBACK_ms)
    {
      s_backupSince = millis();
      if (mainPoolAnswers())
      {
        switchPool(false, "Main pool answers again");
        continue;
      }
      Serial.printf("[STRATUM] Main pool %s:%d still not answering, staying on the backup\n",
                    Settings.PoolAddress.c_str(), Settings.PoolPort);
    }

    uint32_t hw_midstate[8];
    uint32_t diget_mid[8];
    uint32_t bake[16];
    #if defined(CONFIG_IDF_TARGET_ESP32)
    uint8_t sha_buffer_swap[128];
    #endif

    //Read pending messages from pool
    while(client.connected() && client.available())
    {
      String line = client.readStringUntil('\n');
      //Serial.println("  Received message from pool");      
      stratum_method result = parse_mining_method(line);
      switch (result)
      {
          case MINING_NOTIFY:         if(parse_mining_notify(line, mJob))
                                      {
                                          g_lastJobMs = millis(); // a fresh puzzle from the pool
                                          clearPoolError();
                                          // The old job keeps mining while the new one is prepared below
                                          // (calculateMiningData prints a lot to the UART, which blocks for
                                          // hundreds of ms); the queues are swapped atomically just before
                                          // the new chunks are pushed instead of idling the miners up front.
                                          //Increse templates readed
                                          templates++;
                                          s_poolFailures = 0; // the pool works
                                          mMonitor.NerdStatus = NM_hashing;

                                          // Calculate live Bitcoin network difficulty from nBits
                                          if (mJob.nbits.length() > 0)
                                          {
                                            uint32_t nbitsVal = strtoul(mJob.nbits.c_str(), NULL, 16);
                                            uint32_t exponent = nbitsVal >> 24;
                                            uint32_t mantissa = nbitsVal & 0x7FFFFF;
                                            if (mantissa > 0 && exponent <= 32)
                                            {
                                              double diffNet = ((double)0xFFFF / (double)mantissa) * pow(256.0, (double)(29 - (int)exponent));
                                              if (diffNet > 0.0)
                                              {
                                                g_networkDifficulty = diffNet;
                                              }
                                            }
                                          }
                                          // Track Bitcoin Block Epoch by comparing 32-byte prev_block_hash
                                          if (mJob.prev_block_hash.length() > 0)
                                          {
                                            if (s_activePrevBlockHash.length() == 0)
                                            {
                                              s_activePrevBlockHash = mJob.prev_block_hash;
                                              g_blockStartTimeSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
                                            }
                                            else if (mJob.prev_block_hash != s_activePrevBlockHash)
                                            {
                                              uint32_t nowSec = (uint32_t)(esp_timer_get_time() / 1000000ULL);
                                              g_lastBlockDurationSec = (nowSec >= g_blockStartTimeSec) ? (nowSec - g_blockStartTimeSec) : 0;
                                              g_lastBlockHashes = g_currentBlockHashes;
                                              g_currentBlockHashes = 0;
                                              g_blockStartTimeSec = nowSec;
                                              g_blockBestDiff = 0.0;
                                              s_activePrevBlockHash = mJob.prev_block_hash;
                                              Serial.printf("[EPOCH] New Bitcoin block: %s (Prev: %llu hashes in %u s)\n",
                                                            mJob.prev_block_hash.c_str(), g_lastBlockHashes, g_lastBlockDurationSec);
                                            }
                                          }

                                          last_job_time = millis();
                                          mLastTXtoPool = last_job_time;

                                          uint32_t mh = hashes/1000000;
                                          Mhashes += mh;
                                          hashes -= mh*1000000;

                                          //Prepare data for new jobs
                                          HW_SHA_REQUEST(); // S3: the hashes below need the engine the HW miner is holding
                                          mMiner=calculateMiningData(mWorker, mJob);

                                          memset(mMiner.bytearray_blockheader+80, 0, 128-80);
                                          mMiner.bytearray_blockheader[80] = 0x80;
                                          mMiner.bytearray_blockheader[126] = 0x02;
                                          mMiner.bytearray_blockheader[127] = 0x80;

                                          nerd_mids(diget_mid, mMiner.bytearray_blockheader);
                                          nerd_sha256_bake(diget_mid, mMiner.bytearray_blockheader+64, bake);

                                          #ifdef HARDWARE_SHA265
                                          #if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)
                                            g_hwShaMutex.lock();
                                            esp_sha_acquire_hardware();
                                            sha_hal_hash_block(SHA2_256,  mMiner.bytearray_blockheader, 64/4, true);
                                            sha_hal_read_digest(SHA2_256, hw_midstate);
                                            esp_sha_release_hardware();
                                            g_hwShaMutex.unlock();
                                          #endif
                                          #endif

                                          #if defined(CONFIG_IDF_TARGET_ESP32)
                                          for (int i = 0; i < 32; ++i)
                                            ((uint32_t*)sha_buffer_swap)[i] = __builtin_bswap32(((const uint32_t*)(mMiner.bytearray_blockheader))[i]);
                                          #endif

                                          #ifdef RANDOM_NONCE
                                          nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                          #else
                                            #ifdef I2C_SLAVE
                                            if (!i2c_slave_vector.empty())
                                              nonce_pool = 0x10000000;
                                            else
                                            #endif
                                              nonce_pool = 0xDA54E700;  //nonce 0x00000000 is not possible, start from some random nonce
                                          #endif
                                          

                                          {
                                            std::lock_guard<std::mutex> lock(s_job_mutex);
                                            s_job_request_list_sw.clear();
                                            #ifdef HARDWARE_SHA265
                                            s_job_request_list_hw.clear();
                                            #endif
                                            job_pool++;
                                            {
                                              std::lock_guard<std::mutex> clock(s_cryptoAuditMutex);
                                              g_cryptoAudit.job_id = job_pool;
                                              g_cryptoAudit.pool_difficulty = currentPoolDifficulty;
                                              memcpy(g_cryptoAudit.active_midstate, diget_mid, 32);
                                              memcpy(g_cryptoAudit.active_header_tail, mMiner.bytearray_blockheader + 64, 16);
                                              memcpy(g_cryptoAudit.pool_target, mMiner.bytearray_pooltarget, 32);
                                            }
                                            s_working_current_job_id = job_pool & 0xFF; //Terminate current job in thread
                                            #if defined(PIPELINED_ASM_MINING) && defined(CONFIG_IDF_TARGET_ESP32)
                                            s_pl_ctx.run = 0;                           //...and stop the HW loop mid-chunk right now
                                            #endif
                                            for (int i = 0; i < 4; ++ i)
                                            {
                                              #if 1
                                              JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_SW;
                                              #endif
                                              #endif
                                              #ifdef HARDWARE_SHA265
                                                #if defined(CONFIG_IDF_TARGET_ESP32)
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
                                                #else
                                                  JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
                                                #endif
                                              #ifdef RANDOM_NONCE
                                              nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
                                              #else
                                              nonce_pool += NONCE_PER_JOB_HW;
                                              #endif
                                              #endif
                                            }
                                          }
                                          #ifdef I2C_SLAVE
                                          //Nonce for nonce_pool starts from 0x10000000
                                          //For i2c slave we give nonces from 0x20000000, that is 0x10000000 nonces per slave
                                          i2c_feed_slaves(i2c_slave_vector, job_pool & 0xFF, 0x20, currentPoolDifficulty, mMiner.bytearray_blockheader);
                                          #endif
                                      } else
                                      {
                                        Serial.println("Parsing error, need restart");
                                        client.stop();
                                        isMinerSuscribed=false;
                                        MiningJobStop(job_pool, s_submition_map);
                                        poolFailed();
                                      }
                                      break;
          case MINING_SET_DIFFICULTY: parse_mining_set_difficulty(line, currentPoolDifficulty);
                                      s_poolDifficulty = currentPoolDifficulty;
                                      break;
          case STRATUM_SUCCESS:       {
                                        unsigned long id = parse_extract_id(line);
                                        auto itt = s_submition_map.find(id);
                                        if (itt != s_submition_map.end())
                                        {
                                          const bool accepted = parse_extract_result(line);
                                          if (accepted)
                                          {
                                            s_sharesAccepted++;
                                            g_acceptedShares++;
                                          } else {
                                            s_sharesRejected++; // "result":false without an error
                                          }

                                          if (itt->second->submitTime > 0)
                                          {
                                            uint32_t rtt = millis() - itt->second->submitTime;
                                            g_stratumPing = (g_stratumPing == 0) ? rtt : ((g_stratumPing * 3 + rtt) / 4);
                                          }
                                          updateBestDiff(itt->second->diff);
                                          recordShare(itt->second->diff, accepted);
                                          // 32-bit shares: counted as accepted only when the pool
                                          // took them (stock counted refused ones as well).
                                          if (itt->second->is32bit && !accepted)
                                            shares32Rejected++;
                                          if (itt->second->is32bit && accepted)
                                          {
                                            shares++;
                                            // Signal only these (difficulty >= 1, the "32BITS SHARES"
                                            // counter): pool shares at the suggested difficulty arrive
                                            // more than once a second and made the buzzer beep non-stop.
                                            g_shareFoundEvents++;
                                          }
                                          if (itt->second->isValid)
                                          {
                                            Serial.println("CONGRATULATIONS! Valid block found");
                                            valids++;
                                            g_blockFoundEvents++;
                                          }
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          case STRATUM_PARSE_ERROR:   {
                                        unsigned long id = parse_extract_id(line);
                                        auto itt = s_submition_map.find(id);
                                        if (itt == s_submition_map.end())
                                          notePoolError(line); // e.g. authorize refused: no jobs will come
                                        if (itt != s_submition_map.end())
                                        {
                                          s_sharesRejected++;
                                          if (itt->second->submitTime > 0)
                                          {
                                            uint32_t rtt = millis() - itt->second->submitTime;
                                            g_stratumPing = (g_stratumPing == 0) ? rtt : ((g_stratumPing * 3 + rtt) / 4);
                                          }
                                          if (itt->second->is32bit)
                                            shares32Rejected++;
                                          recordShare(itt->second->diff, false);
                                          Serial.printf("Refuse submition %d\n", id);
                                          s_submition_map.erase(itt);
                                        }
                                      }
                                      break;
          default:                    Serial.println("  Parsed JSON: unknown"); break;

      }
    }

    std::list<std::shared_ptr<JobResult>> job_result_list;
    #ifdef I2C_SLAVE
    if (i2c_slave_vector.empty() || job_pool == 0xFFFFFFFF)
    {
      vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    } else
    {
      uint32_t time_start = millis();
      i2c_hit_slaves(i2c_slave_vector);
      vTaskDelay(5 / portTICK_PERIOD_MS);
      uint32_t nonces_done = 0;
      std::vector<uint32_t> nonce_vector = i2c_harvest_slaves(i2c_slave_vector, job_pool & 0xFF, nonces_done);
      hashes += nonces_done;
      g_currentBlockHashes += nonces_done;
      for (size_t n = 0; n < nonce_vector.size(); ++n)
      {
        std::shared_ptr<JobResult> result = std::make_shared<JobResult>();
        ((uint32_t*)(mMiner.bytearray_blockheader+64+12))[0] = nonce_vector[n];
        if (nerd_sha256d_baked(diget_mid, mMiner.bytearray_blockheader+64, bake, result->hash))
        {
          result->id = job_pool;
          result->nonce = nonce_vector[n];
          result->nonce_count = 0;
          result->difficulty = diff_from_target(result->hash);
          job_result_list.push_back(result);
        }
      }
      uint32_t time_end = millis();
      //if (nonces_done > 16384)
        //Serial.printf("Harvest slaves in %dms hashes=%d\n", time_end - time_start, nonces_done);
      if (time_end > time_start)
      {
        uint32_t elapsed = time_end - time_start;
        if (elapsed < 50)
          vTaskDelay((50 - elapsed) / portTICK_PERIOD_MS);
      } else
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
    #else
    vTaskDelay(50 / portTICK_PERIOD_MS); //Small delay
    #endif

    
    if (job_pool != 0xFFFFFFFF)
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      job_result_list.insert(job_result_list.end(), s_job_result_list.begin(), s_job_result_list.end());
      s_job_result_list.clear();

#if 1
      while (s_job_request_list_sw.size() < 4)
      {
        JobPush( s_job_request_list_sw, job_pool, nonce_pool, NONCE_PER_JOB_SW, currentPoolDifficulty, mMiner.bytearray_blockheader, diget_mid, bake);
        #ifdef RANDOM_NONCE
        nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
        #else
        nonce_pool += NONCE_PER_JOB_SW;
        #endif
      }
#endif

      #ifdef HARDWARE_SHA265
      while (s_job_request_list_hw.size() < 4)
      {
        #if defined(CONFIG_IDF_TARGET_ESP32)
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, sha_buffer_swap, hw_midstate, bake);
        #else
          JobPush( s_job_request_list_hw, job_pool, nonce_pool, NONCE_PER_JOB_HW, currentPoolDifficulty, mMiner.bytearray_blockheader, hw_midstate, bake);
        #endif
        #ifdef RANDOM_NONCE
        nonce_pool = RandomGet() & RANDOM_NONCE_MASK;
        #else
        nonce_pool += NONCE_PER_JOB_HW;
        #endif
      }
      #endif
    }

    while (!job_result_list.empty())
    {
      std::shared_ptr<JobResult> res = job_result_list.front();
      job_result_list.pop_front();

      hashes += res->nonce_count;
      g_currentBlockHashes += res->nonce_count;
      if (res->difficulty > g_blockBestDiff && res->difficulty < 2.0e30)
      {
        g_blockBestDiff = res->difficulty;
      }
      if (res->nonce != 0xFFFFFFFF)
        updateBestDiff(res->difficulty); // I2C slave results (local miners already did it)
      if (res->difficulty > currentPoolDifficulty && job_pool == res->id && res->nonce != 0xFFFFFFFF)
      {
        hashSamplePushDigest(res->hash);
        if (!client.connected())
          break;
        unsigned long sumbit_id = 0;
        tx_mining_submit(client, mWorker, mJob, res->nonce, sumbit_id);
        Serial.print("   - Current diff share: "); Serial.println(res->difficulty,12);
        Serial.print("   - Current pool diff : "); Serial.println(currentPoolDifficulty,12);
        Serial.print("   - TX SHARE: ");
        for (size_t i = 0; i < 32; i++)
            Serial.printf("%02x", res->hash[i]);
        Serial.println("");
        mLastTXtoPool = millis();

        std::shared_ptr<Submition> submition = std::make_shared<Submition>();
        submition->diff = res->difficulty;
        submition->is32bit = (res->hash[29] == 0 && res->hash[28] == 0);
        submition->submitTime = mLastTXtoPool;
        if (submition->is32bit)
        {
          submition->isValid = checkValid(res->hash, mJob.nbits.c_str()); // res->id == job_pool: mJob is this share's job
        } else
          submition->isValid = false;

        s_submition_map.insert(std::make_pair(sumbit_id, submition));
        if (s_submition_map.size() > 32)
          s_submition_map.erase(s_submition_map.begin());
      }
    }
  }
}

//////////////////THREAD CALLS///////////////////

void minerWorkerSw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerSw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint32_t wdt_counter = 0;
  #ifdef DEBUG_MINING
  volatile uint32_t *myDebugCounter = &debugHashesSw[miner_id & 1];
  #endif
  while (1)
  {
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_sw.empty())
      {
        job = s_job_request_list_sw.front();
        s_job_request_list_sw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = acquireJobResult();
      result->difficulty = job->difficulty;
      result->nonce = 0xFFFFFFFF;
      result->id = job->id;
      result->nonce_count = job->nonce_count;
      uint8_t job_in_work = job->id & 0xFF;
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        uint32_t nonce_n = job->nonce_start + n;
        ((uint32_t*)(job->sha_buffer+64+12))[0] = nonce_n;
        #ifdef DEBUG_MINING
        (*myDebugCounter)++;
        #endif
        uint32_t sw0 = 0, sw1 = 0;
        asm volatile("rsr.ccount %0" : "=a"(sw0));
        bool sw_hit = nerd_sha256d_baked(job->midstate, job->sha_buffer+64, job->bake, hash);
        asm volatile("rsr.ccount %0" : "=a"(sw1));
        uint32_t sw_cycles = sw1 - sw0;
        if (g_cycleProfile.sw_cycles_per_hash == 0)
          g_cycleProfile.sw_cycles_per_hash = sw_cycles;
        else
          g_cycleProfile.sw_cycles_per_hash = (g_cycleProfile.sw_cycles_per_hash * 15 + sw_cycles) / 16;
        g_cycleProfile.total_sw_hashes++;
        g_cryptoAudit.sw_nonce_cursor = nonce_n;

        if (sw_hit)
        {
          double diff_hash = diff_from_target(hash);
          pushCryptoAuditSample(nonce_n, diff_hash, hash);
          updateBestDiff(diff_hash);
          if (diff_hash > result->difficulty)
          {
            result->difficulty = diff_hash;
            result->nonce = nonce_n;
            memcpy(result->hash, hash, 32);
          }
        }

        if ( (uint16_t)(n & 0xFF) == 0 && s_working_current_job_id != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#ifdef HARDWARE_SHA265

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    REG_WRITE(&reg_addr_buf[0], data_words[0]);
    REG_WRITE(&reg_addr_buf[1], data_words[1]);
    REG_WRITE(&reg_addr_buf[2], data_words[2]);
#if 0
    REG_WRITE(&reg_addr_buf[3], nonce);
    //REG_WRITE(&reg_addr_buf[3], data_words[3]);    
    REG_WRITE(&reg_addr_buf[4], data_words[4]);
    REG_WRITE(&reg_addr_buf[5], data_words[5]);
    REG_WRITE(&reg_addr_buf[6], data_words[6]);
    REG_WRITE(&reg_addr_buf[7], data_words[7]);
    REG_WRITE(&reg_addr_buf[8], data_words[8]);
    REG_WRITE(&reg_addr_buf[9], data_words[9]);
    REG_WRITE(&reg_addr_buf[10], data_words[10]);
    REG_WRITE(&reg_addr_buf[11], data_words[11]);
    REG_WRITE(&reg_addr_buf[12], data_words[12]);
    REG_WRITE(&reg_addr_buf[13], data_words[13]);
    REG_WRITE(&reg_addr_buf[14], data_words[14]);
    REG_WRITE(&reg_addr_buf[15], data_words[15]);
#else
    REG_WRITE(&reg_addr_buf[3], nonce);
    REG_WRITE(&reg_addr_buf[4], 0x00000080);
    REG_WRITE(&reg_addr_buf[5], 0x00000000);
    REG_WRITE(&reg_addr_buf[6], 0x00000000);
    REG_WRITE(&reg_addr_buf[7], 0x00000000);
    REG_WRITE(&reg_addr_buf[8], 0x00000000);
    REG_WRITE(&reg_addr_buf[9], 0x00000000);
    REG_WRITE(&reg_addr_buf[10], 0x00000000);
    REG_WRITE(&reg_addr_buf[11], 0x00000000);
    REG_WRITE(&reg_addr_buf[12], 0x00000000);
    REG_WRITE(&reg_addr_buf[13], 0x00000000);
    REG_WRITE(&reg_addr_buf[14], 0x00000000);
    REG_WRITE(&reg_addr_buf[15], 0x80020000);
#endif
}

static inline void nerd_sha_ll_fill_text_block_sha256_inter()
{
  uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

  DPORT_INTERRUPT_DISABLE();
  REG_WRITE(&reg_addr_buf[0], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4));
  REG_WRITE(&reg_addr_buf[1], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4));
  REG_WRITE(&reg_addr_buf[2], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4));
  REG_WRITE(&reg_addr_buf[3], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4));
  REG_WRITE(&reg_addr_buf[4], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4));
  REG_WRITE(&reg_addr_buf[5], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4));
  REG_WRITE(&reg_addr_buf[6], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4));
  REG_WRITE(&reg_addr_buf[7], DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4));
  DPORT_INTERRUPT_RESTORE();

  REG_WRITE(&reg_addr_buf[8], 0x00000080);
  REG_WRITE(&reg_addr_buf[9], 0x00000000);
  REG_WRITE(&reg_addr_buf[10], 0x00000000);
  REG_WRITE(&reg_addr_buf[11], 0x00000000);
  REG_WRITE(&reg_addr_buf[12], 0x00000000);
  REG_WRITE(&reg_addr_buf[13], 0x00000000);
  REG_WRITE(&reg_addr_buf[14], 0x00000000);
  REG_WRITE(&reg_addr_buf[15], 0x00010000);
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}


static inline bool nerd_sha_ll_read_digest_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t last = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 7 * 4);
  #if 1
  if ( (uint16_t)(last >> 16) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  #endif

  ((uint32_t*)ptr)[7] = last;
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_H_BASE + 6 * 4);  
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline void nerd_sha_ll_write_digest(void *digest_state)
{
    uint32_t *digest_state_words = (uint32_t *)digest_state;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_H_BASE);

    REG_WRITE(&reg_addr_buf[0], digest_state_words[0]);
    REG_WRITE(&reg_addr_buf[1], digest_state_words[1]);
    REG_WRITE(&reg_addr_buf[2], digest_state_words[2]);
    REG_WRITE(&reg_addr_buf[3], digest_state_words[3]);
    REG_WRITE(&reg_addr_buf[4], digest_state_words[4]);
    REG_WRITE(&reg_addr_buf[5], digest_state_words[5]);
    REG_WRITE(&reg_addr_buf[6], digest_state_words[6]);
    REG_WRITE(&reg_addr_buf[7], digest_state_words[7]);
}

static inline void nerd_sha_hal_wait_idle()
{
    while (REG_READ(SHA_BUSY_REG))
    {}
}

#if defined(PIPELINED_S3_MINING) && defined(CONFIG_IDF_TARGET_ESP32S3)
// Overlapped ESP32-S3 mining loop (build flag PIPELINED_S3_MINING).
//
// Measured on the chip (src/sha_bench_s3.cpp): a block computes in ~150 CPU
// cycles, but every register access crosses the 80 MHz peripheral bus (~12
// cycles per store, ~24 per load), so the loop above spends most of its ~1080
// cycles per nonce on register traffic. The engine latches TEXT at the
// trigger and leaves the registers untouched, which allows two savings:
//   - the next block's TEXT is written while the current block computes;
//   - only the words that differ between the two blocks are written.
// Result: ~740 cycles per nonce (324 KH/s against 222), every hash checked.
//
// Tries `count` nonces from `nonce`. Returns how many were done; *hit is set
// when the last of them has the 16 zero bits of a candidate (the caller
// recomputes that one in software).
static uint32_t __attribute__((noinline, optimize("O2")))
s3_pl_mine(const uint32_t *mid, const uint32_t *tail, uint32_t nonce, uint32_t count, bool *hit)
{
  volatile uint32_t *const text = (volatile uint32_t *)SHA_TEXT_BASE;
  volatile uint32_t *const h = (volatile uint32_t *)SHA_H_BASE;
  volatile uint32_t *const busy = (volatile uint32_t *)SHA_BUSY_REG;
  const uint32_t m0 = mid[0], m1 = mid[1], m2 = mid[2], m3 = mid[3], m4 = mid[4], m5 = mid[5], m6 = mid[6],
                 m7 = mid[7];
  const uint32_t t0 = tail[0], t1 = tail[1], t2 = tail[2];

  // Second block of the header for the first nonce
  text[0] = t0;
  text[1] = t1;
  text[2] = t2;
  text[3] = nonce;
  text[4] = 0x00000080;
  for (int i = 5; i < 15; ++i)
    text[i] = 0;
  text[15] = 0x80020000;

  for (uint32_t i = 0; i < count; ++i)
  {
    h[0] = m0; // midstate of the header's first block
    h[1] = m1;
    h[2] = m2;
    h[3] = m3;
    h[4] = m4;
    h[5] = m5;
    h[6] = m6;
    h[7] = m7;
    REG_WRITE(SHA_CONTINUE_REG, 1); // first hash
    text[8] = 0x00000080;           // meanwhile: the fixed words of the second hash's block
    text[15] = 0x00010000;          // (9..14 are zero in both blocks)
    while (*busy)
    {
    }
    text[0] = h[0];
    text[1] = h[1];
    text[2] = h[2];
    text[3] = h[3];
    text[4] = h[4];
    text[5] = h[5];
    text[6] = h[6];
    text[7] = h[7];
    REG_WRITE(SHA_START_REG, 1); // second hash
    text[0] = t0;                // meanwhile: the header block for the next nonce
    text[1] = t1;
    text[2] = t2;
    text[3] = nonce + i + 1;
    text[4] = 0x00000080;
    text[5] = 0;
    text[6] = 0;
    text[7] = 0;
    text[8] = 0;
    text[15] = 0x80020000;
    while (*busy)
    {
    }
    if ((h[7] >> 16) == 0)
    {
      *hit = true;
      return i + 1;
    }
  }
  *hit = false;
  return count;
}

// Known-answer test: for the header hdr[i] = i * 37 + 11, these are the only
// candidates among the 131072 nonces from 0x1000 up. The loop passes if it
// reports exactly these, in order.
static bool s3_pl_selftest()
{
  static const uint32_t expected[] = {0x642c, 0x66ce, 0x731b, 0x7697, 0x1478f};
  const uint32_t first = 0x1000, last = first + 131072;
  uint8_t hdr[80];
  uint32_t mid[8];
  for (int i = 0; i < 80; ++i)
    hdr[i] = (uint8_t)(i * 37 + 11);

  g_hwShaMutex.lock();
  esp_sha_acquire_hardware();
  sha_hal_hash_block(SHA2_256, hdr, 64 / 4, true);
  sha_hal_read_digest(SHA2_256, mid);
  REG_WRITE(SHA_MODE_REG, SHA2_256);
  size_t found = 0;
  bool ok = true;
  uint32_t n = first;
  while (n != last)
  {
    bool hit = false;
    n += s3_pl_mine(mid, (const uint32_t *)(hdr + 64), n, last - n, &hit);
    if (!hit)
      break;
    if (found >= sizeof(expected) / sizeof(expected[0]) || expected[found] != n - 1)
    {
      ok = false;
      break;
    }
    found++;
  }
  esp_sha_release_hardware();
  g_hwShaMutex.unlock();
  return ok && found == sizeof(expected) / sizeof(expected[0]);
}
#endif // PIPELINED_S3_MINING

//#define VALIDATION
void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerHw Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t interResult[64];
  uint8_t hash[32];
  uint8_t digest_mid[32];
  uint8_t sha_buffer[64];
  uint32_t wdt_counter = 0;

#if defined(PIPELINED_S3_MINING) && defined(CONFIG_IDF_TARGET_ESP32S3)
  // Best of three, so one disturbed run does not cost the fast loop for the
  // whole uptime; without it the loop below is used.
  const bool s3_pl_ok = s3_pl_selftest() || s3_pl_selftest() || s3_pl_selftest();
  Serial.printf("[MINER] HW SHA loop: %s\n", s3_pl_ok ? "overlapped" : "stock (the overlapped loop failed its self-test)");
#endif

#ifdef VALIDATION
  uint8_t doubleHash[32];
  uint32_t diget_mid[8];
  uint32_t bake[16];
#endif

  while (1)
  {
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      result = acquireJobResult();
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->difficulty = job->difficulty;
      uint8_t job_in_work = job->id & 0xFF;
      memcpy(digest_mid, job->midstate, sizeof(digest_mid));
      memcpy(sha_buffer, job->sha_buffer+64, sizeof(sha_buffer));
#ifdef VALIDATION
      nerd_mids(diget_mid, job->sha_buffer);
      nerd_sha256_bake(diget_mid, job->sha_buffer+64, bake);
#endif

      g_hwShaMutex.lock();
      esp_sha_acquire_hardware();
      REG_WRITE(SHA_MODE_REG, SHA2_256);
      uint32_t nend = job->nonce_start + job->nonce_count;
#if defined(PIPELINED_S3_MINING) && defined(CONFIG_IDF_TARGET_ESP32S3)
      if (s3_pl_ok)
      {
        // Software midstate of this job, to recompute candidates with
        uint32_t sw_mid[8], sw_bake[16];
        nerd_mids(sw_mid, job->sha_buffer);
        nerd_sha256_bake(sw_mid, job->sha_buffer + 64, sw_bake);

        uint32_t n = job->nonce_start;
        uint32_t reported = 0; // nonces already handed to the stratum task
        while (n != nend)
        {
          // Up to the next multiple of 256, where the job is checked for staleness
          uint32_t want = 256 - (n & 0xFF);
          if (want > nend - n)
            want = nend - n;
          bool hit = false;
          n += s3_pl_mine((const uint32_t *)digest_mid, (const uint32_t *)sha_buffer, n, want, &hit);

          if (hit)
          {
            #ifdef DEBUG_MINING
            debugPipelinedHits++;
            #endif
            const uint32_t cand = n - 1;
            ((uint32_t *)(job->sha_buffer + 64 + 12))[0] = cand;
            uint8_t sw_hash[32];
            bool confirmed = nerd_sha256d_baked(sw_mid, job->sha_buffer + 64, sw_bake, sw_hash);
            #ifdef DEBUG_MINING
            if (confirmed)
              debugPipelinedRealHits++;
            else
              debugPipelinedHwSwMismatch++;
            #endif
            const double cand_diff = confirmed && isSha256Valid(sw_hash) ? diff_from_target(sw_hash) : 0.0;
            updateBestDiff(cand_diff);
            if (cand_diff > job->difficulty)
            {
              // A share: hand it over now, not when the chunk ends (a job
              // change in between would void it).
              #ifdef DEBUG_MINING
              debugPipelinedAccepted++;
              #endif
              std::shared_ptr<JobResult> share = acquireJobResult();
              share->id = job->id;
              share->nonce = cand;
              share->nonce_count = 0;
              share->difficulty = cand_diff;
              memcpy(share->hash, sw_hash, sizeof(sw_hash));
              std::lock_guard<std::mutex> lock(s_job_mutex);
              if (s_job_result_list.size() < 64) // only guards against a stuck stratum task
                s_job_result_list.push_back(share);
            }
          }

          const uint32_t done = n - job->nonce_start;
          if (s_working_current_job_id != job_in_work)
          {
            nend = n; // stale job: stop here
          }
          else if (n != nend && g_hwShaWanted.load() != 0)
          {
            // Somebody else needs the engine (see mining.h): hand it over
            // and stay away until they are done.
            esp_sha_release_hardware();
            g_hwShaMutex.unlock();
            while (g_hwShaWanted.load() != 0)
              vTaskDelay(1);
            g_hwShaMutex.lock();
            esp_sha_acquire_hardware();
            REG_WRITE(SHA_MODE_REG, SHA2_256);
            continue;
          }
          else if (n != nend && done - reported >= 32768)
          {
            // Count the work in small steps: a chunk takes most of a second
            // here, and counting it only at its end makes the displayed
            // hashrate jump between seconds with one chunk and with two.
            std::shared_ptr<JobResult> progress = acquireJobResult();
            progress->id = job->id;
            progress->nonce = 0xFFFFFFFF;
            progress->nonce_count = done - reported;
            progress->difficulty = job->difficulty;
            reported = done;
            std::lock_guard<std::mutex> lock(s_job_mutex);
            if (s_job_result_list.size() < 64)
              s_job_result_list.push_back(progress);
          }
        }
        result->nonce_count = (n - job->nonce_start) - reported;
        #ifdef DEBUG_MINING
        debugHashesHw += n - job->nonce_start;
        #endif
      }
      else
#endif
      for (uint32_t n = job->nonce_start; n < nend; ++n)
      {
        #ifdef DEBUG_MINING
        debugHashesHw++;
        #endif
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_write_digest(digest_mid);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer, n);
        //sha_ll_continue_block(SHA2_256);
        REG_WRITE(SHA_CONTINUE_REG, 1);
        
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_inter();
        //sha_ll_start_block(SHA2_256);
        REG_WRITE(SHA_START_REG, 1);
        sha_ll_load(SHA2_256);
        nerd_sha_hal_wait_idle();
        if (nerd_sha_ll_read_digest_if(hash))
        {
          //Serial.printf("Hw 16bit Share, nonce=0x%X\n", n);
#ifdef VALIDATION
          //Validation
          ((uint32_t*)(job->sha_buffer+64+12))[0] = n;
          nerd_sha256d_baked(diget_mid, job->sha_buffer+64, bake, doubleHash);
          for (int i = 0; i < 32; ++i)
          {
            if (hash[i] != doubleHash[i])
            {
              Serial.println("***HW sha256 esp32s3 bug detected***");
              break;
            }
          }
#endif
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (isSha256Valid(hash))
            updateBestDiff(diff_hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = n;
              memcpy(result->hash, hash, sizeof(hash));
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id != job_in_work)
        {
          result->nonce_count = n-job->nonce_start+1;
          break;
        }
      }
      esp_sha_release_hardware();
      g_hwShaMutex.unlock();
      #if defined(PIPELINED_S3_MINING) && defined(CONFIG_IDF_TARGET_ESP32S3)
      // This task has its core to itself and would take the engine again
      // within microseconds. Leave a real gap once per chunk for users of
      // the engine that do not announce themselves (WiFi key handshakes go
      // through mbedTLS too).
      if (s3_pl_ok)
        vTaskDelay(1);
      #endif
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    wdt_counter++;
    if (wdt_counter >= 8)
    {
      wdt_counter = 0;
      esp_task_wdt_reset();
    }
  }
}

#endif  //#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)

#if defined(CONFIG_IDF_TARGET_ESP32)

static inline bool nerd_sha_ll_read_digest_swap_if(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  uint32_t fin = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  if ( (uint32_t)(fin & 0xFFFF) != 0)
  {
    DPORT_INTERRUPT_RESTORE();
    return false;
  }
  ((uint32_t*)ptr)[7] = __builtin_bswap32(fin);
  ((uint32_t*)ptr)[0] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4));
  ((uint32_t*)ptr)[1] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4));
  ((uint32_t*)ptr)[2] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4));
  ((uint32_t*)ptr)[3] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4));
  ((uint32_t*)ptr)[4] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4));
  ((uint32_t*)ptr)[5] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4));
  ((uint32_t*)ptr)[6] = __builtin_bswap32(DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4));
  DPORT_INTERRUPT_RESTORE();
  return true;
}

static inline void nerd_sha_ll_read_digest(void* ptr)
{
  DPORT_INTERRUPT_DISABLE();
  ((uint32_t*)ptr)[0] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 0 * 4);
  ((uint32_t*)ptr)[1] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 1 * 4);
  ((uint32_t*)ptr)[2] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 2 * 4);
  ((uint32_t*)ptr)[3] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 3 * 4);
  ((uint32_t*)ptr)[4] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 4 * 4);
  ((uint32_t*)ptr)[5] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 5 * 4);
  ((uint32_t*)ptr)[6] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 6 * 4);
  ((uint32_t*)ptr)[7] = DPORT_SEQUENCE_REG_READ(SHA_TEXT_BASE + 7 * 4);
  DPORT_INTERRUPT_RESTORE();
}

static inline void nerd_sha_hal_wait_idle()
{
    while (DPORT_REG_READ(SHA_256_BUSY_REG))
    {}
}

static inline void nerd_sha_ll_fill_text_block_sha256(const void *input_text)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
}

static inline void nerd_sha_ll_fill_text_block_sha256_upper(const void *input_text, uint32_t nonce)
{
    uint32_t *data_words = (uint32_t *)input_text;
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = __builtin_bswap32(nonce);
#if 1
    reg_addr_buf[4]  = 0x80000000;
    reg_addr_buf[5]  = 0x00000000;
    reg_addr_buf[6]  = 0x00000000;
    reg_addr_buf[7]  = 0x00000000;
    reg_addr_buf[8]  = 0x00000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000280;
#else
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
    reg_addr_buf[8]  = data_words[8];
    reg_addr_buf[9]  = data_words[9];
    reg_addr_buf[10] = data_words[10];
    reg_addr_buf[11] = data_words[11];
    reg_addr_buf[12] = data_words[12];
    reg_addr_buf[13] = data_words[13];
    reg_addr_buf[14] = data_words[14];
    reg_addr_buf[15] = data_words[15];
#endif
}

static inline void nerd_sha_ll_fill_text_block_sha256_double()
{
    uint32_t *reg_addr_buf = (uint32_t *)(SHA_TEXT_BASE);

#if 0
    //No change
    reg_addr_buf[0]  = data_words[0];
    reg_addr_buf[1]  = data_words[1];
    reg_addr_buf[2]  = data_words[2];
    reg_addr_buf[3]  = data_words[3];
    reg_addr_buf[4]  = data_words[4];
    reg_addr_buf[5]  = data_words[5];
    reg_addr_buf[6]  = data_words[6];
    reg_addr_buf[7]  = data_words[7];
#endif
    reg_addr_buf[8]  = 0x80000000;
    reg_addr_buf[9]  = 0x00000000;
    reg_addr_buf[10] = 0x00000000;
    reg_addr_buf[11] = 0x00000000;
    reg_addr_buf[12] = 0x00000000;
    reg_addr_buf[13] = 0x00000000;
    reg_addr_buf[14] = 0x00000000;
    reg_addr_buf[15] = 0x00000100;
}

#ifdef PIPELINED_ASM_MINING
// Pipelined HW loops: they overlap the CPU's register traffic with the SHA
// engine's busy time instead of idle-spinning through it. All share one
// calling convention -- 1 = candidate at ctx->nonce - 1, 0 = budget spent or
// ctx->run cleared -- so the worker can swap one for another.
typedef uint32_t (*pl_mine_fn)(volatile uint32_t *sha_base, pl_ctx_t *ctx);

// src/pipelined_hw_sha_classic_v2.S: counted delays, no status polling.
extern "C" uint32_t pipelined_hw_mine_classic_v2(volatile uint32_t *sha_base, pl_ctx_t *ctx);
extern "C" uint32_t pipelined_hw_mine_classic_v2_margin(volatile uint32_t *sha_base, pl_ctx_t *ctx);
extern "C" uint32_t pipelined_hw_mine_classic_v2_wide(volatile uint32_t *sha_base, pl_ctx_t *ctx);

// src/pipelined_hw_sha_classic.cpp: the original BUSY-polling loop, ~27%
// slower. Only kept as the last fallback.
extern "C" bool pipelined_hw_mine_classic(
    volatile uint32_t *sha_base,
    const uint32_t *header_swapped,
    uint32_t *nonce_swapped_inout,
    volatile uint32_t *hash_count_low,
    volatile bool *mining_flag,
    uint32_t iter_budget);
extern "C" void pipelined_hw_mine_classic_reinit(void);

static uint32_t pl_mine_legacy(volatile uint32_t *sha_base, pl_ctx_t *ctx)
{
  uint32_t done = 0;
  // It polls a bool and clears it itself when the budget is spent; ctx->run's
  // low byte serves as that bool.
  bool hit = pipelined_hw_mine_classic(sha_base, ctx->hdr, &ctx->nonce, &done,
                                       (volatile bool *)&ctx->run, ctx->budget);
  ctx->budget -= done;
  if (hit)
    pipelined_hw_mine_classic_reinit();
  else if (ctx->budget == 0)
    ctx->run = 1;
  return hit;
}

// The non-pipelined loop (same sequence as the #else branch in minerWorkerHw):
// less than half the speed, but every read of a SHA (DPORT) register goes
// through the SDK's DPORT workaround. The loops above read them raw, which
// only chip revision 3 and later tolerate -- see pl_select_loop.
static uint32_t pl_mine_plain(volatile uint32_t *sha_base, pl_ctx_t *ctx)
{
  uint8_t hash[32];
  while (ctx->run && ctx->budget > 0)
  {
    nerd_sha_ll_fill_text_block_sha256(ctx->hdr);
    sha_ll_start_block(SHA2_256);

    nerd_sha_hal_wait_idle();
    // ctx->nonce is already in the engine's byte order; the fill swaps its argument.
    nerd_sha_ll_fill_text_block_sha256_upper(ctx->hdr + 16, __builtin_bswap32(ctx->nonce));
    sha_ll_continue_block(SHA2_256);

    nerd_sha_hal_wait_idle();
    sha_ll_load(SHA2_256);

    nerd_sha_hal_wait_idle();
    nerd_sha_ll_fill_text_block_sha256_double();
    sha_ll_start_block(SHA2_256);

    nerd_sha_hal_wait_idle();
    sha_ll_load(SHA2_256);

    ctx->nonce++;
    ctx->budget--;
    if (nerd_sha_ll_read_digest_swap_if(hash))
      return 1;
  }
  return 0;
}

static const struct { pl_mine_fn fn; const char *name; } s_pl_loops[] = {
  {pipelined_hw_mine_classic_v2,        "fast"},
  {pipelined_hw_mine_classic_v2_margin, "fast, +2 margins"},
  {pipelined_hw_mine_classic_v2_wide,   "fast, wide margins"},
  {pl_mine_legacy,                      "polled"},
  {pl_mine_plain,                       "plain"},
};
#define PL_LOOP_COUNT (sizeof(s_pl_loops) / sizeof(s_pl_loops[0]))
#define PL_LOOP_PLAIN (PL_LOOP_COUNT - 1)
static unsigned s_pl_loop = 0;

// Guard against getting stuck on a slow loop. The self-test (and the runtime
// health check) can fail for a transient reason -- core 0 busy with Wi-Fi /
// TLS / a web screenshot at the wrong moment -- and that used to cost ~30% of
// the hashrate for the whole uptime: "polled" runs ~356 cycles/hash (~690
// KH/s) against ~247 (~980 KH/s) for "fast". So whenever the miner is not on
// the fast loop, re-run the selection later, backing off 1 -> 2 -> 4 ... 30 min.
static bool s_pl_fast_ok = true;      // false on chips that must not run the pipelined loops
static uint32_t s_pl_retry_at = 0;    // millis() of the next re-selection, 0 = none pending
static uint32_t s_pl_retry_gap = 60000;
static void pl_schedule_retry()
{
  if (!s_pl_fast_ok)
    return;
  s_pl_retry_at = millis() + s_pl_retry_gap;
  if (s_pl_retry_at == 0)
    s_pl_retry_at = 1;
  if (s_pl_retry_gap < 30UL * 60UL * 1000UL)
    s_pl_retry_gap *= 2;
}

const char *activeHwShaLoopName(void)
{
  if (s_pl_loop < PL_LOOP_COUNT)
    return s_pl_loops[s_pl_loop].name;
  return "unknown";
}

// Known-answer test: for the header hdr[i] = 0x9E3779B9 * (i + 1), these are
// the only candidates among the 262144 nonces from 0x1000 up. A loop passes
// if it reports exactly these, in order -- one wrong hash among the 262144
// that happens to be a candidate, or hides one, fails it.
static bool pl_selftest(pl_mine_fn fn)
{
  static const uint32_t expected[] = {0xa94c, 0xe496, 0x1f476, 0x302f0, 0x35af9, 0x3cf5c};
  const uint32_t first = 0x1000;
  static pl_ctx_t c; // not s_pl_ctx: the stratum task may clear that one's run flag at any time
  for (uint32_t i = 0; i < 19; ++i)
    c.hdr[i] = 0x9E3779B9u * (i + 1);
  c.nonce = first;
  c.budget = 262144;
  c.pad = 0x80000000u;
  c.run = 1;
  size_t found = 0;
  while (c.budget > 0 && fn((volatile uint32_t *)SHA_TEXT_BASE, &c))
  {
    if (found >= sizeof(expected) / sizeof(expected[0]) || c.nonce - 1 - first != expected[found])
      return false;
    found++;
  }
  return found == sizeof(expected) / sizeof(expected[0]) && c.budget == 0;
}

// Picks the fastest loop this chip runs correctly. The fast loops rely on
// the engine's timing as measured on ESP32-D0WD-V3; anything that behaves
// differently fails the known-answer test and drops to the next one.
//
// Chips before revision 3 are not offered the pipelined loops at all. Seen on
// an ESP32-D0WDQ6 rev 1.1 (LilyGO T-Display): the fast loop passes the
// self-test and hashes correctly, but within seconds core 0 stops (Monitor
// goes silent, the WiFi stack asserts, no reboot). That matches the silicon
// bug fixed in revision 3, where a raw DPORT read on one core corrupts APB
// reads (UART, WiFi MAC, timers) on the other, so the self-test cannot see it.
static void pl_select_loop()
{
  g_hwShaMutex.lock();
  esp_sha_lock_engine(SHA2_256);
  periph_module_reset(PERIPH_SHA_MODULE);
  unsigned n = 0;
  const unsigned chip_rev = efuse_hal_get_major_chip_version();
  if (chip_rev < 3)
  {
    Serial.printf("[MINER] chip revision %u: the pipelined HW SHA loops need revision 3\n", chip_rev);
    n = PL_LOOP_PLAIN;
    s_pl_fast_ok = false;
  }
  // Best of three: one stray candidate (a misread digest word) must not cost
  // the fast loop for the whole uptime.
  while (n + 1 < PL_LOOP_COUNT && !pl_selftest(s_pl_loops[n].fn) && !pl_selftest(s_pl_loops[n].fn) &&
         !pl_selftest(s_pl_loops[n].fn))
  {
    Serial.printf("[MINER] HW SHA loop '%s' failed its self-test\n", s_pl_loops[n].name);
    n++;
  }
  #ifdef PL_FORCE_LOOP
  n = PL_FORCE_LOOP; // A/B testing: 0 fast, 1 +2 margins, 2 wide margins, 3 polled, 4 plain
  #endif
  s_pl_loop = n;
  esp_sha_unlock_engine(SHA2_256);
  g_hwShaMutex.unlock();
  Serial.printf("[MINER] HW SHA loop: %s\n", s_pl_loops[n].name);
  if (n != 0)
    pl_schedule_retry();
}

// Every candidate is recomputed in software before it is used, which makes
// the share of candidates software does NOT confirm a running estimate of how
// many hashes the loop gets wrong. It is ~0 when healthy; if it ever isn't,
// stop trusting the loop and use the next, more conservative one.
static void pl_health(bool confirmed)
{
  static uint32_t hits = 0, unconfirmed = 0;
  hits++;
  if (!confirmed)
    unconfirmed++;
  if (hits < 256)
    return;
  if (unconfirmed >= 8 && s_pl_loop + 1 < PL_LOOP_COUNT)
  {
    s_pl_loop++;
    Serial.printf("[MINER] %u of %u HW candidates not confirmed; switching HW SHA loop to: %s\n",
                  unconfirmed, hits, s_pl_loops[s_pl_loop].name);
    pl_schedule_retry();
  }
  hits = unconfirmed = 0;
}
#else
const char *activeHwShaLoopName(void)
{
  return "default";
}
#endif

void minerWorkerHw(void * task_id)
{
  unsigned int miner_id = (uint32_t)task_id;
  Serial.printf("[MINER] %d Started minerWorkerHwEsp32D Task!\n", miner_id);

  std::shared_ptr<JobRequest> job;
  std::shared_ptr<JobResult> result;
  uint8_t hash[32];
  uint8_t sha_buffer[128];

  #ifdef PIPELINED_ASM_MINING
  // The asm searches by incrementing the byte-swapped nonce as a raw
  // integer (that's what makes the pipelining trick fast), but job->nonce_start
  // chunk allocations are spaced out in *native* nonce space (shared with the
  // SW queue). Byte-swapping doesn't preserve numeric locality, so a clean,
  // non-overlapping native-space gap between chunks can collapse to a tiny
  // (sometimes near-zero) gap in swapped-space -- chunks that look
  // non-overlapping on the native side can end up scanning almost the same
  // swapped-space range, deterministically rediscovering (and resubmitting)
  // the same nonce. Fix: track our own cursor that increments purely in
  // swapped-space, persisting across chunk pops for as long as it's still
  // the same stratum job, only reseeding (from job->nonce_start) when the
  // job actually changes. This never touches the shared native-space
  // allocator, so it can't affect the SW worker or the non-pipelined HW path.
  uint32_t pipelined_swapped_cursor = 0;
  uint32_t pipelined_last_job_id = 0xFFFFFFFF;
  // SW midstate/bake precompute (and the native-order header it's derived
  // from) only depend on job->sha_buffer, which is invariant across every
  // chunk pop of the same stratum job. Recomputing them on every chunk
  // (previously: every 16384 nonces) was a redundant full SHA256 transform
  // paid ~16x more often than necessary; cache and only redo on job change,
  // keyed off the same pipelined_last_job_id used for the cursor above.
  uint8_t pipelined_native_header_cache[80];
  uint32_t pipelined_midstate_cache[8];
  uint32_t pipelined_bake_cache[16];

  pl_select_loop();
  #endif

  #ifdef DEBUG_MINING
  int64_t debugChunkEndUs = 0;
  #endif

  while (1)
  {
    #ifdef PIPELINED_ASM_MINING
    if (s_pl_loop != 0 && s_pl_retry_at != 0 && (int32_t)(millis() - s_pl_retry_at) >= 0)
    {
      s_pl_retry_at = 0;
      Serial.printf("[MINER] on the '%s' HW SHA loop; re-testing the faster ones\n", s_pl_loops[s_pl_loop].name);
      pl_select_loop();
    }
    #endif
    {
      std::lock_guard<std::mutex> lock(s_job_mutex);
      if (result)
      {
        if (s_job_result_list.size() < 16)
          s_job_result_list.push_back(result);
        result.reset();
      }
      if (!s_job_request_list_hw.empty())
      {
        job = s_job_request_list_hw.front();
        s_job_request_list_hw.pop_front();
      } else
        job.reset();
    }
    if (job)
    {
      #ifdef DEBUG_MINING
      // Gap since the PREVIOUS chunk released g_hwShaMutex, i.e. everything
      // outside the hot loop: job-queue pop above, and (below) shared_ptr
      // alloc + memcpy + acquiring g_hwShaMutex/esp_sha_lock_engine again.
      // Skip the very first chunk (debugChunkEndUs still 0 -- no prior
      // chunk to measure a gap from).
      if (debugChunkEndUs != 0)
      {
        int64_t gap = esp_timer_get_time() - debugChunkEndUs;
        if (gap > 0 && gap < 10000000) // sanity bound only; the HW queue is kept 4 chunks deep so it shouldn't run dry
        {
          debugChunkGapUs += (uint32_t)gap;
          debugChunkCount++;
        }
      }
      #endif // DEBUG_MINING
      result = acquireJobResult();
      result->id = job->id;
      result->nonce = 0xFFFFFFFF;
      result->nonce_count = job->nonce_count;
      result->difficulty = job->difficulty;
      uint8_t job_in_work = job->id & 0xFF;
      memcpy(sha_buffer, job->sha_buffer, 80);

      g_hwShaMutex.lock();
      esp_sha_lock_engine(SHA2_256);
#ifdef PIPELINED_ASM_MINING
      {
        if (job->id != pipelined_last_job_id)
        {
          // New stratum job: reseed from this job's native nonce_start, and
          // recompute the SW midstate/bake cache used to recheck candidates
          // (job->midstate is HW-format and never computed for classic
          // ESP32). Any later chunk pop that's still the same job continues
          // from pipelined_swapped_cursor and reuses the cache, so chunks
          // can never overlap in the space actually being searched.
          pipelined_last_job_id = job->id;
          pipelined_swapped_cursor = __builtin_bswap32(job->nonce_start);
          for (int i = 0; i < 20; ++i)
            ((uint32_t *)pipelined_native_header_cache)[i] = __builtin_bswap32(((const uint32_t *)sha_buffer)[i]);
          nerd_mids(pipelined_midstate_cache, pipelined_native_header_cache);
          nerd_sha256_bake(pipelined_midstate_cache, pipelined_native_header_cache + 64, pipelined_bake_cache);
        }

        memcpy(s_pl_ctx.hdr, sha_buffer, sizeof(s_pl_ctx.hdr));
        s_pl_ctx.nonce = pipelined_swapped_cursor;
        s_pl_ctx.budget = job->nonce_count;
        s_pl_ctx.pad = 0x80000000u;
        // Set, then look at the job id: whichever way this interleaves with
        // the stratum task switching jobs (it changes the id first and
        // clears run second), a stale chunk ends up with run == 0.
        s_pl_ctx.run = 1;
        if (s_working_current_job_id != job_in_work)
          s_pl_ctx.run = 0;

        // Whatever used the engine last (TLS, the previous chunk's
        // abandoned block) is finished; start from a clean peripheral.
        periph_module_reset(PERIPH_SHA_MODULE);

        static uint32_t s_last_chunk_end_cycle = 0;
        uint32_t chunk_start_cycle = 0;
        asm volatile("rsr.ccount %0" : "=a"(chunk_start_cycle));
        if (s_last_chunk_end_cycle != 0)
        {
          uint32_t gap = chunk_start_cycle - s_last_chunk_end_cycle;
          if (gap < 240000000)
          {
            if (g_cycleProfile.chunk_gap_cycles == 0)
              g_cycleProfile.chunk_gap_cycles = gap;
            else
              g_cycleProfile.chunk_gap_cycles = (g_cycleProfile.chunk_gap_cycles * 7 + gap) / 8;
          }
        }

        while (s_pl_ctx.run && s_pl_ctx.budget > 0)
        {
          if (!s_pl_loops[s_pl_loop].fn((volatile uint32_t *)SHA_TEXT_BASE, &s_pl_ctx))
            break;

          #ifdef DEBUG_MINING
          int64_t hitOverheadStartUs = esp_timer_get_time();
          debugPipelinedHits++;
          #endif

          // The loop only looks at 16 bits of the digest and doesn't keep
          // it; recompute the candidate (the nonce before the one it
          // stopped on) in software.
          uint32_t cand_native_pl = __builtin_bswap32(s_pl_ctx.nonce - 1);
          ((uint32_t *)(pipelined_native_header_cache + 64 + 12))[0] = cand_native_pl;
          uint8_t sw_hash[32];
          uint32_t h0 = 0, h1 = 0;
          asm volatile("rsr.ccount %0" : "=a"(h0));
          bool confirmed = nerd_sha256d_baked(pipelined_midstate_cache, pipelined_native_header_cache + 64,
                                              pipelined_bake_cache, sw_hash);
          asm volatile("rsr.ccount %0" : "=a"(h1));
          uint32_t h_overhead = h1 - h0;
          if (g_cycleProfile.hit_overhead_cycles == 0)
            g_cycleProfile.hit_overhead_cycles = h_overhead;
          else
            g_cycleProfile.hit_overhead_cycles = (g_cycleProfile.hit_overhead_cycles * 7 + h_overhead) / 8;

          pl_health(confirmed);
          #ifdef DEBUG_MINING
          if (confirmed)
            debugPipelinedRealHits++;
          else
            debugPipelinedHwSwMismatch++;
          #endif

          const double cand_diff = confirmed && isSha256Valid(sw_hash) ? diff_from_target(sw_hash) : 0.0;
          updateBestDiff(cand_diff);
          if (cand_diff > 0.0)
            pushCryptoAuditSample(cand_native_pl, cand_diff, sw_hash);
          if (cand_diff > job->difficulty)
          {
            // A share. Hand it over now rather than with the chunk's
            // result: at low pool difficulty a 256K-nonce chunk regularly
            // holds two, and it should not wait ~0.3 s for the chunk to
            // end (a job change in that time would void it).
            #ifdef DEBUG_MINING
            debugPipelinedAccepted++;
            #endif
            std::shared_ptr<JobResult> share = acquireJobResult();
            share->id = job->id;
            share->nonce = cand_native_pl;
            share->nonce_count = 0;
            share->difficulty = cand_diff;
            memcpy(share->hash, sw_hash, sizeof(sw_hash));
            std::lock_guard<std::mutex> lock(s_job_mutex);
            if (s_job_result_list.size() < 64) // only guards against a stuck stratum task
              s_job_result_list.push_back(share);
          }

          #ifdef DEBUG_MINING
          debugHitOverheadUs += (uint32_t)(esp_timer_get_time() - hitOverheadStartUs);
          debugHitOverheadCount++;
          #endif
        }

        uint32_t chunk_end_cycle = 0;
        asm volatile("rsr.ccount %0" : "=a"(chunk_end_cycle));
        s_last_chunk_end_cycle = chunk_end_cycle;

        uint32_t nonces_done = job->nonce_count - s_pl_ctx.budget;
        if (nonces_done > 0)
        {
          uint32_t total_cycles = chunk_end_cycle - chunk_start_cycle;
          uint32_t cph = total_cycles / nonces_done;
          if (g_cycleProfile.hw_cycles_per_hash == 0)
            g_cycleProfile.hw_cycles_per_hash = cph;
          else
            g_cycleProfile.hw_cycles_per_hash = (g_cycleProfile.hw_cycles_per_hash * 7 + cph) / 8;
          int jitter = (int)cph - (int)g_cycleProfile.hw_cycles_per_hash;
          if (jitter < 0) jitter = -jitter;
          g_cycleProfile.hw_cycle_jitter = (g_cycleProfile.hw_cycle_jitter * 7 + (uint32_t)jitter) / 8;
          g_cycleProfile.total_hw_hashes += nonces_done;
        }

        // Persist where the search got to so the NEXT chunk pop of this
        // same stratum job continues from here instead of re-deriving a
        // (possibly overlapping) start point from that chunk's own
        // native-space nonce_start.
        pipelined_swapped_cursor = s_pl_ctx.nonce;
        g_cryptoAudit.hw_nonce_cursor = pipelined_swapped_cursor;
        result->nonce_count = job->nonce_count - s_pl_ctx.budget;
        #ifdef DEBUG_MINING
        debugHashesHw += result->nonce_count;
        #endif
      }

#else
      for (uint32_t n = 0; n < job->nonce_count; ++n)
      {
        #ifdef DEBUG_MINING
        debugHashesHw++;
        #endif
        //((uint32_t*)(sha_buffer+64+12))[0] = __builtin_bswap32(job->nonce_start+n);

        //sha_hal_hash_block(SHA2_256, s_test_buffer, 64/4, true);
        //nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256(sha_buffer);
        sha_ll_start_block(SHA2_256);

        //sha_hal_hash_block(SHA2_256, s_test_buffer+64, 64/4, false);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_upper(sha_buffer+64, job->nonce_start+n);
        sha_ll_continue_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);

        //sha_hal_hash_block(SHA2_256, interResult, 64/4, true);
        nerd_sha_hal_wait_idle();
        nerd_sha_ll_fill_text_block_sha256_double();
        sha_ll_start_block(SHA2_256);

        nerd_sha_hal_wait_idle();
        sha_ll_load(SHA2_256);
        if (nerd_sha_ll_read_digest_swap_if(hash))
        {
          //~5 per second
          double diff_hash = diff_from_target(hash);
          if (isSha256Valid(hash))
            updateBestDiff(diff_hash);
          if (diff_hash > result->difficulty)
          {
            if (isSha256Valid(hash))
            {
              result->difficulty = diff_hash;
              result->nonce = job->nonce_start+n;
              memcpy(result->hash, hash, sizeof(hash));
            }
          }
        }
        if (
             (uint8_t)(n & 0xFF) == 0 &&
             s_working_current_job_id != job_in_work)
        {
          result->nonce_count = n+1;
          break;
        }
      }
#endif // PIPELINED_ASM_MINING
      esp_sha_unlock_engine(SHA2_256);
      g_hwShaMutex.unlock();
      #ifdef DEBUG_MINING
      debugChunkEndUs = esp_timer_get_time();
      #endif
    } else
      vTaskDelay(2 / portTICK_PERIOD_MS);

    esp_task_wdt_reset();
  }
}

#endif  //CONFIG_IDF_TARGET_ESP32

#endif  //HARDWARE_SHA265


#define DELAY 100
#define REDRAW_EVERY 10

// ---------------------------------------------------------------------------
// Saved mining stats: two slots, "statA" / "statB", written in turn.
//
// Each save is one blob (a single NVS item) with a sequence number and its
// own CRC; at boot the newest slot with a good CRC wins. If the power goes
// in the middle of a save, only the slot being written can be damaged: the
// other one still holds the previous save (at most STATS_SAVE_MAX_s old).
// The stock format wrote 7 separate keys; a save cut half-way left a CRC
// mismatch and every counter was reset to zero at the next boot.
// Stats in the old format are read once (migration) and then saved as slots.
// ---------------------------------------------------------------------------
#define STATS_MAGIC_V1 0x53524D4EUL // "NMRS": first slot format, no rejected count
#define STATS_MAGIC    0x32524D4EUL // "NMR2": + 32-bit shares the pool refused

struct StatsRecordV1 { // read only (migration)
  uint32_t magic;
  uint32_t seq;
  double best_diff;
  uint32_t Mhashes;
  uint32_t shares;
  uint32_t valids;
  uint32_t templates;
  uint64_t upTime;
  uint32_t crc;
};

struct StatsRecord {
  uint32_t magic;
  uint32_t seq;
  double best_diff;
  uint32_t Mhashes;
  uint32_t shares;           // 32-bit shares accepted
  uint32_t valids;
  uint32_t templates;
  uint32_t shares32Rejected; // 32-bit shares refused
  uint64_t upTime;
  uint32_t crc; // over every field above
};

static std::mutex s_statsMutex; // saves come from the monitor task and, before a restart/power off, from others
static uint32_t s_statsSeq = 0;

static uint32_t statsRecordCrc(const StatsRecord &r)
{
  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &r, offsetof(StatsRecord, crc));
  return crc32_finish(crc);
}

static uint32_t statsRecordV1Crc(const StatsRecordV1 &r)
{
  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &r, offsetof(StatsRecordV1, crc));
  return crc32_finish(crc);
}

// Reads a slot in either format; a v1 record comes back as v2 (0 refused).
// missing: the key doesn't exist yet (only one save so far), as opposed to a
// slot that is there but unreadable.
static bool readStatsSlot(const char *key, StatsRecord &r, bool &missing)
{
  union { StatsRecord v2; StatsRecordV1 v1; } buf;
  size_t size = sizeof(buf);
  const esp_err_t err = nvs_get_blob(stat_handle, key, &buf, &size);
  missing = (err == ESP_ERR_NVS_NOT_FOUND);
  if (err != ESP_OK)
    return false;
  if (size == sizeof(StatsRecord) && buf.v2.magic == STATS_MAGIC && buf.v2.crc == statsRecordCrc(buf.v2)) {
    r = buf.v2;
    return true;
  }
  if (size == sizeof(StatsRecordV1) && buf.v1.magic == STATS_MAGIC_V1 && buf.v1.crc == statsRecordV1Crc(buf.v1)) {
    const StatsRecordV1 old = buf.v1;
    memset(&r, 0, sizeof(r));
    r.magic = STATS_MAGIC;
    r.seq = old.seq;
    r.best_diff = old.best_diff;
    r.Mhashes = old.Mhashes;
    r.shares = old.shares;
    r.valids = old.valids;
    r.templates = old.templates;
    r.shares32Rejected = 0;
    r.upTime = old.upTime;
    return true;
  }
  return false;
}

// Stock format (separate keys + "crc32"): only used when no slot exists yet.
static bool readLegacyStats()
{
  double nv_best = 0.0;
  uint32_t nv_mhashes = 0, nv_shares = 0, nv_valids = 0, nv_templates = 0, nv_crc = 0;
  uint64_t nv_uptime = 0;
  size_t required_size = sizeof(double);
  if (nvs_get_u32(stat_handle, "crc32", &nv_crc) != ESP_OK)
    return false; // never saved
  nvs_get_blob(stat_handle, "best_diff", &nv_best, &required_size);
  nvs_get_u32(stat_handle, "Mhashes", &nv_mhashes);
  nvs_get_u32(stat_handle, "shares", &nv_shares);
  nvs_get_u32(stat_handle, "valids", &nv_valids);
  nvs_get_u32(stat_handle, "templates", &nv_templates);
  nvs_get_u64(stat_handle, "upTime", &nv_uptime);

  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &nv_best, sizeof(nv_best));
  crc = crc32_add(crc, &nv_mhashes, sizeof(nv_mhashes));
  crc = crc32_add(crc, &nv_shares, sizeof(nv_shares));
  crc = crc32_add(crc, &nv_valids, sizeof(nv_valids));
  crc = crc32_add(crc, &nv_templates, sizeof(nv_templates));
  crc = crc32_add(crc, &nv_uptime, sizeof(nv_uptime));
  crc = crc32_finish(crc);
  if (crc != nv_crc)
    return false;

  best_diff = nv_best;
  Mhashes = nv_mhashes;
  shares = nv_shares;
  valids = nv_valids;
  templates = nv_templates;
  upTime = nv_uptime;
  return true;
}

void restoreStat() {
  if(!Settings.saveStats) return;
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.printf("[MONITOR] NVS init failed (%s), erasing NVS partition...\n", esp_err_to_name(ret));
    esp_err_t er = nvs_flash_erase();
    if (er != ESP_OK) {
      Serial.printf("[MONITOR] nvs_flash_erase failed: %s\n", esp_err_to_name(er));
    }
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    Serial.printf("[MONITOR] nvs_flash_init failed: %s - stats not restored\n", esp_err_to_name(ret));
    return;
  }

  ret = nvs_open("state", NVS_READWRITE, &stat_handle);
  if (ret != ESP_OK) {
    Serial.printf("[MONITOR] Stats: NVS open failed (%d), not saved this run\n", ret);
    return;
  }

  std::lock_guard<std::mutex> lock(s_statsMutex);
  StatsRecord a, b;
  bool missingA, missingB;
  const bool okA = readStatsSlot("statA", a, missingA);
  const bool okB = readStatsSlot("statB", b, missingB);
  if (okA || okB) {
    // newest valid slot (sequence compare that survives the 32-bit wrap)
    const StatsRecord &r = (okA && (!okB || (int32_t)(a.seq - b.seq) > 0)) ? a : b;
    best_diff = r.best_diff;
    Mhashes = r.Mhashes;
    shares = r.shares;
    valids = r.valids;
    templates = r.templates;
    shares32Rejected = r.shares32Rejected;
    upTime = r.upTime;
    s_statsSeq = r.seq;
    const bool otherMissing = (&r == &a) ? missingB : missingA;
    Serial.printf("[MONITOR] Stats restored from slot %s (save #%u)%s\n", &r == &a ? "A" : "B",
                  (unsigned)r.seq, (okA && okB) ? "" : otherMissing ? ", other slot empty" : ", other slot damaged");
  } else if (readLegacyStats()) {
    shares32Rejected = 0;
    s_statsSeq = 0;
    Serial.printf("[MONITOR] Stats restored from the old format, now saved in slots A/B\n");
  } else {
    best_diff = 0.0;
    Mhashes = 0;
    shares = 0;
    valids = 0;
    templates = 0;
    shares32Rejected = 0;
    upTime = 0;
    s_statsSeq = 0;
    Serial.printf("[MONITOR] No saved stats, starting from zero\n");
  }
  s_statsLoaded = true;
  loadShareHistory();
}

// ---------------------------------------------------------------------------
// Per-pool stats. Shares, best difficulty, blocks found and the recent-share
// ring belong to the pool (and so the coin) they were mined on: a BTC best of
// 31 says nothing about an altcoin pool. Each pool ("host:port") gets one NVS
// blob ("pst0".."pst7", least recently used evicted). The globals (shares,
// best_diff, valids, shares32Rejected, the share ring) always hold the ACTIVE
// pool's numbers, so the rest of the firmware reads them as before. Total
// hashes, templates and uptime stay global (StatsRecord).
// The first boot with this code adopts the old global stats for the pool in
// the settings (the one they were mined on).
// ---------------------------------------------------------------------------
#define POOLSTAT_MAGIC 0x31535450UL // "PST1"
struct PoolStatRec {
  uint32_t magic;
  uint32_t lru;   // higher = used more recently
  char key[64];   // "host:port"
  double best_diff;
  uint32_t shares;
  uint32_t valids;
  uint32_t shares32Rejected;
  uint32_t ringCount;
  ShareRec ring[SHARE_HIST_N]; // oldest..newest
  uint32_t crc;
};

static std::recursive_mutex s_poolStatMutex;
static char s_poolKey[64] = "";
static int s_poolSlot = -1;
static uint32_t s_poolLru = 0;
static PoolStatSummary s_poolSum[POOLSTAT_N]; // RAM copy of the table for /api/stats
static volatile bool s_shareDirty = false;
static uint32_t s_shareSavedMs = 0;
static bool s_shareSaveEver = false;

const char *poolCoinLabel(const char *key)
{
  if (strstr(key, "shears.co.uk"))
  {
    const char *c = strrchr(key, ':');
    int port = c ? atoi(c + 1) : 0;
    return port == 3690 ? "POL" : port == 3700 ? "EMB" : port == 3710 ? "FXTC" : "ALT";
  }
  return "BTC";
}

void poolShortName(const char *key, char *out, size_t n)
{
  char host[64];
  strncpy(host, key, sizeof(host) - 1);
  host[sizeof(host) - 1] = 0;
  char *c = strchr(host, ':');
  if (c)
    *c = 0;
  const char *h = host;
  static const char *pre[] = {"stratum.", "pool.", "solo.", "www.", "eu.", "us."};
  for (bool again = true; again;)
  {
    again = false;
    for (const char *p : pre)
      if (!strncmp(h, p, strlen(p)) && strchr(h + strlen(p), '.'))
      {
        h += strlen(p);
        again = true;
      }
  }
  if (!strncmp(h, "shears.", 7))
  {
    snprintf(out, n, "Shears");
    return;
  }
  snprintf(out, n, "%s", h);
  char *dot = strchr(out, '.');
  if (dot)
    *dot = 0;
}

static void poolKeyFor(const String &host, int port, char *out, size_t n)
{
  snprintf(out, n, "%s:%d", host.c_str(), port);
}

static uint32_t poolRecCrc(const PoolStatRec &r)
{
  uint32_t crc = crc32_reset();
  crc = crc32_add(crc, &r, offsetof(PoolStatRec, crc));
  return crc32_finish(crc);
}

static bool readPoolSlot(int i, PoolStatRec &r)
{
  char k[6];
  snprintf(k, sizeof(k), "pst%d", i);
  size_t size = sizeof(r);
  return nvs_get_blob(stat_handle, k, &r, &size) == ESP_OK && size == sizeof(r) &&
         r.magic == POOLSTAT_MAGIC && r.crc == poolRecCrc(r) && r.key[0];
}

static void sumFromRec(int i, const PoolStatRec &r)
{
  PoolStatSummary &s = s_poolSum[i];
  memset(&s, 0, sizeof(s));
  strncpy(s.key, r.key, sizeof(s.key) - 1);
  s.best = r.best_diff;
  s.shares = r.shares;
  s.valids = r.valids;
  s.used = true;
}

static void writePoolSlot(int i, PoolStatRec &r)
{
  char k[6];
  snprintf(k, sizeof(k), "pst%d", i);
  r.magic = POOLSTAT_MAGIC;
  r.crc = poolRecCrc(r);
  if (nvs_set_blob(stat_handle, k, &r, sizeof(r)) != ESP_OK || nvs_commit(stat_handle) != ESP_OK)
    Serial.printf("[MONITOR] Pool stats save failed (%s)\n", r.key);
  sumFromRec(i, r);
}

static void recFromActive(PoolStatRec &r)
{
  memset(&r, 0, sizeof(r));
  strncpy(r.key, s_poolKey, sizeof(r.key) - 1);
  r.lru = s_poolLru;
  r.best_diff = getBestDiff();
  r.shares = shares;
  r.valids = valids;
  r.shares32Rejected = shares32Rejected;
  r.ringCount = (uint32_t)getShareHistory(r.ring, SHARE_HIST_N);
}

static void activeFromRec(const PoolStatRec *r)
{
  portENTER_CRITICAL(&s_bestDiffMux);
  best_diff = r ? r->best_diff : 0.0;
  portEXIT_CRITICAL(&s_bestDiffMux);
  shares = r ? r->shares : 0;
  valids = r ? r->valids : 0;
  shares32Rejected = r ? r->shares32Rejected : 0;
  g_blockBestDiff = 0.0;
  uint32_t n = r ? (r->ringCount <= SHARE_HIST_N ? r->ringCount : SHARE_HIST_N) : 0;
  portENTER_CRITICAL(&s_shareMux);
  for (uint32_t i = 0; i < n; i++)
    s_shareHist[i] = r->ring[i];
  s_shareCount = (uint8_t)n;
  s_shareHead = (uint8_t)(n % SHARE_HIST_N);
  s_shareSeq++;
  portEXIT_CRITICAL(&s_shareMux);
}

static bool poolStatsUsable() { return Settings.saveStats && s_statsLoaded; }

// Writes the active pool's numbers to its slot.
static void savePoolStats(void)
{
  std::lock_guard<std::recursive_mutex> lock(s_poolStatMutex);
  if (!poolStatsUsable() || s_poolSlot < 0)
    return;
  std::unique_ptr<PoolStatRec> r(new PoolStatRec());
  recFromActive(*r);
  s_shareDirty = false;
  s_shareSavedMs = millis();
  s_shareSaveEver = true;
  writePoolSlot(s_poolSlot, *r);
}

// Makes host:port the active pool: saves the old one's numbers, loads the
// new one's (or starts it at zero). adoptGlobals: no table yet (first boot
// with per-pool stats) -- the restored global numbers become this pool's.
static void selectPoolStatsKey(const char *key, bool adoptGlobals)
{
  std::lock_guard<std::recursive_mutex> lock(s_poolStatMutex);
  if (!strcmp(key, s_poolKey))
    return;
  if (!Settings.saveStats)
  { // nothing persisted: just don't mix pools
    if (s_poolKey[0])
      activeFromRec(nullptr);
    strncpy(s_poolKey, key, sizeof(s_poolKey) - 1);
    return;
  }
  if (!s_statsLoaded)
    return;
  savePoolStats(); // the pool we are leaving
  std::unique_ptr<PoolStatRec> r(new PoolStatRec());
  int found = -1, freeSlot = -1, oldest = -1, nValid = 0;
  uint32_t maxLru = s_poolLru, minLru = UINT32_MAX;
  for (int i = 0; i < POOLSTAT_N; i++)
  {
    if (!readPoolSlot(i, *r))
    {
      s_poolSum[i].used = false;
      if (freeSlot < 0)
        freeSlot = i;
      continue;
    }
    nValid++;
    sumFromRec(i, *r);
    if (r->lru > maxLru)
      maxLru = r->lru;
    if (!strcmp(r->key, key))
      found = i;
    else if (r->lru < minLru)
    {
      minLru = r->lru;
      oldest = i;
    }
  }
  int slot = found >= 0 ? found : (freeSlot >= 0 ? freeSlot : oldest);
  if (found >= 0)
  {
    readPoolSlot(found, *r);
    activeFromRec(r.get());
    Serial.printf("[MONITOR] Pool stats for %s restored (%u shares, best %.3f)\n", key, (unsigned)r->shares, r->best_diff);
  }
  else if (adoptGlobals && nValid == 0)
    Serial.printf("[MONITOR] Pool stats: saved totals assigned to %s\n", key);
  else
  {
    activeFromRec(nullptr);
    Serial.printf("[MONITOR] Pool stats for %s start at zero (slot %d)\n", key, slot);
  }
  strncpy(s_poolKey, key, sizeof(s_poolKey) - 1);
  s_poolKey[sizeof(s_poolKey) - 1] = 0;
  s_poolSlot = slot;
  s_poolLru = maxLru + 1;
  savePoolStats(); // claim the slot now (and bump its LRU)
}

void selectPoolStats(const String &host, int port)
{
  char key[64];
  poolKeyFor(host, port, key, sizeof(key));
  selectPoolStatsKey(key, false);
}

void getActivePool(char *key, size_t keyLen, const char **coin)
{
  std::lock_guard<std::recursive_mutex> lock(s_poolStatMutex);
  if (s_poolKey[0])
    strncpy(key, s_poolKey, keyLen - 1);
  else
    poolKeyFor(activePoolAddress(), activePoolPort(), key, keyLen);
  key[keyLen - 1] = 0;
  if (coin)
    *coin = poolCoinLabel(key);
}

int getPoolStatsTable(PoolStatSummary *out, int max)
{
  std::lock_guard<std::recursive_mutex> lock(s_poolStatMutex);
  int n = 0;
  for (int i = 0; i < POOLSTAT_N && n < max; i++)
  {
    if (i == s_poolSlot)
    { // live numbers for the active pool
      PoolStatSummary s;
      memset(&s, 0, sizeof(s));
      strncpy(s.key, s_poolKey, sizeof(s.key) - 1);
      s.best = getBestDiff();
      s.shares = shares;
      s.valids = valids;
      s.used = true;
      s.active = true;
      out[n++] = s;
    }
    else if (s_poolSum[i].used)
      out[n++] = s_poolSum[i];
  }
  return n;
}

// A share landed: save soon (at most once a minute -- a pool with a tiny
// difficulty can send several a second); saveStat() flushes the rest.
static void saveShareHistory(void)
{
  s_shareDirty = true;
  if (!s_shareSaveEver || millis() - s_shareSavedMs >= 60000UL)
    savePoolStats();
}

// Boot: pick the pool in the settings, adopting the old global totals if
// there is no per-pool table yet.
static void loadShareHistory(void)
{
  char key[64];
  poolKeyFor(activePoolAddress(), activePoolPort(), key, sizeof(key));
  selectPoolStatsKey(key, true);
}

void saveStat() {
  if(!Settings.saveStats) return;
  if(!s_statsLoaded) return; // not read back yet: would overwrite them with zeros
  std::lock_guard<std::mutex> lock(s_statsMutex);

  StatsRecord r;
  memset(&r, 0, sizeof(r));
  r.magic = STATS_MAGIC;
  r.seq = s_statsSeq + 1;
  r.best_diff = getBestDiff();
  r.Mhashes = Mhashes;
  r.shares = shares;
  r.valids = valids;
  r.templates = templates;
  r.shares32Rejected = shares32Rejected;
  r.upTime = upTime;
  r.crc = statsRecordCrc(r);

  const char *slot = (r.seq & 1) ? "statA" : "statB";
  if (nvs_set_blob(stat_handle, slot, &r, sizeof(r)) == ESP_OK && nvs_commit(stat_handle) == ESP_OK) {
    s_statsSeq = r.seq; // the next save goes to the other slot
    Serial.printf("[MONITOR] Stats saved (slot %s, #%u)\n", slot + 4, (unsigned)r.seq);
    savePoolStats();
  } else {
    Serial.printf("[MONITOR] Stats save failed (slot %s)\n", slot + 4);
  }
}

void resetStat() {
    Serial.printf("[MONITOR] Resetting NVS stats\n");
    templates = hashes = Mhashes = totalKHashes = elapsedKHs = upTime = shares = valids = 0;
    shares32Rejected = 0;
    best_diff = 0.0;
    clearShareHistory();
    g_currentBlockHashes = g_lastBlockHashes = g_lastBlockDurationSec = 0;
    g_blockBestDiff = 0.0;
    if (stat_handle) s_statsLoaded = true; // zeros are what we want saved here
    saveStat();
}

void resetSessionStats() {
    Serial.printf("[MONITOR] Resetting session stats (shares & best diff)\n");
    shares = 0;
    valids = 0;
    best_diff = 0.0;
    g_currentBlockHashes = 0;
    g_blockBestDiff = 0.0;
    clearShareHistory();
}

// Plain portable SHA-256 (no hardware engine, no early exit) for the
// once-a-second display sample in runMonitor(). Not on any mining path.
static const uint32_t kSwShaK[64] = {
  0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
  0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
  0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
  0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
  0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
  0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
  0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
  0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static inline uint32_t swRotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void swSha256Block(uint32_t st[8], const uint8_t blk[64])
{
  uint32_t w[64];
  for (int i = 0; i < 16; i++)
    w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) | ((uint32_t)blk[i * 4 + 2] << 8) | blk[i * 4 + 3];
  for (int i = 16; i < 64; i++)
  {
    uint32_t s0 = swRotr(w[i - 15], 7) ^ swRotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = swRotr(w[i - 2], 17) ^ swRotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4], f = st[5], g = st[6], h = st[7];
  for (int i = 0; i < 64; i++)
  {
    uint32_t t1 = h + (swRotr(e, 6) ^ swRotr(e, 11) ^ swRotr(e, 25)) + ((e & f) ^ (~e & g)) + kSwShaK[i] + w[i];
    uint32_t t2 = (swRotr(a, 2) ^ swRotr(a, 13) ^ swRotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

// SHA-256 of a message of len < 120 bytes (fits in two blocks).
static void swSha256(const uint8_t *msg, size_t len, uint8_t out[32])
{
  uint32_t st[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  uint8_t buf[128] = {0};
  memcpy(buf, msg, len);
  buf[len] = 0x80;
  size_t total = (len + 9 <= 64) ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++)
    buf[total - 1 - i] = (uint8_t)(bits >> (8 * i));
  for (size_t off = 0; off < total; off += 64)
    swSha256Block(st, buf + off);
  for (int i = 0; i < 8; i++)
  {
    out[i * 4] = (uint8_t)(st[i] >> 24); out[i * 4 + 1] = (uint8_t)(st[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(st[i] >> 8); out[i * 4 + 3] = (uint8_t)st[i];
  }
}

static void swSha256d80(const uint8_t hdr[80], uint8_t out[32])
{
  uint8_t first[32];
  swSha256(hdr, 80, first);
  swSha256(first, 32, out);
}

void runMonitor(void *name)
{

  Serial.println("[MONITOR] started");
  restoreStat();

  unsigned long mLastCheck = 0;

  resetToFirstScreen();

  unsigned long frame = 0;

  uint32_t seconds_elapsed = 0;

  totalKHashes = (Mhashes * 1000) + hashes / 1000;
  uint32_t last_update_millis = millis();
  uint32_t uptime_frac = 0;
  s_bootKh = totalKHashes;
  s_bootMs = last_update_millis;
  statusRateSample(s_bootKh, s_bootMs);

  while (1)
  {
    uint32_t now_millis = millis();
    if (now_millis < last_update_millis)
      now_millis = last_update_millis;
    
    uint32_t mElapsed = now_millis - mLastCheck;
    if (mElapsed >= 1000)
    { 
      mLastCheck = now_millis;
      last_update_millis = now_millis;

      #ifdef DEBUG_MINING
      {
        static uint32_t lastHw = 0, lastSw0 = 0, lastSw1 = 0;
        uint32_t curHw = debugHashesHw, curSw0 = debugHashesSw[0], curSw1 = debugHashesSw[1];
        Serial.printf("[HASHRATE] HW=%u/s SW0=%u/s SW1=%u/s (total=%u/s)\n",
                      curHw - lastHw, curSw0 - lastSw0, curSw1 - lastSw1,
                      (curHw - lastHw) + (curSw0 - lastSw0) + (curSw1 - lastSw1));
        lastHw = curHw; lastSw0 = curSw0; lastSw1 = curSw1;
        #if defined(PIPELINED_S3_MINING) && !defined(PIPELINED_ASM_MINING)
        Serial.printf("[PLDBG] hits=%u real=%u mismatch=%u accepted=%u\n",
                      debugPipelinedHits, debugPipelinedRealHits, debugPipelinedHwSwMismatch, debugPipelinedAccepted);
        #endif
        #ifdef PIPELINED_ASM_MINING
        Serial.printf("[PLDBG] hits=%u real=%u mismatch=%u accepted=%u\n",
                      debugPipelinedHits, debugPipelinedRealHits, debugPipelinedHwSwMismatch, debugPipelinedAccepted);
        {
          static uint32_t lastGapUs = 0, lastGapCount = 0;
          uint32_t curGapUs = debugChunkGapUs, curGapCount = debugChunkCount;
          uint32_t dGapUs = curGapUs - lastGapUs, dGapCount = curGapCount - lastGapCount;
          Serial.printf("[CHUNKGAP] chunks=%u totalGapUs=%u avgGapUs=%u\n",
                        dGapCount, dGapUs, dGapCount ? (dGapUs / dGapCount) : 0);
          lastGapUs = curGapUs; lastGapCount = curGapCount;
        }
        {
          static uint32_t lastHitUs = 0, lastHitCount = 0;
          uint32_t curHitUs = debugHitOverheadUs, curHitCount = debugHitOverheadCount;
          uint32_t dHitUs = curHitUs - lastHitUs, dHitCount = curHitCount - lastHitCount;
          Serial.printf("[HITOVERHEAD] hits=%u totalUs=%u avgUs=%u\n",
                        dHitCount, dHitUs, dHitCount ? (dHitUs / dHitCount) : 0);
          lastHitUs = curHitUs; lastHitCount = curHitCount;
        }
        #endif
      }
      #endif

      unsigned long currentKHashes = (Mhashes * 1000) + hashes / 1000;
      elapsedKHs = currentKHashes - totalKHashes;
      totalKHashes = currentKHashes;
      statusRateSample(currentKHashes, now_millis);

      uptime_frac += mElapsed;
      while (uptime_frac >= 1000)
      {
        uptime_frac -= 1000;
        upTime ++;
      }

      applyPendingSettings();
      backlightTick();
      drawCurrentScreen(mElapsed);

      // Safely feed 1 display sample per second into the Screen 3 scatter buffer
      // using pure software hashing (never touches hardware SHA or blocks Core 1).
      if (client.connected() && isMinerSuscribed)
      {
        uint8_t sw_hdr[80];
        memcpy(sw_hdr, mMiner.bytearray_blockheader, 80);
        uint32_t r_nonce = (uint32_t)rand();
        ((uint32_t *)(sw_hdr + 76))[0] = r_nonce;
        // nerd_sha256d_baked() bails out early unless the top 16 bits are
        // zero (1 in 65536), leaving the digest unwritten -- so this sample
        // was garbage and hash_samples stayed empty. Use a full software
        // double SHA-256 for this one display hash.
        uint8_t s_hash[32];
        swSha256d80(sw_hdr, s_hash);
        hashSamplePushDigest(s_hash);
      }

      // Monitor state when hashrate is 0.0
      if (elapsedKHs == 0)
      {
        Serial.printf(">>> [i] Miner: newJob>%s / inRun>%s) - Client: connected>%s / subscribed>%s / wificonnected>%s\n",
            "true",//(1) ? "true" : "false",
            isMinerSuscribed ? "true" : "false",
            client.connected() ? "true" : "false", isMinerSuscribed ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false");
      }

      #ifdef DEBUG_MEMORY
      Serial.printf("### [Total Heap / Free heap / Min free heap]: %d / %d / %d \n", ESP.getHeapSize(), ESP.getFreeHeap(), ESP.getMinFreeHeap());
      Serial.printf("### Max stack usage: %d\n", uxTaskGetStackHighWaterMark(NULL));
      #endif

      seconds_elapsed++;

      int saveInterval = saveIntervals[currentIntervalIndex];
      if (saveInterval > STATS_SAVE_MAX_s) saveInterval = STATS_SAVE_MAX_s;
      if(seconds_elapsed % saveInterval == 0){
        saveStat();
        seconds_elapsed = 0;
        if(currentIntervalIndex < saveIntervalsSize - 1)
          currentIntervalIndex++;
      }    
    }
    animateCurrentScreen(frame);
    doLedStuff(frame);

    vTaskDelay(DELAY / portTICK_PERIOD_MS);
    frame++;
  }
}
