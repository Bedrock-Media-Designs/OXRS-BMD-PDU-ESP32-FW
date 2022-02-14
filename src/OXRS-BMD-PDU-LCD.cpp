/*
 * OXRS_LCD.cpp
 * 
 */
 
#include "Arduino.h"
#include "OXRS-BMD-PDU-LCD.h"

#include <TFT_eSPI.h>               // Hardware-specific library
#include "OXRS_logo.h"              // default logo bitmap (24-bit-bitmap)
#include "Free_Fonts.h"             // GFX Free Fonts supplied with TFT_eSPI
#include "roboto_fonts.h"           // roboto_fonts Created by http://oleddisplay.squix.ch/
#include "icons.h"                  // resource file for icons
#include <pgmspace.h>

TFT_eSPI tft = TFT_eSPI();          // Invoke library
// horizontal bars
H_Bar     _h_bar[BAR_COUNT];

// for ethernet
OXRS_LCD::OXRS_LCD(EthernetClass& ethernet, OXRS_MQTT& mqtt)
{
  _wifi = NULL;
  _ethernet = &ethernet;
  _mqtt = &mqtt;
}

// for wifi
OXRS_LCD::OXRS_LCD(WiFiClass& wifi, OXRS_MQTT& mqtt)
{
  _wifi = &wifi;
  _ethernet = NULL;
  _mqtt = &mqtt;
}

void OXRS_LCD::begin()
{
  // initialise the display
  tft.begin();
  tft.setRotation(1);
  tft.fillRect(0, 0, 240, 240,  TFT_BLACK);

  // set up for backlight dimming (PWM)
  ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RESOLUTION);
  ledcAttachPin(TFT_BL, BL_PWM_CHANNEL);
  _set_backlight(_brightness_on);
}

/*
 * horizontal bar specific methods
 */
void OXRS_LCD::drawBars(void)
{
  
  int y = 95;
  for (int channel = 0; channel < BAR_COUNT; channel++)
  {
    _h_bar[channel].begin(&tft, channel, y);
    y+=14;
  }
  // fill bottom field with gray (event display space)
  _clear_event();
}

// value to be shown for channel (0-based) (bar and numeric)
void  OXRS_LCD::setBarValue(int channel, float value)
{
  _h_bar[channel].setValue(value);
}

// state to be shown for channel (0-based) (OFF, ON, FAULT, ...)
void  OXRS_LCD::setBarState(int channel, int state)
{
  _h_bar[channel].setState(state);
}

// sets the full scale value for channels  (0-based) bar (if run time config desired)
void  OXRS_LCD::setBarMaxValue(int channel, float value)
{
  _h_bar[channel].setMaxValue(value);
}


// ontime_display : display on after event occured    (default: 10 seconds)
// ontime_event   : time to show event on bottom line (default: 3 seconds)
// value range: 
//    0          : ever (no timer)
//    1 .. 600   : time in seconds (10 minutes max) range can be defined by the UI, not checked here
void OXRS_LCD::setOnTimeDisplay (int ontime_display)
{
  _ontime_display_ms = ontime_display * 1000;
}

void OXRS_LCD::setOnTimeEvent (int ontime_event)
{
  _ontime_event_ms = ontime_event * 1000;
}

// brightness_on  : brightness when on        (default: 100 %)
// brightness_dim : brightness when dimmed    (default:  10 %)
// value range    : 0 .. 100  : brightness in %  range can be defined by the UI, not checked here
void OXRS_LCD::setBrightnessOn (int brightness_on)
{
  _brightness_on = brightness_on;
}

void OXRS_LCD::setBrightnessDim (int brightness_dim)
{
  _brightness_dim = brightness_dim;
}

