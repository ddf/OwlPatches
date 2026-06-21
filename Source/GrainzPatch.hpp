#pragma once

#include "AudioBufferSourceSink.h"
#include "Patch.h"
#include "vessicle/Granulator.h"
#include "Reverb.h"

#define PROFILE

#ifdef PROFILE
#include <cstring>
#endif

// must be power of two
static constexpr int RECORD_BUFFER_SIZE = 1 << 18; // approx 5.5 seconds at 48k

using Array = vessl::array<float>;
using HighPassFilter = vessl::processors::filter<float, vessl::filtering::biquad<1>::high_pass>;
using DcBlockingFilter = vessl::processors::filter<float, vessl::filtering::dc_block>;
using Limiter = vessl::processors::limiter<float>;
using Clock = vessl::generators::clock<uint8_t>;
using Noise = vessl::generators::noise<float, vessl::noise::white>;
using Lfo   = vessl::generators::oscil<vessl::sample::waves::unipolar::triangle<float>>;
using Smoother = vessl::math::easing::smoother<float>;
using ReverbProcessor = Reverb<float>;

template <int MaxGrains>
class GrainzBase : public Patch
{
  using GranularProcessor = Granulator<float, 2, MaxGrains>;
  using GranularSampleType = typename GranularProcessor::SampleType;
  
  AudioBuffer*        feedback_buffer_;
  AudioBuffer*        tail_buffer_;
  GranularSampleType* grain_buffer_;
  GranularProcessor*  granular_processor_;
  ReverbProcessor*    reverb_processor_;
  DcBlockingFilter    dc_filter_left_;
  DcBlockingFilter    dc_filter_right_;
  HighPassFilter      feedback_filter_left_;
  HighPassFilter      feedback_filter_right_;
  Limiter             grain_limiter_;
  Clock               clock_;
  Noise               noise_unipolar_;
  Noise               noise_bipolar_;
  Lfo                 lfo_;
  
  // panel controls
  struct
  {
    PatchParameterId duration = PARAMETER_A;
    PatchParameterId speed    = PARAMETER_B;
    PatchParameterId position = PARAMETER_C;
    PatchParameterId feedback = PARAMETER_D;
    PatchParameterId density  = PARAMETER_E;
    PatchButtonId    clock    = BUTTON_1;
    PatchButtonId    reverse  = BUTTON_2;
    PatchButtonId    freeze   = BUTTON_3;
    PatchButtonId    reverb   = BUTTON_4;
    PatchButtonId    trigger  = BUTTON_8;

    // midi controls
    PatchParameterId varidur  = PARAMETER_AA;
    PatchParameterId varispd  = PARAMETER_AB;
    PatchParameterId varipos  = PARAMETER_AC;
    PatchParameterId velocity = PARAMETER_AD;
    PatchParameterId spread   = PARAMETER_AE;
    PatchParameterId envelope = PARAMETER_AF;
    PatchParameterId verb_amt = PARAMETER_AG;
    PatchParameterId dry_wet  = PARAMETER_AH;
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
  Smoother overdub_;
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
  
  size_t overdub_sample_delay_;
  
