#include "MonochromeScreenPatch.h"
#include "BlurPatch.hpp"
#include "Noise.hpp"

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
  GaussianBlur2DGeniusPatch() : BlurPatch(geniusBlurParams)
  {
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int displayHeight = screen.getHeight() - 18;
    const int cy = displayHeight / 2;
    const int cxL = screen.getWidth() / 4;
    const int cxR = screen.getWidth() - screen.getWidth() / 4;
    const int txLeft = roundf(Interpolator::linear(2, displayHeight, (textureSizeLeft - minTextureSize) / (maxTextureSize - minTextureSize)));
    const int txRight = roundf(Interpolator::linear(2, displayHeight, (textureSizeRight - minTextureSize) / (maxTextureSize - minTextureSize)));
    const int blurR = roundf(txRight * blurSizeRight);

    drawTexture(screen, cxL, cy, txLeft, blurSizeLeft);
    drawTexture(screen, cxR, cy, txRight, blurSizeRight);
  }

  void drawTexture(MonochromeScreenBuffer &screen, const int cx, const int cy, const int texDim, const float blurSize)
  {
    const int tx = cx - texDim / 2;
    const int ty = cy - texDim / 2;
    screen.drawRectangle(tx, ty, texDim, texDim, WHITE);
    for (int x = 0, y = 0; x < texDim; x+=2, y+=2)
    {
      screen.drawLine(tx, ty + y, tx+x, ty, WHITE);
      screen.drawLine(tx + texDim - 1, ty + texDim - y - 1, tx + texDim - x - 1, ty + texDim - 1, WHITE);
    }
    for (int x = 2; x < texDim - 2; ++x)
    {
      for (int y = 2; y < texDim - 2; ++y)
      {
        if (perlin2d(x, y, texDim / 4, 1) + 0.001f < blurSize*2)
        {
          screen.invertPixel(tx + x - 1, ty + y - 1);
          screen.invertPixel(tx + x - 1, ty + y);
          screen.invertPixel(tx + x - 1, ty + y + 1);

          screen.invertPixel(tx + x, ty + y - 1);
          screen.invertPixel(tx + x, ty + y);
          screen.invertPixel(tx + x, ty + y + 1);

          screen.invertPixel(tx + x + 1, ty + y - 1);
          screen.invertPixel(tx + x + 1, ty + y);
          screen.invertPixel(tx + x + 1, ty + y + 1);
        }
      }
    }
  }

};
