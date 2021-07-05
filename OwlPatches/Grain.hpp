#include "SignalGenerator.h"
#include "basicmaths.h"
#include "Envelope.h"

template<typename T>
class Grain : public SignalGenerator, MultiSignalGenerator
{
  T* left;
  T* right;
  int bufferSize;
  int sampleRate;
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
  Grain(T* inLeft, T* inRight, int bufferSz, int sr)
    : left(inLeft), right(inRight), bufferSize(bufferSz)
    , sampleRate(sr), ramp(randf()*bufferSize), start(0), decayStart(0)
    , size(bufferSize), speed(1), attackMult(0), decayMult(0)
    , leftScale(1), rightScale(1)
  {
  }

  inline bool isDone() const
  {
    return attackMult == 0 && decayMult == 0;
  }

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
  void trigger(float end, float length, float rate, float env, float balance, float velocity)
  {
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

    float nextAttack = max(0.01f, min(env, 0.99f));
    float nextDecay = 1.0f - nextAttack;
    decayStart = nextAttack * size;
    attackMult = 1.0f / (nextAttack*size);
    decayMult = 1.0f / (nextDecay*size);
  }

  float generate() override
  {
    float pos = start + ramp;
    float sample = left->readAt(pos) * envelope();

    // keep looping, but silently, mainly so we can keep track of grain performance
    if ((ramp += speed) >= size)
    {
      ramp -= size;
      attackMult = decayMult = 0;
    }

    return sample;
  }


  void generate(AudioBuffer& output) override
  {
    const int outLen = output.getSize();
    float* outL = output.getSamples(0);
    float* outR = output.getSamples(1);
    for (int s = 0; s < outLen; ++s)
    {
      float pos = start + ramp;
      float env = envelope();

      *outL++ += left->readAt(pos) * env * leftScale;
      *outR++ += right->readAt(pos) * env * rightScale;

      // keep looping, but silently, mainly so we can keep track of grain performance
      if ((ramp += speed) >= size)
      {
        ramp -= size;
        attackMult = decayMult = 0;
      }
    }
  }

private:

  //float interpolated(float* buffer, int i, int j, float t) const
  //{
  //  float low = buffer[i];
  //  float high = buffer[j];
  //  return low + t * (high - low);
  //}

public:
  static Grain<T>* create(T* buffer, int size, int sampleRate)
  {
    return new Grain<T>(buffer, buffer, size, sampleRate);
  }

  static Grain<T>* create(T* left, T* right, int size, int sampleRate)
  {
    return new Grain<T>(left, right, size, sampleRate);
  }

  static void destroy(Grain<T>* grain)
  {
    delete grain;
  }

};
