#include "Patch.h"
#include "CircularBuffer.h"
#include "RampOscillator.h"

class GlitchLichPatch : public Patch
{
  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  int bufferLen;
  int readLfo;

  const float BUFFER_SIZE_IN_SECONDS = 0.5f;
  const PatchParameterId inDuration = PARAMETER_A;
  const PatchParameterId outRamp = PARAMETER_F;

public:
  GlitchLichPatch()
  {
    bufferLen = (int)(getSampleRate() * BUFFER_SIZE_IN_SECONDS);
    bufferL = CircularBuffer<float>::create(bufferLen);
    bufferR = CircularBuffer<float>::create(bufferLen);

    readLfo = 0;

    registerParameter(inDuration, "Duration");
    registerParameter(outRamp, "Ramp>");
  }

  ~GlitchLichPatch()
  {
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    bool freeze = isButtonPressed(BUTTON_1);
    int size = audio.getSize();

    float dur = 0.001f + getParameterValue(inDuration) * 0.999f;
    int   len = (int)(bufferLen*dur);
    readLfo %= len;

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
        // we want a ramp that goes from 0 -> 1
        float readIdx = readStartIdx + readLfo;
        readLfo = (readLfo + 1) % len;
        left[i] = bufferL->interpolatedReadAt(readIdx);
        right[i] = bufferR->interpolatedReadAt(readIdx);
      }
    }
    else
    {
      for (int i = 0; i < size; ++i)
      {
        readLfo = (readLfo + 1) % len;
        bufferL->write(left[i]);
        bufferR->write(right[i]);
        left[i] = left[i] * dur;
        right[i] = right[i] * dur;
      }
    }

    setParameterValue(outRamp, (float)readLfo / len);
  }

};