int OXRS_LCD::draw_header(const char * fwShortName, const char * fwMaker, const char * fwVersion, const char * fwPlatform, const uint8_t * fwLogo)
{
  char buffer[30];
  int return_code;

  int logo_w = 40;
  int logo_h = 40;
  int logo_x = 0;
  int logo_y = 0;

  // 1. try to draw maker supplied /logo.bmp from SPIFFS
  // 2, if not successful try to draw maker supplied logo via fwLogo (fwLogo from PROGMEM)
  // 3. if not successful draw embedded OXRS logo from PROGMEM
  return_code = LCD_INFO_LOGO_FROM_SPIFFS;
  if (!_drawBmp("/logo.bmp", logo_x, logo_y, logo_w, logo_h))
  {
    return_code = LCD_INFO_LOGO_FROM_PROGMEM;
    if (!fwLogo || !_drawBmp_P(fwLogo, logo_x, logo_y, logo_w, logo_h))
    {  
      return_code = LCD_INFO_LOGO_DEFAULT;
      if (!_drawBmp_P(OXRS_logo, logo_x, logo_y, logo_w, logo_h))
      {
        return_code = LCD_ERR_NO_LOGO;
      }
    }
  }

  tft.fillRect(42, 0, 240, 40,  TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_BLACK);
  tft.setFreeFont(&Roboto_Light_13);
  
  tft.drawString(fwShortName, 46, 0);
  tft.drawString(fwMaker, 46, 13);
 
  tft.drawString("Version", 46, 26); 
  sprintf(buffer, ": %s / %s", fwVersion, fwPlatform); 
  tft.drawString(buffer, 46+50, 26); 
  
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&Roboto_Mono_Thin_13);
  tft.drawString("Starting up...", 240/2 , 50); 
  tft.setTextDatum(TL_DATUM);
  
  return return_code;
}


/*
 * update LCD if
 *  show_event timed out
 *  LCD_on timed out
 *  rx and tx led timed out
 *  link status has changed
 */
void OXRS_LCD::loop(void)
{   
  // Clear event display if timed out
  if (_ontime_event_ms && _last_event_display)
  {
    if ((millis() - _last_event_display) > _ontime_event_ms)
    {
      _clear_event();      
      _last_event_display = 0L;
    }
  }

  // Dim LCD if timed out
  if (_ontime_display_ms && _last_lcd_trigger)
  {
    if ((millis() - _last_lcd_trigger) > _ontime_display_ms)
    {
      _set_backlight(_brightness_dim);
      _last_lcd_trigger = 0L;
    }
  }
  
  // turn off rx LED if timed out
  if (_last_rx_trigger)
  {
    if ((millis() - _last_rx_trigger) > RX_TX_LED_ON)
    {
      _set_mqtt_rx_led(MQTT_STATE_UP);
      _last_rx_trigger = 0L;
    }
  }
 
  // turn off tx LED if timed out
  if (_last_tx_trigger)
  {
    if ((millis() - _last_tx_trigger) > RX_TX_LED_ON)
    {
      _set_mqtt_tx_led(MQTT_STATE_UP);
      _last_tx_trigger = 0L;
    }
  }
 
  // check if IP or MQTT state has changed
  _check_IP_state(_get_IP_state());
  _check_MQTT_state(_get_MQTT_state());
}

/*
 * control mqtt rx/tx virtual leds 
 */
void OXRS_LCD::trigger_mqtt_rx_led(void)
{
  _set_mqtt_rx_led(MQTT_STATE_ACTIVE);
  _last_rx_trigger = millis(); 
}

void OXRS_LCD::trigger_mqtt_tx_led(void)
{
  _set_mqtt_tx_led(MQTT_STATE_ACTIVE);
  _last_tx_trigger = millis(); 
}

void OXRS_LCD::show_temp(float temperature, char unit)
{
  char buffer[30];
  
  tft.fillRect(0, 75, 240, 13,  TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&Roboto_Mono_Thin_13);
  sprintf(buffer, "TEMP: %2.1f %c", temperature, unit);
  tft.drawString(buffer, 12, 75);
}

/*
 * draw event on bottom line of screen
 */
