#include "SpectralHarpPatch.hpp"

typedef SpectralHarpPatch<2048, false, Patch> BasePatch;

static const SpectralHarpParameterIds spectraHarpLichParams =
{
  .inHarpFundamental = PARAMETER_A,
  .inHarpOctaves = PARAMETER_B,
  .inDensity = PARAMETER_C,
  .inTuning = PARAMETER_D,
  .inDecay = PARAMETER_E,
  .inSpread = PARAMETER_AE,
  .inBrightness = PARAMETER_AF,
  .inCrush = PARAMETER_H,

  .inWidth = PARAMETER_AA,
  .inReverbBlend = PARAMETER_AB,
  .inReverbTime = PARAMETER_AC,
  .inReverbTone = PARAMETER_AD,

  .outStrumX = PARAMETER_F,
  .outStrumY = PARAMETER_G,
};

class SpectralHarpLichPatch : public BasePatch
{
  float highElapsedTime = 0;

public:
  SpectralHarpLichPatch() : BasePatch(spectraHarpLichParams) {}

  // returns CPU% as [0,1] value
  float getElapsedTime()
  {
    return getElapsedCycles() / getBlockSize() / 10000.0f;
  }

  void processAudio(AudioBuffer& audio) override
  {
    // have to invert audio input on Lich
    audio.getSamples(0).multiply(-1);
    audio.getSamples(1).multiply(-1);

    float elapsed = getElapsedTime();
    BasePatch::processAudio(audio);
    elapsed = getElapsedTime() - elapsed;
    if (elapsed > highElapsedTime)
    {
      highElapsedTime = elapsed;
    }
    //else
    //{
    //  highElapsedTime += (elapsed - highElapsedTime)*0.001f;
    //}

    debugMessage("CPU High: ", highElapsedTime);
  }
};
