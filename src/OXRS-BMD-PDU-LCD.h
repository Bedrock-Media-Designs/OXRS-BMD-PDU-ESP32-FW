/*
 * OXRS-BMD-PDU-LCD.h
 * 
 */

#ifndef OXRS_BMD_PDU_LCD_H
#define OXRS_BMD_PDU_LCD_H

#include <TFT_eSPI.h>               // Hardware-specific library
#include "H_Bar.h"

#define     BAR_COUNT                   9

 
class OXRS_LCD_CUSTOM
{
  public:
    OXRS_LCD_CUSTOM(){};
    
    // initialises and shows 9 horizontal bars, all states N/A
    void drawBars(void);
    // value to be shown for channel (0-based) (bar and numeric)
    void setBarValue(int channel, float value);
    // state to be shown for channel (0-based) (N/A, OFF, ON, FAULT, ...)
    void setBarState(int channel, int state);
    // sets the full scale value for channels (0-based) bar (if run time config desired)
    void setBarMaxValue(int channel, float value);

    void begin(TFT_eSPI* tft);
    void loop(void);  
    
  private:  
    void _clear_event(void);
};

#endif
