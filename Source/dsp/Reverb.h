// Based on the reverb from Clouds:
// https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/fx/reverb.h
// Which is itself based on a paper by Dattoro.
#pragma once

#include "vessicle/vessl/vessl.h"
#include "AllpassNetwork.h"

template<typename T = float>
class Reverb : public vessl::unit_processor<vessl::sample::frame<T,2>>
             , vessl::plist<5>
{
public:
  using size_t = vessl::size_t;
  using sample_t = T;
  using SampleFrame = vessl::sample::frame<T,2>;
  using Parameter = vessl::parameter;
  using DelayLine = vessl::sample::delay_line<sample_t>;
  using Allpass4 = AllpassNetwork<sample_t, 4>;
  using Allpass2 = AllpassNetwork<sample_t, 2>;

  static Reverb* create(float sr)
  {
    static size_t diffuse_times[4] { 113, 162, 241, 399 };
    static size_t dap1_times[2] { 1653, 2038 };
    static size_t dap2_times[2] { 1913, 1663 };
    static size_t delay_times[2] { 3411, 4782 };
    
    sample_t* delay_buffer_1 = new sample_t[delay_times[0]];
    sample_t* delay_buffer_2 = new sample_t[delay_times[1]];

    static constexpr sample_t diffusion = vessl::cast<sample_t>(0.625f);
    return new Reverb(sr, diffusion, 
      Allpass4::create(diffuse_times, 0),
      Allpass2::create(dap1_times, 0),
      Allpass2::create(dap2_times, 0),
      delay_buffer_1, delay_times[0], 
      delay_buffer_2, delay_times[1]);
  }

  static void destroy(const Reverb* reverb)
  {
    Allpass4::destroy(reverb->diffuser_);
    Allpass2::destroy(reverb->dap1_);
    Allpass2::destroy(reverb->dap2_);
    delete[] reverb->delay1_.data();
    delete[] reverb->delay2_.data();
    delete reverb;
  }
  
  [[nodiscard]] const parameter_list & parameters() const override { return *this; }
  
  [[nodiscard]] Parameter input_gain() const { return params_.input_gain("input gain", 'g'); }
  [[nodiscard]] Parameter diffusion() const { return params_.diffusion("diffusion", 'd'); }
  [[nodiscard]] Parameter reverb_time() const { return params_.reverb_time("size", 't'); }
  [[nodiscard]] Parameter low_pass() const { return params_.lp_amount("low pass", 'l'); }
  [[nodiscard]] Parameter wet_mix() const { return params_.wet_amount("wet mix", 'w'); }
  
  VESSL_INLINE SampleFrame process(const SampleFrame& in) override
  {
    if (diffusion_ != params_.diffusion.value)
    {
      diffusion_ = params_.diffusion.value;
      diffuser_->diffusion() = diffusion_;
      dap1_->diffusion() = diffusion_;
      dap2_->diffusion() = diffusion_;
    }
    
    sample_t left = in.left();
    sample_t right = in.right();
    sample_t m = (left + right) * params_.input_gain.value;

    sample_t lfo = lfo1_.wave.evaluate(lfo1_.phase);
    lfo1_.phase += lfo1_.phase_step;
    sample_t smear = diffuser_->read(0, 10.0f + lfo * 60.0f);
    diffuser_->write(0, 100, smear);

    sample_t d = diffuser_->process(m);

    sample_t verb_left = d;
    // interpolated read from delay2
    lfo = lfo2_.wave.evaluate(lfo2_.phase);
    lfo2_.phase += lfo2_.phase_step;
    float df = 4680.0f + lfo*100.0f;
    verb_left += delay2_.readf(df);
    
    // low pass filter
    lp_decay1_ += params_.lp_amount.value * (verb_left - lp_decay1_);
    // through two allpass filters
    verb_left = dap1_->process(lp_decay1_);
    sample_t pd1 = delay1_.write(verb_left);
    verb_left *= 2;

    sample_t verb_right = d + pd1 * params_.reverb_time.value;
    lp_decay2_ += params_.lp_amount.value * (verb_right - lp_decay2_);
    verb_right = dap2_->process(lp_decay2_);
    delay2_.write(verb_right);
    verb_right *= 2;

    sample_t wet = params_.wet_amount.value;
    return { left + (verb_left - left) * wet, right + (verb_right - right) * wet };
  }
  
protected:
  [[nodiscard]] Parameter element_at(vessl::size_t index) const override
  {
    switch (index)
    {
    case 0: return input_gain();
    case 1: return diffusion();
    case 2: return reverb_time();
    case 3: return low_pass();
    case 4: return wet_mix();
    default: return Parameter::none();
    }
  }
  
private:
  struct Lfo
  {
    vessl::phase_t phase = 0;
    vessl::phase_t phase_step = 0;
    vessl::sample::waves::unipolar::sine<sample_t> wave;
  };

  Allpass4* diffuser_;
  Allpass2* dap1_;
  Allpass2* dap2_;
  DelayLine delay1_;
  DelayLine delay2_;
  Lfo lfo1_;
  Lfo lfo2_; 
  sample_t lp_decay1_;
  sample_t lp_decay2_;
  sample_t diffusion_;
  
  using sample_p = vessl::param<sample_t>;
  
  struct
  {
    sample_p input_gain;
    sample_p diffusion;
    sample_p reverb_time;
    sample_p lp_amount;
    sample_p wet_amount;
  } params_;

  Reverb(float sample_rate,
         sample_t diffusion_amount,
         Allpass4* diffuser, 
         Allpass2* dap1, 
         Allpass2* dap2, 
         sample_t* delay_data_1, size_t delay_data_size_1, 
         sample_t* delay_data_2, size_t delay_data_size_2)
    : diffuser_(diffuser)
    , dap1_(dap1)
    , dap2_(dap2)
    , delay1_(delay_data_1, delay_data_size_1)
    , delay2_(delay_data_2, delay_data_size_2)
    , lp_decay1_(0)
    , lp_decay2_(0)
    , diffusion_(diffusion_amount)
  {
    lfo1_.phase_step = 0.5f * vessl::cast<vessl::phase_t>(1.f / sample_rate);
    lfo2_.phase_step = 0.3f * vessl::cast<vessl::phase_t>(1.f / sample_rate);
    params_.input_gain.value = vessl::cast<sample_t>(0.2f);
    params_.reverb_time.value = 0;
    params_.lp_amount.value = vessl::cast<sample_t>(0.7f);
    params_.wet_amount.value = 0;
    params_.diffusion.value = diffusion_;
    diffuser_->diffusion() = diffusion_;
    dap1_->diffusion() = diffusion_;
    dap2_->diffusion() = diffusion_;
  }
};
