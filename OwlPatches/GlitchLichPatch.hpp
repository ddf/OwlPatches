#include "Patch.h"
#include "SmoothValue.h"
#include "CircularBuffer.h"
#include "BitCrusher.hpp"

static const int glitchDropRateCount = 8;
static const int glitchDropRates[glitchDropRateCount] = { 1, 2, 3, 4, 6, 8, 12, 16 };


class GlitchLichPatch : public Patch
{
  const float BUFFER_SIZE_IN_SECONDS = 0.5f;
  const PatchParameterId inSize = PARAMETER_A;
  const PatchParameterId inSpeed = PARAMETER_B;
  const PatchParameterId inDrop = PARAMETER_C;
  const PatchParameterId inCrush = PARAMETER_D;
  const PatchParameterId outRamp = PARAMETER_F;
  const PatchParameterId outRand = PARAMETER_G;

  const int circularBufferLength;
  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  BitCrusher<24>* crushL;
  BitCrusher<24>* crushR;
  SmoothFloat freezeLength;
  float readLfo;
  float readSpeed;
  float dropLfo;
  bool dropSamples;
  float dropRand;

public:
  GlitchLichPatch()
    : circularBufferLength((int)(getSampleRate() * BUFFER_SIZE_IN_SECONDS))
  {
    bufferL = CircularBuffer<float>::create(circularBufferLength);
    bufferR = CircularBuffer<float>::create(circularBufferLength);
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

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    bool freeze = isButtonPressed(BUTTON_1);
    bool mangle = isButtonPressed(BUTTON_2);
    int size = audio.getSize();

    float dur = 0.001f + getParameterValue(inSize) * 0.999f;
    freezeLength = circularBufferLength * dur;

    readSpeed = (-4.f + getParameterValue(inSpeed) * 8.f) / freezeLength;

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
      int writeLen = min(size, bufferL->getWriteCapacity());
      if (writeLen > 0)
      {
        bufferL->write(left, writeLen);
        bufferR->write(right, writeLen);
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
      bufferL->setWriteIndex(0);
      bufferR->setWriteIndex(0);
    }
  }

};
