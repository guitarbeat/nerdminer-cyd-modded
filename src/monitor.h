#ifndef MONITOR_API_H
#define MONITOR_API_H

#include <Arduino.h>
#include "coinProfile.h"

// Monitor states
#define SCREEN_MINING   0
#define SCREEN_CLOCK    1
#define SCREEN_GLOBAL   2
#define NO_SCREEN       3   //Used when board has no TFT

//Time update period
#define UPDATE_PERIOD_h   5

//API BTC price (Update to USDT cus it's more liquidity and flow price updade)   

//#define getBTCAPI "https://api.coindesk.com/v1/bpi/currentprice.json" -- doesn't work anymore
//#define getBTCAPI "https://api.blockchain.com/v3/exchange/tickers/BTC-USDT" -- updates infrequently
#define getBTCAPI "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=" // + currency code

#ifndef UPDATE_BTC_min // a board can stretch it from platformio.ini
#define UPDATE_BTC_min   1
#endif

//API Block height
#define getHeightAPI "/api/blocks/tip/height"   // path on mempoolUrl()
#ifndef UPDATE_Height_min // a board can stretch it from platformio.ini
#define UPDATE_Height_min 2
#endif

//APIs Global Stats
// Paths appended to mempoolUrl() (Settings.MempoolHost, default mempool.space)
#define getGlobalHash "/api/v1/mining/hashrate/3d"
#define getDifficulty "/api/v1/difficulty-adjustment"
#define getFees "/api/v1/fees/recommended"
#ifndef UPDATE_Global_min // a board can stretch it from platformio.ini
#define UPDATE_Global_min 2
#endif

//API public-pool.io
// https://public-pool.io:40557/api/client/btcString
#define getPublicPool "https://public-pool.io:40557/api/client/" // +btcString
// Each refresh holds g_hwShaMutex for the whole HTTPS request (~1.5 s with the
// TLS handshake), which pauses the hardware miner. At 1 min that was ~2.4% of
// all mining time; 5 min costs ~0.5% and pool stats barely change in 5 min.
#define UPDATE_POOL_min   5

#define NEXT_HALVING_EVENT 1050000 //840000
#define HALVING_BLOCKS (getActiveCoinProfile().halvingInterval)

enum NMState {
  NM_waitingConfig,
  NM_Connecting,
  NM_hashing
};

typedef struct{
  uint8_t screen;
  bool rotation;
  NMState NerdStatus;
}monitor_data;

// ---- BTC/USDT candlestick chart (Screen 1 MARKET) -------------------------
// The live candle feed was removed (its HTTPS fetch had to pause mining via
// g_hwShaMutex). copyKlines() always returns 0, so the chart shows its empty
// "waiting for market data" state.
struct Kline {
  float open, high, low, close;
  uint32_t timestamp;   // candle open time, unix seconds (UTC)
};

#define KLINE_POINTS 24
#define KLINE_INTERVAL_LABEL "1H"

extern uint8_t g_klineCount;              // always 0 (no feed)
extern uint32_t g_klineEpoch;             // always 0 (no feed)

// Copies candles into out[]; returns count (always 0 now).
uint8_t copyKlines(Kline *out);
// Compact JSON object with market data for the web twin (empty candle list).
String getMarketJson(void);
// Minutes since a new tip height was observed (-1 until a change is seen).
int blockAgeMin(void);
// "~Mon YYYY" estimate for `rem` blocks from now (10 min/block), "" if no NTP.
String halvingEtaStr(unsigned long rem);

typedef struct{
  String globalHash; //hexahashes
  String currentBlock;
  String difficulty;
  String blocksHalving;
  float progressPercent;
  int remainingBlocks;
  int halfHourFee;
  int feeFast;   // fastestFee (same /fees/recommended response)
  int feeHour;   // hourFee
#ifdef NERDMINER_T_HMI
  int fastestFee;
  int hourFee;
  int economyFee;
  int minimumFee;
#endif
}global_data;

typedef struct {
  String completedShares;
  String totalMHashes;
  String totalKHashes;
  String currentHashRate;
  String templates;
  String bestDiff;
  String timeMining;
  String valids;
  String temp;
  String currentTime;
  uint64_t blockHashes;
  uint32_t blockTimeSec;
  uint64_t lastBlockHashes;
}mining_data;

typedef struct {
  String completedShares;
  String totalKHashes;
  String currentHashRate;
  String btcPrice;
  String blockHeight;
  String currentTime;  
  String currentDate;
  String temp;
  String timeMining;
}clock_data;

typedef struct {
  String currentHashRate;
  String valids;
  unsigned long currentHours;
  unsigned long currentMinutes;
  unsigned long currentSeconds;
}clock_data_t;

typedef struct {
  String completedShares;
  String totalKHashes;
  String currentHashRate;
  String btcPrice;
  String currentTime;
  String halfHourFee;
#ifdef NERDMINER_T_HMI
  String hourFee;
  String fastestFee;
  String economyFee;
  String minimumFee;
#endif
  String netwrokDifficulty;
  String globalHashRate;
  String blockHeight;
  float progressPercent;
  String remainingBlocks;
  int blockAgeMin;     // minutes since this device saw the tip change; -1 = unknown
  int feeFast, feeHour; // 0 = unknown
  String halvingEta;   // "~Apr 2028" (10-min block estimate) or empty without NTP
}coin_data;

typedef struct{
  int workersCount;       // Workers count, how many nerdminers using your address
  String workersHash;     // Workers Total Hash Rate
  String bestDifficulty;  // Your miners best difficulty
}pool_data;

void setup_monitor(void);

mining_data getMiningData(unsigned long mElapsed);
clock_data getClockData(unsigned long mElapsed);
coin_data getCoinData(unsigned long mElapsed);
pool_data getPoolData(void);

clock_data_t getClockData_t(unsigned long mElapsed);
String getPoolAPIUrl(void);
// Full URL for a mempool API path on the user-configured host.
String mempoolUrl(const char *path);
bool getUtcTime(time_t &utc, bool sync = false);

// True once real data is in (see monitor.cpp); screens show "--" until then.
bool isTimeSynced(void);
bool isBlockHeightKnown(void);
unsigned int getBTCpriceValue(void); // raw price in the selected currency, 0 = unknown
uint32_t getBlockHeightValue(void);  // last block height fetched, 0 = unknown (any task)
bool getLocalClock(int &hour, int &minute, int &second, int &day, int &month, int &year); // no NTP call (any task)
long currentDstOffset(void);         // summer-time offset in force now, seconds (AUTO_DST_EU)

// Settings changed on the LAN settings page without a restart. The monitor
// task (screens, price, pool stats) applies them itself, between screen
// refreshes, in applyPendingSettings(): a new Settings.Timezone, a currency
// handed over with requestCurrencyChange() (a kCurrencies code), and, after
// poolSettingsChanged() from the stratum task, the pool stats lookup.
// currencySetting() is the currency in use or the one waiting.
void requestCurrencyChange(const char *code);
String currencySetting(void);
pool_data getPoolData(void);
void poolSettingsChanged(void);
void applyPendingSettings(void);

String getTime(void);
String getDate(void);
String getCurrentHashRate(unsigned long mElapsed);

extern unsigned int bitcoin_price;
extern String current_block;
extern global_data gData;
extern pool_data pData;
extern double best_diff;
extern volatile uint32_t shares;
extern volatile uint32_t valids;
extern uint32_t templates;
extern uint32_t Mhashes;
extern uint64_t upTime;

#endif //MONITOR_API_H

