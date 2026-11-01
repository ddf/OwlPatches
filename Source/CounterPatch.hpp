#pragma once

#include "PatchBase.h"
#include "PatchParameterIds.h"
#include "PatchParameterDescription.h"
#include "vessicle/Counter.h"

class CounterPatch : public PatchBase
{
  using size_t = vessl::size_t;
  using count_t = uint16_t;
  using Filter = vessl::processors::filter<float, vessl::filtering::moog::band_pass>;
  using Array  = vessl::array<float>;
  using Ramp   = vessl::generators::ramp<float>;
  
  static constexpr int count_min = 3;
  static constexpr int count_max = 15;

  static constexpr IntPatchParameterDescription   inc_desc = { "INC", 0, 16, 1 };
  static constexpr IntPatchParameterDescription   max_desc = { "MAX", count_min, count_max+1, count_max };
  static constexpr FloatPatchParameterDescription flt_q_desc = { "Q", 0.7f, 0.99f, 0.86f };
  static constexpr FloatPatchParameterDescription flt_fm_desc = { "FM", 0.f, 4.f, 0.f };

  static constexpr OutputParameterDescription   val_a = { "VAL 1", OutputParameterId::A };
  static constexpr OutputParameterDescription   val_b = { "VAL 2", OutputParameterId::B };

  Counter<count_t> counter_a_;
  Counter<count_t> counter_b_;
  Filter filter_a_;
  Filter filter_b_;
  Ramp   ramp_a_;
  Ramp   ramp_b_;

  struct 
  {
    IntParameter inc_a;
    IntParameter inc_b;
    IntParameter max_a;
    IntParameter max_b;
    FloatParameter fm_a;
    FloatParameter q_a;
    FloatParameter fm_b;
    FloatParameter q_b;
  } pin_;
  
public:
  CounterPatch()
  : filter_a_(getSampleRate(), 880.f, 0.86f)
  , filter_b_(getSampleRate(), 440.f, 0.86f)
  , ramp_a_(getSampleRate(), 0.01f, 440.f, 340.f)
  , ramp_b_(getSampleRate(), 0.01f, 380.f, 280.f)
  {
    pin_.inc_a = inc_desc.registerParameter(this, "INC 1");
    pin_.max_a = max_desc.registerParameter(this, "MAX 1");
    pin_.inc_b = inc_desc.registerParameter(this, "INC 2");
    pin_.max_b = max_desc.registerParameter(this, "MAX 2");
    pin_.fm_a  = flt_fm_desc.registerParameter(this, "FM L");
    pin_.q_a   = flt_q_desc.registerParameter(this, "Q L");
    pin_.fm_b  = flt_fm_desc.registerParameter(this, "FM R");
    pin_.q_b   = flt_q_desc.registerParameter(this, "Q R"); 

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
    Array left = Array(audio.getSamples(0), audio.getSize());
    Array right = Array(audio.getSamples(1), audio.getSize());

    counter_a_.increment() = static_cast<count_t>(pin_.inc_a.getValue());
    counter_b_.increment() = static_cast<count_t>(pin_.inc_b.getValue());

    counter_a_.maximum() = (count_t)vessl::math::constrain(pin_.max_a.getValue(), count_min, count_max);
    counter_b_.maximum() = (count_t)vessl::math::constrain(pin_.max_b.getValue(), count_min, count_max);

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

    if (trig_a)
    {
      ramp_a_.trigger();
    }
    if (trig_b)
    {
      ramp_b_.trigger();
    }

    filter_a_.q() = pin_.q_a.getValue();
    filter_b_.q() = pin_.q_b.getValue();

    float afm = pin_.fm_a.getValue();
    float bfm = pin_.fm_b.getValue();
    for(int i = 0; i < size; ++i)
    {
      float af = ramp_a_.generate();
      float bf = ramp_b_.generate();
      af += af*afm;
      bf += bf*bfm;
      filter_a_.fhz() = af + af*left[i]*0.5f;
      filter_b_.fhz() = bf + bf*right[i]*0.5f;

      left[i]  = filter_a_.process(trig_a*2.5f);
      right[i] = filter_b_.process(trig_b*2.5f);
    }

    setParameterValue(OUT_PARAMETER_A, static_cast<float>(counter_a_.value()) / (count_max + 1.f));
    setParameterValue(OUT_PARAMETER_B, static_cast<float>(counter_b_.value()) / (count_max + 1.f));

    setButton(BUTTON_1, trig_a ? ON : OFF);
    setButton(BUTTON_2, trig_b ? ON : OFF);
  }

  void drawCounter(MonochromeScreenBuffer& screen, Counter<count_t>& c, int x, int y, int w)
  {
    count_t val = c.value();
    count_t inc = c.increment().read<count_t>();
    count_t max = c.maximum().read<count_t>();
    screen.setCursor(x - w/2, y - w/2 - 2);
    screen.print("+");
    screen.print(inc);
    for(count_t i = 0; i < max; ++i)
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
    int x = 7, y = 16, s = 24, w = 6;
    drawCounter(screen, counter_a_, x, y, w);
    drawCounter(screen, counter_b_, x, y + s, w);
  }
};