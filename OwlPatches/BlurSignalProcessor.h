#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "basicmaths.h"

enum BlurAxis
{
  AxisX,
  AxisY
};

template<BlurAxis AXIS>
class BlurSignalProcessor : public SignalProcessor 
{
protected:
  CircularFloatTexture texture;

private:
  float size = 0.2f;
  float standardDev = 0.02f;
  int samples = 8;
  ComplexFloatArray sampleOffsetsAndWeights;

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY)
    : texture(textureData, textureSizeX, textureSizeY)
  {
    sampleOffsetsAndWeights = ComplexFloatArray::create(samples);
    calculateSampleSettings();
  }

  ~BlurSignalProcessor()
  {
    ComplexFloatArray::destroy(sampleOffsetsAndWeights);
  }

  void setTextureSize(int texSizeX, int texSizeY)
  {
  }

  void setSizeAndStandardDeviation(float inSize, float inStandardDev)
  {
    size = std::clamp(inSize, 0.0f, 0.99f);
    standardDev = std::max(inStandardDev, 0.01f);
    calculateSampleSettings();
  }

  float process(float input) override
  {
    texture.write(input);

    float c = size * 0.5f;
    float v = 0;

    for (int s = 0; s < samples; ++s)
    {
      float offset = sampleOffsetsAndWeights[s].re;
      float gaussWeight = sampleOffsetsAndWeights[s].im;
      if (AXIS == AxisX)
      {
        v += texture.readBilinear(c + offset, 0) * gaussWeight;
      }
      else
      {
        v += texture.readBilinear(0, c + offset) * gaussWeight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

private:
  void calculateSampleSettings()
  {
    float sum = 0;
    float standardDevSq = standardDev * standardDev;
    float gaussCoeff = 1.0f / sqrtf(2 * M_PI*standardDevSq);
    
    for (int s = 0; s < samples; ++s)
    {
      float offset = ((float)s / (samples - 1) - 0.5f)*size;
      float gaussWeight = gaussCoeff * pow(M_E, -((offset*offset) / (2 * standardDevSq)));
      sampleOffsetsAndWeights[s] = ComplexFloat(offset, gaussWeight);
      sum += gaussWeight;
    }

    // normalize the weights so we don't have to do this during processing
    for (int s = 0; s < samples; ++s)
    {
      sampleOffsetsAndWeights[s].im = sampleOffsetsAndWeights[s].im / sum;
    }
  }

public:
  static BlurSignalProcessor<AXIS>* create(int maxTextureSize)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize], maxTextureSize, 1);
    else
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize);
  }

  static void destroy(BlurSignalProcessor<AXIS>* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
