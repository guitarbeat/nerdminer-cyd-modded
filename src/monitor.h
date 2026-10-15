#ifndef MONITOR_API_H
#define MONITOR_API_H

#include <Arduino.h>

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

#define UPDATE_BTC_min   1

//API Block height
#define getHeightAPI "/api/blocks/tip/height"   // path on mempoolUrl()
#define UPDATE_Height_min 2

//APIs Global Stats
// Paths appended to mempoolUrl() (Settings.MempoolHost, default mempool.space)
#define getGlobalHash "/api/v1/mining/hashrate/3d"
#define getDifficulty "/api/v1/difficulty-adjustment"
#define getFees "/api/v1/fees/recommended"
#define UPDATE_Global_min 2

//API public-pool.io
// https://public-pool.io:40557/api/client/btcString
#define getPublicPool "https://public-pool.io:40557/api/client/" // +btcString
// Each refresh holds g_hwShaMutex for the whole HTTPS request (~1.5 s with the
// TLS handshake), which pauses the hardware miner. At 1 min that was ~2.4% of
// all mining time; 5 min costs ~0.5% and pool stats barely change in 5 min.
#define UPDATE_POOL_min   5

#define NEXT_HALVING_EVENT 1050000 //840000
#define HALVING_BLOCKS 210000

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

// ---- BTC/USDT 1h candlestick feed (Screen 1 MARKET) -----------------------
// Fetched from Binance US (Kraken fallback) once a minute by its own low-priority
// "Klines" task (startKlineTask), never from the Monitor loop.
// Stored in a fixed ring buffer so a refresh never reallocates; readers take a
// linear, oldest-first snapshot with copyKlines() so they never see a
// half-written update.
struct Kline {
  float open, high, low, close;
  uint32_t timestamp;   // candle open time, unix seconds (UTC)
};

#define KLINE_POINTS 24
#define KLINE_INTERVAL_LABEL "1H"
#define UPDATE_KLINE_ms (60UL * 1000UL)
#define KLINE_RETRY_ms  (60UL * 1000UL)   // back-off after a failed fetch (offline)

extern Kline g_btcKlines[KLINE_POINTS];   // ring storage, see g_klineHead
extern uint8_t g_klineCount;              // valid entries (0..KLINE_POINTS)
extern uint8_t g_klineHead;               // index of the oldest entry
extern uint32_t g_klineEpoch;             // bumps on every successful refresh
extern float g_btc24hChange;              // % change across the buffered window

// Copies up to KLINE_POINTS candles, oldest first, into out[]; returns count.
uint8_t copyKlines(Kline *out);
// Refresh the kline buffer if due. Safe to call often: returns immediately
// unless a minute has elapsed; keeps cached candles on any failure.
void fetchKlines(void);
// Spawns the dedicated low-priority "Klines" task that calls fetchKlines().
void startKlineTask(void);
// Compact JSON object with market data + candles for the web twin.
String getMarketJson(void);

typedef struct{
  String globalHash; //hexahashes
  String currentBlock;
  String difficulty;
  String blocksHalving;
  float progressPercent;
  int remainingBlocks;
  int halfHourFee;
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
}mining_data;

typedef struct {
  String completedShares;
  String totalKHashes;
  String currentHashRate;
  String btcPrice;
  String blockHeight;
  String currentTime;  
  String currentDate;
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

#endif //MONITOR_API_H
