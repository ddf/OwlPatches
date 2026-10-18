#pragma once

#include "PatchBase.h"
#include "vessicle/Counter.h"

class CounterPatch : public PatchBase
{

  Counter<uint16_t> counters_[4];
  size_t counter_idx_ = 0;

public:
  CounterPatch()
  {
    for(int c = 0; c < 4; ++c)
    {
      counters_[c].increment() = vessl::math::random::range<uint16_t>(1, 8);
    }

    registerParameter(PARAMETER_A, "INC 1");
    registerParameter(PARAMETER_B, "INC 2");
    registerParameter(PARAMETER_C, "INC 3");
    registerParameter(PARAMETER_D, "INC 4");

    registerParameter(PARAMETER_E, "MAX 1");
    registerParameter(PARAMETER_F, "MAX 2");
    registerParameter(PARAMETER_G, "MAX 3");
    registerParameter(PARAMETER_H, "MAX 4");
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples)
  {
    if (bid == BUTTON_1 && value == ON)
    {
      // counters_[counter_idx_++].clock();
      // counter_idx_ &= 3;
      for(int i = 0; i < 4; ++i)
      {
        counters_[i].clock();
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    counters_[0].increment() = static_cast<uint16_t>(getParameterValue(PARAMETER_A)*16);
    counters_[1].increment() = static_cast<uint16_t>(getParameterValue(PARAMETER_B)*16);
    counters_[2].increment() = static_cast<uint16_t>(getParameterValue(PARAMETER_C)*16);
    counters_[3].increment() = static_cast<uint16_t>(getParameterValue(PARAMETER_D)*16);

    counters_[0].maximum() = static_cast<uint16_t>(16 - getParameterValue(PARAMETER_E)*12);
    counters_[1].maximum() = static_cast<uint16_t>(16 - getParameterValue(PARAMETER_F)*12);
    counters_[2].maximum() = static_cast<uint16_t>(16 - getParameterValue(PARAMETER_G)*12);
    counters_[3].maximum() = static_cast<uint16_t>(16 - getParameterValue(PARAMETER_H)*12);

    uint16_t trig = 0;
    size_t size = audio.getSize();
    for(int i = 0; i < size; ++i)
    {
      uint16_t sum = 0;
      for(int c = 0; c < 4; ++c)
      {
        sum += counters_[c].generate();
        trig |= counters_[c].carry().read<uint16_t>() > 0;
      }
      audio.getSamples(0)[i] = -1.f + (float)sum / 32;
      audio.getSamples(1)[i] = -1.f + (float)sum / 32;
    }

    setButton(BUTTON_1, trig);
  }

  void drawCounter(MonochromeScreenBuffer& screen, Counter<uint16_t>& c, int x, int y)
  {
    uint16_t dim = c.value();
    screen.fillRectangle(x - dim/2, y - dim/2, dim, dim, WHITE);
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    int x = 16, y = 16, s = 32;
    for(int i = 0; i < 4; ++i)
    {
      drawCounter(screen, counters_[i], x + i*s, y);
    }
  }
};