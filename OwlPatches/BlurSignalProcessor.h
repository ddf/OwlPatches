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
  CircularFloatTexture textureA;
  CircularFloatTexture textureB;
  float textureBlend;
  BlurKernel kernel;

public:
  BlurSignalProcessor() {}
  BlurSignalProcessor(float* textureData, int textureSizeX, int textureSizeY, BlurKernel kernel)
    : textureA(textureData, textureSizeX, textureSizeY)
    , textureB(textureData, textureSizeX, textureSizeY)
    , textureBlend(0)
    , kernel(kernel)
  {
  }

  void setKernel(BlurKernel kernel)
  {
    this->kernel = kernel;
  }

  void setTextureSize(float textureSize)
  {
    int texSize = (int)textureSize;
    textureBlend = textureSize - texSize;
    if (AXIS == AxisX)
    {
      textureA = textureA.subtexture(texSize, 1);
      textureB = textureB.subtexture(texSize + 1, 1);
    }
    else
    {
      textureA = textureA.subtexture(texSize, texSize);
      textureB = textureB.subtexture(texSize + 1, texSize + 1);
    }
  }

  float process(float input) override
  {
    // only need to write to textureA because textureB is just a different view on it
    textureA.write(input);

    float c = kernel.getBlurSize() * 0.5f;
    float v = 0;
    const int samples = kernel.getSize();
    const float dimA = AXIS == AxisX ? textureA.getWidth() : textureA.getHeight();
    for (int s = 0; s < samples; ++s)
    {
      BlurKernelSample samp = kernel[s];
      // read with linear interp across the axis we care about
      if (AXIS == AxisX)
      {
        //v += textureA.readBilinear(c + samp.offset, 0) * samp.weight;
        float x = (c + samp.offset)*dimA;
        int x1 = int(x);
        int x2 = x1 + 1;
        float xt = x - x1;
        //float vA = Interpolator::linear(textureA.read(x1, 0), textureA.read(x2, 0), xt);

        v += Interpolator::linear(textureA.read(x1, 0), textureA.read(x2, 0), xt) * samp.weight;

        //x = (c + samp.offset)*textureB.getWidth();
        //x1 = int(x);
        //x2 = x1 + 1;
        //xt = x - x1;
        //float vB = Interpolator::linear(textureA.read(x1, 0), textureA.read(x2, 0), xt);

        //v += Interpolator::linear(vA, vB, textureBlend)* samp.weight;
      }
      else
      {
        //v += textureA.readBilinear(0, c + samp.offset) * samp.weight;
        float y = (c + samp.offset)*dimA;
        int y1 = int(y);
        int y2 = y1 + 1;
        float yt = y - y1;
        //float vA = Interpolator::linear(textureA.read(0, y1), textureA.read(0, y2), yt);

        v += Interpolator::linear(textureA.read(0, y1), textureA.read(0, y2), yt) * samp.weight;

        //y = (c + samp.offset)*textureB.getHeight();
        //y1 = int(y);
        //y2 = y1 + 1;
        //yt = y - y1;
        //float vB = Interpolator::linear(textureB.read(0, y1), textureB.read(0, y2), yt);

        //v += Interpolator::linear(vA, vB, textureBlend) * samp.weight;
      }
    }

    return v;
  }

  using SignalProcessor::process;

public:
  static BlurSignalProcessor<AXIS>* create(int maxTextureSize, BlurKernel blurKernel)
  {
    if (AXIS == AxisX)
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize], maxTextureSize, 1, blurKernel);
    else
      return new BlurSignalProcessor<AXIS>(new float[maxTextureSize*maxTextureSize], maxTextureSize, maxTextureSize, blurKernel);
  }

  static void destroy(BlurSignalProcessor<AXIS>* blur)
  {
    delete[] blur->textureA.getData();
    delete blur;
  }
};
