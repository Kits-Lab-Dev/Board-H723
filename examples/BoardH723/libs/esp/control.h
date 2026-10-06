#ifndef __SPI_WIFI_CONTROL_H
#define __SPI_WIFI_CONTROL_H

#include "driver.h"
#include "string.h"
#include <stdio.h>

#define SSID_LENGTH 33
#define PASSWORD_LENGTH 64
#define MAC_LEN 6
#define BSSID_LENGTH MAC_LEN
#define VENDOR_OUI_BUF 3

#define CONTROL_DATA_LEN (SPI_BUF_DATA_LEN - sizeof(CtrlMsgId) - sizeof(uint16_t) - sizeof(esp_interface) - sizeof(uint32_t))

#define MAX_WIFI_AP_RECS (CONTROL_DATA_LEN / sizeof(wifiAPrecord))

typedef enum __packed
{
  MsgId_Invalid = 0,
  /*
   ** Request Msgs *
   */
  Req_Base = 10,
  Req_GetMACAddress,
  Req_SetMACAddress,
  Req_GetWifiMode,
  Req_SetWifiMode,
  Req_GetAPScanList,
  Req_GetAPConfig,
  Req_ConnectAP,
  Req_DisconnectAP,
  Req_GetSoftAPConfig,
  Req_SetSoftAPVendorSpecificIE,
  Req_StartSoftAP,
  Req_GetSoftAPConnectedSTAList,
  Req_StopSoftAP,
  Req_SetPowerSaveMode,
  Req_GetPowerSaveMode,
  Req_OTABegin,
  Req_OTAWrite,
  Req_OTAEnd,
  Req_SetWifiMaxTxPower,
  Req_GetWifiCurrTxPower,
  Req_ConfigHeartbeat,
  Req_EnableDisable,
  Req_GetFwVersion,
  /*
   * Add new control path command response before Req_Max
   * and update Req_Max
   */
  Req_Max,
  /*
   ** Response Msgs *
   */
  Resp_Base = 50,
  Resp_GetMACAddress,
  Resp_SetMACAddress,
  Resp_GetWifiMode,
  Resp_SetWifiMode,
  Resp_GetAPScanList,
  Resp_GetAPConfig,
  Resp_ConnectAP,
  Resp_DisconnectAP,
  Resp_GetSoftAPConfig,
  Resp_SetSoftAPVendorSpecificIE,
  Resp_StartSoftAP,
  Resp_GetSoftAPConnectedSTAList,
  Resp_StopSoftAP,
  Resp_SetPowerSaveMode,
  Resp_GetPowerSaveMode,
  Resp_OTABegin,
  Resp_OTAWrite,
  Resp_OTAEnd,
  Resp_SetWifiMaxTxPower,
  Resp_GetWifiCurrTxPower,
  Resp_ConfigHeartbeat,
  Resp_EnableDisable,
  Resp_GetFwVersion,
  /*
   * Add new control path command response before Resp_Max
   * and update Resp_Max
   */
  Resp_Max,
  /*
   ** Event Msgs *
   */
  Event_Base = 100,
  Event_ESPInit,
  Event_Heartbeat,
  Event_StationDisconnectFromAP,
  Event_StationDisconnectFromESPSoftAP,
  Event_StationConnectedToAP,
  Event_StationConnectedToESPSoftAP,
  /*
   * Add new control path command notification before Event_Max
   * and update Event_Max
   */
  Event_Max
} CtrlMsgId;

typedef struct __packed
{
  CtrlMsgId id;
  esp_interface type;
  uint16_t len;
  uint32_t queue;
  uint8_t data[CONTROL_DATA_LEN];
} ControlMsg;

typedef struct 
{
  CtrlMsgId id;
  esp_interface type;
  uint16_t len;
  uint8_t *data;
  void(*free_fn)(void* p);
} pControlMsg;

typedef enum __packed
{
  AuthOpen = 0,
  AuthWEP = 1,
  AuthWPA_PSK = 2,
  AuthWPA2_PSK = 3,
  AuthWPA_WPA2_PSK = 4,
  AuthWPA2_ENTERPRISE = 5,
  AuthWPA3_PSK = 6,
  AuthWPA2_WPA3_PSK = 7
} wifiAuthMode;

