#include "Patch.h"
#include "SmoothValue.h"
#include "CircularBuffer.h"

#include "TapTempo.hpp"
#include "BitCrusher.hpp"

static const int TRIGGER_LIMIT = (1 << 17);

// these are expressed multiples of the clock
// and used to determine how long the frozen section of audio should be.
static const int FREEZE_RATIOS_COUNT = 9;
static const float freezeRatios[FREEZE_RATIOS_COUNT] = { 
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              2.0 / 3,
              1.0,
              3.0 / 2,
              2.0,
              3.0,
              4.0 
};

// these are the speeds at which the frozen audio should be played back.
// negative numbers mean the frozen audio should be played in reverse.
static const int PLAYBACK_SPEEDS_COUNT = 18;
static const float playbackSpeeds[PLAYBACK_SPEEDS_COUNT] = { 
             -4.0,
             -3.0,
             -2.0,
             -3.0 / 2,
             -1.0,
             -2.0 / 3,
             -1.0 / 2,
             -1.0 / 3,
             -1.0 / 4,
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              2.0 / 3,
              1.0,
              3.0 / 2,
              2.0,
              3.0,
              4.0 
};

// these are counters that indicate how many clock ticks should occur
// before resetting the read LFO when not frozen, in order to keep it in sync with the clock.
// it is a matrix because the period of the LFO, relative to the clock,
// is the speed divided by the freeze ratio
static const uint32_t counters[FREEZE_RATIOS_COUNT][PLAYBACK_SPEEDS_COUNT] = {
// speed: -4  -3  -2  -3/2  -1  -2/3  -1/2  -1/3  -1/4  1/4  1/3  1/2  2/3  1  3/2  2  3  4  |     freeze ratio
         { 1,  1,  1,   1,   1,   3,    1,    3,    1,   1,   3,   1,   3,  1,  1,  1, 1, 1  }, // 1/4
         { 1,  1,  1,   2,   1,   1,    2,    1,    4,   4,   1,   2,   1,  1,  2,  1, 1, 1  }, // 1/3
         { 1,  1,  1,   1,   1,   3,    1,    3,    2,   2,   3,   1,   3,  1,  1,  1, 1, 1  }, // 1/2
         { 1,  2,  1,   4,   2,   1,    4,    2,    8,   8,   2,   4,   1,  2,  4,  1, 2, 1  }, // 2/3
         { 1,  1,  1,   2,   1,   3,    2,    3,    4,   4,   3,   2,   3,  1,  2,  1, 1, 1  }, // 1
         { 3,  1,  3,   1,   3,   9,    3,    9,    6,   6,   9,   3,   9,  3,  1,  3, 1, 3  }, // 3/2
         { 1,  2,  1,   4,   2,   3,    4,    6,    8,   8,   6,   4,   3,  2,  4,  1, 2, 1  }, // 2
         { 3,  1,  3,   2,   3,   9,    6,    9,   12,  12,   9,   6,   9,  3,  2,  3, 1, 3  }, // 3
         { 1,  4,  2,   8,   4,   6,    8,   12,   16,  16,  12,   8,   6,  4,  8,  2, 4, 1  }, // 4
};

static const int DROP_RATIOS_COUNT = 8;
static const float dropRatios[DROP_RATIOS_COUNT] = { 1, 1.0/2, 1.0/3, 1.0/4, 1.0/6, 1.0/8, 1.0/12, 1.0/16 };

