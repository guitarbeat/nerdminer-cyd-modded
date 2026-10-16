#include "coinProfile.h"

static const CoinProfile kProfileBitcoin = {
    "btc",
    "BTC",
    "Bitcoin",
    "bitcoin",
    "sat/vB",
    210000,
    50.0f,
    600};

const CoinProfile &getActiveCoinProfile(void)
{
  return kProfileBitcoin;
}
