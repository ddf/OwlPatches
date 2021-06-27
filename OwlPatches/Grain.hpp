#include "SignalGenerator.h"
#include "basicmaths.h"
#include "Envelope.h"

class Grain : public SignalGenerator, MultiSignalGenerator
{
  FloatArray left;
  FloatArray right;
  int bufferSize;
  int sampleRate;
  float ramp;
  float phase;
  float start;
  float density;
  float size;
  float speed;
  float decayStart;
  float attackMult;
  float decayMult;
  float nextSize;
  float nextSpeed;
  float nextAttack;
  float nextDecay;

public:
  Grain(float* inLeft, float* inRight, int bufferSz, int sr)
    : left(inLeft, bufferSz), right(inRight, bufferSz), bufferSize(bufferSz)
    , sampleRate(sr), ramp(randf()), phase(0), start(0), decayStart(0)
    , density(0.5f), size(bufferSize*0.1f), speed(1), attackMult(0), decayMult(0)
    , nextSize(size), nextSpeed(speed), nextAttack(attackMult), nextDecay(decayMult)
  {
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
    float sample = interpolated(left, start + ramp) * envelope();
    ramp += speed;
    if (ramp >= size)
    {
      startGrain();
    }
    return sample;
  }


  void generate(AudioBuffer& output) override
  {
    const int outLen = output.getSize();
    float* outL = output.getSamples(0);
    float* outR = output.getSamples(1);
    for (int i = 0; i < outLen; ++i)
    {
      float env = envelope();
      *outL++ = interpolated(left, start + ramp) * env;
      *outR++ = interpolated(right, start + ramp) * env;
      ramp += speed;
      if (ramp >= size)
      {
        startGrain();
      }
    }
  }

private:

  void startGrain()
  {
    speed = nextSpeed;
    size = nextSize;
    decayStart = nextAttack * size;
    attackMult = 1.0f / (nextAttack*size);
    decayMult = 1.0f / (nextDecay*size);
    ramp = 0;
    if (randf() < density)
    {
      start = size > phase ? phase - size + bufferSize : phase - size;
    }
    else
    {
      attackMult = decayMult = 0;
    }
  }

  float envelope()
  {
    return ramp < decayStart ? ramp * attackMult : (size - ramp) * decayMult;
  }

  float interpolated(float* buffer, float index)
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
    return new Grain(buffer, buffer, size, sampleRate);
  }

  static Grain* create(float* left, float* right, int size, int sampleRate)
  {
    return new Grain(left, right, size, sampleRate);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }

};
