#include "MonochromeScreenPatch.h"
#include "BlurPatch.hpp"

static const BlurPatchParameterIds geniusBlurParams =
{
  .inTextureSize = PARAMETER_A,
  .inBlurSize = PARAMETER_B,
  .inFeedMag = PARAMETER_C,
  .inWetDry = PARAMETER_D,
  .inTextureTilt = PARAMETER_E,
  .inBlurTilt = PARAMETER_F,
  .inFeedTilt = PARAMETER_G,
  .inBlurBrightness = PARAMETER_H,

  .inCompressionThreshold = PARAMETER_AA,
  .inCompressionRatio = PARAMETER_AB,
  .inCompressionAttack = PARAMETER_AC,
  .inCompressionRelease = PARAMETER_AD,
  .inCompressionMakeupGain = PARAMETER_AE,
  .inCompressionBlend = PARAMETER_AF,

  .outLeftFollow = PARAMETER_AG,
  .outRightFollow = PARAMETER_AH
};

// It turns out that when running without downsampling,
// the volume of the blurred signal gets much quieter at higher blur amounts
// than when running with 4x downsampling.
// So I need to decide if I want the Genius version of the patch
// to sound the same as the Lich version or not.
// One thing is that with no downsampling this runs at ~90% CPU,
// but with 4x downsampling it runs at ~20%.
// This means we could run with downsampling and add a basic reverb,
// which would be a nice addition to this patch.
class GaussianBlur2DGeniusPatch : public BlurPatch<11, 4, 4, MonochromeScreenPatch>
{
public:
  GaussianBlur2DGeniusPatch() : BlurPatch(geniusBlurParams) {}

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
