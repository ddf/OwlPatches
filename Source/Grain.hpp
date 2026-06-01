#pragma once
#include "vessicle/vessl/vessl.h"

// tried out recording to a sample buffer of shorts,
// which requires converting back to float when a grain reads from the buffer.
// this turned out to be quite a bit slower than just operating on the float buffer.
#if 1
//#include "ComplexFloatArray.h"
using GrainSample = ComplexFloat;
#define SAMPLE_TO_FLOAT 1
#define FLOAT_TO_SAMPLE 1
#else
#include "ComplexShortArray.h"
typedef ComplexShort GrainSample;
#define SampleToFloat 0.0000305185f // 1 / 32767
#define FloatToSample 32767
#endif

class Grain : public vessl::generator<GrainSample>
{
  using SampleBuffer = const GrainSample*;
  
  SampleBuffer buffer_;
  int buffer_size_;
  int buffer_wrap_mask_;
  int pre_delay_;
  float ramp_;
  float start_;
  float size_;
  float speed_;
  float decay_start_;
  float attack_mult_;
  float decay_mult_;
  float left_scale_;
  float right_scale_;

public:
  // buffer size argument must be power of two!
  Grain(SampleBuffer in_buffer, int buffer_sz)
    : buffer_(in_buffer), buffer_size_(buffer_sz), buffer_wrap_mask_(buffer_sz - 1)
    , pre_delay_(0), ramp_(randf()*buffer_size_), start_(0), size_(buffer_size_)
    , speed_(1), decay_start_(0), attack_mult_(0), decay_mult_(0)
    , left_scale_(1), right_scale_(1), is_done(true)
  {
  }
  
  bool is_done;

  inline float progress() const
  {
    return ramp_ / size_;
  }

  inline float envelope() const
  {
    return ramp_ < decay_start_ ? ramp_ * attack_mult_ : (size_ - ramp_) * decay_mult_;
  }

  // all arguments [0,1], relative to buffer size,
  // env describes a blend from:
  // short attack / long decay -> triangle -> long attack / short delay
  // balance is only left channel at 0, only right channel at 1
  void trigger(int delay, float end, float length, float rate, float env, float balance, float velocity)
  {
    pre_delay_ = delay;
    ramp_ = 0;
    size_ = length * buffer_size_;
    // we always advance by buffer size
    // so we don't have to worry about accessing negative indices
    start_ = end * buffer_size_ - size_ + buffer_size_;
    speed_ = rate;
    // convert -1 to 1
    balance = (balance * 2) - 1;
    left_scale_ = (balance < 0  ? 1 : 1.0f - balance) * velocity;
    right_scale_ = (balance > 0 ? 1 : 1.0f + balance) * velocity;

    float next_attack = vessl::math::constrain(env, 0.01f, 0.99f);
    float next_decay = 1.0f - next_attack;
    decay_start_ = next_attack * size_;
    attack_mult_ = 1.0f / (next_attack*size_);
    decay_mult_ = 1.0f / (next_decay*size_);
    is_done = false;
  }

  VESSL_INLINE GrainSample generate() override
  {
    if (pre_delay_)
    {
      --pre_delay_;
      return { 0.f, 0.f };
    }

    const float pos = start_ + ramp_;
    const float env = envelope();
    const int i = static_cast<int>(pos);
    const int j = i + 1;
    const float t = pos - i;
    float sample_left = vessl::math::lerp(buffer_[i&buffer_wrap_mask_].re, buffer_[j&buffer_wrap_mask_].im, t) * env * left_scale_;
    float sample_right = vessl::math::lerp(buffer_[i&buffer_wrap_mask_].re, buffer_[j&buffer_wrap_mask_].im, t) * env * right_scale_;

    // keep looping, but silently, mainly so we can keep track of grain performance
    if ((ramp_ += speed_) >= size_)
    {
      ramp_ -= size_;
      attack_mult_ = decay_mult_ = 0;
      is_done = true;
    }

    return { sample_left, sample_right };
  }
  
  static Grain* create(GrainSample* buffer, int size)
  {
    return new Grain(buffer, size);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }
};
