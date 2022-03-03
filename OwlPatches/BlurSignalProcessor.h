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
  CircularTexture<float, size_t> texture;
  size_t texSizeLow, texSizeHi;
  float texSizeBlend;

public:
  BlurKernel kernel;

  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX, textureSizeY)
    , kernel(kernel)
    , texSizeLow(textureSizeX), texSizeHi(textureSizeX), texSizeBlend(0)
  {
    //texture.setReadOffset(texture.getDataSize() * maxBlurSize * 0.5f);
  }

  void setTextureSize(TextureSizeType textureSize)
  {
    if (AXIS == AxisX)
    {
      texSizeLow = (size_t)textureSize;
      texSizeHi = texSizeLow + 1;
      texSizeBlend - textureSize - texSizeLow;
    }
    else
    {
      texture = texture.subtexture(textureSize, textureSize);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float v = 0;
    float c = kernel.blurSize * 0.5f;
    float w = texture.getWidth();
    const int samples = kernel.getSize();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      const float coord = c + samp.offset;
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        //v += texture.readBilinear(c + samp.offset, 0) * samp.weight;

        float x = coord * texSizeLow;
        size_t x1 = (size_t)x;
        size_t x2 = x1 + 1;
        float xt = x - x1;

        float vL = Interpolator::linear(texture.read(x1, 0), texture.read(x2, 0), xt) * samp.weight;

        x = coord * texSizeHi;
        x1 = (size_t)x;
        x2 = x1 + 1;
        xt = x - x1;

        float vH = Interpolator::linear(texture.read(x1, 0), texture.read(x2, 0), xt) * samp.weight;

        v += Interpolator::linear(vL, vH, texSizeBlend);
      }
      else
      {
        v += texture.readBilinear(0, c + samp.offset) * samp.weight;
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
      //if (AXIS == AxisX)
      //{
      //  texture.setReadOffset(textureSize[i] * kernel.blurSize * 0.5f);
      //}
      //else
      //{
      //  texture.setReadOffset(textureSize[i] * textureSize[i] * kernel.blurSize * 0.5f);
      //}
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