  uint16_t  freeze_; 
  uint16_t  reverse_;
  uint16_t  reverb_;
  uint8_t   clock_value_;
  uint8_t   freeze_toggled_;

public:
  GrainzBase()
    : feedback_buffer_(nullptr)
    , tail_buffer_(nullptr)
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
    , grain_duration_min_(0.008f)
    , grain_duration_max_(0.25f*(RECORD_BUFFER_SIZE/getSampleRate()))
    , noise_unipolar_value_(0)
    , noise_bipolar_value_(0)
    , lfo_value_(0)
    , overdub_sample_delay_(0)
    , freeze_(OFF)
    , reverse_(OFF)
    , reverb_(OFF)
    , clock_value_(0)
  {
    granular_processor_ = GranularProcessor::create(RECORD_BUFFER_SIZE, getBlockSize());
    grain_buffer_ = new GranularSampleType[getBlockSize()];
    feedback_buffer_ = AudioBuffer::create(2, getBlockSize());
    tail_buffer_ = AudioBuffer::create(2, getBlockSize());
    
    reverb_processor_ = ReverbProcessor::create(getSampleRate());
    reverb_processor_->diffusion() = 0.7f;
    reverb_processor_->input_gain() = 0.2f;

    registerParameter(pin_.position, "Position");
    registerParameter(pin_.duration, "Duration");
    registerParameter(pin_.speed, "Speed");
    registerParameter(pin_.density, "Density");
    registerParameter(pin_.envelope, "Envelope");
    registerParameter(pin_.spread, "Spread");
    registerParameter(pin_.velocity, "Velocity Variation");
    registerParameter(pin_.feedback, "Feedback");
    registerParameter(pin_.dry_wet, "Dry/Wet");
    registerParameter(pin_.varidur, "Duration Vari");
    registerParameter(pin_.varispd, "Speed Vari");
    registerParameter(pin_.varipos, "Position Vari");
    registerParameter(pin_.verb_amt, "Reverb");
    
    registerParameter(pout_.envelope, "Envelope>");
    registerParameter(pout_.random_value, "Random>");

    // default to triangle window
    setParameterValue(pin_.envelope, 0.5f);
    setParameterValue(pin_.spread, 0);
    setParameterValue(pin_.velocity, 0);
    setParameterValue(pin_.feedback, 0);
    setParameterValue(pin_.dry_wet, 1);
    setParameterValue(pin_.feedback, 0);
    setParameterValue(pin_.verb_amt, 0.5f);
  }

