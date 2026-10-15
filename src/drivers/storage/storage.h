#ifndef _STORAGE_H_
#define _STORAGE_H_

#include <Arduino.h>

// config files

// default settings
#ifndef HAN
#define DEFAULT_SSID		"NerdMinerAP"
#else
#define DEFAULT_SSID		"HanSoloAP"
#endif
#define DEFAULT_WIFIPW		"MineYourCoins"
#define DEFAULT_POOLURL		"public-pool.io"
#define DEFAULT_POOLPASS	"x"
#define DEFAULT_WALLETID	"yourBtcAddress"
#define DEFAULT_POOLPORT	3333
#define DEFAULT_TIMEZONE	2
#define DEFAULT_SAVESTATS	false
#define DEFAULT_INVERTCOLORS	false
#define DEFAULT_BRIGHTNESS	250
#define DEFAULT_FLIPDISPLAY	false
#define DEFAULT_CURRENCY	"usd"
#define DEFAULT_MEMPOOLHOST	"mempool.space"	// public mempool; may be a local node e.g. "http://umbrel.local:3006"

// JSON config files
#define JSON_CONFIG_FILE	"/config.json"

// JSON config file SD card (for user interaction, readme.md)
#define JSON_KEY_SSID		"SSID"
#define JSON_KEY_PASW		"WifiPW"
#define JSON_KEY_POOLURL	"PoolUrl"
#define JSON_KEY_POOLPASS	"PoolPassword"
#define JSON_KEY_WALLETID	"BtcWallet"
#define JSON_KEY_POOLPORT	"PoolPort"
#define JSON_KEY_TIMEZONE	"Timezone"
#define JSON_KEY_STATS2NV	"SaveStats"
#define JSON_KEY_INVCOLOR	"invertColors"
#define JSON_KEY_BRIGHTNESS	"Brightness"

#define DEFAULT_FALLBACK_POOL	"pool.nerdminers.org"
#define DEFAULT_FALLBACK_PORT	3333

// JSON config file SPIFFS (different for backward compatibility with existing devices)
#define JSON_SPIFFS_KEY_POOLURL		"poolString"
#define JSON_SPIFFS_KEY_POOLPORT	"portNumber"
#define JSON_SPIFFS_KEY_POOLPASS	"poolPassword"
#define JSON_SPIFFS_KEY_WALLETID	"btcString"
#define JSON_SPIFFS_KEY_TIMEZONE	"gmtZone"
#define JSON_SPIFFS_KEY_STATS2NV	"saveStatsToNVS"
#define JSON_SPIFFS_KEY_INVCOLOR	"invertColors"
#define JSON_SPIFFS_KEY_BRIGHTNESS	"Brightness"
#define JSON_SPIFFS_KEY_FLIPDISPLAY	"flipDisplay"
#define JSON_SPIFFS_KEY_ROTATION	"screenRotation"
#define JSON_SPIFFS_KEY_THEME		"displayTheme"
#define JSON_SPIFFS_KEY_CURRENCY	"currency"
#define JSON_SPIFFS_KEY_MEMPOOL	"mempoolHost"
#define JSON_SPIFFS_KEY_FBPOOL		"fallbackPool"
#define JSON_SPIFFS_KEY_FBPORT		"fallbackPort"

// settings
struct TSettings
{
	String WifiSSID{ DEFAULT_SSID };
	String WifiPW{ DEFAULT_WIFIPW };
	String PoolAddress{ DEFAULT_POOLURL };
	char BtcWallet[80]{ DEFAULT_WALLETID };
	char PoolPassword[80]{ DEFAULT_POOLPASS };
	int PoolPort{ DEFAULT_POOLPORT };
	String FallbackPoolAddress{ DEFAULT_FALLBACK_POOL };
	int FallbackPoolPort{ DEFAULT_FALLBACK_PORT };
	int Timezone{ DEFAULT_TIMEZONE };
	bool saveStats{ DEFAULT_SAVESTATS };
	bool invertColors{ DEFAULT_INVERTCOLORS };
	int Brightness{ DEFAULT_BRIGHTNESS };
	bool flipDisplay{ DEFAULT_FLIPDISPLAY };	// rotate display 180 degrees (USB on the left)
	int screenRotation{ 1 };	// 0=Portrait (USB bottom), 1=Landscape (USB right), 2=Portrait Inv (USB top), 3=Landscape Inv (USB left)
	int displayTheme{ 0 };	// 0=Instrument Amber, 1=Matrix Green, 2=Cyber Amber, 3=Arcade Neon, 4=Ice Blue, 5=Monochrome
	String Currency{ DEFAULT_CURRENCY };	// fiat currency for the BTC price (CoinGecko vs_currency code)
	String MempoolHost{ DEFAULT_MEMPOOLHOST };	// mempool API host for block/fee/difficulty stats
};

#endif // _STORAGE_H_