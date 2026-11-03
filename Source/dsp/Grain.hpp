#pragma once
#include "vessicle/vessl/vessl.h"

template<typename T>
struct Grain : public vessl::generator<T>
{
  using SampleType   = T;
  using SampleBuffer = const T*;
  
  SampleBuffer buffer_;
  int buffer_size_;
  int buffer_wrap_mask_;
  float ramp_;
  float start_;
  float size_;
  float speed_;
  float decay_start_;
  float attack_mult_;
  float decay_mult_;
  float balance_;
  float volume_;
  
  // buffer size argument must be power of two!
  Grain(SampleBuffer in_buffer, int buffer_sz)
    : buffer_(in_buffer), buffer_size_(buffer_sz), buffer_wrap_mask_(buffer_sz - 1)
    , ramp_(randf()*buffer_size_), start_(0), size_(buffer_size_)
    , speed_(1), decay_start_(0), attack_mult_(0), decay_mult_(0)
    , balance_(0), volume_(1)
  {
  }
  
  VESSL_INLINE bool is_done() const
  {
    return ramp_ >= size_;
  }

  VESSL_INLINE float progress() const
  {
    return ramp_ / size_;
  }

  VESSL_INLINE float envelope() const
  {
    return ramp_ < decay_start_ ? ramp_ * attack_mult_ : (size_ - ramp_) * decay_mult_;
  }
  
  // env describes a blend from:
  // short attack / long decay -> triangle -> long attack / short delay
  // balance is only left channel at 0, only right channel at 1
  void trigger(
    float end_sample_position, 
    float length_in_samples, 
    float playback_rate, 
    float env, 
    float balance, 
    float velocity
  )
  {
    ramp_ = 0;
    size_ = length_in_samples;
    // we always advance by buffer size
    // so we don't have to worry about accessing negative indices
    start_ = end_sample_position - size_ + static_cast<float>(buffer_size_);
    speed_ = playback_rate;
    // convert -1 to 1
    balance_ = (balance * 2) - 1;
    volume_ = velocity;

    float next_attack = vessl::math::constrain(env, 0.01f, 0.99f);
    float next_decay = 1.0f - next_attack;
    decay_start_ = next_attack * size_;
    attack_mult_ = 1.0f / (next_attack*size_);
    decay_mult_ = 1.0f / (next_decay*size_);
  }

  VESSL_INLINE SampleType generate() override
  {
    const float pos = start_ + ramp_;
    const float env = envelope() * volume_;
    const int i = static_cast<int>(pos);
    const int j = i + 1;
    const float t = pos - i;
    SampleType si = buffer_[i&buffer_wrap_mask_];
    SampleType sj = buffer_[j&buffer_wrap_mask_];
    SampleType grn = vessl::math::lerp(si, sj, t) * env;
    
    ramp_ += speed_;

    return grn;
  }
  
  static Grain* create(SampleType* buffer, int size)
  {
    return new Grain(buffer, size);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }
};
