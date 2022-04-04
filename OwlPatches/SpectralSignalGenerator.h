#include "SignalGenerator.h"

class SpectralSignalGenerator : public SignalGenerator
{

public:
  float generate() override
  {
    return 0;
  }

  using SignalGenerator::generate;

  static SpectralSignalGenerator* create(float sampleRate)
  {
    return new SpectralSignalGenerator();
  }

  static void destroy(SpectralSignalGenerator* spectralGen)
  {
    delete spectralGen;
  }
};
