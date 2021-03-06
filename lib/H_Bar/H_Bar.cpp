/*
 * Inspired by 
 *  https://forum.arduino.cc/t/tft_espi-new-example-for-animated-dials/643382/2
 *  https://playground.arduino.cc/Main/Fscale/
 */

#include "H_Bar.h"

void H_Bar::begin(TFT_eSPI *tft, int y, int index)
{
  _tft = tft;
  _y = y;

  // Index is 1-based, set to 0 for "T"otal bar
  if (index == 0)
  {
    _drawTotal();
  }
  else
  {
    _drawIndex(index);
  }
  
  _drawMeter(0, BAR_X, y, BAR_W, BAR_H, BAR_GAP, BAR_SEGMENTS, GREEN2RED);
  _drawValue(0, VALUE_X_A, DP_A, "A");

  if (index != 0)
  {
    // Only display voltage and state for outputs (not "T"otal bar)
    _drawValue(0, VALUE_X_V, DP_V, "V");
    _drawState(_state);
  }  
}

void H_Bar::setMaxValue(float mA)
{
  // Sets the max current allowed and thus the limit of our bar graph
  _max_mA = mA;
}

void H_Bar::setValue(float mA, float mV)
{
  int bar_mA = (int)(BAR_SEGMENTS * mA / _max_mA + .9);
  
  _drawMeter(bar_mA, BAR_X, _y, BAR_W, BAR_H, BAR_GAP, BAR_SEGMENTS, GREEN2RED);
  _drawValue(mA / 1000.0, VALUE_X_A, DP_A, "A");

  if (!isnan(mV))
  {
    _drawValue(mV / 1000.0, VALUE_X_V, DP_V, "V");
  }
}

void H_Bar::setState(int state)
{
  _state = state;
  
  _drawState(_state);
}

/*
 * Draw the linear meter
 * mA   =  reading to show (range is 0 to n)
 * x, y = position of top left corner
 * w, h = width and height of a single bar
 * g    = pixel gap to next bar (can be 0)
 * n    = number of segments
 * s    = color scheme
 */
void H_Bar::_drawMeter(int mA, int x, int y, int w, int h, int g, int n, byte s)
{
  // Keep track of our peak
  if (mA > _peak_mA) {_peak_mA = mA;}
  
  // Draw n color blocks
  for (int b = 1; b <= n; b++)
  {
    int color = TFT_BLUE;
    
    if (mA > 0 && b <= mA)
    { 
      // Determine colour of active segment
      switch (s)
      {
        case SOLID_RED  : color = TFT_RED; break; // Fixed color
        case SOLID_GREEN: color = TFT_GREEN; break; // Fixed color
        case GREEN2RED  : color = _rainbowColor(_fscale(b, 0, n, 63, 0, -5)); break; // Green to red
      }
    }
    else 
    {
      // Determine colour of blank segment or peak value
      color = (b == _peak_mA) ? TFT_CYAN : TFT_DARKGREY;
    }

    // Draw the block
    _tft->fillRect(x + b*(w+g), y, w, h, color);
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

void H_Bar::_drawIndex(int index)
{
  _tft->setTextFont(1);
  _tft->setTextDatum(TL_DATUM);
  _tft->drawNumber(index, 0, _y+1);    
}

void H_Bar::_drawTotal()
{
  _tft->setTextFont(1);
  _tft->setTextDatum(TL_DATUM);
  _tft->drawString("T", 0, _y+1);
}

void H_Bar::_drawValue(float value, int x, int dp, const char * units)
{
  // create sprite to draw Value
  TFT_eSprite _spr = TFT_eSprite(_tft);   
  _spr.createSprite(VALUE_W, VALUE_H, 2);

  _spr.fillSprite(TFT_BLACK);
  _spr.setTextFont(1);
  _spr.setTextDatum(TR_DATUM);
  _spr.drawFloat(value, dp, VALUE_X_OFFS, 1);
  _spr.setTextDatum(TL_DATUM);
  _spr.drawString(units, VALUE_X_OFFS+2, 1); 
  _spr.pushSprite(x, _y); 
}

void H_Bar::_drawState(int state)
{
  // create sprite to draw state 
  TFT_eSprite _spr = TFT_eSprite(_tft);   
  _spr.createSprite(STATE_W, STATE_H, 2);

  _spr.setTextFont(1);
  _spr.setTextDatum(TL_DATUM);

  switch (state)
  {
    case STATE_NA:
      _spr.fillSprite(TFT_DARKGREY);
      _spr.setTextColor(TFT_WHITE);
      _spr.drawString("N/A", STATE_X_OFFS, 1); 
      _peak_mA = -1;
      break;
    case STATE_OFF:
      _spr.fillSprite(TFT_DARKGREY);
      _spr.setTextColor(TFT_WHITE);
      _spr.drawString("OFF", STATE_X_OFFS, 1); 
      _peak_mA = -1;
      break;
    case STATE_ON:
      _spr.fillSprite(TFT_GREEN);
      _spr.setTextColor(TFT_BLACK);
      _spr.drawString("ON", STATE_X_OFFS, 1); 
      break;
    case STATE_ALERT:
      _spr.fillSprite(TFT_RED);
      _spr.setTextColor(TFT_WHITE);
      _spr.drawString("ALERT", STATE_X_OFFS, 1); 
      break;
   }
  _spr.pushSprite(STATE_X, _y); 
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
