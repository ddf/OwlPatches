#include "SignalGenerator.h"
#include "basicmaths.h"
#include "Envelope.h"

class Grain : public SignalGenerator
{
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
  float attack;
  float decay;
  float nextSize;
  float nextSpeed;
  float nextAttack;
  float nextDecay;

public:
  Grain(float* inBuffer, int bufferSz, int sr)
    : buffer(inBuffer, bufferSz), bufferSize(bufferSz), sampleRate(sr)
    , ramp(randf()), stepSize(0), phase(0), start(-1)
    , density(0.5f), size(bufferSize*0.1f), speed(1), attack(0.5f), decay(0.5f)
    , nextSize(size), nextSpeed(speed), nextAttack(attack), nextDecay(decay)
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
    // TODO: using an ADSR I can only get 16 grains and still have CPU headroom for other features.
    // Probably it would be faster to do our own AD envelope in this class.
    // Could also run ramp from 0 to size to eliminate an multiply here.
    float sample = interpolated(start + ramp * size) * envelope();
    ramp += stepSize;
    if (ramp >= 1)
    {
      ramp -= 1;
      if (randf() < density)
      {
        setStepSize();
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
    attack = nextAttack;
    decay = nextDecay;
    stepSize = speed / size;
  }

  float envelope()
  {
    return ramp < attack ? ramp / attack : (1.0f - ramp) / decay;
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
