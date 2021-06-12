#include "Patch.h"
#include "SmoothValue.h"
#include "CircularBuffer.h"

#include "TapTempo.hpp"
#include "BitCrusher.hpp"

static const int glitchDropRateCount = 8;
static const int glitchDropRates[glitchDropRateCount] = { 1, 2, 3, 4, 6, 8, 12, 16 };
static const int TRIGGER_LIMIT = (1 << 17);

static const int FREEZE_RATIOS_COUNT = 11;
static const float freezeRatios[FREEZE_RATIOS_COUNT] = { 
              1.0 / 8,
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              3.0 / 4,
              1.0,
              1.5,
              2.0,
              2.5,
              3.0,
              4.0 
};

static const uint32_t counters[FREEZE_RATIOS_COUNT] = { 
             1,
             1,
             1,
             1,
             1,
             1,
             3,
             2,
             3,
             3,
             4 
};

static const int SPEED_RATIOS_COUNT = 18;
static const float speedRatios[SPEED_RATIOS_COUNT] = { 
             -4.0,
             -3.0,
             -2.0,
             -3.0 / 2,
             -1.0,
             -3.0 / 4,
             -1.0 / 2,
             -1.0 / 3,
             -1.0 / 4,
              1.0 / 4,
              1.0 / 3,
              1.0 / 2,
              3.0 / 4,
              1.0,
              3.0 / 2,
              2.0,
              3.0,
              4.0 
};

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
    : bufferL(0), bufferR(0), crushL(0), crushR(0), tempo(getSampleRate()*60/120), freeze(false)
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

    setParameterValue(inSpeed, 0.5f);
    setParameterValue(inDrop, 0);
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

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    // button 2 is for tap tempo now
    bool mangle = false; // isButtonPressed(BUTTON_2);

    int size = audio.getSize();
    freezeRatio = (int)(getParameterValue(inSize) * FREEZE_RATIOS_COUNT);
    int speedRatio = (int)(getParameterValue(inSpeed) * SPEED_RATIOS_COUNT);

    tempo.clock(size);

    float newFreezeLength = freezeDuration(freezeRatio) * (TRIGGER_LIMIT - 1);
    float newReadSpeed    = speedRatios[speedRatio] / newFreezeLength;

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
    float dropMult = dropParam * glitchDropRateCount;
    // on the one hand it's nice that this is sync'd with the read lfo
    // because we can use it to self-patch and change the speed every cycle,
    // but in terms of rhythmically dropping out audio based on the tempo,
    // it might make more sense to keep it sync'd with the clock and only use the speed input for ratios.
    float dropSpeed = readSpeed * glitchDropRates[(int)dropMult];
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
        dropLfo = 0;
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
      // TODO: for this to work properly, we need to take into account both the freeze ratio and the speed ratio
      //if (on && ++counter >= counters[freezeRatio]) 
      //{
      //  readLfo = 0;
      //  dropLfo = 0;
      //  counter = 0;
      //}
    }
  }

};
