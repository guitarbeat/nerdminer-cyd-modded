#include "coinProfile.h"

static const CoinProfile kProfileBitcoin = {
    "btc",
    "BTC",
    "Bitcoin",
    "bitcoin",
    "sat/vB",
    210000,
    50.0f,
    600,
    "public-pool.io",
    3333,
    "mempool.space",
    "bc1q... (Native SegWit), 3... (Nested SegWit), or 1... (Legacy)"};

const CoinProfile &getActiveCoinProfile(void)
{
  return kProfileBitcoin;
}
