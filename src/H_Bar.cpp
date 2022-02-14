/*
 * inspired by 
 * https://forum.arduino.cc/t/tft_espi-new-example-for-animated-dials/643382/2
 * and
 * https://playground.arduino.cc/Main/Fscale/
 */

#include "H_Bar.h"


void H_Bar::begin(TFT_eSPI *tft, int channel, int y)
{
  _tft = tft;
  _y = y;
  _peak = -1;
  _state = CHANNEL_NA;
  _barMaxValue = BAR_MAX_VAL;
  _drawChannelNumber(channel);
  _drawLinearMeter(0, BAR_X, y, BAR_W, BAR_H, BAR_GAP, BAR_SEGMENTS, GREEN2RED);
  _drawValue(0.0);
  _drawState(_state);
}

void H_Bar::setValue(float value)
{
  int bar_val = (int)(BAR_SEGMENTS * value / _barMaxValue + .9);
  _drawLinearMeter(bar_val, BAR_X, _y, BAR_W, BAR_H, BAR_GAP, BAR_SEGMENTS, GREEN2RED);
  _drawValue (value);
}

void H_Bar::setState(int state)
{
  _state = state;
  _drawState(_state);
}

void H_Bar::setMaxValue(float value)
{
  _barMaxValue = value;
}

/*
 * Draw the linear meter
 * val =  reading to show (range is 0 to n)
 * x, y = position of top left corner
 * w, h = width and height of a single bar
 * g    = pixel gap to next bar (can be 0)
 * n    = number of segments
 * s    = color scheme
 */
void H_Bar::_drawLinearMeter(int val, int x, int y, int w, int h, int g, int n, byte s)
{
  int color = TFT_BLUE;
  if (val > _peak) {_peak = val;}
  // Draw n color blocks
  for (int b = 1; b <= n; b++)
  {
    if (val > 0 && b <= val)
    // Fill in colored blocks
    { 
      switch (s)
      {
        case SOLID_RED  : color = TFT_RED; break; // Fixed color
        case SOLID_GREEN: color = TFT_GREEN; break; // Fixed color
        case GREEN2RED  : color = _rainbowColor(_fscale(b, 0, n,  63,   0, -5)); break; // Green to red
      }
      _tft->fillRect(x + b*(w+g), y, w, h, color);
    }
    else 
    // Fill in blank segments or peak value
    {
      color = (b == _peak) ? TFT_CYAN : TFT_DARKGREY;
      _tft->fillRect(x + b*(w+g), y, w, h, color);
    }
  }
}

/*
 * 'spectrum' is in the range 0-63. It is converted to a spectrum color
 * from 0 = green through to 63 = red
 */
uint16_t H_Bar::_rainbowColor(uint8_t spectrum)
{
  spectrum = spectrum%192;
  
  uint8_t red   = 0; // Red is the top 5 bits of a 16 bit color spectrum
  uint8_t green = 0; // Green is the middle 6 bits, but only top 5 bits used here
  uint8_t blue  = 0; // Blue is the bottom 5 bits

  uint8_t sector = spectrum >> 5;
  uint8_t amplit = spectrum & 0x1F;

  switch (sector)
  {
    case 0:
      red   = 0x1F;
      green = amplit; // Green ramps up
      blue  = 0;
      break;
    case 1:
      red   = 0x1F - amplit; // Red ramps down
      green = 0x1F;
      blue  = 0;
      break;
  }
  return red << 11 | green << 6 | blue;
}

void H_Bar::_drawChannelNumber(int channel)
{
  _tft->setTextFont(1);
  if(channel < 8)
  {
    _tft->drawNumber(channel+1, 0, _y+1);
  }
  else
  {
    _tft->drawString("T", 0, _y+1);
  }
}

void H_Bar::_drawState (int state)
{
  _tft->setTextFont(1);
  switch (state)
  {
    case CHANNEL_NA:
      _tft->setTextColor(TFT_WHITE);
      _tft->fillRect(190, _y, 40, 9, TFT_DARKGREY);
      _tft->drawString("N/A", 195, _y+1); 
      _peak = -1;
      break;
    case CHANNEL_OFF:
      _tft->setTextColor(TFT_WHITE);
      _tft->fillRect(190, _y, 40, 9, TFT_DARKGREY);
      _tft->drawString("OFF", 195, _y+1); 
      _peak = -1;
      break;
    case CHANNEL_ON:
      _tft->setTextColor(TFT_BLACK);
      _tft->fillRect(190, _y, 40, 9, TFT_GREEN);
      _tft->drawString("ON", 195, _y+1); 
      break;
    case CHANNEL_FAULT:
      _tft->setTextColor(TFT_WHITE);
      _tft->fillRect(190, _y, 40, 9, TFT_RED);
      _tft->drawString("FAULT", 195, _y+1); 
      _drawLinearMeter(BAR_SEGMENTS, BAR_X, _y, BAR_W, BAR_H, BAR_GAP, BAR_SEGMENTS, SOLID_RED);
      break;
   }
  _tft->setTextColor(TFT_WHITE);
}

void H_Bar::_drawValue (float val)
{
  uint8_t actualDatum = _tft->getTextDatum();
  _tft->setTextFont(1);
  _tft->fillRect(180-45, _y, 45, 9, TFT_BLACK);
  _tft->setTextDatum(TR_DATUM);
  _tft->drawFloat(val, 2, 180, _y+1);
  _tft->setTextDatum(actualDatum);
}

/*
 * transform inputValue from originalRange to outputValue from newRange
 * curve = 0 : linear     |gggggyrrrrr|
 * curve = - : late red   |ggggggggyrr| 
 * curve = + : early red  |ggyrrrrrrrr|
 */
float  H_Bar::_fscale( float inputValue, float originalMin, float originalMax, float newBegin, float newEnd, float curve)
{
  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;

  // condition curve parameter
  // limit range

  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;

  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  // Check for out of range inputValues
  if (inputValue < originalMin)
  {
    inputValue = originalMin;
  }
  if (inputValue > originalMax)
  {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin)
  {
    NewRange = newEnd - newBegin;
  }
  else
  {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }

  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax )
  {
    return 0;
  }

  if (invFlag == 0)
  {
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;
  }
  else     // invert the ranges
  {  
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }

  return rangedValue;
}
