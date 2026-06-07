#pragma once

#include "Patch.h"
#include "VoltsPerOctave.h"
#include "Reverb.h"
#include "Grain.hpp"

//#define PROFILE

#ifdef PROFILE
#include <string.h>
#endif

// must be power of two
static constexpr int RECORD_BUFFER_SIZE = 1 << 18; // approx 5.5 seconds at 48k
static constexpr int RECORD_BUFFER_WRAP = RECORD_BUFFER_SIZE - 1;

using GrainType = Grain;
using Array = vessl::array<float>;
using HighPassFilter = vessl::processors::filter<float, vessl::filtering::biquad<1>::high_pass>;
using DcBlockingFilter = vessl::processors::filter<float, vessl::filtering::dc_block>;
using Clock = vessl::generators::clock<uint8_t>;
using Noise = vessl::generators::noise<float, vessl::noise::white>;
using Smoother = vessl::math::easing::smoother<float>;

template <int MaxGrains, bool WithReverb>
class GrainzBase : public Patch
{
  GrainSample*     record_buffer_;
  AudioBuffer*     grain_buffer_;
  AudioBuffer*     feedback_buffer_;
  Reverb*          reverb_;
  DcBlockingFilter dc_filter_left_;
  DcBlockingFilter dc_filter_right_;
  HighPassFilter   feedback_filter_left_;
  HighPassFilter   feedback_filter_right_;
  Clock            clock_;
  Noise            noise_;
  
  // panel controls
  struct
  {
    PatchParameterId duration = PARAMETER_A;
    PatchParameterId speed    = PARAMETER_B;
    PatchParameterId position = PARAMETER_C;
    PatchParameterId feedback = PARAMETER_D;
    PatchParameterId density  = PARAMETER_E;
    PatchParameterId reverb   = PARAMETER_H;
    PatchButtonId    trigger  = BUTTON_1;
    PatchButtonId    clock    = BUTTON_2;
    PatchButtonId    freeze   = BUTTON_3;

    // midi controls
    PatchParameterId envelope = PARAMETER_AA;
    PatchParameterId spread   = PARAMETER_AB;
    PatchParameterId velocity = PARAMETER_AC;
    PatchParameterId dry_wet  = PARAMETER_AD;
  } pin_;

  // outputs
  struct 
  {  
    PatchButtonId    grain_played   = PUSHBUTTON;
    PatchButtonId    random_gate    = BUTTON_6;
    PatchParameterId grain_playback = PARAMETER_F;
    PatchParameterId random_value   = PARAMETER_G;
  } pout_;
  
  Smoother grain_overlap_;
  Smoother grain_rate_;
  Smoother grain_position_;
  Smoother grain_duration_;
  Smoother grain_speed_;
  Smoother grain_envelope_;
  Smoother grain_spread_;
  Smoother grain_velocity_;
  Smoother feedback_;
  Smoother reverb_amount_;
  Smoother dry_wet_;
  
  VoltsPerOctave voct_;
  
  int record_write_index_;
  int active_grains_;
  int out_gate_sample_length_;
  int played_gate_;
  int random_gate_;

  // these are in seconds
  float grain_duration_min_;
  float grain_duration_max_;
  
  float grain_rate_phasor_;
  float grain_trigger_delay_;
  float noise_value_;
  
  uint16_t  freeze_; 
  uint8_t   clock_value_;
  uint8_t   grain_triggered_;
  
