#pragma once

#include "vessl/vessl.h"

struct BlurKernelSample
{
  constexpr BlurKernelSample() : offset(0), weight(0) {}
  constexpr BlurKernelSample(float offset, float weight) : offset(offset), weight(weight) {}

  // the offset of the sample from target pixel, in normalized coordinates
  float offset;

  // the weight of the sample
  float weight;
};

class BlurKernel : public vessl::array<BlurKernelSample>
{
public:
  float blurSize;
  
  BlurKernel() = default;
  BlurKernel(BlurKernelSample* inData, std::size_t inSize) : array(inData, inSize), blurSize(0) {}

  void setGauss(float withBlurSize, float standardDeviation, float scale = 1.0f)
  {
    blurSize = vessl::math::constrain(withBlurSize, 0.0f, 0.99f);
    standardDeviation = vessl::math::max(standardDeviation, 0.01f);

    float sum = 0;
    float standardDevSq = standardDeviation * standardDeviation;
    float gaussCoeff = 1.0f / vessl::math::sqrt<float>(vessl::math::twoPi<float>()*standardDevSq);

    for (std::size_t s = 0; s < size; ++s)
    {
      float offset = (static_cast<float>(s) / static_cast<float>(size - 1) - 0.5f)*blurSize;
      float gaussWeight = gaussCoeff * vessl::math::pow(vessl::math::e<float>(), -((offset*offset) / (2 * standardDevSq)));
      data[s] = BlurKernelSample(offset, gaussWeight);
      sum += gaussWeight;
    }

    // normalize the weights so we don't have to do this during processing and apply the scale
    float weightScale = scale / sum;
    for (BlurKernelSample& sample : *this)
    {
      sample.weight *= weightScale;
    }
  }

  void clear()
  {
    for (BlurKernelSample& sample : *this)
    {
      sample.offset = 0;
      sample.weight = 0;
    }
  }

  static BlurKernel create(std::size_t sampleCount)
  {
    BlurKernel kernel(new BlurKernelSample[sampleCount], sampleCount);
    kernel.clear();
    return kernel;
  }

  static void destroy(BlurKernel kernel)
  {
    delete[] kernel.data;
  }
};
