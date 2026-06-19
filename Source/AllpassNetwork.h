#pragma once

// Configurable network of Allpass filters
#include "vessicle/vessl/vessl.h"

template<typename T, vessl::size_t Stages>
class AllpassNetwork : public vessl::unit_processor<T>, vessl::plist<2>
{
public:
  using sample_t = T;
  using size_t = vessl::size_t;
  using Parameter = vessl::parameter;
  
  [[nodiscard]] const parameter_list & parameters() const override { return *this; }
  
  [[nodiscard]] Parameter amount() const { return params_.amount("amount", 'a'); }
  [[nodiscard]] Parameter diffusion() const { return params_.coeff("diffusion", 'd'); }

  VESSL_INLINE sample_t read(int api, float offset)
  {
    DelayLine& d = delays_[api];
    int lidx = static_cast<int>(offset);
    int hidx = lidx + 1;
    sample_t t = vessl::cast<sample_t>(offset - lidx);
    // wrap in the buffer
    lidx = (d.buf_pos - lidx + d.buf_len) % d.buf_len;
    hidx = (d.buf_pos - hidx + d.buf_len) % d.buf_len;
    return d.buf[lidx] + t * (d.buf[hidx] - d.buf[lidx]);
  }

  VESSL_INLINE void write(int api, int offset, sample_t v)
  {
    DelayLine& d = delays_[api];
    int widx = d.buf_pos - offset;
    if (widx < 0) widx += d.buf_len;
    d.buf[widx] = v;
  }

  VESSL_INLINE sample_t process(const sample_t& input) override
  {
    sample_t output = input;
    for (int i = 0; i < Stages; ++i)
    {
      DelayLine& d = delays_[i];
      sample_t y = d.buf[d.buf_pos];
      sample_t z = params_.coeff.value * y + output;
      d.buf[d.buf_pos++] = z;
      output = y - params_.coeff.value * z;
      
      if (d.buf_pos == d.buf_len)
      {
        d.buf_pos = 0;
      }
    }
    return input + params_.amount.value * (output - input);
  }
  
  static AllpassNetwork* create(const size_t (&delay_lengths)[Stages], sample_t diffusion)
  {
    size_t buffer_size = 0;
    for (int i = 0; i < Stages; ++i)
    {
      buffer_size += delay_lengths[i];
    }
    sample_t* buffer_data = new sample_t[buffer_size];
    memset(buffer_data, 0, sizeof(sample_t)*buffer_size);
    AllpassNetwork* ap = new AllpassNetwork(buffer_data, diffusion);
    sample_t* head = buffer_data;
    for (int i = 0; i < Stages; ++i)
    {
      size_t len = delay_lengths[i];
      DelayLine& dl = ap->delays_[i];
      dl.buf_pos = 0;
      dl.buf_len = len;
      dl.buf = head;
      head = head + len;
    }
    return ap;
  }

  static void destroy(const AllpassNetwork* network)
  {
    delete[] network->shared_buffer_;
    delete network;
  }
  
protected:
  [[nodiscard]] Parameter element_at(vessl::size_t index) const override
  {
    switch (index)
    {
      case 0: return amount();
      case 1: return diffusion();
      default: return Parameter::none();
    }
  }
  
private:
  struct DelayLine
  {
    sample_t* buf;
    size_t buf_pos;
    size_t buf_len;
  };
  
  sample_t* shared_buffer_;
  DelayLine delays_[Stages];
  
  struct
  {
    vessl::param<sample_t> coeff;
    vessl::param<sample_t> amount;
  } params_;
  
  AllpassNetwork(sample_t* buffer_data, sample_t diffusion)
    : shared_buffer_(buffer_data)
  {
    params_.coeff.value = diffusion;
    params_.amount.value = vessl::cast<sample_t>(1.f);
  }
};
