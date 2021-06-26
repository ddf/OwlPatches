#include "SignalGenerator.h"
#include "basicmaths.h"
#include "Envelope.h"

class Grain : public SignalGenerator
{
  AdsrEnvelope envelope;
  FloatArray buffer;
  int bufferSize;
  int sampleRate;
  float stepSize;
  float ramp;
  float phase;
  float start;
  float density;
  float size;
  float speed;
  float nextSize;
  float nextSpeed;
  float nextAttack;
  float nextDecay;

public:
  Grain(float* inBuffer, int bufferSz, int sr)
    : envelope(sr), buffer(inBuffer, bufferSz), bufferSize(bufferSz), sampleRate(sr)
    , ramp(randf()), stepSize(0), phase(0)
    , start(-1), density(0.5f), size(bufferSize*0.1f), speed(1)
    , nextSize(size), nextSpeed(speed), nextAttack(0.5f), nextDecay(0.5f)
  {
    envelope.setSustain(0);
    envelope.setRelease(0);
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

  void setPhase(float grainPhase)
  {
    phase = grainPhase*bufferSize;
  }

  void setAttack(float dur)
  {
    nextAttack = max(0.01f, min(dur, 0.99f));
    nextDecay = 1.0f - nextAttack;
  }

  float generate() override
  {
    float sample = interpolated(start + ramp * size) * envelope.generate();
    ramp += stepSize;
    if (ramp >= 1)
    {
      ramp -= 1;
      if (randf() < density)
      {
        setStepSize();
        envelope.setLevel(0);
        envelope.trigger();
        start = size > phase ? phase - size + bufferSize : phase - size;
      }
    }
    return sample;
  }

private:

  void setStepSize()
  {
    speed = nextSpeed;
    size = nextSize;
    stepSize = speed / size;
    const float grainLengthInSeconds = (size / sampleRate) / speed;
    envelope.setAttack(nextAttack * grainLengthInSeconds);
    envelope.setDecay(nextDecay * grainLengthInSeconds);
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