  GrainType* grains_[MaxGrains];
  float norms_[MaxGrains + 1];
  int available_grains_[MaxGrains];

public:
  GrainzBase()
    : record_buffer_(nullptr)
    , grain_buffer_(nullptr)
    , dc_filter_left_(getSampleRate())
    , dc_filter_right_(getSampleRate())
    , feedback_filter_left_(getSampleRate())
    , feedback_filter_right_(getSampleRate())
    , clock_(getSampleRate(), 2, getSampleRate()*4)
    , noise_(getSampleRate())
    , voct_(-0.5f, 4)
    , record_write_index_(0)
    , active_grains_(0)
    , out_gate_sample_length_(getBlockSize()) // 8ms
    , played_gate_(0)
    , random_gate_(0)
    , grain_duration_min_(2.0f/getSampleRate())
    , grain_duration_max_(0.25f*(RECORD_BUFFER_SIZE/getSampleRate()))
    , grain_rate_phasor_(0)
    , noise_value_(0)
    , freeze_(OFF)
    , clock_value_(0)
    , grain_triggered_(false)
  {
    norms_[0] = 1;
    for (int i = 1; i < MaxGrains + 1; i++) 
    {
      norms_[i] = 1 / sqrtf(static_cast<float>(i));
    }
    voct_.setTune(-4);
    feedback_buffer_ = AudioBuffer::create(2, getBlockSize());
    feedback_filter_left_.q() = vessl::filtering::q::butterworth<float>();
    feedback_filter_right_.q() = vessl::filtering::q::butterworth<float>();

    record_buffer_ = new GrainSample[RECORD_BUFFER_SIZE];
    grain_buffer_ = AudioBuffer::create(2, getBlockSize());

    for (int i = 0; i < MaxGrains; ++i)
    {
      grains_[i] = GrainType::create(record_buffer_, RECORD_BUFFER_SIZE);
    }

    if constexpr (WithReverb)
    {
      reverb_ = Reverb::create(getSampleRate());
    }

    registerParameter(pin_.position, "Position");
    registerParameter(pin_.duration, "Duration");
    registerParameter(pin_.speed, "Speed");
    registerParameter(pin_.density, "Density");
    registerParameter(pin_.envelope, "Envelope");
    registerParameter(pin_.spread, "Spread");
    registerParameter(pin_.velocity, "Velocity Variation");
    registerParameter(pin_.feedback, "Feedback");
    registerParameter(pin_.dry_wet, "Dry/Wet");
    if constexpr (WithReverb)
    {
      registerParameter(pin_.reverb, "Reverb");
      setParameterValue(pin_.reverb, 0);
    }
    registerParameter(pout_.grain_playback, "Playback>");
    registerParameter(pout_.random_value, "Random>");

    // default to triangle window
    setParameterValue(pin_.envelope, 0.5f);
    setParameterValue(pin_.spread, 0);
    setParameterValue(pin_.velocity, 0);
    setParameterValue(pin_.feedback, 0);
    setParameterValue(pin_.dry_wet, 1);
    setParameterValue(pin_.feedback, 0);
  }

  ~GrainzBase() override
  {
    AudioBuffer::destroy(feedback_buffer_);
    AudioBuffer::destroy(grain_buffer_);

    delete[] record_buffer_;

    for (int i = 0; i < MaxGrains; i+=2)
    {
      GrainType::destroy(grains_[i]);
    }

    if constexpr (WithReverb)
    {
      Reverb::destroy(reverb_);
    }
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == pin_.trigger && value == ON)
    {
      grain_trigger_delay_ = samples;
      grain_triggered_ = true;
    }
    else if (bid == pin_.clock && value == ON)
    {
      clock_.tap(samples);
    }
    else if (bid == pin_.freeze && value == ON)
    {
      freeze_ = freeze_ == ON ? OFF : ON;
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
#ifdef PROFILE
    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "blk ");
    debugCpy = stpcpy(debugCpy, msg_itoa(audio.getSize(), 10));
    const float processStart = getElapsedBlockTime();
#endif
    const int blockSize = audio.getSize();
    Array in_out_left(audio.getSamples(0), blockSize);
    Array in_out_right(audio.getSamples(1), blockSize);
    Array grain_left(grain_buffer_->getSamples(0).getData(), blockSize);
    Array grain_right(grain_buffer_->getSamples(1).getData(), blockSize);
    Array feed_left(feedback_buffer_->getSamples(0).getData(), blockSize);
    Array feed_right(feedback_buffer_->getSamples(1).getData(), blockSize);

    // like Clouds, Density describes how many grains we want playing simultaneously at any given time
    float density_param = getParameterValue(pin_.density);
    grain_overlap_ = vessl::math::interp<vessl::math::easing::quad::in>(0.f, 0.999f, density_param);
    grain_rate_ = density_param < 0.45f ? vessl::math::lerp(4.0f, 1.0f, density_param)
      : density_param > 0.55f ? vessl::math::lerp(1.0f, 0.25f, density_param)
        : 1.0f;
    grain_position_ = vessl::math::interp<vessl::math::easing::expo::in>(
      1.f, 0.25f*RECORD_BUFFER_SIZE, getParameterValue(pin_.position)
    );
    grain_duration_ = vessl::math::interp<vessl::math::easing::expo::in>(
      grain_duration_min_, grain_duration_max_, getParameterValue(pin_.duration)
    );
    grain_speed_ = voct_.getFrequency(getParameterValue(pin_.speed)) / 440.0f;
    grain_envelope_ = getParameterValue(pin_.envelope);
    grain_spread_ = getParameterValue(pin_.spread);
    grain_velocity_ = getParameterValue(pin_.velocity);
    feedback_ = getParameterValue(pin_.feedback);
    reverb_amount_ = getParameterValue(pin_.reverb);
    dry_wet_ = getParameterValue(pin_.dry_wet);