class GlitchLichPatch : public Patch
{
  const PatchParameterId inSize = PARAMETER_A;
  const PatchParameterId inSpeed = PARAMETER_B;
  const PatchParameterId inDrop = PARAMETER_C;
  const PatchParameterId inCrush = PARAMETER_D;
  const PatchParameterId outRamp = PARAMETER_F;
  const PatchParameterId outRand = PARAMETER_G;

  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  BitCrusher<24>* crushL;
  BitCrusher<24>* crushR;
  TapTempo<TRIGGER_LIMIT> tempo;
  int freezeRatio;
  int playbackSpeed;
  float freezeLength;
  bool freeze;
  int freezeWriteCount;
  int readStartIdx;
  float readLfo;
  float readSpeed;
  float dropLfo;
  bool dropSamples;
  float dropRand;

public:
  GlitchLichPatch()
    : bufferL(0), bufferR(0), crushL(0), crushR(0)
    , tempo(getSampleRate()*60/120), freeze(false)
    , freezeRatio(0), playbackSpeed(0)
  {
    bufferL = CircularBuffer<float>::create(TRIGGER_LIMIT);
    bufferR = CircularBuffer<float>::create(TRIGGER_LIMIT);
    crushL = BitCrusher<24>::create(getSampleRate(), getSampleRate());
    crushR = BitCrusher<24>::create(getSampleRate(), getSampleRate());

    readLfo = 0;
    readSpeed = 1;
    dropLfo = 0;

    registerParameter(inSize,  "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDrop,  "Drop");
    registerParameter(inCrush, "Crush");
    registerParameter(outRamp, "Ramp>");
    registerParameter(outRand, "Rand>");

    setParameterValue(inSize, 0.5f);
    setParameterValue(inSpeed, 0.75f);
    setParameterValue(inDrop, 0);
    setParameterValue(inCrush, 0);
  }

  ~GlitchLichPatch()
  {
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
    BitCrusher<24>::destroy(crushL);
    BitCrusher<24>::destroy(crushR);
  }


  float stepReadLFO(float speed)
  {
    readLfo = readLfo + speed;
    if (readLfo >= 1)
    {
      readLfo -= 1;
    }
    else if (readLfo < 0)
    {
      readLfo += 1;
    }
    return readLfo;
  }

  bool stepDropLFO(float speed)
  {
    dropLfo = dropLfo + speed;
    if (dropLfo >= 1)
    {
      dropLfo -= 1;
      return true;
    }
    else if (dropLfo < 0)
    {
      dropLfo += 1;
      return true;
    }
    return false;
  }

  inline float interpolatedReadAt(CircularBuffer<float>* buffer, float index) 
  {
    size_t idx = (size_t)index;
    float low = buffer->readAt(idx);
    float high = buffer->readAt(idx + 1);
    float frac = index - idx;
    return high + frac * (low - high);
  }

  float freezeDuration(int ratio)
  {
    float dur = tempo.getPeriod() * freezeRatios[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  float dropDuration(int ratio)
  {
    float dur = tempo.getPeriod() * dropRatios[ratio];
    dur = max(0.0001f, min(0.9999f, dur));
    return dur;
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    // button 2 is for tap tempo now
    bool mangle = false; // isButtonPressed(BUTTON_2);

    int size = audio.getSize();
    freezeRatio   = (int)(getParameterValue(inSize) * FREEZE_RATIOS_COUNT);
    playbackSpeed = (int)(getParameterValue(inSpeed) * PLAYBACK_SPEEDS_COUNT);

    tempo.clock(size);

    float newFreezeLength = freezeDuration(freezeRatio) * (TRIGGER_LIMIT - 1);
    float newReadSpeed    = playbackSpeeds[playbackSpeed] / newFreezeLength;

    float sr = getSampleRate();
    float crush = getParameterValue(inCrush);
    float bits = crush > 0.001f ? (8.f - crush * 6) 
                                : 24;
    float rate = crush > 0.001f ? sr*0.25f + getParameterValue(inCrush)*(100 - sr*0.25f) 
                                : sr;
    crushL->setBitDepth(bits);
    crushL->setBitRate(rate);
    crushL->setMangle(mangle);
    crushR->setBitDepth(bits);
    crushR->setBitRate(rate);
    crushR->setMangle(mangle);

    for (int i = 0; i < size; ++i)
    {
      if (freeze && freezeWriteCount == TRIGGER_LIMIT)
        break;

      bufferL->write(left[i]);
      bufferR->write(right[i]);

      if (freeze)
        ++freezeWriteCount;
    }

    for (int i = 0; i < size; ++i)
    {
      float x1 = (float)i / size;
      float x0 = 1.0f - x1;
      if (freeze)
      {
        float read0 = readStartIdx + readLfo * freezeLength;
        float read1 = readStartIdx + readLfo * newFreezeLength;
        left[i]  = interpolatedReadAt(bufferL, read0)*x0 
                 + interpolatedReadAt(bufferL, read1)*x1;
        right[i] = interpolatedReadAt(bufferR, read0)*x0 
                 + interpolatedReadAt(bufferR, read1)*x1;
      }
      stepReadLFO(readSpeed*x0 + newReadSpeed*x1);
    }

    freezeLength = newFreezeLength;
    readSpeed = newReadSpeed;

    crushL->process(left, left);
    crushR->process(right, right);

    float dropParam = getParameterValue(inDrop);
    float dropMult = dropParam * DROP_RATIOS_COUNT;
    float dropSpeed = 1.0f / (dropDuration((int)dropMult) * (TRIGGER_LIMIT - 1));
    float dropProb = dropParam < 0.0001f ? 0 : 0.1f + 0.9*dropParam;
    for (int i = 0; i < size; ++i)
    {
      if (stepDropLFO(dropSpeed))
      {
        dropRand = randf();
        dropSamples = dropRand < dropProb;
      }

      if (dropSamples)
      {
        left[i] = 0;
        right[i] = 0;
      }
    }

    setParameterValue(outRamp, readLfo);
    setParameterValue(outRand, dropRand);
    setButton(PUSHBUTTON, readLfo < 0.5f);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    static uint32_t counter = 0;

    if (bid == BUTTON_1)
    {
      if (value == ON)
      {
        freeze = true;
        freezeWriteCount = samples;
        readStartIdx = bufferL->getWriteIndex() - samples;
        if (readStartIdx < 0)
        {
          readStartIdx += TRIGGER_LIMIT;
        }
        readLfo = 0;
      }
      else
      {
        freeze = false;
      }
    }

    if (bid == BUTTON_2)
    {
      bool on = value == ON;
      tempo.trigger(on, samples);
      if (on && !freeze && ++counter >= counters[freezeRatio][playbackSpeed])
      {
        readLfo = 0;
        counter = 0;
      }
      // dropLfo is never slower than the clock, so always reset it
      if (on)
      {
        dropLfo = 0;
      }
    }
  }

};
