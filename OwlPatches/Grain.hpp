#include "SignalGenerator.h"
#include "RampOscillator.h"
#include "basicmaths.h"

class Grain : public SignalGenerator
{
  FloatArray buffer;
  RampOscillator ramp;
  float start;
  float size;
  float lastRead;

public:
  Grain(FloatArray inBuffer, int sampleRate)
    : buffer(inBuffer), ramp(1, sampleRate)
    , start(0), size(inBuffer.getSize()*0.1f)
    , lastRead(1)
  {
    ramp.setFrequency(1.0f / size);
  }

  float generate() override
  {
    float read = ramp.generate()*0.5f + 0.5f;
    if (read < lastRead)
    {
      if (randf() < 0.5f)
      {
        start = randf()*buffer.getSize();
      }
      else
      {
        start = -1;
      }
    }
    lastRead = read;
    if (start >= 0)
    {
      return interpolated(start + read);
    }
    else
    {
      return 0;
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