    dc_filter_left_.process(in_out_left, in_out_left);
    dc_filter_right_.process(in_out_right, in_out_right);

    if (played_gate_ > 0)
    {
      played_gate_ -= blockSize;
    }
    
    if (random_gate_ > 0)
    {
      random_gate_ -= blockSize;
    }

    // #TODO: clouds does a cool thing where when freeze is enabled
    // it continues recording input for 256 samples into a "tail" buffer
    // and then when freeze is disabled it crossfades from the tail to 
    // the new incoming audio as it writes into the record buffer,
    // which prevents discontinuities in the record buffer.
    // #TODO: add smoothed freeze state for fading feedback in/out.
    if (freeze_ == OFF)
    {
      // Note: the way feedback is applied is based on how Clouds does it
      float cutoff = (20.0f + 100.0f * feedback_.value * feedback_.value);
      feedback_filter_left_.fhz() = cutoff;
      feedback_filter_right_.fhz() = cutoff;
      feedback_filter_left_.process(feed_left, feed_left);
      feedback_filter_right_.process(feed_right, feed_right);
      float soft_limit_coeff = feedback_.value * 1.4f;
      for (int i = 0; i < blockSize; ++i)
      {
        float left = in_out_left[i];
        float right = in_out_right[i];
        left += feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_left[i] + left) - left);
        right += feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_right[i] + right) - right);
        GrainSample& grain_sample = record_buffer_[record_write_index_];
        grain_sample.re = left;
        grain_sample.im = right;
        record_write_index_ = (record_write_index_ + 1) & RECORD_BUFFER_WRAP;
      }
    }
    
    float grain_playback_rate = grain_speed_.value;
    float grain_sample_length = grain_duration_.value * getSampleRate();
    float grain_spacing;
    if (clock_.is_clocked())
    {
      float dur = clock_.tempo().read<vessl::time::duration>().to_seconds(getSampleRate());
      grain_spacing = dur * getSampleRate() * grain_rate_.value;
    }
    else
    {
      float target_grains = MaxGrains * grain_overlap_.value;
      grain_spacing = target_grains > 0.0001f ? grain_sample_length / target_grains : 0;
      clock_.tempo() = vessl::duration_t::from_seconds(grain_spacing / getSampleRate(), getSampleRate());
    }
    // we want a grain to always last the same amount of real time, regardless of playback rate.
    // so now we adjust the length with playback speed
    grain_sample_length *= grain_playback_rate;

    int num_available_grains = updateAvailableGrains();
    const int read_idx = record_write_index_ - blockSize;
    float grain_envelope = grain_envelope_.value;
    bool grains_enabled = grain_spacing > 0;
    float grain_phasor_rate = grains_enabled ? 1.0f : 0.f;
    for (int i = 0; i < blockSize; ++i)
    {
      grain_rate_phasor_ += grain_phasor_rate;
      bool start_steady = grains_enabled && grain_rate_phasor_ >= grain_spacing;
      bool start_grain = start_steady || grain_triggered_;
      if (start_grain && num_available_grains)
      {
        --num_available_grains;
        int gidx = available_grains_[num_available_grains];
        GrainType* g = grains_[gidx];
        float gdi = static_cast<float>(i);
        int grain_delay = gdi > grain_trigger_delay_ ? gdi : grain_trigger_delay_;
        float grain_end_pos = static_cast<float>(read_idx + i) - grain_position_.value;
        float pan = 0.5f + (vessl::math::random::range(0.f, 1.f) - 0.5f) * grain_spread_.value;
        float vel = 1.0f + (vessl::math::random::range(0.f, 1.f) * 2 - 1.0f) * grain_velocity_.value;
        g->trigger(grain_delay, grain_end_pos, grain_sample_length,
          grain_playback_rate, grain_envelope, pan, vel);
        grain_triggered_ = false;
        grain_trigger_delay_ = 0;
      }
      if (start_steady)
      {
        played_gate_ = out_gate_sample_length_;
        grain_rate_phasor_ -= grain_spacing;
      }
    }