typedef enum {
    WIFI_BW_HT20   = 1,       /* Bandwidth is HT20      */
    WIFI_BW20 = WIFI_BW_HT20, /* Bandwidth is 20 MHz    */
    WIFI_BW_HT40   = 2,       /* Bandwidth is HT40      */
    WIFI_BW40 = WIFI_BW_HT40, /* Bandwidth is 40 MHz    */
    WIFI_BW80      = 3,       /* Bandwidth is 80 MHz    */
    WIFI_BW160     = 4,       /* Bandwidth is 160 MHz   */
    WIFI_BW80_BW80 = 5,       /* Bandwidth is 80+80 MHz */
} wifiBandWidth;

typedef struct __packed
{
  uint8_t ssid[SSID_LENGTH];
  uint8_t pwd[PASSWORD_LENGTH];
  wifiAuthMode auth_mode;
  uint8_t chnl;
  uint8_t max_conn;
  uint8_t ssid_hidden;
  uint8_t bw;
} wifiSoftAPConfig;

typedef struct __packed
{
  uint8_t mac[MAC_LEN];
  int8_t rssi;
  uint32_t phy_bits;
} wifiConnectedStantion;

typedef struct __packed
{
  uint8_t bssid[BSSID_LENGTH]; /**< MAC address of AP */
  uint8_t ssid[SSID_LENGTH];   /**< SSID of AP */
  uint8_t chnl;                /**< channel of AP */
  int8_t rssi;                 /**< signal strength of AP. Note that in some rare cases where signal strength is very strong, rssi values can be slightly positive */
  uint8_t authmode;            /**< authmode of AP */
  uint32_t phy_bits;
  char country_code[3];          /**< country code string */
  uint8_t schan;       /**< start channel */
  uint8_t nchan;       /**< total channel number */
  int8_t max_tx_power; /**< This field is used for getting WiFi maximum transmitting power, call esp_wifi_set_max_tx_power to set the maximum transmitting power. */
} wifiAPConfig;

typedef struct __packed
{
  uint8_t ssid[SSID_LENGTH];
  uint8_t bssid[BSSID_LENGTH];
  uint8_t chnl;
  int8_t rssi;
} wifiAPrecord;

typedef struct __packed
{
  uint8_t ssid[SSID_LENGTH];
  uint8_t bssid[BSSID_LENGTH];
  uint8_t password[PASSWORD_LENGTH];
  bool bssid_set;
  bool is_wpa3_supported;
  int32_t listen_interval;
} wifiConnRecord;

typedef struct __packed
{
  uint8_t ssid[SSID_LENGTH];
  uint8_t bssid[BSSID_LENGTH];
  uint8_t channel;           /**< channel of connected AP*/
  uint8_t authmode; /**< authentication mode used by AP*/
  uint16_t aid;              /**< authentication id assigned by the connected AP */
} wifiEventSTAConnected;


typedef struct __packed
{
  uint8_t ssid[SSID_LENGTH];
  uint8_t bssid[BSSID_LENGTH];
  uint8_t reason; /**< reason of disconnection */
  int8_t rssi;    /**< rssi of disconnection */
} wifiEventSTADisconnected;


typedef struct __packed{
    uint8_t mac[MAC_LEN];           /**< MAC address of the station connected to Soft-AP */
    uint8_t aid;              /**< the aid that soft-AP gives to the station connected to  */
    bool is_mesh_child;       /**< flag to identify mesh child */
} wifiEventAPSTAConnected;


typedef struct __packed{
    uint8_t mac[MAC_LEN];           /**< MAC address of the station disconnects to soft-AP */
    uint8_t aid;              /**< the aid that soft-AP gave to the station disconnects to  */
    bool is_mesh_child;       /**< flag to identify mesh child */
    uint16_t reason;           /**< reason of disconnection */
} wifiEventAPSTADisconnected;

typedef enum  __packed{
  Beacon = 0,
  Probe_req = 1,
  Probe_resp = 2,
  Assoc_req = 3,
  Assoc_resp = 4
} VendorIEType;

