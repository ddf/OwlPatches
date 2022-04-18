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
    float tscale = fmodf(input * 0.5f + 0.5f, 1.0f);
    if (tscale < 0)
    {
      tscale += 1.0f;
    }
    float sidx = (tableSize-1)*tscale;
    int lidx = (int)sidx;
    int hidx = lidx + 1;
    float t = sidx - lidx;
    return Interpolator::linear(waveTable[lidx], waveTable[hidx], t);
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
