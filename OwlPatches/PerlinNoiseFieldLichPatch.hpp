#include "Patch.h"
#include "PerlinNoiseField.hpp"

class PerlinNoiseFieldLichPatch : public Patch
{
  PerlinNoiseField* noiseField;

  static const PatchParameterId inNoiseFrequency = PARAMETER_A;
  static const PatchParameterId inNoiseDepth = PARAMETER_B;
  static const PatchParameterId inOffsetX = PARAMETER_C;
  static const PatchParameterId inOffsetY = PARAMETER_D;

public:
  PerlinNoiseFieldLichPatch()
  {
    noiseField = PerlinNoiseField::create();
    registerParameter(inNoiseFrequency, "Noise Frequency");
    registerParameter(inNoiseDepth, "Noise Depth");
    registerParameter(inOffsetX, "X Offset");
    registerParameter(inOffsetY, "Y Offset");

    setParameterValue(inNoiseFrequency, 0);
    setParameterValue(inNoiseDepth, 0);
    setParameterValue(inOffsetX, 0);
    setParameterValue(inOffsetY, 0);
  }

  ~PerlinNoiseFieldLichPatch()
  {
    PerlinNoiseField::destroy(noiseField);
  }

  void processAudio(AudioBuffer& audio) override
  {
    noiseField->setOffsetX(getParameterValue(inOffsetX));
    noiseField->setOffsetY(getParameterValue(inOffsetY));
    noiseField->setFrequency(getParameterValue(inNoiseFrequency) * 16 + 8);
    noiseField->setDepth(getParameterValue(inNoiseDepth) * 8 + 1);
    noiseField->process(audio, audio);
  }

};
