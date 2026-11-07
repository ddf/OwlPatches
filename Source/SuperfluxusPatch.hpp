#pragma once

#include "owl/PatchBase.h"
#include "owl/PatchParameterIds.h"
#include "owl/PatchParameterDescription.h"
#include "vessicle/Counter.h"
#include "vessicle/ClockedDelay.h"

class SuperfluxusPatch : public PatchBase
{
  using size_t = vessl::size_t;
  using count_t = uint16_t;
  using Filter = vessl::processors::filter<float, vessl::filtering::moog::band_pass>;
  using Array  = vessl::array<float>;
  using Ramp   = vessl::generators::ramp<float>;
  using Delay  = ClockedDelay<float>;
  using FeedProc = vessl::processors::conditioner<float>;

  static constexpr int count_min = 2;
  static constexpr int count_max = 16;

  static constexpr IntPatchParameterDescription   inc_desc = { "INC", 0, count_max+1, 1 };
  static constexpr IntPatchParameterDescription   max_desc = { "MAX", count_min, count_max+1, count_max+1 };
  static constexpr FloatPatchParameterDescription flt_q_desc = { "Q", 0.7f, 0.99f, 0.86f };
  static constexpr FloatPatchParameterDescription flt_fm_desc = { "FM", 0.f, 4.f, 0.f };
  static constexpr FloatPatchParameterDescription dly_fbk_desc = { "FBK", 0.f, 1.f, 0.f, 0.98f };
  static constexpr FloatPatchParameterDescription dly_mix_desc = { "MIX", 0.f, 1.f, 0.f, 0.f };

  static constexpr OutputParameterDescription   val_a = { "VAL 1", OutputParameterId::A };
  static constexpr OutputParameterDescription   val_b = { "VAL 2", OutputParameterId::B };

  FloatArray delay_buffer_;
  FloatArray scratch_buffer_;

  Counter<count_t> counter_a_;
  Counter<count_t> counter_b_;
  Filter filter_a_;
  Filter filter_b_;
  Ramp   ramp_a_;
  Ramp   ramp_b_;
  FeedProc feed_a_;
  FeedProc feed_b_;
  Delay  delay_a_;
  Delay  delay_b_;

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
    FloatParameter fbk_a;
    FloatParameter mix_a;
    FloatParameter fbk_b;
    FloatParameter mix_b;
  } pin_;
  
public:
  SuperfluxusPatch()
  : delay_buffer_(FloatArray::create(getSampleRate()*16))
  , scratch_buffer_(FloatArray::create(getBlockSize()))
  , filter_a_(getSampleRate(), 880.f, 0.86f)
  , filter_b_(getSampleRate(), 440.f, 0.86f)
  , ramp_a_(getSampleRate(), 0.01f, 440.f, 340.f)
  , ramp_b_(getSampleRate(), 0.01f, 380.f, 280.f)
  , feed_a_(getSampleRate(), 600.f)
  , feed_b_(getSampleRate(), 600.f)
  , delay_a_(Array(delay_buffer_.getData(), delay_buffer_.getSize()/2), getSampleRate(), 16, getSampleRate()*2, 60.f, 0.f, &feed_a_)
  , delay_b_(Array(delay_buffer_.getData() + delay_buffer_.getSize()/2, delay_buffer_.getSize()/2), getSampleRate(), 16, getSampleRate()*2, 60.f, 0.f, &feed_b_)
  {
    pin_.max_a = max_desc.registerParameter(this, "MAX 1");
    pin_.inc_a = inc_desc.registerParameter(this, "INC 1");
    pin_.max_b = max_desc.registerParameter(this, "MAX 2");
    pin_.inc_b = inc_desc.registerParameter(this, "INC 2");
    pin_.fm_a  = flt_fm_desc.registerParameter(this, "FM L");
    pin_.q_a   = flt_q_desc.registerParameter(this, "Q L");
    pin_.fm_b  = flt_fm_desc.registerParameter(this, "FM R");
    pin_.q_b   = flt_q_desc.registerParameter(this, "Q R"); 
    pin_.fbk_a = dly_fbk_desc.registerParameter(this, "DLY FBK 1");
    pin_.mix_a = dly_mix_desc.registerParameter(this, "DLY MIX 1");
    pin_.fbk_b = dly_fbk_desc.registerParameter(this, "DLY FBK 2");
    pin_.mix_b = dly_mix_desc.registerParameter(this, "DLY MIX 2");

    registerParameter(OUT_PARAMETER_A, "VAL 1>");
    registerParameter(OUT_PARAMETER_B, "VAL 2>");
  }

  virtual ~SuperfluxusPatch()
  {
    FloatArray::destroy(delay_buffer_);
    FloatArray::destroy(scratch_buffer_);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples)
  {
    if (bid == BUTTON_1 && value == ON)
    {
      counter_a_.clock();
      delay_a_.clock();
    }

    if (bid == BUTTON_2 && value == ON)
    {
      counter_b_.clock();
      delay_b_.clock();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    Array left = Array(audio.getSamples(0), audio.getSize());
    Array right = Array(audio.getSamples(1), audio.getSize());
    Array scratch = Array(scratch_buffer_.getData(), scratch_buffer_.getSize());

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

    feed_a_.cutoff() = filter_a_.fhz().read_analog() * 4.f;
    feed_b_.cutoff() = filter_b_.fhz().read_analog() * 4.f;

    float numa = static_cast<float>(counter_a_.maximum().read<count_t>()) / count_max;
    float dena = counter_a_.increment().read<count_t>();
    delay_a_.ratio() = dena > 0.f ? numa / dena : 1.f;

    float numb = static_cast<float>(counter_b_.maximum().read<count_t>()) / count_max;
    float denb = counter_b_.increment().read<count_t>(); 
    delay_b_.ratio() = denb > 0.f ? numb / denb : 1.f;

    delay_a_.feedback() = pin_.fbk_a.getValue();
    delay_b_.feedback() = pin_.fbk_b.getValue();

    delay_a_.process<vessl::time::mode::slew>(left, scratch);
    vessl::sample::mix(left, scratch, pin_.mix_a.getValue(), left);

    delay_b_.process<vessl::time::mode::slew>(right, scratch);
    vessl::sample::mix(right, scratch, pin_.mix_b.getValue(), right);

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
    screen.print(max);
    screen.print(" / ");
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
    int x = 7, y = 16, s = 24, w = 5;
    drawCounter(screen, counter_a_, x, y, w);
    drawCounter(screen, counter_b_, x, y + s, w);
  }
};