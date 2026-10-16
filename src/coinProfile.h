#ifndef COIN_PROFILE_H
#define COIN_PROFILE_H

#include <stdint.h>

struct CoinProfile
{
  const char *id;
  const char *ticker;
  const char *name;

  const char *priceApiCoinId;

  const char *feeUnit;

  uint32_t halvingInterval;
  float initialSubsidy;
  uint16_t targetBlockTimeSec;
};

const CoinProfile &getActiveCoinProfile(void);

#endif // COIN_PROFILE_H
