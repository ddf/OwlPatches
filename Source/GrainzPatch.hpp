#pragma once

#include "Patch.h"
#include "CircularBuffer.h"
#include "VoltsPerOctave.h"
#include "Reverb.h"
#include "Grain.hpp"

//#define PROFILE

#ifdef PROFILE
#include <string.h>
#endif

// must be power of two
static constexpr int RECORD_BUFFER_SIZE = 1 << 19; // approx 11 seconds at 48k
static constexpr int RECORD_BUFFER_WRAP = RECORD_BUFFER_SIZE - 1;

using GrainType = Grain;
using Array = vessl::array<float>;
using HighPassFilter = vessl::processors::filter<float, vessl::filtering::biquad<1>::high_pass>;
using DcBlockingFilter = vessl::processors::filter<float, vessl::filtering::dc_block>;

template <int MaxGrains, bool WithReverb>
class GrainzBase : public Patch
{
  // panel controls
  struct
  {
    PatchParameterId position = PARAMETER_A;
    PatchParameterId size     = PARAMETER_B;
    PatchParameterId speed    = PARAMETER_C;
    PatchParameterId density  = PARAMETER_D;
    PatchParameterId dry_wet  = PARAMETER_E;
    PatchParameterId reverb   = PARAMETER_H;
    PatchButtonId    freeze   = BUTTON_1;
    PatchButtonId    trigger  = BUTTON_2;

    // midi controls
    PatchParameterId envelope = PARAMETER_AA;
    PatchParameterId spread   = PARAMETER_AB;
    PatchParameterId velocity = PARAMETER_AC;
    PatchParameterId feedback = PARAMETER_AD;
  } pin_;

  // outputs
  struct 
  {  
    PatchButtonId    grain_played   = PUSHBUTTON;
    PatchParameterId grain_playback = PARAMETER_F;
    PatchParameterId grain_envelope = PARAMETER_G;
  } pout_;
  
  VoltsPerOctave voct_;
  DcBlockingFilter dc_filter_left_;
  DcBlockingFilter dc_filter_right_;

  GrainSample* record_buffer_;
  int record_write_index_;

  GrainType* grains_[MaxGrains];
  int available_grains_[MaxGrains];
  int active_grains_;
  uint16_t  freeze_;
  AudioBuffer* grain_buffer_;
  float grain_rate_phasor_;
  bool grain_triggered_;
  float grain_trigger_delay_;

  // these are expressed as a percentage of the total buffer size
  float min_grain_size_;
  float max_grain_size_;

  int played_gate_sample_length_;
  int played_gate_;

  AudioBuffer* feedback_buffer_;
  HighPassFilter feedback_filter_left_;
  HighPassFilter feedback_filter_right_;
  Reverb* reverb_;

  SmoothFloat grain_overlap_;
  SmoothFloat grain_position_;
  SmoothFloat grain_size_;
  SmoothFloat grain_speed_;
  SmoothFloat grain_envelope_;
  SmoothFloat grain_spread_;
  SmoothFloat grain_velocity_;
  SmoothFloat feedback_;
  SmoothFloat reverb_amount_;
  SmoothFloat dry_wet_;
  float norms_[MaxGrains + 1];

public:
  GrainzBase()
    : voct_(-0.5f, 4), dc_filter_left_(getSampleRate()), dc_filter_right_(getSampleRate())
    , record_buffer_(nullptr), record_write_index_(0)
    , active_grains_(0), freeze_(OFF), grain_buffer_(nullptr), grain_rate_phasor_(0)
    , grain_triggered_(false) // 8ms
    , min_grain_size_(getSampleRate()*0.008f / RECORD_BUFFER_SIZE) // 1 second
    , max_grain_size_(getSampleRate()*1.0f / RECORD_BUFFER_SIZE), played_gate_sample_length_(10 * getSampleRate() / 1000)
    , played_gate_(0)
    , feedback_filter_left_(getSampleRate()), feedback_filter_right_(getSampleRate())
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
    registerParameter(pin_.size, "Size");
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
    registerParameter(pout_.grain_envelope, "Envelope>");

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
    const int size = audio.getSize();
    Array in_out_left(audio.getSamples(0), size);
    Array in_out_right(audio.getSamples(1), size);
    Array grain_left(grain_buffer_->getSamples(0).getData(), size);
    Array grain_right(grain_buffer_->getSamples(1).getData(), size);
    Array feed_left(feedback_buffer_->getSamples(0).getData(), size);
    Array feed_right(feedback_buffer_->getSamples(1).getData(), size);

