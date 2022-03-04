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
  CircularTexture<float, size_t> textureB;
  float texSize;
  size_t texSizeLow, texSizeHi;
  float texSizeBlend;

public:
  BlurKernel kernel;

  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, float maxBlurSize, BlurKernel kernel)
    : texture(textureData, textureSizeX, textureSizeY), textureB(textureData, textureSizeX, textureSizeY)
    , kernel(kernel), texSize(textureSizeX)
    , texSizeLow(textureSizeX), texSizeHi(textureSizeX), texSizeBlend(0)
  {
    //texture.setReadOffset(texture.getDataSize() * maxBlurSize * 0.5f);
  }

  void setTextureSize(TextureSizeType textureSize)
  {
    texSize = textureSize;
    texSizeLow = (size_t)textureSize;
    texSizeHi = texSizeLow + 1;
    texSizeBlend = textureSize - texSizeLow;

    if (AXIS == AxisX)
    {
      //texture = texture.subtexture(textureSize, 1);
    }
    else
    {
      texture = texture.subtexture(texSizeLow, texSizeLow);
      textureB = texture.subtexture(texSizeHi, texSizeHi);
    }
  }

  float process(float input) override
  {
    texture.write(input);

    float v = 0;
    const float c = kernel.blurSize * 0.5f;
    const int samples = kernel.getSize();
    const float readOffset = AXIS == AxisY ? texSize * texSize * c + 1: 0.0f;
    //const float u1 = AXIS == AxisY ? c * (texSize + 1 - texSizeLow) / texSizeLow : 0.0f;
    //const float u2 = AXIS == AxisY ? c * (texSize + 1 - texSizeHi) / texSizeHi : 0.0f;
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        //v += texture.readBilinear(c + samp.offset, 0) * samp.weight;

        float x = (c + samp.offset) * texSize;
        size_t x1 = (size_t)x;
        size_t x2 = x1 + 1;
        float xt = x - x1;

        v += Interpolator::linear(texture.read(x1, 0), texture.read(x2, 0), xt) * samp.weight;
      }
      else
      {
        //v += Interpolator::linear(texture.readBilinear(u1, coord), textureB.readBilinear(u2, coord), texSizeBlend) * samp.weight;
        
        float ya = samp.offset * texSizeLow;
        size_t ya1 = (size_t)ya;
        size_t ya2 = ya1 + 1;
        float yat = ya - ya1;

        float yb = samp.offset * texSizeHi;
        size_t yb1 = (size_t)yb;
        size_t yb2 = yb1 + 1;
        float ybt = yb + yb1;

        float va = Interpolator::linear(texture.read(0, ya1, readOffset), texture.read(0, ya2, readOffset), yat);
        float vb = Interpolator::linear(textureB.read(0, yb1, readOffset), textureB.read(0, yb2, readOffset), ybt);

        //v += Interpolator::linear(va, vb, texSizeBlend) * samp.weight;
        v += va * samp.weight;
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
