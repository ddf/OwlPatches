#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "BlurKernel.h"
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
  BlurKernel kernel;

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, BlurKernel kernel)
    : texture(textureData, textureSizeX, textureSizeY), kernel(kernel)
  {
  }

  ~BlurSignalProcessor()
  {
    ComplexFloatArray::destroy(sampleOffsetsAndWeights);
  }

  void setKernel(BlurKernel kernel)
  {
    this->kernel = kernel;
  }

  void setTextureSize(int texSize)
  {
    if (AXIS == AxisX)
    {
      texture = texture.subtexture(texSize, 1);
    }
    else
    {
      texture = texture.subtexture(texSize, texSize);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float c = kernel.getBlurSize() * 0.5f;
    float v = 0;
    int samples = kernel.getSize();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      if (AXIS == AxisX)
      {
        v += texture.readBilinear(c + samp.offset, 0) * samp.weight;
      }
      else
      {
        v += texture.readBilinear(0, c + samp.offset) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

public:
  static BlurSignalProcessor<AXIS>* create(int maxTextureSize, BlurKernel kernal)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize], maxTextureSize, 1, kernal);
    else
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, kernel);
  }

  static void destroy(BlurSignalProcessor<AXIS>* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
