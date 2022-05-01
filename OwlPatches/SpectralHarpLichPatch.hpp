#include "SpectralHarpPatch.hpp"

typedef SpectralHarpPatch<512, false, Patch> BasePatch;

class SpectralHarpLichPatch : public BasePatch
{
  float highElapsedTime = 0;

  void processAudio(AudioBuffer& audio) override
  {
    // have to invert audio input on Lich
    audio.getSamples(0).multiply(-1);
    audio.getSamples(1).multiply(-1);

    float elapsed = BasePatch::getElapsedBlockTime();
    BasePatch::processAudio(audio);
    elapsed = BasePatch::getElapsedBlockTime() - elapsed;
    if (elapsed > highElapsedTime)
    {
      highElapsedTime = elapsed;
    }
    else
    {
      highElapsedTime += (elapsed - highElapsedTime)*0.001f;
    }

    debugMessage("CPU High: ", highElapsedTime);
  }
};