void OXRS_LCD::show_event(const char * s_event)
{
  // Show last input event on bottom line
  tft.fillRect(0, 225, 240, 15,  TFT_WHITE);
  tft.setTextColor(TFT_BLACK, TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(FMB9);       // Select Free Mono Bold 9
  tft.drawString(s_event, 0, 225);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  _last_event_display = millis(); 
}

void OXRS_LCD::_clear_event()
{
  tft.fillRect(0, 225, 240, 240,  TFT_DARKGREY);
}

IPAddress OXRS_LCD::_get_IP_address(void)
{
  if (_get_IP_state() == IP_STATE_UP)
  {
    if (_ethernet)
    {
      return _ethernet->localIP();
    }

    if (_wifi)
    {
      return _wifi->localIP();
    }
  }
  
  return IPAddress(0, 0, 0, 0);
}

int OXRS_LCD::_get_IP_state(void)
{
  if (_ethernet)
  {
    return _ethernet->linkStatus() == LinkON ? IP_STATE_UP : IP_STATE_DOWN;
  }
  
  if (_wifi)
  {
    return _wifi->status() == WL_CONNECTED ? IP_STATE_UP : IP_STATE_DOWN;
  }

  return IP_STATE_UNKNOWN;
}

void OXRS_LCD::_check_IP_state(int state)
{
  if (state != _ip_state)
  {
    _ip_state = state;

    // refresh IP address on state change
    IPAddress ip = _get_IP_address();
    _show_IP(ip);

    // update the link LED after refreshing IP address
    // since that clears that whole line on the screen
    _set_ip_link_led(_ip_state);
    
    // if the link is up check we actually have an IP address
    // since DHCP might not have issued an IP address yet
    if (_ip_state == IP_STATE_UP && ip[0] == 0)
    {
      _ip_state = IP_STATE_DOWN;
    }
  }
}

void OXRS_LCD::_show_IP(IPAddress ip)
{
  // clear anything already displayed
  tft.fillRect(0, 45, 240, 15, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&Roboto_Mono_Thin_13);
 
  char buffer[30];
  if (ip[0] == 0)
  {
    sprintf(buffer, "  IP: ---.---.---.---");
  }
  else
  {
    sprintf(buffer, "  IP: %03d.%03d.%03d.%03d", ip[0], ip[1], ip[2], ip[3]);
  }
  tft.drawString(buffer, 12, 45);
  
  if (_wifi)
  {
    tft.drawBitmap(13, 46, icon_wifi, 11, 10, TFT_BLACK, TFT_WHITE);
  }
  if (_ethernet)
  {
    tft.drawBitmap(13, 46, icon_ethernet, 11, 10, TFT_BLACK, TFT_WHITE);
  }
}


int OXRS_LCD::_get_MQTT_state(void)
{
  if (_get_IP_state() == IP_STATE_UP)
  {
    return _mqtt->connected() ? MQTT_STATE_UP : MQTT_STATE_DOWN;
  }

  return MQTT_STATE_UNKNOWN;
}

void OXRS_LCD::_check_MQTT_state(int state)
{
  if (state != _mqtt_state)
  {
    _mqtt_state = state;

    // don't show any topic if we are in an unknown state
    if (_mqtt_state == MQTT_STATE_UNKNOWN)
    {
      _show_MQTT_topic("-/------");
    }
    else
    {
      char topic[64];
      _show_MQTT_topic(_mqtt->getWildcardTopic(topic));
    }

    // update the activity LEDs after refreshing MQTT topic
    // since that clears that whole line on the screen
    _set_mqtt_tx_led(_mqtt_state);
    _set_mqtt_rx_led(_mqtt_state);
    
    // ensure any activity timers don't reset the LEDs
    _last_tx_trigger = 0L;
    _last_rx_trigger = 0L;    
  }
}

void OXRS_LCD::_show_MQTT_topic(const char * topic)
{
  // clear anything already displayed
  tft.fillRect(0, 60, 240, 13, TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&Roboto_Mono_Thin_13);

  char buffer[30];
  strcpy(buffer, "MQTT: ");
  strncat(buffer, topic, sizeof(buffer)-strlen(buffer)-1);
  tft.drawString(buffer, 12, 60);
}


/*
 * set backlight of LCD (val in % [0..100])
 */
void OXRS_LCD::_set_backlight(int val)
{
  ledcWrite(BL_PWM_CHANNEL, 255*val/100); 
}

/*
 * animated "leds"
 */
void OXRS_LCD::_set_ip_link_led(int state)
{
  // UP, DOWN, UNKNOWN
  uint16_t color[3] = {TFT_GREEN, TFT_RED, TFT_BLACK};
  if (state < 3) tft.fillRoundRect(2, 49, 8, 5, 2, color[state]);
}

void OXRS_LCD::_set_mqtt_rx_led(int state)
{
  // UP, ACTIVE, DOWN, UNKNOWN
  uint16_t color[4] = {TFT_GREEN, TFT_YELLOW, TFT_RED, TFT_BLACK};  
  if (state < 4) tft.fillRoundRect(2, 60, 8, 5, 2, color[state]);
}

void OXRS_LCD::_set_mqtt_tx_led(int state)
{
  // UP, ACTIVE, DOWN, UNKNOWN
  uint16_t color[4] = {TFT_GREEN, TFT_ORANGE, TFT_RED, TFT_BLACK};
  if (state < 4) tft.fillRoundRect(2, 68, 8, 5, 2, color[state]);
}

/*
 * Bodmers BMP image rendering function
 */
// render logo from file in SPIFFS
bool OXRS_LCD::_drawBmp(const char *filename, int16_t x, int16_t y, int16_t bmp_w, int16_t bmp_h) 
{
  uint32_t  seekOffset;
  uint16_t  w, h, row, col;
  uint8_t   r, g, b;


  if (!SPIFFS.begin())
    return false;

  File file = SPIFFS.open(filename, "r");

  if (!file) 
    return false;  

  if (file.size() == 0) 
    return false;

  if (_read16(file) == 0x4D42)
  {
    _read32(file);
    _read32(file);
    seekOffset = _read32(file);
    _read32(file);
    w = _read32(file);
    h = _read32(file);

    if ((_read16(file) == 1) && (_read16(file) == 24) && (_read32(file) == 0))
    {
      // crop to bmp_h
      y += bmp_h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      file.seek(seekOffset);

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++)
      {
        file.read(lineBuffer, sizeof(lineBuffer));
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        // crop to bmp_w
        tft.pushImage(x, y--, bmp_w, 1, (uint16_t*)lineBuffer);
      }
      tft.setSwapBytes(oldSwapBytes);

      file.close();
      return true;
    }
  }
  
  file.close();
  return false;
}

