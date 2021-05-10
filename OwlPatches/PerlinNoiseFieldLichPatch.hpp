#include "Patch.h"
#include "PerlinNoiseField.hpp"

class PerlinNoiseFieldLichPatch : public Patch
{
  PerlinNoiseField* noiseField;
  AudioBuffer*      noiseBuffer;
  FloatArray        fmArray;
  bool              sampleNoise1;
  float             sampledNoise1;
  bool              sampleNoise2;
  float             sampledNoise2;

  static const PatchParameterId inNoiseFrequency = PARAMETER_A;
  static const PatchParameterId inWetDry = PARAMETER_B;
  static const PatchParameterId inOffsetX = PARAMETER_C;
  static const PatchParameterId inOffsetY = PARAMETER_D;
  static const PatchParameterId outNoise1 = PARAMETER_F;
  static const PatchParameterId outNoise2 = PARAMETER_G;

public:
  PerlinNoiseFieldLichPatch()
  : sampleNoise1(false), sampleNoise2(false)
  {
    noiseField = PerlinNoiseField::create();
    noiseBuffer = AudioBuffer::create(1, getBlockSize());
    fmArray = FloatArray::create(getBlockSize());
    fmArray.clear();

    registerParameter(inNoiseFrequency, "Noise Frequency");
    registerParameter(inWetDry, "Wet / Dry");
    registerParameter(inOffsetX, "X Offset");
    registerParameter(inOffsetY, "Y Offset");

    registerParameter(outNoise1, "Noise 1>");
    registerParameter(outNoise2, "Noise 2>");

    setParameterValue(inNoiseFrequency, 0);
    setParameterValue(inWetDry, 0);
    setParameterValue(inOffsetX, 0);
    setParameterValue(inOffsetY, 0);
    setParameterValue(outNoise1, 0);
    setParameterValue(outNoise2, 0);
  }

  ~PerlinNoiseFieldLichPatch()
  {
    PerlinNoiseField::destroy(noiseField);
    AudioBuffer::destroy(noiseBuffer);
    FloatArray::destroy(fmArray);
  }

  void processAudio(AudioBuffer& audio) override
  {
    float targetFreq = getParameterValue(inNoiseFrequency) * 127 + 1;
    fmArray.ramp(fmArray[fmArray.getSize() - 1], targetFreq);

    noiseField->setOffsetX(getParameterValue(inOffsetX));
    noiseField->setOffsetY(getParameterValue(inOffsetY));
    noiseField->process(audio, *noiseBuffer, fmArray);

    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);
    FloatArray noise = noiseBuffer->getSamples(0);

    if (sampleNoise1)
    {
      sampledNoise1 = noise[0];
      sampleNoise1 = false;
    }

    if (sampleNoise2)
    {
      sampledNoise2 = noise[0];
      sampleNoise2 = false;
    }

    // shift noise to [-1,1]
    noise.multiply(2);
    noise.subtract(1);

    // do wet/dry mix with original signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    left.multiply(dry);
    right.multiply(dry);
    noise.multiply(wet);
    left.add(noise);
    right.add(noise);

    setParameterValue(outNoise1, sampledNoise1);
    setParameterValue(outNoise2, sampledNoise2);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_A && value)
    {
      sampleNoise1 = true;
    }

    if (bid == BUTTON_B && value)
    {
      sampleNoise2 = true;
    }
  }

};