    // like Clouds, Density describes how many grains we want playing simultaneously at any given time
    float grain_density = getParameterValue(pin_.density);
    float overlap = 0;
    if (grain_density >= 0.53f)
    {
      overlap = (grain_density - 0.53f) * 2.12f;
    }
    else if (grain_density <= 0.47f)
    {
      overlap = (0.47f - grain_density) * 2.12f;
    }
    grain_overlap_ = overlap * overlap * overlap;
    grain_position_ = getParameterValue(pin_.position)*0.25f;
    grain_size_ = (min_grain_size_ + getParameterValue(pin_.size)*(max_grain_size_ - min_grain_size_));
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
      played_gate_ -= getBlockSize();
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
      float cutoff = (20.0f + 100.0f * feedback_ * feedback_);
      feedback_filter_left_.fhz() = cutoff;
      feedback_filter_right_.fhz() = cutoff;
      feedback_filter_left_.process(feed_left, feed_left);
      feedback_filter_right_.process(feed_right, feed_right);
      float soft_limit_coeff = feedback_ * 1.4f;
      for (int i = 0; i < size; ++i)
      {
        float left = in_out_left[i];
        float right = in_out_right[i];
        left += feedback_ * (vessl::sample::softlimit(soft_limit_coeff * feed_left[i] + left) - left);
        right += feedback_ * (vessl::sample::softlimit(soft_limit_coeff * feed_right[i] + right) - right);
        GrainSample& grain_sample = record_buffer_[record_write_index_];
        grain_sample.re = left;
        grain_sample.im = right;
        record_write_index_ = (record_write_index_ + 1) & RECORD_BUFFER_WRAP;
      }
    }

    float grain_sample_length = (grain_size_*RECORD_BUFFER_SIZE);
    float target_grains = MaxGrains * grain_overlap_;
    float grain_prob = target_grains / grain_sample_length;
    float grain_spacing = grain_sample_length / target_grains;

    if (grain_density < 0.5f)
    {
      grain_prob = -1.0f;
    }
    else
    {
      grain_rate_phasor_ = -getBlockSize();
    }

    int num_available_grains = updateAvailableGrains();
    const int read_idx = record_write_index_ - size;
    for (int i = 0; i < size; ++i)
    {
      grain_rate_phasor_ += 1.0f;
      bool start_prob = vessl::math::random::range(0.f, 1.f) < grain_prob && target_grains > active_grains_;
      bool start_steady = grain_rate_phasor_ >= grain_spacing;
      bool start_grain = start_prob || start_steady || grain_triggered_;
      if (start_grain && num_available_grains)
      {
        --num_available_grains;
        int gidx = available_grains_[num_available_grains];
        GrainType* g = grains_[gidx];
        float gdi = static_cast<float>(i);
        float grain_delay = gdi > grain_trigger_delay_ ? gdi : grain_trigger_delay_;
        int head = read_idx + i;
        float grain_end_pos = static_cast<float>(head) / RECORD_BUFFER_SIZE;
        float pan = 0.5f + (vessl::math::random::range(0.f, 1.f) - 0.5f)*grain_spread_;
        float vel = 1.0f + (vessl::math::random::range(0.f, 1.f) * 2 - 1.0f)*grain_velocity_;
        g->trigger(grain_delay, grain_end_pos - grain_position_, grain_size_, grain_speed_, grain_envelope_, pan, vel);
        grain_triggered_ = false;
        grain_trigger_delay_ = 0;
        grain_rate_phasor_ = 0;
        played_gate_ = played_gate_sample_length_;
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

        for (int i = 0; i < size; ++i)
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
      float reverb_level = reverb_amount_ * 0.95f;
      reverb_level += feedback_ * (2.0f - feedback_) * freeze_;
      reverb_level = vessl::math::constrain(reverb_level, 0.0f, 1.0f);

      reverb_->setAmount(reverb_level * 0.54f);
      reverb_->setDiffusion(0.7f);
      reverb_->setReverbTime(0.35f + 0.63f * reverb_level);
      reverb_->setInputGain(0.2f);
      reverb_->setLowPass(0.6f + 0.37f * feedback_);
      reverb_->process(*grain_buffer_, *grain_buffer_);
    }

    const float wet_amt = dry_wet_;
    const float dry_amt = 1.0f - wet_amt;
    for (int i = 0; i < size; ++i)
    {
      in_out_left[i]  = in_out_left[i]*dry_amt  + grain_left[i]*wet_amt;
      in_out_right[i] = in_out_right[i]*dry_amt + grain_right[i]*wet_amt;
    }

    setButton(pin_.freeze, freeze_);
    setButton(pout_.grain_played, played_gate_ > 0);
    setParameterValue(pout_.grain_playback, avg_progress);
    setParameterValue(pout_.grain_envelope, avg_envelope);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - processStart - genTime;
    debugCpy = stpcpy(debugCpy, " proc ");
    debugCpy = stpcpy(debugCpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debugMsg);
#endif
  }
private:
  
  int updateAvailableGrains()
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
using GrainzPatch = GrainzBase<16,false>;
#else
using GrainzPatch = GrainzBase<56,true>;
#endif