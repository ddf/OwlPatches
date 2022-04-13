#ifndef __ALLPASS_NETWORK_H__
#define __ALLPASS_NETWORK_H__

// Configurable network of Allpass filters
#include "SignalProcessor.h"
#include "Filters/allpass.h"
#include "FloatArray.h"
#include "SimpleArray.h"

class AllpassNetwork : public SignalProcessor 
{
  struct DelayLine
  {
    float* buf;
    size_t bufPos;
    size_t bufLen;
  };

  FloatArray buffer;
  SimpleArray<DelayLine> delays;
  float coeff;
  float amount;

  AllpassNetwork(float* bufferData, size_t bufferSize, DelayLine* delayData, size_t apSize, float diffusion)
    : buffer(bufferData, bufferSize)
    , delays(delayData, apSize)
    , coeff(diffusion), amount(0)
  {

  }

public:
  void setAmount(float amt)
  {
    amount = amt;
  }

  void setDiffusion(float diffusion)
  {
    coeff = diffusion;
  }

  float process(float input) override
  {
    float output = input;
    for (int i = 0; i < delays.getSize(); ++i)
    {
      DelayLine& d = delays[i];
      float y = d.buf[d.bufPos];
      float z = coeff * y + output;
      d.buf[d.bufPos] = z;
      output = y - coeff * z;
      d.bufPos = (d.bufPos + 1) % d.bufLen;
    }
    return input + amount * (output - input);
  }

  using SignalProcessor::process;

  static AllpassNetwork* create(size_t* delayLengths, size_t delayCount, float diffusion)
  {
    DelayLine* delayData = new DelayLine[delayCount];
    size_t bufferSize = 0;
    for (int i = 0; i < delayCount; ++i)
    {
      bufferSize += delayLengths[i];
    }
    float* bufferData = new float[bufferSize];
    memset(bufferData, 0, sizeof(float)*bufferSize);
    float* head = bufferData;
    for (int i = 0; i < delayCount; ++i)
    {
      int len = delayLengths[i];
      DelayLine& ap = delayData[i];
      ap.bufPos = 0;
      ap.bufLen = len;
      ap.buf = head;
      head = head + len;
    }
    return new AllpassNetwork(bufferData, bufferSize, delayData, delayCount, diffusion);
  }

  static void destroy(AllpassNetwork* network)
  {
    delete[] network->buffer.getData();
    delete[] network->delays.getData();
    delete network;
  }
};

#endif // __ALLPASS_NETWORK_H__
