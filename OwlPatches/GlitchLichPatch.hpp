#include "Patch.h"
#include "CircularBuffer.h"
#include "RampOscillator.h"

class GlitchLichPatch : public Patch
{
  CircularBuffer<float>* bufferL;
  CircularBuffer<float>* bufferR;
  int bufferLen;
  RampOscillator* rampLfo;

  const float BUFFER_SIZE_IN_SECONDS = 0.5f;
  const PatchParameterId inDuration = PARAMETER_A;

public:
  GlitchLichPatch()
  {
    bufferLen = static_cast<int>(getSampleRate() * BUFFER_SIZE_IN_SECONDS);
    bufferL = CircularBuffer<float>::create(bufferLen);
    bufferR = CircularBuffer<float>::create(bufferLen);

    rampLfo = RampOscillator::create(getSampleRate());

    registerParameter(inDuration, "Duration");
  }

  ~GlitchLichPatch()
  {
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
    RampOscillator::destroy(rampLfo);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    bool freeze = isButtonPressed(BUTTON_1);
    int size = audio.getSize();

    if (freeze)
    {
      float dur = 0.001f + getParameterValue(inDuration) * 0.999f;
      float len = (bufferLen - 1)*dur;
      rampLfo->setFrequency(dur * BUFFER_SIZE_IN_SECONDS);
      int writeIdx = bufferL->getWriteIndex();
      float readStartIdx = writeIdx - len;
      if (readStartIdx < 0)
      {
        readStartIdx += bufferLen;
      }
      for (int i = 0; i < size; ++i)
      {
        // we want a ramp that goes from 0 -> 1
        float pos = 0.5f*rampLfo->generate() + 0.5f;
        float readIdx = readStartIdx + pos * len;
        left[i] = bufferL->interpolatedReadAt(readIdx);
        right[i] = bufferR->interpolatedReadAt(readIdx);
      }
    }
    else
    {
      for (int i = 0; i < size; ++i)
      {
        bufferL->write(left[i]);
        bufferR->write(right[i]);
      }
    }
  }

};