// These read 16- and 32-bit types from the SPIFFS file
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t OXRS_LCD::_read16(File &f) 
{
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t OXRS_LCD::_read32(File &f) 
{
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

// render logo from array in PROGMEM
bool OXRS_LCD::_drawBmp_P(const uint8_t *image, int16_t x, int16_t y, int16_t bmp_w, int16_t bmp_h) 
{
  uint32_t  seekOffset;
  uint16_t  w, h, row, col;
  uint8_t   r, g, b;
  uint8_t*  ptr;

  ptr = (uint8_t*)image;

  if (_read16_P(&ptr) == 0x4D42)
  {
    _read32_P(&ptr);
    _read32_P(&ptr);
    seekOffset = _read32_P(&ptr);
    _read32_P(&ptr);
    w = _read32_P(&ptr);
    h = _read32_P(&ptr);

    if ((_read16_P(&ptr) == 1) && (_read16_P(&ptr) == 24) && (_read32_P(&ptr) == 0))
    {
      // crop to bmp_h
      y += bmp_h - 1;

      bool oldSwapBytes = tft.getSwapBytes();
      tft.setSwapBytes(true);
      ptr = (uint8_t*)image + seekOffset;

      uint16_t padding = (4 - ((w * 3) & 3)) & 3;
      uint8_t lineBuffer[w * 3 + padding];

      for (row = 0; row < h; row++)
      {
        memcpy_P(lineBuffer, ptr, sizeof(lineBuffer));
        ptr += sizeof(lineBuffer);
        uint8_t*  bptr = lineBuffer;
        uint16_t* tptr = (uint16_t*)lineBuffer;
        // Convert 24 to 16 bit colours
        for (uint16_t col = 0; col < w; col++)
        {
          b = *bptr++;
          g = *bptr++;
          r = *bptr++;
          *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }

        // Push the pixel row to screen, pushImage will crop the line if needed
        // y is decremented as the BMP image is drawn bottom up
        // crop to bmp_w
        tft.pushImage(x, y--, bmp_w, 1, (uint16_t*)lineBuffer);
      }

      tft.setSwapBytes(oldSwapBytes);
      return true;
    }
  }

  return false;
}

// These read 16- and 32-bit types from PROGMEM.
// BMP data is stored little-endian, Arduino is little-endian too.
// May need to reverse subscript order if porting elsewhere.

uint16_t OXRS_LCD::_read16_P(uint8_t** p) 
{
  uint16_t result;
   
  memcpy_P((uint8_t *)&result, *p, 2);
  *p += 2;
  return result;
}

uint32_t OXRS_LCD::_read32_P(uint8_t** p) 
{
  uint32_t result;
  
  memcpy_P((uint8_t *)&result, *p, 4);
  *p += 4;
  return result;
}
