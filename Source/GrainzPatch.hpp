#pragma once

#include "Patch.h"
#include "vessicle/Granulator.h"
#include "Reverb.h"

#define PROFILE

#ifdef PROFILE
#include <string.h>
#endif

// must be power of two
static constexpr int RECORD_BUFFER_SIZE = 1 << 18; // approx 5.5 seconds at 48k

using Array = vessl::array<float>;
using HighPassFilter = vessl::processors::filter<float, vessl::filtering::biquad<1>::high_pass>;
using DcBlockingFilter = vessl::processors::filter<float, vessl::filtering::dc_block>;
using Clock = vessl::generators::clock<uint8_t>;
using Noise = vessl::generators::noise<float, vessl::noise::white>;
using Lfo   = vessl::generators::oscil<vessl::sample::waves::unipolar::triangle<float>>;
using Smoother = vessl::math::easing::smoother<float>;

template <int MaxGrains, bool WithReverb>
class GrainzBase : public Patch
{
  using GranularProcessor = Granulator<float, 2, MaxGrains>;
  using GranularSampleType = typename GranularProcessor::SampleType;
  
  AudioBuffer*     feedback_buffer_;
  GranularSampleType* grain_buffer_;
  GranularProcessor* granular_processor_;
  Reverb*          reverb_;
  DcBlockingFilter dc_filter_left_;
  DcBlockingFilter dc_filter_right_;
  HighPassFilter   feedback_filter_left_;
  HighPassFilter   feedback_filter_right_;
  Clock            clock_;
  Noise            noise_unipolar_;
  Noise            noise_bipolar_;
  Lfo              lfo_;
  
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
    PatchButtonId    reverse  = BUTTON_3;
    PatchButtonId    freeze   = BUTTON_4;

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
    PatchParameterId envelope       = PARAMETER_F;
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
  
  int out_gate_sample_length_;
  int played_gate_;
  int random_gate_;

  // these are in seconds
  float grain_duration_min_;
  float grain_duration_max_;

  float noise_unipolar_value_;
  float noise_bipolar_value_;
  float lfo_value_;
  
  uint16_t  freeze_; 
  uint16_t  reverse_;
  uint8_t   clock_value_;
  
  float norms_[MaxGrains + 1];

public:
  GrainzBase()
    : feedback_buffer_(nullptr)
    , grain_buffer_(nullptr)
    , dc_filter_left_(getSampleRate())
    , dc_filter_right_(getSampleRate())
    , feedback_filter_left_(getSampleRate())
    , feedback_filter_right_(getSampleRate())
    , clock_(getSampleRate(), 2, getSampleRate()*4)
    , noise_unipolar_(getSampleRate())
    , noise_bipolar_(getSampleRate())
    , lfo_(getSampleRate(), 1.f)
    , out_gate_sample_length_(getBlockSize()) // 8ms
    , played_gate_(0)
    , random_gate_(0)
    , grain_duration_min_(2.0f/getSampleRate())
    , grain_duration_max_(0.25f*(RECORD_BUFFER_SIZE/getSampleRate()))
    , noise_unipolar_value_(0)
    , noise_bipolar_value_(0)
    , lfo_value_(0)
    , freeze_(OFF)
    , reverse_(OFF)
    , clock_value_(0)
  {
    norms_[0] = 1;
    for (int i = 1; i < MaxGrains + 1; i++) 
    {
      norms_[i] = 1 / sqrtf(static_cast<float>(i));
    }
    
    granular_processor_ = GranularProcessor::create(RECORD_BUFFER_SIZE, getBlockSize());
    grain_buffer_ = new GranularSampleType[getBlockSize()];
    feedback_buffer_ = AudioBuffer::create(2, getBlockSize());

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
    registerParameter(pout_.envelope, "Envelope>");
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
    GranularProcessor::destroy(granular_processor_);
    AudioBuffer::destroy(feedback_buffer_);
    delete[] grain_buffer_;

    if constexpr (WithReverb)
    {
      Reverb::destroy(reverb_);
    }
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == pin_.trigger && value == ON)
    {
      granular_processor_->trigger(samples);
      played_gate_ = out_gate_sample_length_;
    }
    else if (bid == pin_.clock && value == ON)
    {
      clock_.tap(samples);
    }
    else if (bid == pin_.reverse && value == ON)
    {
      reverse_ = reverse_ == ON ? OFF : ON;
    }
    else if (bid == pin_.freeze && value == ON)
    {
      freeze_ = freeze_ == ON ? OFF : ON;
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
#ifdef PROFILE
    char debug_msg[64];
    char* debug_cpy = stpcpy(debug_msg, "blk ");
    debug_cpy = stpcpy(debug_cpy, msg_itoa(audio.getSize(), 10));
    const float process_start = getElapsedBlockTime();
#endif
    const int block_size = audio.getSize();
    Array in_out_left(audio.getSamples(0), block_size);
    Array in_out_right(audio.getSamples(1), block_size);
    Array feed_left(feedback_buffer_->getSamples(0).getData(), block_size);
    Array feed_right(feedback_buffer_->getSamples(1).getData(), block_size);

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
    constexpr float octaves = 2;
    float speed_param = getParameterValue(pin_.speed)*octaves;
    grain_speed_ = vessl::math::exp2(speed_param)/octaves;
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
      played_gate_ -= block_size;
    }
    
