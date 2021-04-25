#include "Patch.h"
#include "PerlinNoiseField.hpp"

class PerlinNoiseFieldLichPatch : public Patch
{
  PerlinNoiseField* noiseField;
  AudioBuffer*      noiseBuffer;

  static const PatchParameterId inNoiseFrequency = PARAMETER_A;
  static const PatchParameterId inWetDry = PARAMETER_B;
  static const PatchParameterId inOffsetX = PARAMETER_C;
  static const PatchParameterId inOffsetY = PARAMETER_D;

public:
  PerlinNoiseFieldLichPatch()
  {
    noiseField = PerlinNoiseField::create();
    noiseBuffer = AudioBuffer::create(1, getBlockSize());
    registerParameter(inNoiseFrequency, "Noise Frequency");
    registerParameter(inWetDry, "Wet / Dry");
    registerParameter(inOffsetX, "X Offset");
    registerParameter(inOffsetY, "Y Offset");

    setParameterValue(inNoiseFrequency, 0);
    setParameterValue(inWetDry, 0);
    setParameterValue(inOffsetX, 0);
    setParameterValue(inOffsetY, 0);
  }

  ~PerlinNoiseFieldLichPatch()
  {
    PerlinNoiseField::destroy(noiseField);
    AudioBuffer::destroy(noiseBuffer);
  }

  void processAudio(AudioBuffer& audio) override
  {
    noiseField->setOffsetX(getParameterValue(inOffsetX));
    noiseField->setOffsetY(getParameterValue(inOffsetY));
    noiseField->setFrequency(getParameterValue(inNoiseFrequency) * 16 + 1);
    noiseField->process(audio, *noiseBuffer);

    FloatArray noise = noiseBuffer->getSamples(0);
    noise.multiply(2);
    noise.subtract(1);

    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);
    float wetDry = getParameterValue(inWetDry);
    for (int i = 0; i < getBlockSize(); ++i)
    {
      float nz = noise[i];
      left[i] += wetDry * (nz - left[i]);
      right[i] += wetDry * (nz - right[i]);
    }
  }

};
