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
#define BAR_GAP       2
#define BAR_SEGMENTS  20 
#define BAR_MAX_VAL   1000.0

// Meter colour schemes
#define SOLID_RED   0
#define SOLID_GREEN 1
#define GREEN2RED   2

// Channel state
#define STATE_NA    0
#define STATE_OFF   1
#define STATE_ON    2
#define STATE_ALERT 3

class H_Bar
{
  public:
    H_Bar(void){};
    
    // index = -1 for "T"otal bar
    void begin(TFT_eSPI *tft, int y, int index = -1);

    void setValue(float value);
    void setState(int state);
    void setMaxValue(float value);

  private:  
    TFT_eSPI *_tft;
    int   _y;

    float _peak = -1;
    int   _state = STATE_NA;
    float _maxValue = BAR_MAX_VAL;

    void _drawIndex(int index);
    void _drawTotal();
    
    void _drawLinearMeter(int val, int x, int y, int w, int h, int g, int n, byte s);
    void _drawState(int state);
    void _drawValue(float val);
    
    uint16_t  _rainbowColor(uint8_t spectrum);
    float     _fscale( float inputValue, float originalMin, float originalMax, float newBegin, float newEnd, float curve);
};

#endif
