#include "MonoChromeScreenPatch.h"
#include "SpectralHarpPatch.hpp"

class SpectralHarpGeniusPatch : public SpectralHarpPatch<4096, MonochromeScreenPatch>
{

public:
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    
  }

};
