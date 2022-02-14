/*
 * OXRS-BMD-PDU-LCD.h
 * 
 */

#ifndef OXRS_BMD_PDU_LCD_H
#define OXRS_BMD_PDU_LCD_H

#include <OXRS_MQTT.h>
#include <Ethernet.h>
#include "H_Bar.h"

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#define SPIFFS LittleFS
#else
#include <WiFi.h>
#include <SPIFFS.h>
#endif

#define     LCD_BL_ON                   100       // LCD backlight in % when ON, i.e. after an event
#define     LCD_BL_DIM                  10        // LCD backlight in % when DIMMED (0 == OFF), i.e. after LCD_ON_MS expires
#define     LCD_ON_MS                   10000     // How long to turn on the LCD after an event
#define     LCD_EVENT_MS                3000      // How long to display an event in the bottom line
#define     RX_TX_LED_ON                300       // How long to turn mqtt rx/tx led on after trgger

// LCD backlight control
// TFT_BL GPIO pin defined in user_setup.h of tft_eSPI
// setting PWM properties
#define     BL_PWM_FREQ                 5000
#define     BL_PWM_CHANNEL              0
#define     BL_PWM_RESOLUTION           8

// IP link states
#define     IP_STATE_UP                 0
#define     IP_STATE_DOWN               1
#define     IP_STATE_UNKNOWN            2

// MQTT led states
#define     MQTT_STATE_UP               0
#define     MQTT_STATE_ACTIVE           1
#define     MQTT_STATE_DOWN             2
#define     MQTT_STATE_UNKNOWN          3

// return codes from draw_header
#define     LCD_INFO_LOGO_FROM_SPIFFS   101   // logo found on SPIFFS and displayed
#define     LCD_INFO_LOGO_FROM_PROGMEM  102   // logo found in PROGMEM and displayed
#define     LCD_INFO_LOGO_DEFAULT       103   // used default OXRS logo
#define     LCD_ERR_NO_LOGO             1     // no logo successfully rendered

#define     BAR_COUNT                   9

 
class OXRS_LCD
{
  public:
    OXRS_LCD(EthernetClass& ethernet, OXRS_MQTT& mqtt);
    OXRS_LCD(WiFiClass& wifi, OXRS_MQTT& mqtt);
    
    int draw_header(const char * fwShortName, const char * fwMaker, const char * fwVersion, const char * fwPlatform, const uint8_t * fwLogo = NULL);

    // initialises and shows 9 horizontal bars, all states N/A
    void drawBars(void);
    // value to be shown for channel (0-based) (bar and numeric)
    void setBarValue(int channel, float value);
    // state to be shown for channel (0-based) (N/A, OFF, ON, FAULT, ...)
    void setBarState(int channel, int state);
    // sets the full scale value for channels (0-based) bar (if run time config desired)
    void setBarMaxValue(int channel, float value);

    void begin(void);
    void loop(void);  
    
    void trigger_mqtt_rx_led(void);
    void trigger_mqtt_tx_led(void);
    
    void show_temp(float temperature, char unit = 'C');
    void show_event(const char * s_event);
    
    void setBrightnessOn(int brightness_on);
    void setBrightnessDim(int brightness_dim);
    void setOnTimeDisplay(int ontime_display);
    void setOnTimeEvent(int ontime_event);

    // placeholders for backward compatibility, can be deleted if not needed
    void draw_ports(int port_layout, uint8_t mcps_found){}; // placeholder
    void process(uint8_t mcp, uint16_t io_value){}; // placeholder
    void setPortConfig(uint8_t mcp, uint8_t pin, int config){}; // placeholder

  private:  
    // for timeout (clear) of bottom line input event display
    uint32_t _last_event_display = 0L;
    
    // for timeout (dim) of LCD
    uint32_t _last_lcd_trigger = 0L;
    uint32_t _last_tx_trigger = 0L;
    uint32_t _last_rx_trigger = 0L;

    uint32_t  _ontime_display_ms = LCD_ON_MS;
    uint32_t  _ontime_event_ms = LCD_EVENT_MS;
    int       _brightness_on = LCD_BL_ON;
    int       _brightness_dim = LCD_BL_DIM;

    EthernetClass * _ethernet;
    WiFiClass *     _wifi;
    int             _ip_state = -1;
    
    OXRS_MQTT *     _mqtt;
    int             _mqtt_state = -1;
 
    void _clear_event(void);
    
    byte * _get_MAC_address(byte * mac);
    IPAddress _get_IP_address(void);

    int _get_IP_state(void);    
    void _check_IP_state(int state);
    void _show_IP(IPAddress ip);
    void _show_MAC(byte mac[]);

    int _get_MQTT_state(void);
    void _check_MQTT_state(int state);
    void _show_MQTT_topic(const char * topic);

    void _set_backlight(int val);
    void _set_ip_link_led(int state);
    void _set_mqtt_rx_led(int state);
    void _set_mqtt_tx_led(int state);

    bool _drawBmp(const char *filename, int16_t x, int16_t y, int16_t bmp_w, int16_t bmp_h);
    uint16_t _read16(File &f);
    uint32_t _read32(File &f);   

    bool _drawBmp_P(const uint8_t *image, int16_t x, int16_t y, int16_t bmp_w, int16_t bmp_h);
    uint16_t _read16_P(uint8_t** p);
    uint32_t _read32_P(uint8_t** p);   
};

#endif
