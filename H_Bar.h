/*
 * H_Bar.h
 * 
 */

#ifndef H_BAR_H
#define H_BAR_H

#include <TFT_eSPI.h>               // Hardware-specific library

// Single bar definitions
#define BAR_X         8
#define BAR_W         4
#define BAR_H         9
#define BAR_GAP       1
#define BAR_SEGMENTS  20 
#define BAR_MAX_MA    2000.0

// Value 'x' locations (current/volts)
#define VALUE_X_A     150
#define VALUE_X_V     190
#define VALUE_W       25
#define VALUE_H       9

// State 'x' location
#define STATE_X       205
#define STATE_X_TXT   208
#define STATE_W       40
#define STATE_H       9

// Meter colour schemes
#define SOLID_RED     0
#define SOLID_GREEN   1
#define GREEN2RED     2

// Channel state
#define STATE_NA      0
#define STATE_OFF     1
#define STATE_ON      2
#define STATE_ALERT   3

// Decimal places
#define DP_A          2
#define DP_V          1

class H_Bar
{
  public:
    H_Bar(void){};
    
    // Index is 1-based, set to 0 for "T"otal bar
    void begin(TFT_eSPI *tft, int y, int index = 0);

    void setMaxValue(float mA);
    void setValue(float mA, float mV = NAN);
    void setState(int state);

  private:  
    TFT_eSPI *_tft;
    int   _y;

    int   _state   = STATE_NA;
    float _peak_mA = -1;
    float _max_mA  = BAR_MAX_MA;

    void _drawIndex(int index);
    void _drawTotal();
    
    void _drawMeter(int mA, int x, int y, int w, int h, int g, int n, byte s);
    void _drawValue(float value, int x, int dp, const char * units);
    void _drawState(int state);
    
    uint16_t _rainbowColor(uint8_t spectrum);
    float    _fscale( float inputValue, float originalMin, float originalMax, float newBegin, float newEnd, float curve);
};

#endif
