#include "Patch.h"
#include "SpectralSignalGenerator.h"

class SpectralHarpPatch : public Patch
{
  SpectralSignalGenerator* spectralGen;

public:
  SpectralHarpPatch() : Patch()
  {
    spectralGen = SpectralSignalGenerator::create(getSampleRate());
  }

  ~SpectralHarpPatch()
  {
    SpectralSignalGenerator::destroy(spectralGen);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int blockSize = audio.getSize();
    FloatArray left = audio.getSamples(0);
    FloatArray right = audio.getSamples(1);

    spectralGen->generate(left);
    left.copyTo(right);
  }

};
