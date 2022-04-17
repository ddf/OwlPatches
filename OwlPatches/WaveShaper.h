#include "SignalProcessor.h"
#include "Interpolator.h"

class WaveShaper : public SignalProcessor
{
  FloatArray waveTable;
  int tableSize;

public:
  WaveShaper(FloatArray waveTable) : waveTable(waveTable), tableSize(waveTable.getSize())
  {

  }

  float process(float input) override
  {
    float tscale = clamp((input * 0.5f + 0.5f)*0.99f, 0.0f, 1.0f);
    float sidx = tableSize*tscale;
    int lidx = (int)sidx;
    int hidx = lidx + 1;
    float t = sidx - lidx;
    return Interpolator::linear(waveTable[lidx%tableSize], waveTable[hidx%tableSize], t);
  }

  static WaveShaper* create(FloatArray waveTable)
  {
    return new WaveShaper(waveTable);
  }

  static void destroy(WaveShaper* shaper)
  {
    delete shaper;
  }
};
