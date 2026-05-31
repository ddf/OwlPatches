#include "SignalGenerator.h"
#include "Envelope.h"
#include "FloatArray.h"
#include "basicmaths.h"

// tried out recording to a sample buffer of shorts,
// which requires converting back to float when a grain reads from the buffer.
// this turned out to be quite a bit slower than just operating on the float buffer.
#if 1
#include "ComplexFloatArray.h"
typedef ComplexFloat Sample;
#define SAMPLE_TO_FLOAT 1
#define FLOAT_TO_SAMPLE 1
#else
#include "ComplexShortArray.h"
typedef ComplexShort Sample;
#define SampleToFloat 0.0000305185f // 1 / 32767
#define FloatToSample 32767
#endif

class Grain : public SignalGenerator, MultiSignalGenerator
{
  Sample* buffer_;
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
  Grain(Sample* in_buffer, int buffer_sz)
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

    float next_attack = clamp(env, 0.01f, 0.99f);
    float next_decay = 1.0f - next_attack;
    decay_start_ = next_attack * size_;
    attack_mult_ = 1.0f / (next_attack*size_);
    decay_mult_ = 1.0f / (next_decay*size_);
    is_done = false;
  }

  float generate() override
  {
    if (pre_delay_)
    {
      --pre_delay_;
      return 0.0f;
    }

    const float pos = start_ + ramp_;
    const int i = static_cast<int>(pos);
    const int j = i + 1;
    const float t = pos - i;
    float sample = interpolated(buffer_[i&buffer_wrap_mask_].re, buffer_[j&buffer_wrap_mask_].re, t) * envelope();

    // keep looping, but silently, mainly so we can keep track of grain performance
    if ((ramp_ += speed_) >= size_)
    {
      ramp_ -= size_;
      attack_mult_ = decay_mult_ = 0;
      is_done = true;
    }

    return sample;
  }

  void generate(AudioBuffer& output) override
  {
    int out_len = output.getSize();
    FloatArray out_l = output.getSamples(0);
    FloatArray out_r = output.getSamples(1);

    generate<false>(out_l, out_r, out_len);
  }

  template<bool Clear>
  void generate(FloatArray gen_left, FloatArray gen_right, int gen_len)
  {
    if (const int skip = min(pre_delay_, gen_len))
    {
      pre_delay_ -= skip;
      gen_len -= skip;
      if constexpr(Clear) 
      {
        gen_left.subArray(0, skip).clear();
        gen_right.subArray(0, skip).clear();
      }
      gen_left = gen_left.subArray(skip, gen_len);
      gen_right = gen_right.subArray(skip, gen_len);
    }

    float* out_l = gen_left.getData();
    float* out_r = gen_right.getData();

    static Sample scratch[512];
    
    // copy the buffer data we need into our scratch array
    if (gen_len)
    {
      int offset = static_cast<int>(start_ + ramp_) & buffer_wrap_mask_;
      // need at least two samples at slower speeds when genLen*speed truncates to 0
      int read_len = static_cast<int>(gen_len * speed_) + 2;
      int rem = buffer_size_ - offset;
      if (read_len >= rem)
      { 
        memcpy(scratch, buffer_ + offset, rem*sizeof(Sample));
        memcpy(scratch + rem, buffer_, (read_len - rem)*sizeof(Sample));
      }
      else
      {
        memcpy(scratch, buffer_ + offset, read_len*sizeof(Sample));
      }
    }

    float ramp_begin = ramp_;

    while(gen_len--)
    {
      // setting all of these is basically free.
      // removing modulo and using ternary logic doesn't improve performance.
      const float pos = ramp_ - ramp_begin;
      const float t = pos - static_cast<int>(pos);
      const int i = static_cast<int>(pos);
      const int j = (i + 1);
      const float env = envelope();
      const Sample si = scratch[i];
      const Sample sj = scratch[j];

      // biggest perf hit is here
      // time jumps from 50ns to 297ns uncommenting only one of these.
      // and then when adding the second channel, only to 380-400ns.
      // not sure where the extra time comes from with the first sample,
      // but probably something relating to array access?
      // doesn't seem to matter whether we access the member arrays or pass in arguments.
      // on the forums it was pointed out that accessing the array is just slow because it lives in SDRAM.
      if constexpr(Clear) 
      {
        *out_l++ = interpolated(si.re, sj.re, t) * env * left_scale_;
        *out_r++ = interpolated(si.im, sj.im, t) * env * right_scale_;
      }
      else 
      {
        *out_l++ += interpolated(si.re, sj.re, t) * env * left_scale_;
        *out_r++ += interpolated(si.im, sj.im, t) * env * right_scale_;
      }

      // keep looping, but silently, mainly so we can keep track of grain performance
      // just this on its own is about 6ns per grain
      if ((ramp_ += speed_) >= size_)
      {
        ramp_ = size_;
        //ramp -= size;
        //attackMult = decayMult = 0;
        is_done = true;
        if constexpr(Clear) 
        {
          memset(out_l, 0, sizeof(float) * gen_len);
          memset(out_r, 0, sizeof(float) * gen_len);
        }
        break;
      }
    }
  }

private:

  inline float interpolated(float a, float b, float t) const
  {
    return (a + t * (b - a)) * SAMPLE_TO_FLOAT;
  }

public:
  static Grain* create(Sample* buffer, int size)
  {
    return new Grain(buffer, size);
  }

  static void destroy(Grain* grain)
  {
    delete grain;
  }
};
