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
  const PatchParameterId outRamp = PARAMETER_F;

public:
  GlitchLichPatch()
  {
    bufferLen = static_cast<int>(getSampleRate() * BUFFER_SIZE_IN_SECONDS);
    bufferL = CircularBuffer<float>::create(bufferLen);
    bufferR = CircularBuffer<float>::create(bufferLen);

    rampLfo = RampOscillator::create(getSampleRate());

    registerParameter(inDuration, "Duration");
    registerParameter(outRamp, "Ramp>");
  }

  ~GlitchLichPatch()
  {
    CircularBuffer<float>::destroy(bufferL);
    CircularBuffer<float>::destroy(bufferR);
    RampOscillator::destroy(rampLfo);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);

    bool freeze = isButtonPressed(BUTTON_1);
    int size = audio.getSize();

    float dur = 0.001f + getParameterValue(inDuration) * 0.999f;
    float len = (bufferLen - 1)*dur;
    //rampLfo->setFrequency(1.0f / (dur * BUFFER_SIZE_IN_SECONDS));
    rampLfo->setFrequency(2);

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
        rampLfo->generate();
        bufferL->write(left[i]);
        bufferR->write(right[i]);
        left[i] = 0;
        right[i] = 0;
      }
    }

    setParameterValue(outRamp, rampLfo->getPhase() / (2*M_PI));
  }

};
