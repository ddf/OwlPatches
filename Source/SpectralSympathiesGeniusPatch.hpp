#pragma once

#include "SpectralSympathiesPatch.hpp"

typedef SpectralSympathiesPatch<4096, true> BasePatch;

static const SpectralSympathiesParameterIds spectraHarpGeniusParams =
{
  .inHarpFundamental = PARAMETER_A,
  .inHarpOctaves = PARAMETER_B,
  .inDensity = PARAMETER_C,
  .inTuning = PARAMETER_D,
  .inDecay = PARAMETER_E,
  .inSpread = PARAMETER_F,
  .inBrightness = PARAMETER_G,
  .inCrush = PARAMETER_H,

  .inWidth = PARAMETER_AA,
  .inReverbBlend = PARAMETER_AB,
  .inReverbTime = PARAMETER_AC,
  .inReverbTone = PARAMETER_AD,

  .outStrumX = PARAMETER_AE,
  .outStrumY = PARAMETER_AF,
};

class SpectralSympathiesGeniusPatch : public BasePatch
{
  float stringAnimation = 0;
  float highElapsedTime = 0;

public:
  SpectralSympathiesGeniusPatch() : BasePatch(spectraHarpGeniusParams) {}

  // returns CPU% as [0,1] value
  float getElapsedTime()
  {
    return getElapsedCycles() / getBlockSize() / 10000.0f;
  }

  void processAudio(AudioBuffer& audio) override
  {
    float elapsed = getElapsedTime();
    BasePatch::processAudio(audio);
    elapsed = getElapsedTime() - elapsed;
    if (elapsed > highElapsedTime)
    {
      highElapsedTime = elapsed;
    }
    else
    {
      highElapsedTime += (elapsed - highElapsedTime)*0.001f;
    }
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int top = 8;
    const int bottom = screen.getHeight() - 18;
    const int height = bottom - top;
    const int numBands = getStringCount();
    for (int b = 0; b < numBands; ++b)
    {
      float freq = frequencyOfString(b);
      float x = Interpolator::linear(0, screen.getWidth() - 1, (float)b / (numBands - 1));
      auto band = spectralGen->getBand(freq);
      band.phase += stringAnimation;

      // solid line animation that wobbles back and forth based on amplitude
      //float w = Interpolator::linear(0, 2, band.amplitude);
      //int segments = w > 0 ? 32 : 1;
      //float segLength = (float)height / segments;
      //float py0 = 0;
      //float px0 = x + w * sinf(band.phase);
      //for (int i = 0; i < segments + 1; ++i)
      //{
      //  float py1 = i * segLength;
      //  float s1 = py1 / height * (float)M_PI * 8 + band.phase;
      //  float px1 = x + w * sinf(s1);
      //  screen.drawLine(px0, py0, px1, py1, WHITE);
      //  px0 = px1;
      //  py0 = py1;
      //}

      // same animation, viewed from the side with "pegs" at top and bottom
      screen.drawLine(x, top, x, top + 1, WHITE);
      screen.drawLine(x, bottom - 1, x, bottom, WHITE);
      for (int y = top + 2; y < bottom - 1; ++y)
      {
        float s1 = (float)y / height * M_PI * band.amplitude * 600 + band.phase;
        if (fabsf(band.amplitude*sinf(s1)) > 0.004f)
        {
          screen.setPixel(x, y, WHITE);
        }
      }
    }

    char* bandFirstStr = msg_itoa((int)bandFirst, 10);
    screen.setCursor(0, top);
    screen.print(bandFirstStr);
    screen.print(" Hz");

    char* bandLastStr = msg_itoa((int)bandLast, 10);
    screen.setCursor(screen.getWidth() - 6 * (strlen(bandLastStr) + 3), top);
    screen.print(bandLastStr);
    screen.print(" Hz");

    screen.setCursor(screen.getWidth() / 2 - 16, top);
    screen.print(highElapsedTime);
    //screen.print(spectralGen->getMagnitudeMean());

    const float dt = 1.0f / 60.0f;
    stringAnimation += dt * M_PI * 4;
    if (stringAnimation > M_PI * 2)
    {
      stringAnimation -= M_PI * 2;
    }
  }

};
