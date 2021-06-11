#include "Patch.h"
#include "SmoothValue.h"
#include "CircularBuffer.h"

#include "TapTempo.hpp"
#include "BitCrusher.hpp"

static const int glitchDropRateCount = 8;
static const int glitchDropRates[glitchDropRateCount] = { 1, 2, 3, 4, 6, 8, 12, 16 };
static const int TRIGGER_LIMIT = (1 << 16);

static const int FREEZE_RATIOS_COUNT = 9;
static const float freezeRatios[FREEZE_RATIOS_COUNT] = { 
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

static const int SPEED_RATIOS_COUNT = 19;
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
              0.0,
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
  SmoothFloat freezeLength;
  int recordLength;
  float readLfo;
  float readSpeed;
  float dropLfo;
  bool dropSamples;
  float dropRand;

public:
  GlitchLichPatch()
    : bufferL(0), bufferR(0), crushL(0), crushR(0), tempo(getSampleRate()*60/120)
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
    dur = max(0.0001, min(0.9999, dur));
    return dur;
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    bool freeze = isButtonPressed(BUTTON_1);
    // button 2 is for tap tempo now
    bool mangle = false; // isButtonPressed(BUTTON_2);

    int size = audio.getSize();
    int freezeRatio = (int)(getParameterValue(inSize) * FREEZE_RATIOS_COUNT);
    int speedRatio = (int)(getParameterValue(inSpeed) * SPEED_RATIOS_COUNT);

    freezeLength = freezeDuration(freezeRatio) * (TRIGGER_LIMIT - 1);
    readSpeed = 1.0f / freezeLength; // speedRatios[speedRatio] / freezeLength;

    float sr = getSampleRate();
    float crush = getParameterValue(inCrush);
    float bits = crush > 0.001f ? (8.f - crush * 6) : 24;
    float rate = crush > 0.001f ? sr*0.25f + getParameterValue(inCrush)*(100 - sr*0.25f) : sr;
    crushL->setBitDepth(bits);
    crushL->setBitRate(rate);
    crushL->setMangle(mangle);
    crushR->setBitDepth(bits);
    crushR->setBitRate(rate);
    crushR->setMangle(mangle);

    if (freeze)
    {
      // while frozen, record into our buffers until they are full
      if (recordLength > 0)
      {
        int writeLen = min(size, recordLength);
        bufferL->write(left, writeLen);
        bufferR->write(right, writeLen);
        recordLength -= writeLen;
      }

      for (int i = 0; i < size; ++i)
      {
        float readIdx = stepReadLFO(readSpeed)*freezeLength;
        left[i] =  interpolatedReadAt(bufferL, readIdx);
        right[i] = interpolatedReadAt(bufferR, readIdx);
      }
    }
    else
    {
      for (int i = 0; i < size; ++i)
      {
        stepReadLFO(readSpeed);
        //bufferL->write(left[i]);
        //bufferR->write(right[i]);
      }
    }

    crushL->process(left, left);
    crushR->process(right, right);

    float dropParam = getParameterValue(inDrop);
    float dropMult = dropParam * glitchDropRateCount;
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
    if (bid == BUTTON_1 && value == ON)
    {
      readLfo = 0;
      recordLength = TRIGGER_LIMIT;
      bufferL->setWriteIndex(0);
      bufferR->setWriteIndex(0);
    }

    if (bid == BUTTON_2)
    {
      bool on = value == ON;
      tempo.trigger(on, samples);
    }
  }

};