    if (random_gate_ > 0)
    {
      random_gate_ -= block_size;
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
      for (int i = 0; i < block_size; ++i)
      {
        float left = in_out_left[i];
        float right = in_out_right[i];
        GranularSampleType& grn = grain_buffer_[i];
        grn.left() = left + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_left[i] + left) - left);
        grn.right() = right + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_right[i] + right) - right);
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
    
    bool grains_enabled = grain_spacing > 0;

#ifdef PROFILE
    const float gen_start = getElapsedBlockTime();
#endif

    granular_processor_->envelope.set_pulse_width(vessl::cast<vessl::phase_t>(grain_envelope_.value));
    granular_processor_->grain_duration() = vessl::duration_t(grain_sample_length);
    granular_processor_->grain_speed() = grain_playback_rate;
    granular_processor_->grain_offset() = vessl::duration_t(grain_position_.value);
    granular_processor_->grain_rate() = vessl::duration_t(grain_spacing);
    granular_processor_->grain_pan() = noise_bipolar_value_ * grain_spread_.value; // vessl::math::random::range(-grain_spread_.value, grain_spread_.value);
    granular_processor_->grain_volume() = 1.f - noise_unipolar_value_ * grain_velocity_.value; // vessl::math::random::range(1.f - grain_velocity_.value, 1.0f);
    granular_processor_->grain_reverse() = reverse_;
    
    vessl::array grain_buffer(grain_buffer_, getBlockSize());
    if (freeze_ == ON)
    {
      granular_processor_->generate(grain_buffer);
    }
    else
    {
      granular_processor_->process(grain_buffer, grain_buffer);
    }
    
    if (granular_processor_->started_grain())
    {
      played_gate_ = out_gate_sample_length_;
    }

#ifdef PROFILE
    const float gen_time = getElapsedBlockTime() - gen_start;
    debug_cpy = stpcpy(debug_cpy, " gen(");
    debug_cpy = stpcpy(debug_cpy, msg_itoa(granular_processor_->active_grain_count(), 10));
    debug_cpy = stpcpy(debug_cpy, ") ");
    debug_cpy = stpcpy(debug_cpy, msg_itoa((int)(gen_time * 1000), 10));
#endif
    
    // float from_gain_adjust = norms_[prev_active_grains];
    // float to_gain_adjust = norms_[active_grains_];
    // grain_left.scale(from_gain_adjust, to_gain_adjust);
    // grain_right.scale(from_gain_adjust, to_gain_adjust);
    auto gread = grain_buffer.make_reader();
    auto flw = feed_left.make_writer();
    auto frw = feed_right.make_writer();
    while (gread)
    {
      auto g = gread.read();
      flw << g.left();
      frw << g.right();
    }

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
    
    float clock_rate = clock_.tempo().read<vessl::time::duration>().to_frequency(getSampleRate()); 
    noise_unipolar_.rate() = clock_rate*0.25f;
    noise_bipolar_.rate() = clock_rate*0.25f;
    lfo_.fhz() = clock_rate*0.25f;

    const float wet_amt = dry_wet_.value;
    const float dry_amt = 1.0f - wet_amt;
    for (int i = 0; i < block_size; ++i)
    {
      auto& gs = grain_buffer_[i];
      in_out_left[i]  = in_out_left[i]*dry_amt  + gs.left()*wet_amt;
      in_out_right[i] = in_out_right[i]*dry_amt + gs.right()*wet_amt;
      
      noise_bipolar_value_ = noise_bipolar_.generate<vessl::math::easing::smoothstep>()*2.f - 1.f;
      noise_unipolar_value_ = noise_unipolar_.generate<vessl::math::easing::smoothstep>();
      uint8_t cs = clock_.generate();
      if (cs > clock_value_)
      {
        if (noise_unipolar_value_ < vessl::math::random::range(0.f, 1.f))
        {
          random_gate_ = out_gate_sample_length_;
        }
        //lfo_.reset();
      }
      lfo_value_ = lfo_.generate();
      clock_value_ = cs;
    }

    setButton(pin_.reverse, reverse_);
    setButton(pin_.freeze, freeze_);
    setButton(pout_.grain_played, played_gate_ > 0);
    setButton(pout_.random_gate, random_gate_ > 0);
    setParameterValue(pout_.envelope, lfo_value_);
    setParameterValue(pout_.random_value, noise_unipolar_value_);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - process_start - gen_time;
    debug_cpy = stpcpy(debug_cpy, " proc ");
    debug_cpy = stpcpy(debug_cpy, msg_itoa((int)(processTime * 1000), 10));
    debugMessage(debug_msg);
#endif
  }
};

#ifdef OWL_WITCH
using GrainzPatch = GrainzBase<16,false>;
#else
using GrainzPatch = GrainzBase<56,true>;
#endif