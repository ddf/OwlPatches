#pragma once

#include "PatchBase.h"
#include "PatchParameterIds.h"
#include "PatchParameterDescription.h"
#include "vessicle/Counter.h"

class CounterPatch : public PatchBase
{
  using size_t = vessl::size_t;
  using count_t = uint16_t;
  
  static constexpr int count_max = 15;

  static constexpr IntPatchParameterDescription inc_a = { "INC 1", 0, 8, };
  static constexpr IntPatchParameterDescription inc_b = { "INC 2", 0, 8, };
  static constexpr IntPatchParameterDescription max_a = { "MAX 1", count_max, 3, count_max };
  static constexpr IntPatchParameterDescription max_b = { "MAX 2", count_max, 3, count_max };
  static constexpr OutputParameterDescription   val_a = { "VAL 1", OutputParameterId::A };
  static constexpr OutputParameterDescription   val_b = { "VAL 2", OutputParameterId::B };

  Counter<count_t> counter_a_;
  Counter<count_t> counter_b_;

  struct 
  {
    IntParameter inc_a;
    IntParameter inc_b;
    IntParameter max_a;
    IntParameter max_b;
  } pin_;
  
public:
  CounterPatch()
  {
    pin_.inc_a = inc_a.registerParameter(this);
    pin_.inc_b = inc_b.registerParameter(this);
    pin_.max_a = max_a.registerParameter(this);
    pin_.max_b = max_b.registerParameter(this);

    registerParameter(OUT_PARAMETER_A, "VAL 1>");
    registerParameter(OUT_PARAMETER_B, "VAL 2>");
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples)
  {
    if (bid == BUTTON_1 && value == ON)
    {
      counter_a_.clock();
    }

    if (bid == BUTTON_2 && value == ON)
    {
      counter_b_.clock();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    counter_a_.increment() = static_cast<count_t>(pin_.inc_a.getValue());
    counter_b_.increment() = static_cast<count_t>(pin_.inc_b.getValue());

    counter_a_.maximum() = static_cast<count_t>(pin_.max_a.getValue());
    counter_b_.maximum() = static_cast<count_t>(pin_.max_b.getValue());

    bool trig_a = 0;
    bool trig_b = 0;
    size_t size = audio.getSize();
    for(int i = 0; i < size; ++i)
    {
      count_t a = counter_a_.generate(); 
      count_t carry = counter_a_.carry().read<count_t>();

      if (carry)
      {
        trig_a |= true;
        if (counter_b_.add(carry))
        {
          trig_b |= true;
        }
        counter_a_.carry() = 0u;
      }

      count_t b = counter_b_.generate();
      carry = counter_b_.carry().read<count_t>();
      if (carry)
      {
        trig_b |= true;
        if (counter_a_.add(carry))
        {
          trig_a |= true;
        }
        counter_b_.carry() = 0u;
      }
    }

    setParameterValue(OUT_PARAMETER_A, static_cast<float>(counter_a_.value()) / (count_max + 1.f));
    setParameterValue(OUT_PARAMETER_B, static_cast<float>(counter_b_.value()) / (count_max + 1.f));

    setButton(BUTTON_1, trig_a ? ON : OFF);
    setButton(BUTTON_2, trig_b ? ON : OFF);
  }

  void drawCounter(MonochromeScreenBuffer& screen, Counter<count_t>& c, int x, int y, int w)
  {
    uint16_t val = c.value();
    for(count_t i = 0; i < count_max; ++i)
    {
      int cx = x + i*(w+2);
      if (i < val)
      {
        screen.fillRectangle(cx - w/2, y - w/2, w, w, WHITE);
      }
      else
      {
        screen.drawRectangle(cx -w/2, y - w/2, w, w, WHITE);
      }
    }
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    int x = 7, y = 8, s = 16, w = 6;
    drawCounter(screen, counter_a_, x, y, w);
    drawCounter(screen, counter_b_, x, y + s, w);
  }
};