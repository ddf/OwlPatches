#include "SignalGenerator.h"
#include "basicmaths.h"

class Grain : public SignalGenerator
{
  FloatArray buffer;
  int sampleRate;
  float stepSize;
  float ramp;
  float start;
  float size;

public:
  Grain(FloatArray inBuffer, int sr)
    : buffer(inBuffer), sampleRate(sr)
    , ramp(0), stepSize(0)
    , start(-1), size(inBuffer.getSize()*0.1f)
  {
    setSpeed(1);
  }

  void setSpeed(float speed)
  {
    stepSize = speed / size;
  }

  float generate() override
  {
    float sample = start >= 0 ? interpolated(start + ramp * size) : 0;
    ramp += stepSize;
    if (ramp >= stepSize)
    {
      ramp -= 1;
      if (randf() < 0.5f)
      {
        start = randf()*buffer.getSize();
      }
      else
      {
        start = -1;
      }
    }
  }
private:

  float interpolated(float index)
  {
    int i = (int)index;
    int j = (i + 1);
    float low = buffer[i%buffer.getSize()];
    float high = buffer[j%buffer.getSize()];

    float frac = index - i;
    return high + frac * (low - high);
  }

public:
  static Grain* create(float* buffer, int size, int sampleRate)
  {
    return new Grain(FloatArray(buffer, size), sampleRate);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }

};
