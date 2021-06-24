#include "SignalGenerator.h"
#include "basicmaths.h"

class Grain : public SignalGenerator
{
  FloatArray buffer;
  int sampleRate;
  float stepSize;
  float ramp;
  float start;
  float density;
  float size;
  float speed;
  float nextSize;
  float nextSpeed;

public:
  Grain(float* inBuffer, int bufferSize, int sr)
    : buffer(inBuffer, bufferSize), sampleRate(sr)
    , ramp(randf()), stepSize(0)
    , start(-1), density(0.5f), size(bufferSize*0.1f), speed(1)
    , nextSize(size), nextSpeed(speed)
  {
    setStepSize();
  }

  void setSpeed(float speed)
  {
    nextSpeed = speed;
  }

  void setDensity(float density)
  {
    this->density = density;
  }

  void setSize(float grainSize)
  {
    nextSize = grainSize * buffer.getSize();
    nextSize = max(2, min(nextSize, buffer.getSize()));
  }

  float generate() override
  {
    float sample = start >= 0 ? interpolated(start + ramp * size) * sinf(ramp*M_PI)  : 0;
    ramp += stepSize;
    if (ramp >= 1)
    {
      ramp -= 1;
      if (randf() < density)
      {
        start = randf()*buffer.getSize();
      }
      else
      {
        start = -1;
      }
      setStepSize();
    }
    return sample;
  }
private:

  void setStepSize()
  {
    speed = nextSpeed;
    size = nextSize;
    stepSize = speed / size;
  }

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
    return new Grain(buffer, size, sampleRate);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }

};
