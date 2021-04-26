#include "Patch.h"
#include "DcBlockingFilter.h"
#include "PerlinNoiseField.hpp"

class PerlinNoiseFieldLichPatch : public Patch
{
  StereoDcBlockingFilter* dcBlockingFilter;
  PerlinNoiseField* noiseField;
  AudioBuffer*      noiseBuffer;

  static const PatchParameterId inNoiseFrequency = PARAMETER_A;
  static const PatchParameterId inWetDry = PARAMETER_B;
  static const PatchParameterId inOffsetX = PARAMETER_C;
  static const PatchParameterId inOffsetY = PARAMETER_D;

public:
  PerlinNoiseFieldLichPatch()
  {
    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);
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
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
    PerlinNoiseField::destroy(noiseField);
    AudioBuffer::destroy(noiseBuffer);
  }

  void processAudio(AudioBuffer& audio) override
  {
    dcBlockingFilter->process(audio, audio);

    noiseField->setOffsetX(getParameterValue(inOffsetX));
    noiseField->setOffsetY(getParameterValue(inOffsetY));
    noiseField->setFrequency(getParameterValue(inNoiseFrequency) * 16 + 1);
    noiseField->process(audio, *noiseBuffer);

    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);
    FloatArray noise = noiseBuffer->getSamples(0);

    // shift noise to [-1,1]
    noise.multiply(2);
    noise.subtract(1);

    // do wet/dry mix with orignal signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    left.multiply(dry);
    right.multiply(dry);
    noise.multiply(wet);
    //left.add(noise);
    //right.add(noise);
  }

};
