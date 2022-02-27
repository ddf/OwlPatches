#include "SignalProcessor.h"
#include "CircularTexture.h"
#include "BlurKernel.h"
#include "basicmaths.h"

enum BlurAxis
{
  AxisX,
  AxisY
};

template<BlurAxis AXIS, typename TextureSizeType = size_t>
class BlurSignalProcessor : public SignalProcessor 
{
protected:
  CircularTexture<float, TextureSizeType> texture;

public:
  BlurKernel kernel;

  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX, textureSizeY)
    , kernel(kernel)
  {
    texture.setReadOffset(texture.getDataSize() * maxBlurSize * 0.5f);
  }

  void setTextureSize(TextureSizeType textureSize)
  {
    if (AXIS == AxisX)
    {
      texture = texture.subtexture(textureSize, 1);
    }
    else
    {
      texture = texture.subtexture(textureSize, textureSize);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float c = 0.0;
    float v = 0;
    const int samples = kernel.getSize();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      const float coord = c + samp.offset;
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        v += texture.readBilinear(coord, 0) * samp.weight;
      }
      else
      {
        v += texture.readBilinear(0, coord) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

  void process(FloatArray input, FloatArray output, SimpleArray<TextureSizeType> textureSize, BlurKernel kernelStep)
  {
    const int size = input.getSize();
    const int samples = kernel.getSize();
    for (int i = 0; i < size; ++i)
    {
      setTextureSize(textureSize[i]);
      output[i] = process(input[i]);
      for (int s = 0; s < samples; ++s)
      {
        kernel[s].offset += kernelStep[s].offset;
        kernel[s].weight += kernelStep[s].weight;
      }
      kernel.blurSize += kernelStep.blurSize;
    }
  }

public:
  static BlurSignalProcessor<AXIS, TextureSizeType>* create(size_t maxTextureSize, float maxBlurSize, BlurKernel blurKernel)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS, TextureSizeType>(new float[maxTextureSize], maxTextureSize, 1, maxBlurSize, blurKernel);
    else
      return new BlurSignalProcessor<AXIS, TextureSizeType>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, maxBlurSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor<AXIS, TextureSizeType>* blur)
  {
    delete[] blur->texture.getData();
    delete blur;
  }
};