  ~GrainzBase() override
  {
    GranularProcessor::destroy(granular_processor_);
    ReverbProcessor::destroy(reverb_processor_);
    AudioBuffer::destroy(feedback_buffer_);
    AudioBuffer::destroy(tail_buffer_);
    delete[] grain_buffer_;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    // if (bid == pin_.trigger && value == ON)
    // {
    //   granular_processor_->trigger(samples);
    //   played_gate_ = out_gate_sample_length_;
    // }
    // else 
      if (bid == pin_.clock && value == ON)
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
      freeze_toggled_ = true;
      setParameterValue(pin_.feedback, freeze_ == ON ? overdub_.value : feedback_.value);
    }
    else if (bid == pin_.reverb && value == ON)
    {
      reverb_ = reverb_ == ON ? OFF : ON;
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
    
    const float sample_rate = getSampleRate();
    const unsigned max_grains = reverb_ ? MaxGrains / 2 : MaxGrains;
    const bool reverb_enabled = reverb_ && granular_processor_->active_grain_count() <= max_grains;

    // like Clouds, Density describes how many grains we want playing simultaneously at any given time
    float density_param = getParameterValue(pin_.density);
    grain_overlap_ = vessl::math::interp<vessl::math::easing::quad::in>(0.f, 0.999f, density_param);
    grain_rate_ = density_param < 0.45f ? vessl::math::lerp(4.0f, 0.25f, density_param * 2.125f)
                  : density_param > 0.55f ? vessl::math::lerp(0.0625f, 0.25f, (1.0f - density_param) * 2.125f)
                  : 0.25f;
    float position_vari = noise_bipolar_value_ * 0.5f * getParameterValue(pin_.varipos);
    float position_param = vessl::math::constrain(getParameterValue(pin_.position) + position_vari, 0.f, 1.f);
    grain_position_ = vessl::math::interp<vessl::math::easing::expo::in>(
      1.f, 0.25f*RECORD_BUFFER_SIZE, position_param
    );
    float duration_vari = (1.0f - noise_bipolar_value_) * 0.25f * getParameterValue(pin_.varidur);
    float duration_param = vessl::math::constrain(getParameterValue(pin_.duration) + duration_vari, 0.f, 1.f);
    grain_duration_ = vessl::math::interp<vessl::math::easing::expo::in>(
      grain_duration_min_, grain_duration_max_, duration_param
    );
    constexpr float octaves = 2;
    float speed_vari  = noise_bipolar_value_ * 0.25f * getParameterValue(pin_.varispd);
    float speed_param = vessl::math::constrain(getParameterValue(pin_.speed) + speed_vari, 0.f, 1.f);
    grain_speed_ = vessl::math::exp2(speed_param*octaves)/octaves;
    grain_envelope_ = getParameterValue(pin_.envelope);
    grain_spread_ = getParameterValue(pin_.spread);
    grain_velocity_ = getParameterValue(pin_.velocity);
    if (freeze_ == ON)
    {
      overdub_ = getParameterValue(pin_.feedback);
    }
    else
    {
      feedback_ = getParameterValue(pin_.feedback); 
    }
    
    float reverb_boost = overdub_.value * (2.0f - overdub_.value) * (freeze_ == ON);
    reverb_amount_ = reverb_enabled ? getParameterValue(pin_.verb_amt) + reverb_boost : 0.f;
    dry_wet_ = getParameterValue(pin_.dry_wet);
    
    float grain_playback_rate = grain_speed_.value;
    float grain_sample_length = (grain_duration_.value + duration_vari) * sample_rate;
    float grain_spacing;
    if (clock_.is_clocked())
    {
      float dur = clock_.tempo().read<vessl::time::duration>().to_seconds(sample_rate);
      grain_spacing = grain_overlap_.value > 0.0001f ? dur * sample_rate * grain_rate_.value : 0.f;
    }
    else
    {
      float target_grains = vessl::math::min(MaxGrains * grain_overlap_.value, static_cast<float>(max_grains));
      grain_spacing = target_grains > 0.0001f ? grain_sample_length / target_grains : 0;
      clock_.tempo() = vessl::time::duration::from_seconds(grain_spacing / sample_rate, sample_rate);
    }
    // we want a grain to always last the same amount of real time, regardless of playback rate.
    // so now we adjust the length with playback speed
    grain_sample_length *= grain_playback_rate;
    
    granular_processor_->envelope.set_pulse_width(vessl::cast<vessl::phase_t>(grain_envelope_.value));
    granular_processor_->duration() = vessl::duration_t(grain_sample_length);
    granular_processor_->speed() = grain_playback_rate;
    granular_processor_->offset() = vessl::duration_t(grain_position_.value);
    granular_processor_->rate() = vessl::duration_t(grain_spacing);
    granular_processor_->pan() = noise_bipolar_value_ * grain_spread_.value; // vessl::math::random::range(-grain_spread_.value, grain_spread_.value);
    granular_processor_->volume() = 1.f - noise_unipolar_value_ * grain_velocity_.value; // vessl::math::random::range(1.f - grain_velocity_.value, 1.0f);
    granular_processor_->reverse() = reverse_;
    granular_processor_->max_active() = max_grains;

    if (played_gate_ > 0)
    {
      played_gate_ -= block_size;
    }
    
    if (random_gate_ > 0)
    {
      random_gate_ -= block_size;
    }
    
    dc_filter_left_.process(in_out_left, in_out_left);
    dc_filter_right_.process(in_out_right, in_out_right);
    
    // Note: the way feedback is applied is based on how Clouds does it
    const float cutoff = (20.0f + 100.0f * feedback_.value * feedback_.value);
    feedback_filter_left_.fhz() = cutoff;
    feedback_filter_right_.fhz() = cutoff;
    feedback_filter_left_.process(feed_left, feed_left);
    feedback_filter_right_.process(feed_right, feed_right);
    const float soft_limit_coeff = feedback_.value * 1.4f;
    
    // #TODO: add smoothed freeze state for fading feedback in/out.
    if (freeze_ == OFF)
    {
      for (int i = 0; i < block_size; ++i)
      {
        float left = in_out_left[i];
        float right = in_out_right[i];
        float grn_left = left + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_left[i] + left) - left);
        float grn_right = right + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_right[i] + right) - right);
        grain_buffer_[i] = { grn_left, grn_right };
      }
      
      if (freeze_toggled_)
      {
        float* tail_left = tail_buffer_->getSamples(0);
        float* tail_right = tail_buffer_->getSamples(1);
        float inc = 1.0f / static_cast<float>(block_size);
        float t = 0;
        for (int i = 0; i < block_size; ++i, t+=inc)
        {
          GranularSampleType& grn = grain_buffer_[i];
          float fade = 1.0f - t;
          vessl::sample::crossfade(grn.left(), *tail_left++, fade, &grn.left());
          vessl::sample::crossfade(grn.right(), *tail_right++, fade, &grn.right());
        }
        freeze_toggled_ = false;
      }
    }
    else if (freeze_ == ON)
    {
      if (freeze_toggled_)
      {
        float* tail_left = tail_buffer_->getSamples(0);
        float* tail_right = tail_buffer_->getSamples(1);
        for (int i = 0; i < block_size; ++i)
        {
          float left = in_out_left[i];
          float right = in_out_right[i];
          tail_left[i] = left + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_left[i] + left) - left);
          tail_right[i] = right + feedback_.value * (vessl::sample::softlimit(soft_limit_coeff * feed_right[i] + right) - right);
        }
        freeze_toggled_ = false;
        overdub_sample_delay_ = 0;
      }

      // overdub feedback onto frozen section of the buffer
      {
        const float overdub = overdub_.value * 0.5f;
        AudioBufferReader<2> feed_read(*feedback_buffer_);
        granular_processor_->overdub(feed_read, overdub, overdub_sample_delay_);
        overdub_sample_delay_ += block_size;
        if (overdub_sample_delay_ > grain_sample_length)
        {
          overdub_sample_delay_ -= grain_sample_length;
        }
      }
    }

