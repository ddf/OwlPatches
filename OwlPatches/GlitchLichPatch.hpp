#include "Patch.h"
#include "CircularBuffer.h"
#include "RampOscillator.h"

class GlitchLichPatch : public Patch
{
  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  int bufferLen;
  float readLfo;
  float readSpeed;

  int dropBlockCount;
  int dropBlockLength;
  bool dropBlock;

  const float BUFFER_SIZE_IN_SECONDS = 0.5f;
  const int dropBlockLengthMin = 2;
  const int dropBlockLengthMax = 24;
  const PatchParameterId inSize = PARAMETER_A;
  const PatchParameterId inSpeed = PARAMETER_B;
  const PatchParameterId inDrop = PARAMETER_C;
  const PatchParameterId outRamp = PARAMETER_F;

public:
  GlitchLichPatch()
  {
    bufferLen = (int)(getSampleRate() * BUFFER_SIZE_IN_SECONDS);
    bufferL = CircularBuffer<float>::create(bufferLen);
    bufferR = CircularBuffer<float>::create(bufferLen);

    readLfo = 0;
    readSpeed = 1;
    dropBlockCount = 0;
    dropBlockLength = dropBlockLengthMax;

    registerParameter(inSize,  "Size");
    registerParameter(inSpeed, "Speed");
    registerParameter(inDrop, "Drop");
    registerParameter(outRamp, "Ramp>");

    setParameterValue(inSpeed, 0.5f);
    setParameterValue(inDrop, 0);
  }

  ~GlitchLichPatch()
  {
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
  }


  float stepReadLFO(float speed, float len)
  {
    readLfo = readLfo + speed;
    if (readLfo >= len)
    {
      readLfo -= len;
    }
    else if (readLfo < 0)
    {
      readLfo += len;
    }
    return readLfo;
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    bool freeze = isButtonPressed(BUTTON_1);
    bool flip = isButtonPressed(BUTTON_2);
    int size = audio.getSize();

    float dur = 0.001f + getParameterValue(inSize) * 0.999f;
    float len = bufferLen * dur;

    readSpeed = -4.f + getParameterValue(inSpeed) * 8.f;

    if (freeze)
    {
      int writeIdx = bufferL->getWriteIndex();
      float readStartIdx = writeIdx - len;
      if (readStartIdx < 0)
      {
        readStartIdx += bufferLen;
      }
      for (int i = 0; i < size; ++i)
      {
        float off = stepReadLFO(readSpeed, len);
        float readIdx = readStartIdx + off;
        left[i] = bufferL->interpolatedReadAt(readIdx);
        right[i] = bufferR->interpolatedReadAt(readIdx);
      }
    }
    else
    {
      for (int i = 0; i < size; ++i)
      {
        stepReadLFO(readSpeed, len);
        bufferL->write(left[i]);
        bufferR->write(right[i]);
      }
    }

    dropBlockLength = dropBlockLengthMax + getParameterValue(inDrop)*(dropBlockLengthMin - dropBlockLengthMax);

    if (++dropBlockCount >= dropBlockLength)
    {
      dropBlockCount = 0;
      dropBlock = randf() < getParameterValue(inDrop);
    }

    if (dropBlock)
    {
      left.clear();
      right.clear();
    }

    float rampVal = (float)readLfo / len;
    setParameterValue(outRamp, rampVal);
    setButton(PUSHBUTTON, rampVal < 0.5f);
  }

};
