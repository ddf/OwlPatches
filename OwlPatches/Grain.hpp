#include "SignalGenerator.h"
#include "Envelope.h"
#include "FloatArray.h"
#include "basicmaths.h"

// tried out recording to a sample buffer of shorts,
// which requires converting back to float when a grain reads from the buffer.
// this turned out to be quite a bit slower than just operating on the float buffer.
#if 1
#include "ComplexFloatArray.h"
typedef ComplexFloat Sample;
#define SampleToFloat 1
#define FloatToSample 1
#else
#include "ComplexShortArray.h"
typedef ComplexShort Sample;
#define SampleToFloat 0.0000305185f // 1 / 32767
#define FloatToSample 32767
#endif

class Grain : public SignalGenerator, MultiSignalGenerator
{
  Sample* buffer;
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
  Grain(Sample* inBuffer, int bufferSz)
    : buffer(inBuffer), bufferSize(bufferSz), bufferWrapMask(bufferSz - 1)
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
    float sample = interpolated(buffer[i&bufferWrapMask].re, buffer[j&bufferWrapMask].re, t) * envelope();

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
    FloatArray outL = output.getSamples(0);
    FloatArray outR = output.getSamples(1);

    generate<false>(outL, outR, outLen);
  }

  template<bool clear>
  void generate(FloatArray genLeft, FloatArray genRight, int genLen)
  {
    const int skip = min(preDelay, genLen);
    if (skip)
    {
      preDelay -= skip;
      genLen -= skip;
      if constexpr(clear) {
        genLeft.subArray(0, skip).clear();
        genRight.subArray(0, skip).clear();
      }
      genLeft = genLeft.subArray(skip, genLen);
      genRight = genRight.subArray(skip, genLen);
    }

    float* outL = genLeft.getData();
    float* outR = genRight.getData();

    while(genLen--)
    {
      // setting all of these is basically free.
      // removing modulo and using ternary logic doesn't improve performance.
      const float pos = start + ramp;
      const float t = pos - (int)pos;
      const int i = ((int)pos) & bufferWrapMask;
      const int j = (i + 1) & bufferWrapMask;
      const float env = envelope();
      const Sample si = buffer[i];
      const Sample sj = buffer[j];

      // biggest perf hit is here
      // time jumps from 50ns to 297ns uncommenting only one of these.
      // and then when adding the second channel, only to 380-400ns.
      // not sure where the extra time comes from with the first sample,
      // but probably something relating to array access?
      // doesn't seem to matter whether we access the member arrays or pass in arguments.
      // on the forums it was pointed out that accessing the array is just slow because it lives in SDRAM.
      if constexpr(clear) {
        *outL++ = interpolated(si.re, sj.re, t) * env * leftScale;
        *outR++ = interpolated(si.im, sj.im, t) * env * rightScale;
      }
      else {
        *outL++ += interpolated(si.re, sj.re, t) * env * leftScale;
        *outR++ += interpolated(si.im, sj.im, t) * env * rightScale;
      }

      // keep looping, but silently, mainly so we can keep track of grain performance
      // just this on its own is about 6ns per grain
      if ((ramp += speed) >= size)
      {
        ramp = size;
        //ramp -= size;
        //attackMult = decayMult = 0;
        isDone = true;
        if constexpr(clear) {
          memset(outL, 0, sizeof(float) * genLen);
          memset(outR, 0, sizeof(float) * genLen);
        }
        break;
      }
    }
  }

private:

  inline float interpolated(float a, float b, float t) const
  {
    return (a + t * (b - a)) * SampleToFloat;
  }

public:
  static Grain* create(Sample* buffer, int size)
  {
    return new Grain(buffer, size);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }
};
