#include "MonoChromeScreenPatch.h"
#include "SpectralHarpPatch.hpp"

typedef SpectralHarpPatch<4096, MonochromeScreenPatch> BasePatch;

class SpectralHarpGeniusPatch : public BasePatch
{
  const int padding = 4;
  float stringAnimation = 0;

public:
  void processAudio(AudioBuffer& audio) override
  {
    // have to invert audio input on Genius
    audio.getSamples(0).multiply(-1);
    audio.getSamples(1).multiply(-1);

    BasePatch::processAudio(audio);
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int height = screen.getHeight() - 18;
    const int numBands = roundf(bandDensity);
    for (int b = 0; b <= numBands; ++b)
    {
      float freq = frequencyOfString(b, numBands, bandFirst, bandLast, linLogLerp);
      float x = Interpolator::linear(padding, screen.getWidth() - padding, (float)b / numBands);
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
      screen.drawLine(x, 0, x, 1, WHITE);
      screen.drawLine(x, height - 1, x, height, WHITE);
      for (int y = 2; y < height - 1; ++y)
      {
        float s1 = (float)y / height * M_PI * band.amplitude * 24 + band.phase;
        if (fabsf(band.amplitude*sinf(s1)) > 0.25f)
        {
          screen.setPixel(x, y, WHITE);
        }
      }
    }

    const float dt = 1.0f / 60.0f;
    stringAnimation += dt * M_PI * 4;
    if (stringAnimation > M_PI*2)
    {
      stringAnimation -= M_PI*2;
    }
  }

};
