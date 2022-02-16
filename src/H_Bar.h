/*
 * H_Bar.h
 * 
 */

#ifndef H_BAR_H
#define H_BAR_H

#include <TFT_eSPI.h>               // Hardware-specific library

// single bar definitions
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

// channel state
#define CHANNEL_NA    0
#define CHANNEL_OFF   1
#define CHANNEL_ON    2
#define CHANNEL_FAULT 3

class H_Bar
{
  public:
    H_Bar(void){};
    void begin(TFT_eSPI *tft, int channel, int y);
    void setValue(float value);
    void setState(int state);
    void setMaxValue(float value);


  private:  
    TFT_eSPI *_tft;

    float _peak;
    int   _y;
    int   _state;
    float _barMaxValue;

    void _drawLinearMeter(int val, int x, int y, int w, int h, int g, int n, byte s);
    void _drawChannelNumber(int channel);
    void _drawState(int state);
    void _drawValue(float val);
    
    uint16_t  _rainbowColor(uint8_t spectrum);
    float     _fscale( float inputValue, float originalMin, float originalMax, float newBegin, float newEnd, float curve);
 
};

#endif
