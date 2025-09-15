#pragma once

#include "MonochromeScreenPatch.h"
#include "PatchParameterIds.h"
#include "AudioBufferSourceSink.h"
#include "Gauss.h"
#include "Noise.hpp"

class GaussPatch : public MonochromeScreenPatch
{
  Gauss gauss;
  
public:
  GaussPatch() : gauss(getSampleRate(), getBlockSize())
  {
    // registered first so they are the default CV IN assignments on Genius
    registerParameter(InputParameterId::E, gauss.textureTilt().getName());
    registerParameter(InputParameterId::F, gauss.blurTilt().getName());
    registerParameter(InputParameterId::G, gauss.gain().getName());
    
    registerParameter(InputParameterId::A, gauss.textureSize().getName());
    registerParameter(InputParameterId::B, gauss.blurSize().getName());
    registerParameter(InputParameterId::C, gauss.feedback().getName());
    registerParameter(InputParameterId::D, gauss.crossFeedback().getName());

    setParameterValue(InputParameterId::E, 0.5f);
    setParameterValue(InputParameterId::F, 0.5f);
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    gauss.textureSize() << getParameterValue(InputParameterId::A);
    gauss.textureTilt() << vessl::easing::lerp(-1.f, 1.f, getParameterValue(InputParameterId::E));

    gauss.blurSize() << getParameterValue(InputParameterId::B);
    gauss.blurTilt() << vessl::easing::lerp(-1.f, 1.f, getParameterValue(InputParameterId::F));
    
    gauss.feedback() << getParameterValue(InputParameterId::C);
    gauss.crossFeedback() << getParameterValue(InputParameterId::D);
    
    gauss.gain() << getParameterValue(InputParameterId::G)*12.0f;

    AudioBufferReader<2> input(audio);
    AudioBufferWriter<2> output(audio);
    gauss.process(input, output);
  }

#ifdef DEBUG
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.clear();
    screen.setCursor(0, 8);
    for (BlurKernelSample& sample : gauss.kernel())
    {
      screen.print("w: ");
      screen.print(sample.weight*100.f);
      screen.print(" o: ");
      screen.print(sample.offset);
      screen.print("\n");
    }
  }
#else
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    int displayHeight = screen.getHeight() - 18;
    int cy = displayHeight / 2;
    int cxL = screen.getWidth() / 4 - 4;
    int cxR = screen.getWidth() - screen.getWidth() / 4 + 4;
    int txLeft = vessl::math::round(
      vessl::easing::lerp(2, displayHeight,
        (gauss.getTextureSizeLeft() - Gauss::MIN_TEXTURE_SIZE) / (Gauss::MAX_TEXTURE_SIZE - Gauss::MIN_TEXTURE_SIZE))
        );
    int txRight = vessl::math::round(
      vessl::easing::lerp(2, displayHeight,
        (gauss.getTextureSizeRight() - Gauss::MIN_TEXTURE_SIZE) / (Gauss::MAX_TEXTURE_SIZE - Gauss::MIN_TEXTURE_SIZE))
        );
    int feedWidth = 6;
    float feedbackMagnitude = *gauss.feedback();
    float feedbackAngle = *gauss.crossFeedback();
    float feedCross = feedbackAngle * feedbackMagnitude;

    //screen.setCursor(cxL - 8, cy); screen.print(txLeft); screen.setCursor(cxL - 16, cy+8); screen.print(gauss.getBlurSizeLeft()*100);
    drawTexture(screen, cxL, cy, txLeft, gauss.getBlurSizeLeft());
    //screen.setCursor(cxR -8, cy); screen.print(txRight); screen.setCursor(cxR - 16, cy+8); screen.print(gauss.getBlurSizeRight()*100);
    drawTexture(screen, cxR, cy, txRight, gauss.getBlurSizeRight());
    drawFeedback<true>(screen, screen.getWidth()/2 - feedWidth - 2, displayHeight-1, feedWidth, feedbackMagnitude - feedCross);
    drawCrossFeedback(screen, screen.getWidth()/2 + 2, displayHeight-1, feedWidth, feedCross);
  }
#endif

  static void drawTexture(MonochromeScreenBuffer& screen, int cx, int cy, int texDim, float withBlurSize)
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
        if (perlin2d(x, y, texDim / 4, 1) + 0.001f < withBlurSize*2)
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

  template<bool PointLeft>
  static void drawFeedback(MonochromeScreenBuffer& screen, int x, int y, int iconDim, float amt)
  {
    const int iconY = y;
    screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
    screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
    screen.drawLine(x + iconDim, iconY - iconDim, x + iconDim, iconY, WHITE);
    if (PointLeft)
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

    constexpr int barHeight = 38;
    screen.drawRectangle(x, iconY - iconDim - barHeight - 1, iconDim+1, barHeight, WHITE);
    screen.fillRectangle(x, iconY - iconDim - barHeight*amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }

  static void drawCrossFeedback(MonochromeScreenBuffer& screen, int x, int y, int iconDim, float amt)
  {
    const int arrowLY = y - iconDim/2 - 1;
    const int arrowRY = y;
    screen.drawLine(x, arrowLY, x+iconDim, arrowLY, WHITE);
    screen.drawLine(x, arrowLY, x + 2, arrowLY - 2, WHITE);
    screen.drawLine(x, arrowLY, x + 2, arrowLY + 2, WHITE);

    screen.drawLine(x, arrowRY, x + iconDim, arrowRY, WHITE);
    screen.drawLine(x + iconDim, arrowRY, x + iconDim - 2, arrowRY - 2, WHITE);
    screen.drawLine(x + iconDim, arrowRY, x + iconDim - 2, arrowRY + 2, WHITE);

    const int barHeight = 38;
    screen.drawRectangle(x, y - iconDim - barHeight - 1, iconDim + 1, barHeight, WHITE);
    screen.fillRectangle(x, y - iconDim - barHeight * amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }
};
