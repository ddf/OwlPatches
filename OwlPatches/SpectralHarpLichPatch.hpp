#include "SpectralHarpPatch.hpp"

typedef SpectralHarpPatch<512, false, Patch> BasePatch;

class SpectralHarpLichPatch : public BasePatch
{
  void processAudio(AudioBuffer& audio) override
  {
    // have to invert audio input on Lich
    audio.getSamples(0).multiply(-1);
    audio.getSamples(1).multiply(-1);

    BasePatch::processAudio(audio);
  }
};
