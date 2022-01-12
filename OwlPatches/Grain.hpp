#include "SignalGenerator.h"
#include "Envelope.h"
#include "basicmaths.h"

class Grain : public SignalGenerator, MultiSignalGenerator
{
  float* left;
  float* right;
  const int bufferSize;
  const int bufferWrapMask;
  int preDelay;
  float ramp;
  float start;
  float size;
  float speed;
  float decayStart;
  float attackMult;
  float decayMult;
  float leftScale;
  float rightScale;

public:
  // buffer size argument must be power of two!
  Grain(float* inLeft, float* inRight, int bufferSz, int sr)
    : left(inLeft), right(inRight), bufferSize(bufferSz), bufferWrapMask(bufferSz - 1)
    , preDelay(0), ramp(randf()*bufferSize), start(0), decayStart(0)
    , size(bufferSize), speed(1), attackMult(0), decayMult(0)
    , leftScale(1), rightScale(1), isDone(true)
  {
  }
  
  bool isDone;

  inline float progress() const
  {
    return ramp / size;
  }

  inline float envelope() const
  {
    return ramp < decayStart ? ramp * attackMult : (size - ramp) * decayMult;
  }

  // all arguments [0,1], relative to buffer size,
  // env describes a blend from:
  // short attack / long decay -> triangle -> long attack / short delay
  // balance is only left channel at 0, only right channel at 1
  void trigger(int delay, float end, float length, float rate, float env, float balance, float velocity)
  {
    preDelay = delay;
    ramp = 0;
    size = length * bufferSize;
    // we always advance by buffer size
    // so we don't have to worry about accessing negative indices
    start = end * bufferSize - size + bufferSize;
    speed = rate;
    // convert -1 to 1
    balance = (balance * 2) - 1;
    leftScale = (balance < 0  ? 1 : 1.0f - balance) * velocity;
    rightScale = (balance > 0 ? 1 : 1.0f + balance) * velocity;

    float nextAttack = clamp(env, 0.01f, 0.99f);
    float nextDecay = 1.0f - nextAttack;
    decayStart = nextAttack * size;
    attackMult = 1.0f / (nextAttack*size);
    decayMult = 1.0f / (nextDecay*size);
    isDone = false;
  }

  float generate() override
  {
    if (preDelay)
    {
      --preDelay;
      return 0.0f;
    }

    const float pos = start + ramp;
    const int i = (int)pos;
    const int j = i + 1;
    const float t = pos - i;
    float sample = interpolated(left[i&bufferWrapMask], left[j&bufferWrapMask], t) * envelope();

    // keep looping, but silently, mainly so we can keep track of grain performance
    if ((ramp += speed) >= size)
    {
      ramp -= size;
      attackMult = decayMult = 0;
      isDone = true;
    }

    return sample;
  }


  void generate(AudioBuffer& output) override
  {
    int outLen = output.getSize();
    float* outL = output.getSamples(0);
    float* outR = output.getSamples(1);

    generate(outL, outR, outLen);
  }

  void generate(float* outL, float* outR, int outLen)
  {
    const int skip = min(preDelay, outLen);
    if (skip)
    {
      outL += skip;
      outR += skip;
      preDelay -= skip;
      outLen -= skip;
    }

    while(outLen--)
    {
      // setting all of these is basically free.
      // removing modulo and using ternary logic doesn't improve performance.
      const float pos = start + ramp;
      const float t = pos - (int)pos;
      const int i = ((int)pos) & bufferWrapMask;
      const int j = (i + 1) & bufferWrapMask;
      const float env = envelope();

      // biggest perf hit is here
      // time jumps from 50ns to 297ns uncommenting only one of these.
      // and then when adding the second channel, only to 380-400ns.
      // not sure where the extra time comes from with the first sample,
      // but probably something relating to array access?
      // doesn't seem to matter whether we access the member arrays or pass in arguments.
      *outL++ += interpolated(left[i], left[j], t) * env * leftScale;
      *outR++ += interpolated(right[i], right[j], t) * env * rightScale;

      // keep looping, but silently, mainly so we can keep track of grain performance
      // just this on its own is about 6ns per grain
      if ((ramp += speed) >= size)
      {
        ramp -= size;
        attackMult = decayMult = 0;
        isDone = true;
      }
    }
  }

private:

  inline float interpolated(float a, float b, float t) const
  {
    return a + t * (b - a);
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
