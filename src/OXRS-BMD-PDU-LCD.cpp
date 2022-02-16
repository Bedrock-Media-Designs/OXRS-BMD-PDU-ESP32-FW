/*
 * OXRS_LCD_CUSTOM.cpp
 * 
 */
 
#include "Arduino.h"
#include "OXRS-BMD-PDU-LCD.h"


// pointer to default tft class
TFT_eSPI* _ctft; 

// horizontal bars
H_Bar     _h_bar[BAR_COUNT];


void OXRS_LCD_CUSTOM::begin(TFT_eSPI* tft)
{
  // get the tft class
  _ctft = tft;
}

/*
 * horizontal bar specific methods
 */
void OXRS_LCD_CUSTOM::drawBars(void)
{
  
  int y = 95;
  for (int channel = 0; channel < BAR_COUNT; channel++)
  {
    _h_bar[channel].begin(_ctft, channel, y);
    y+=14;
  }
  // fill bottom field with gray (event display space)
  _clear_event();
}

// value to be shown for channel (0-based) (bar and numeric)
void  OXRS_LCD_CUSTOM::setBarValue(int channel, float value)
{
  _h_bar[channel].setValue(value);
}

// state to be shown for channel (0-based) (OFF, ON, FAULT, ...)
void  OXRS_LCD_CUSTOM::setBarState(int channel, int state)
{
  _h_bar[channel].setState(state);
}

// sets the full scale value for channels  (0-based) bar (if run time config desired)
void  OXRS_LCD_CUSTOM::setBarMaxValue(int channel, float value)
{
  _h_bar[channel].setMaxValue(value);
}


/*
 * keep track of timer
 */
void OXRS_LCD_CUSTOM::loop(void)
{   
}

void OXRS_LCD_CUSTOM::_clear_event()
{
  _ctft->fillRect(0, 225, 240, 240,  TFT_DARKGREY);
}

