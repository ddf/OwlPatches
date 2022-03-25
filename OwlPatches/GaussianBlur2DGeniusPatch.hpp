#include "MonochromeScreenPatch.h"
#include "BlurPatch.hpp"

class GaussianBlur2DGeniusPatch : public BlurPatch<11, 1, 1, MonochromeScreenPatch>
{

public:
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    if (textureSize.skewEnabled())
    {
      screen.fillRectangle(screen.getWidth() / 4 - 4, screen.getHeight() / 2 - 4, 8, 8, WHITE);
    }
    else
    {
      screen.drawRectangle(screen.getWidth() / 4 - 4, screen.getHeight() / 2 - 4, 8, 8, WHITE);
    }

    if (blurSize.skewEnabled())
    {
      screen.fillRectangle(screen.getWidth() - screen.getWidth() / 4 - 4, screen.getHeight() / 2 - 4, 8, 8, WHITE);
    }
    else
    {
      screen.drawRectangle(screen.getWidth() - screen.getWidth() / 4 - 4, screen.getHeight() / 2 - 4, 8, 8, WHITE);
    }
  }

};
