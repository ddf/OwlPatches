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
  float size = 0.2f;
  int samples = 10;

private:
  float standardDev;
  float standardDevSq;
  float gaussCoeff;

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY)
    : texture(textureData, textureSizeX, textureSizeY)
  {
    setStandardDeviation(0.02f);
  }

  void setStandardDeviation(float value)
  {
    standardDev = std::max(value, 0.01f);
    standardDevSq = standardDev * standardDev;
    gaussCoeff = 1.0f / sqrtf(2 * M_PI*standardDevSq);
  }

  float process(float input) override
  {
    texture.write(input);

    float sum = 0;
    float c = size * 0.5f;
    float v = 0;

    for (float s = 0; s < samples; ++s)
    {
      float offset = (s / (samples - 1) - 0.5f)*size;
      float gaussWeight = gaussCoeff * pow(M_E, -((offset*offset) / (2 * standardDevSq)));
      sum += gaussWeight;
      if (AXIS == AxisX)
      {
        v += texture.readBilinear(c + offset, 0) * gaussWeight;
      }
      else
      {
        v += texture.readBilinear(0, c + offset) * gaussWeight;
      }
    }

    return v / sum;
  }

  using SignalProcessor::process;

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