typedef enum  __packed{
  IEID_ID_0 = 0,
  IEID_ID_1 = 1
} VendorIEID;

typedef struct __packed{
    bool enable;
    VendorIEType IEType;
    VendorIEID IEID;
    uint8_t element_id;      /**< Should be set to WIFI_VENDOR_IE_ELEMENT_ID (0xDD) */
    uint8_t length;          /**< Length of all bytes in the element data following this field. Minimum 4. */
    uint8_t vendor_oui[3];   /**< Vendor identifier (OUI). */
    uint8_t vendor_oui_type; /**< Vendor-specific OUI type. */
} vendorIEdata;

typedef enum {
    WIFI_MODE_NULL = 0,  /**< null mode */
    WIFI_MODE_STA,       /**< WiFi station mode */
    WIFI_MODE_AP,        /**< WiFi soft-AP mode */
    WIFI_MODE_APSTA,     /**< WiFi station + soft-AP mode */
    WIFI_MODE_NAN,       /**< WiFi NAN mode */
    WIFI_MODE_MAX
} wifiMode;

typedef enum {
    WIFI_PS_NONE,        /**< No power save */
    WIFI_PS_MIN_MODEM,   /**< Minimum modem power saving. In this mode, station wakes up to receive beacon every DTIM period */
    WIFI_PS_MAX_MODEM,   /**< Maximum modem power saving. In this mode, interval to receive beacons is determined by the listen_interval parameter in wifi_sta_config_t */
} wifiPowerSaveMode;

typedef enum __packed
{
  ftWifi = 1,
  ftBluetooth = 2
} espFeature;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_NO_AP_FOUND_BIT BIT2
#define WIFI_WRONG_PASSWORD_BIT BIT3
#define WIFI_HOST_REQUEST_BIT BIT4

#define TIMEOUT_IN_MIN (60 * TIMEOUT_IN_SEC)
#define TIMEOUT_IN_HOUR (60 * TIMEOUT_IN_MIN)
#if WIFI_DUALBAND_SUPPORT
#define STA_MODE_TIMEOUT (15 * TIMEOUT_IN_SEC)
#else
#define STA_MODE_TIMEOUT (5 * TIMEOUT_IN_SEC)
#endif
#define RESTART_TIMEOUT (5 * TIMEOUT_IN_SEC)

#define MIN_TX_POWER                8
#define MAX_TX_POWER                84

extern bool wifiEnabled;

static inline bool macToString(const uint8_t *mac, char *str)
{
    int ret = snprintf(str, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return ret >= 0;
}

static inline bool stringToMac(const char *str, uint8_t *mac)
{
    int ret = sscanf(str, "%hhX:%hhX:%hhX:%hhX:%hhX:%hhX",
           &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    return ret == 6;
}


esp_err_t control_rx_process(uint8_t *data, uint16_t len);
void controlInit();

bool wifiGetMacAddress(esp_interface interface, uint8_t *mac);
bool wifiSetMacAddress(esp_interface interface, uint8_t *mac);
bool wifiGetWiFiMode(wifiMode *mode);
bool wifiSetWiFiMode(wifiMode mode);
uint32_t wifiGetScanList(wifiAPrecord* recs);
bool wifiGetAPConfig(wifiAPConfig* rec);
bool wifiConnect(wifiConnRecord *rec, bool autoReconnect);
bool wifiDisconnect();
bool wifiGetSoftAPConfig(wifiSoftAPConfig *rec);
bool wifiStartSoftAP(wifiSoftAPConfig *cfg);
uint32_t wifiGetConnectedStaList(wifiConnectedStantion *rec);
bool wifiStopSoftAP();
bool wifiIsConnected();
bool wifiSetPowerSaveMode(wifiPowerSaveMode mode);
bool wifiGetPowerSaveMode(wifiPowerSaveMode *mode);
bool wifiSetMaxTxPower(int8_t power);
bool wifiGetMaxTxPower(int8_t *power);
bool wifiGetFirmwareVersion(char *fw);

bool espFeatureEnable(espFeature ft, bool en);
bool espEnable(bool enable);
bool espInit();
#endif