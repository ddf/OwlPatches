#ifndef __BLUR_KERNEL__
#define __BLUR_KERNEL__

#include "SimpleArray.h"
#include "basicmaths.h"

struct BlurKernelSample
{
  constexpr BlurKernelSample() : offset(0), weight(0) {}
  constexpr BlurKernelSample(float offset, float weight) : offset(offset), weight(weight) {}

  // the offset of the sample from target pixel, in normalized coordinates
  float offset;

  // the weight of the sample
  float weight;
};

class BlurKernel : public SimpleArray<BlurKernelSample>
{
  float blurSize;

public:
  BlurKernel() : blurSize(0) {}
  BlurKernel(BlurKernelSample* data, size_t size) :
    SimpleArray(data, size), blurSize(0) {}

  float getBlurSize() const { return blurSize; }

  void setGauss(float blurSize, float standardDeviation, float scale = 1.0f)
  {
    this->blurSize = std::clamp(blurSize, 0.0f, 0.99f);
    standardDeviation = std::max(standardDeviation, 0.01f);

    float sum = 0;
    float standardDevSq = standardDeviation * standardDeviation;
    float gaussCoeff = 1.0f / sqrtf(2 * M_PI*standardDevSq);

    for (int s = 0; s < size; ++s)
    {
      float offset = ((float)s / (size - 1) - 0.5f)*blurSize;
      float gaussWeight = gaussCoeff * pow(M_E, -((offset*offset) / (2 * standardDevSq)));
      data[s] = BlurKernelSample(offset, gaussWeight);
      sum += gaussWeight;
    }

    // normalize the weights so we don't have to do this during processing and apply the scale
    for (int s = 0; s < size; ++s)
    {
      data[s].weight = (data[s].weight/sum) * scale;
    }
  }

  void clear()
  {
    for (int i = 0; i < size; ++i)
    {
      data[i] = BlurKernelSample();
    }
  }

  static BlurKernel create(size_t sampleCount)
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

#endif // __BLUR_KERNEL__