#ifdef PROFILE
    const float gen_start = getElapsedBlockTime();
#endif
    
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
    debug_cpy = stpcpy(debug_cpy, msg_itoa(static_cast<int>(gen_time * 1000), 10));
#endif
    
    // #TODO reverb can also wind up with DC offset 
    // in freeze mode when feedback is engaged.
    if (reverb_enabled)
    {
      float reverb_level = reverb_amount_.value * 0.95f;
      reverb_level = vessl::math::constrain(reverb_level, 0.0f, 1.0f);

      reverb_processor_->wet_mix() = reverb_level * 0.54f;
      reverb_processor_->reverb_time() = 0.35f + 0.63f * reverb_level;
      reverb_processor_->low_pass() = 0.6f + 0.37f * overdub_.value;
    }
    
    for (int i = 0; i < block_size; ++i)
    {
      GranularSampleType& g = grain_buffer_[i];
      
      // run the limiter on the mono signal.
      // and then scale the stereo signal based on how much amplitude reduction was applied.
      // this should prevent left/right balance going out of whack?
      grain_limiter_.process(g.to_mono().value());
      const float peak = grain_limiter_.peak().read_analog();
      const float reduction = peak <= 1.f ? 1.f : 1.f / peak;
      g *= reduction;
      
      if (reverb_enabled)
      {
        grain_buffer_[i] = reverb_processor_->process(g);
      }
      
      feed_left[i] = g.left();
      feed_right[i] = g.right();
    }
    
    float clock_rate = clock_.tempo().read<vessl::time::duration>().to_frequency(sample_rate); 
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
    setButton(pin_.reverb, reverb_);
    setButton(pout_.grain_played, played_gate_ > 0);
    setButton(pout_.random_gate, random_gate_ > 0);
    setParameterValue(pout_.envelope, lfo_value_);
    setParameterValue(pout_.random_value, noise_unipolar_value_);

#ifdef PROFILE
    const float processTime = getElapsedBlockTime() - process_start - gen_time;
    debug_cpy = stpcpy(debug_cpy, " proc ");
    debug_cpy = stpcpy(debug_cpy, msg_itoa(static_cast<int>(processTime * 1000), 10));
    debugMessage(debug_msg);
#endif
  }
};

#ifdef OWL_WITCH
using GrainzPatch = GrainzBase<16>;
#else
using GrainzPatch = GrainzBase<56>;
#endif