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
    const int feedWidth = 6;
    const float feedCross = feedbackAngle * feedbackMagnitude;

    drawTexture(screen, cxL, cy, txLeft, blurSizeLeft);
    drawTexture(screen, cxR, cy, txRight, blurSizeRight);
    drawFeedback<false>(screen, 0, displayHeight, feedWidth, feedbackAmtLeft - feedCross);
    drawFeedback<true>(screen, screen.getWidth() - feedWidth - 1, displayHeight, feedWidth, feedbackAmtRight - feedCross);
    drawCrossFeedback<false>(screen, screen.getWidth()/2 - feedWidth - 1, displayHeight, feedWidth, feedCross);
    drawCrossFeedback<true>(screen, screen.getWidth()/2 + 1, displayHeight, feedWidth, feedCross);
  }

  void drawTexture(MonochromeScreenBuffer& screen, const int cx, const int cy, const int texDim, const float blurSize)
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

  template<bool pointLeft>
  void drawFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    const int iconY = y - 2;
    screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
    screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
    screen.drawLine(x + iconDim, iconY - iconDim, x + iconDim, iconY, WHITE);
    if (pointLeft)
    {
      screen.drawLine(x + iconDim, iconY, x + 2, iconY, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY - 2, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY + 2, WHITE);
    }
    else
    {
      screen.drawLine(x, iconY, x + iconDim - 2, iconY, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY - 2, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY + 2, WHITE);
    }

    const int barHeight = 37;
    screen.drawRectangle(x, iconY - iconDim - barHeight - 1, iconDim+1, barHeight, WHITE);
    screen.fillRectangle(x, iconY - iconDim - barHeight*amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }

  template<bool pointLeft>
  void drawCrossFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    const int iconY = y - iconDim/2;
    screen.drawLine(x, iconY, x+iconDim, iconY, WHITE);
    if (pointLeft)
    {
      screen.drawLine(x, iconY, x + 2, iconY - 2, WHITE);
      screen.drawLine(x, iconY, x + 2, iconY + 2, WHITE);
    }
    else
    {
      screen.drawLine(x + iconDim, iconY, x + iconDim - 2, iconY - 2, WHITE);
      screen.drawLine(x + iconDim, iconY, x + iconDim - 2, iconY + 2, WHITE);
    }

    const int barHeight = 37;
    screen.drawRectangle(x, iconY - iconDim/2 - barHeight - 1, iconDim + 1, barHeight, WHITE);
    screen.fillRectangle(x, iconY - iconDim/2 - barHeight * amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }
};