#ifdef PROFILE
    const float genStart = getElapsedBlockTime();
#endif
    float avg_progress = 0;
    float avg_envelope = 0;
    int prev_active_grains = active_grains_;
    active_grains_ = 0;

    grain_left.fill(0);
    grain_right.fill(0);
    
    for (int gi = 0; gi < MaxGrains; ++gi)
    {
      GrainType* g = grains_[gi];

      if (!g->is_done)
      {
        avg_envelope += g->envelope();
        avg_progress += g->progress();
        ++active_grains_;

        for (int i = 0; i < blockSize; ++i)
        {
          GrainSample grain_sample = g->generate();
          grain_left[i] += grain_sample.re;
          grain_right[i] += grain_sample.im;
        }
      }
    }
    
    // float from_gain_adjust = norms_[prev_active_grains];
    // float to_gain_adjust = norms_[active_grains_];
    // grain_left.scale(from_gain_adjust, to_gain_adjust);
    // grain_right.scale(from_gain_adjust, to_gain_adjust);
    grain_left.copy_to(feed_left);
    grain_right.copy_to(feed_right);

    if (active_grains_ > 0)
    {
      avg_envelope /= active_grains_;
      avg_progress /= active_grains_;
    }
#ifdef PROFILE
    const float genTime = getElapsedBlockTime() - genStart;
    debugCpy = stpcpy(debugCpy, " gen(");
    debugCpy = stpcpy(debugCpy, msg_itoa(active_grains_, 10));
    debugCpy = stpcpy(debugCpy, ") ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(genTime * 1000), 10));
#endif

    // #TODO reverb can also wind up with DC offset 
    // in freeze mode when feedback is engaged.
    if constexpr (WithReverb)
    {
      float reverb_level = reverb_amount_.value * 0.95f;
      reverb_level += feedback_.value * (2.0f - feedback_.value) * freeze_;
      reverb_level = vessl::math::constrain(reverb_level, 0.0f, 1.0f);

      reverb_->setAmount(reverb_level * 0.54f);
      reverb_->setDiffusion(0.7f);
      reverb_->setReverbTime(0.35f + 0.63f * reverb_level);
      reverb_->setInputGain(0.2f);
      reverb_->setLowPass(0.6f + 0.37f * feedback_.value);
      reverb_->process(*grain_buffer_, *grain_buffer_);
    }
    
    noise_.rate() = clock_.tempo().read<vessl::time::duration>().to_frequency(getSampleRate());

    const float wet_amt = dry_wet_.value;
    const float dry_amt = 1.0f - wet_amt;
    for (int i = 0; i < blockSize; ++i)
    {
      in_out_left[i]  = in_out_left[i]*dry_amt  + grain_left[i]*wet_amt;
      in_out_right[i] = in_out_right[i]*dry_amt + grain_right[i]*wet_amt;
      
      noise_value_ = noise_.generate<vessl::math::easing::smoothstep>()*0.5f + 0.5f;
      uint8_t cs = clock_.generate();
      if (cs > clock_value_)
      {
        if (noise_value_ < vessl::math::random::range(0.f, 1.f))
        {
          random_gate_ = out_gate_sample_length_;
        }
      }
      clock_value_ = cs;
    }

    setButton(pin_.freeze, freeze_);
    setButton(pout_.grain_played, played_gate_ > 0);
    setButton(pout_.random_gate, random_gate_ > 0);
    setParameterValue(pout_.grain_playback, avg_progress);
    setParameterValue(pout_.random_value, noise_value_);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - processStart - genTime;
    debugCpy = stpcpy(debugCpy, " proc ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debugMsg);
#endif
  }
private:
  
  VESSL_INLINE int updateAvailableGrains()
  {
    int count = 0;
    for (int gi = 0; gi < MaxGrains; ++gi)
    {
      if (grains_[gi]->is_done)
      {
        available_grains_[count++] = gi;
      }
    }
    return count;
  }
};

#ifdef OWL_WITCH
using GrainzPatch = GrainzBase<20,false>;
#else
using GrainzPatch = GrainzBase<56,true>;
#endif