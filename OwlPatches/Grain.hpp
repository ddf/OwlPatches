#include "SignalGenerator.h"
#include "basicmaths.h"

class Grain : public SignalGenerator
{
  FloatArray buffer;
  int bufferSize;
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
  Grain(float* inBuffer, int bufferSz, int sr)
    : buffer(inBuffer, bufferSz), bufferSize(bufferSz), sampleRate(sr)
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
    nextSize = grainSize * bufferSize;
    nextSize = max(2, min(nextSize, bufferSize));
  }

  float generate() override
  {
    // TODO use an ADSR or window here instead of sinf
    float sample = start == -1 ? 0 : interpolated(start + ramp * size) * sinf(ramp*M_PI);
    ramp += stepSize;
    if (ramp >= 1)
    {
      ramp -= 1;
      start = randf() < density ? randf()*bufferSize : -1;
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
    float low = buffer[i%bufferSize];
    float high = buffer[j%bufferSize];

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
