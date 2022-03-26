 /**

AUTHOR:
    (c) 2022 Damien Quartz

LICENSE:
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


DESCRIPTION:
    Treats incoming audio as if it is square texture data and applies
    a Gaussian blur to it.

*/

#include "Patch.h"
#include "DcBlockingFilter.h"
#include "BiquadFilter.h"
#include "Resample.h"
#include "Interpolator.h"
#include "GaussianBlurSignalProcessor.h"
#include "SkewedValue.h"
#include "basicmaths.h"
#include "custom_dsp.h" // for SoftLimit
#include "Dynamics/compressor.h"
#include <string.h>

//#define DEBUG

typedef daisysp::Compressor Compressor;

#define FRACTIONAL_TEXTURE_SIZE
//#define USE_BLUR_FEEDBACK

#ifdef FRACTIONAL_TEXTURE_SIZE
#define SMOOTH_ACROSS_BLOCK
#ifdef USE_BLUR_FEEDBACK
typedef GaussianBlurWithFeedback<float> GaussianBlur;
#else
typedef GaussianBlurSignalProcessor<float> GaussianBlur;
#endif
#else
#ifdef USE_BLUR_FEEDBACK
typedef GaussianBlurWithFeedback<size_t> GaussianBlur;
#else
typedef GaussianBlurSignalProcessor<size_t> GaussianBlur;
#endif
#endif

struct BlurPatchParameterIds
{
  const PatchParameterId inTextureSize; // PARAMETER_A;
  const PatchParameterId inBlurSize; // PARAMETER_B;
  const PatchParameterId inFeedMag; // PARAMETER_C;
  const PatchParameterId inWetDry; // PARAMETER_D;

  const PatchParameterId inTextureTilt;
  const PatchParameterId inBlurTilt;
  const PatchParameterId inFeedTilt; // PARAMETER_E;

  // attenuate or boost the input signal during the blur
  const PatchParameterId inBlurBrightness; // PARAMETER_AA;

  // compressor parameters, which work as you'd expect
  const PatchParameterId inCompressionThreshold; // PARAMETER_AB;
  const PatchParameterId inCompressionRatio; // PARAMETER_AC;
  const PatchParameterId inCompressionAttack; // PARAMETER_AE;
  const PatchParameterId inCompressionRelease; // PARAMETER_AF;
  const PatchParameterId inCompressionMakeupGain; // PARAMETER_AD;
  const PatchParameterId inCompressionBlend; // PARAMETER_AG;

  const PatchParameterId outLeftFollow; // PARAMETER_F;
  const PatchParameterId outRightFollow; // PARAMETER_G;
};

template<int blurKernelSize, int blurResampleFactor, int blurResampleStages, typename PatchClass = Patch>
class BlurPatch : public PatchClass
{
  const BlurPatchParameterIds pid;

  static const int minTextureSize = 16 / blurResampleFactor;
  static const int maxTextureSize = 256 / blurResampleFactor;
  const  float minBlurSize        = 0.0f;
  const  float maxBlurSize        = 0.95f;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  const float standardDeviation = (blurKernelSize - 1) / 4.0f;

  const float blurBrightnessMin = 0.5f;
  const float blurBrightnessMax = 2.0f;
  const float blurBrightnessDefault = 1.0f;

  const float compressorThresholdMin = 0.0f;
  const float compressorThresholdMax = -80.0f;
  const float compressorThresholdDefault = compressorThresholdMin;

  const float compressorRatioMin = 1.0f;
  const float compressorRatioMax = 40.0f;
  const float compressorRatioDefault = 1.5f;

  const float compressorResponseMin = 0.001f;
  const float compressorResponseMax = 10.0f;
  const float compressorResponseDefault = 0.01f;

  const float compressorMakeupGainMin = 0.0f;
  const float compressorMakeupGainMax = 80.0f;
  const float compressorMakeupGainDefault = compressorMakeupGainMin;

  AudioBuffer* blurBuffer;
  AudioBuffer* feedbackBuffer;

  StereoDcBlockingFilter*     dcFilter;
  BiquadFilter*               feedbackFilterLeft;
  BiquadFilter*               feedbackFilterRight;

  DownSampler* blurDownLeft;
  DownSampler* blurDownRight;
  UpSampler*   blurUpLeft;
  UpSampler*   blurUpRight;
  StereoBiquadFilter* blurFilter;

  FloatArray   blurScratchA;
  FloatArray   blurScratchB;
  GaussianBlur* blurLeftA;
  GaussianBlur* blurRightA;

#ifdef SMOOTH_ACROSS_BLOCK
  FloatArray textureSizeRamp;
  BlurKernel blurKernelStep;
#endif

#ifndef FRACTIONAL_TEXTURE_SIZE
  GaussianBlur* blurLeftB;
  GaussianBlur* blurRightB;
#endif

  Compressor blurLeftCompressor;
  Compressor blurRightCompressor;

protected:
  SkewedFloat textureSize;
  SkewedFloat blurSize;

  // actual sizes used for textures based on size and tilt values
  SmoothFloat textureSizeLeft;
  SmoothFloat textureSizeRight;
  SmoothFloat blurSizeLeft;
  SmoothFloat blurSizeRight;
  SmoothFloat feedbackMagnitude;
  SmoothFloat feedbackAngle;

  SmoothFloat inLeftRms;
  SmoothFloat inRightRms;

  SmoothFloat compressionThreshold;
  SmoothFloat compressionRatio;
  SmoothFloat compressionAttack;
  SmoothFloat compressionRelease;
  SmoothFloat compressionMakeupGain;
  SmoothFloat compressionBlend;

  // because our base class is a template parameter, 
  // we need using statements for all base-class methods we call.
  using PatchClass::registerParameter;
  using PatchClass::setParameterValue;
  using PatchClass::getParameterValue;
  using PatchClass::getBlockSize;
  using PatchClass::getSampleRate;
  using PatchClass::setButton;

public:
  BlurPatch(BlurPatchParameterIds params)
    : pid(params), textureSize(0), blurSize(0)
    , textureSizeLeft(0.9f, minTextureSize), textureSizeRight(0.9f, minTextureSize)
    , blurSizeLeft(0.9f, 0.0f), blurSizeRight(0.9f, 0.0f)
    , compressionThreshold(0.9f, compressorThresholdDefault), compressionRatio(0.9f, compressorRatioDefault)
    , compressionAttack(0.9f, compressorResponseDefault), compressionRelease(0.9f, compressorResponseDefault)
    , compressionMakeupGain(0.9f, compressorMakeupGainDefault)
  {
    registerParameter(pid.inTextureSize, "Tex Size");
    if (pid.inTextureSize != pid.inTextureTilt)
    {
      registerParameter(pid.inTextureTilt, "Tex Tilt");
      setParameterValue(pid.inTextureTilt, 0.5f);
    }
    registerParameter(pid.inBlurSize, "Blur Size");
    if (pid.inBlurSize != pid.inBlurTilt)
    {
      registerParameter(pid.inBlurTilt, "Blur Tilt");
      setParameterValue(pid.inBlurTilt, 0.5f);
    }
    registerParameter(pid.inFeedMag, "Fdbk Amt");
    registerParameter(pid.inFeedTilt, "Fdbk Tilt");
    registerParameter(pid.inWetDry, "Dry/Wet");
    registerParameter(pid.inBlurBrightness, "Blur Gain");
    registerParameter(pid.inCompressionThreshold, "Comp Thrsh");
    registerParameter(pid.inCompressionRatio, "Comp Ratio");
    registerParameter(pid.inCompressionAttack, "Comp Att");
    registerParameter(pid.inCompressionRelease, "Comp Rel");
    registerParameter(pid.inCompressionMakeupGain, "Comp Mkup");
    registerParameter(pid.inCompressionBlend, "Comp Blend");

    registerParameter(pid.outLeftFollow, "L Env>");
    registerParameter(pid.outRightFollow, "R Env>");

    setParameterValue(pid.inTextureSize, 0.0f);
    setParameterValue(pid.inBlurSize,    0.0f);
    setParameterValue(pid.inFeedMag, 0.0f);
#ifdef USE_BLUR_FEEDBACK
    setParameterValue(pid.inFeedTilt, 0.125f);
#else
    setParameterValue(pid.inFeedTilt, 0.5f);
#endif
    setParameterValue(pid.inWetDry, 1);
    setParameterValue(pid.inBlurBrightness, (blurBrightnessDefault - blurBrightnessMin) / (blurBrightnessMax - blurBrightnessMin));
    setParameterValue(pid.inCompressionThreshold, (compressorThresholdDefault - compressorThresholdMin) / (compressorThresholdMax - compressorThresholdMin));
    setParameterValue(pid.inCompressionRatio, (compressorRatioDefault - compressorRatioMin) / (compressorRatioMax  - compressorRatioMin));
    setParameterValue(pid.inCompressionAttack, (compressorResponseDefault - compressorResponseMin) / (compressorResponseMax - compressorResponseMin));
    setParameterValue(pid.inCompressionRelease, (compressorResponseDefault - compressorResponseMin) / (compressorResponseMax - compressorResponseMin));
    setParameterValue(pid.inCompressionMakeupGain, (compressorMakeupGainDefault - compressorMakeupGainMin) / (compressorMakeupGainMax - compressorMakeupGainMin));
    setParameterValue(pid.inCompressionBlend, 1.0f);

    setParameterValue(pid.outLeftFollow, 0);
    setParameterValue(pid.outRightFollow, 0);

    dcFilter = StereoDcBlockingFilter::create();
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    if (downsamplingEnabled())
    {
      blurDownLeft = DownSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
      blurDownRight = DownSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
      blurUpLeft = UpSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
      blurUpRight = UpSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);

      // final filter after upsampling to address aliasing
      blurFilter = StereoBiquadFilter::create(getSampleRate());
      // cutoff at half our downsampled sample rate to remove aliasing introduced by resampling
      blurFilter->setLowPass(getSampleRate() / blurResampleFactor * 0.5f, 1.0f);
    }

    blurScratchA = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurScratchB = FloatArray::create(getBlockSize() / blurResampleFactor);

#ifdef USE_BLUR_FEEDBACK
    blurLeftA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize, getSampleRate() / blurResampleFactor, getBlockSize() / blurResampleFactor);
    blurRightA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize, getSampleRate() / blurResampleFactor, getBlockSize() / blurResampleFactor);
#else
    blurLeftA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize);
    blurRightA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize);
#endif

    blurLeftA->setBlur(minBlurSize, standardDeviation);
    blurRightA->setBlur(minBlurSize, standardDeviation);

#ifndef FRACTIONAL_TEXTURE_SIZE
    blurLeftB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviation, blurKernelSize);
    blurRightB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviation, blurKernelSize);
#endif

#ifdef SMOOTH_ACROSS_BLOCK
    textureSizeRamp = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurKernelStep = BlurKernel::create(blurKernelSize);
#endif

    blurLeftCompressor.Init(getSampleRate());
    blurRightCompressor.Init(getSampleRate());

    blurLeftCompressor.SetThreshold(compressionThreshold);
    blurRightCompressor.SetThreshold(compressionThreshold);

    blurLeftCompressor.SetRatio(compressionRatio);
    blurRightCompressor.SetRatio(compressionRatio);

    blurLeftCompressor.SetAttack(compressionAttack);
    blurRightCompressor.SetAttack(compressionAttack);

    blurLeftCompressor.SetRelease(compressionAttack);
    blurRightCompressor.SetRelease(compressionAttack);

    blurLeftCompressor.AutoMakeup(false);
    blurLeftCompressor.SetMakeup(compressionMakeupGain);

    blurRightCompressor.AutoMakeup(false);
    blurRightCompressor.SetMakeup(compressionMakeupGain);
  }

  ~BlurPatch()
  {
    AudioBuffer::destroy(blurBuffer);
    AudioBuffer::destroy(feedbackBuffer);

    if (downsamplingEnabled())
    {
      DownSampler::destroy(blurDownLeft);
      DownSampler::destroy(blurDownRight);
      UpSampler::destroy(blurUpLeft);
      UpSampler::destroy(blurUpRight);
    }

    StereoDcBlockingFilter::destroy(dcFilter);
    BiquadFilter::destroy(feedbackFilterLeft);
    BiquadFilter::destroy(feedbackFilterRight);

    FloatArray::destroy(blurScratchA);
    FloatArray::destroy(blurScratchB);

    GaussianBlur::destroy(blurLeftA);
    GaussianBlur::destroy(blurRightA);

#ifndef FRACTIONAL_TEXTURE_SIZE
    GaussianBlur::destroy(blurLeftB);
    GaussianBlur::destroy(blurRightB);
#endif

#ifdef SMOOTH_ACROSS_BLOCK
    FloatArray::destroy(textureSizeRamp);
    BlurKernel::destroy(blurKernelStep);
#endif
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (pid.inTextureSize == pid.inTextureTilt && bid == BUTTON_1 && value == Patch::ON)
    {
      textureSize.toggleSkew();
      if (textureSize.skewEnabled())
      {
        textureSize.resetSkew();
      }
    }

    if (pid.inBlurSize == pid.inBlurTilt && bid == BUTTON_2 && value == Patch::ON)
    {
      blurSize.toggleSkew();
      if (blurSize.skewEnabled())
      {
        blurSize.resetSkew();
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray outBlurLeft = blurBuffer->getSamples(0);
    FloatArray outBlurRight = blurBuffer->getSamples(1);
    FloatArray feedLeft = feedbackBuffer->getSamples(0);
    FloatArray feedRight = feedbackBuffer->getSamples(1);

    const int blockSize = getBlockSize();
    
    textureSize = getParameterValue(pid.inTextureSize);
    if (pid.inTextureSize != pid.inTextureTilt)
    {
      textureSize.setSkew(getParameterValue(pid.inTextureTilt) * 2 - 1);
    }
    blurSize = getParameterValue(pid.inBlurSize);
    if (pid.inBlurSize != pid.inBlurTilt)
    {
      blurSize.setSkew(getParameterValue(pid.inBlurTilt) * 2 - 1);
    }

#ifdef SMOOTH_ACROSS_BLOCK
    float prevTexLeft = textureSizeLeft;
    float prevTexRight = textureSizeRight;
#endif

    textureSizeLeft   = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSize.getLeft(), 0.0f, 1.0f));
    textureSizeRight  = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSize.getRight(), 0.0f, 1.0f));
    // scale max blur down so we never blur more than a maximum number of samples away
    const float leftBlurScale  = minTextureSize / textureSizeLeft;
    const float rightBlurScale = minTextureSize / textureSizeRight;
    blurSizeLeft  = Interpolator::linear(minBlurSize * leftBlurScale, maxBlurSize * leftBlurScale, std::clamp(blurSize.getLeft(), 0.0f, 1.0f));
    blurSizeRight = Interpolator::linear(minBlurSize * rightBlurScale, maxBlurSize * rightBlurScale, std::clamp(blurSize.getRight(), 0.0f, 1.0f));

    float brightnessParam = getParameterValue(pid.inBlurBrightness);
    float blurBrightness = blurBrightnessDefault;
    if (brightnessParam >= 0.53f)
    {
      blurBrightness = Interpolator::linear(blurBrightnessDefault, blurBrightnessMax, (brightnessParam - 0.53f) * 2.12f);
    }
    else if (brightnessParam <= 0.47f)
    {
      blurBrightness = Interpolator::linear(blurBrightnessDefault, blurBrightnessMin, (0.47f - brightnessParam) * 2.12f);
    }

    feedbackMagnitude = getParameterValue(pid.inFeedMag);
#ifdef USE_BLUR_FEEDBACK
    feedbackAngle = Interpolator::linear(0.0f, M_PI * 2, getParameterValue(pid.inFeedTilt));
#else
    feedbackAngle = Interpolator::linear(0.0f, M_PI_2, getParameterValue(pid.inFeedTilt));
#endif

    compressionThreshold = Interpolator::linear(0, -80, getParameterValue(pid.inCompressionThreshold));
    blurLeftCompressor.SetThreshold(compressionThreshold);
    blurRightCompressor.SetThreshold(compressionThreshold);

    compressionRatio = Interpolator::linear(compressorRatioMin, compressorRatioMax, getParameterValue(pid.inCompressionRatio));
    blurLeftCompressor.SetRatio(compressionRatio);
    blurRightCompressor.SetRatio(compressionRatio);

    compressionAttack = Interpolator::linear(compressorResponseMin, compressorResponseMax, getParameterValue(pid.inCompressionAttack));
    blurLeftCompressor.SetAttack(compressionAttack);
    blurRightCompressor.SetAttack(compressionAttack);

    compressionRelease = Interpolator::linear(compressorResponseMin, compressorResponseMax, getParameterValue(pid.inCompressionRelease));
    blurLeftCompressor.SetRelease(compressionRelease);
    blurRightCompressor.SetRelease(compressionRelease);

    compressionMakeupGain = Interpolator::linear(compressorMakeupGainMin, compressorMakeupGainMax, getParameterValue(pid.inCompressionMakeupGain));
    blurLeftCompressor.SetMakeup(compressionMakeupGain);
    blurRightCompressor.SetMakeup(compressionMakeupGain);

    compressionBlend = getParameterValue(pid.inCompressionBlend);

    //dcFilter->process(audio, audio);

    inLeftRms = inLeft.getRms() * blurBrightness;
    inRightRms = inRight.getRms() * blurBrightness;

#ifdef USE_BLUR_FEEDBACK
    inLeft.copyTo(feedLeft);
    inRight.copyTo(feedRight);
#else
    const float feedStrL = feedbackMagnitude * cosf(feedbackAngle);
    const float feedStrR = feedbackMagnitude * sinf(feedbackAngle);

    // Note: the way feedback is applied is based on how Clouds does it
    const float cutoffL = (20.0f + 100.0f * feedStrL * feedStrL);
    const float cutoffR = (20.0f + 100.0f * feedStrR * feedStrR);
    const float softLimitCoeffL = feedStrL * 1.4f;
    const float softLimitCoeffR = feedStrR * 1.4f;

    feedbackFilterLeft->setHighPass(cutoffL, 1);
    feedbackFilterLeft->process(feedLeft);
    feedbackFilterRight->setHighPass(cutoffR, 1);
    feedbackFilterRight->process(feedRight);
    for (int i = 0; i < blockSize; ++i)
    {
      float left = inLeft[i];
      float right = inRight[i];
      feedLeft[i] = left + feedStrL * (daisysp::SoftLimit(softLimitCoeffL * feedLeft[i] + left) - left);
      feedRight[i] = right + feedStrR * (daisysp::SoftLimit(softLimitCoeffR * feedRight[i] + right) - right);
    }
#endif

    if (blurResampleFactor == 4)
    {
      // HACK: adjust brightness to compensate for signal strength changes from resampling
      // eventually, with correct resampling code, we shouldn't need to adjust signal strength
      //blurBrightness *= 0.375f; // for upsampling that boosted the signal too much
      blurBrightness *= 2.25f; // for upsampling that doesn't boost the signal at all
    }

#ifndef FRACTIONAL_TEXTURE_SIZE
    int texLeftA = (int)textureSizeLeft;
    int texLeftB = texLeftA + 1;
    float texLeftBlend = textureSizeLeft - texLeftA;

    int texRightA = (int)textureSizeRight;
    int texRightB = texRightA + 1;
    float texRightBlend = textureSizeRight - texRightA;

    blurLeftA->setBlur(blurSizeLeft, standardDeviation, (1.0f - texLeftBlend)*blurBrightness);
    blurLeftA->setTextureSize(texLeftA);
    blurLeftB->setBlur(blurSizeLeft, standardDeviation, texLeftBlend*blurBrightness);
    blurLeftB->setTextureSize(texLeftB);

    blurRightA->setBlur(blurSizeRight, standardDeviation, (1.0f - texRightBlend)*blurBrightness);
    blurRightA->setTextureSize(texRightA);
    blurRightB->setBlur(blurSizeRight, standardDeviation, texRightBlend*blurBrightness);
    blurRightB->setTextureSize(texRightB);
#endif

    // left channel blur
    {

      // downsample and copy
      if (downsamplingEnabled())
      {
        blurDownLeft->process(feedLeft, blurScratchA);
      }
      else
      {
        feedLeft.copyTo(blurScratchA);
      }

#ifdef FRACTIONAL_TEXTURE_SIZE
#ifdef USE_BLUR_FEEDBACK
      blurLeftA->setFeedback(feedbackMagnitude, feedbackAngle);
#endif
#ifdef SMOOTH_ACROSS_BLOCK
      textureSizeRamp.ramp(prevTexLeft, textureSizeLeft);
      blurKernelStep.setGauss(blurSizeLeft, standardDeviation, blurBrightness);
      blurKernelStep.blurSize = (blurSizeLeft - blurLeftA->getBlurSize()) / blockSize;
      for (int i = 0; i < blurKernelSize; ++i)
      {
        BlurKernelSample to = blurKernelStep[i];
        BlurKernelSample from = blurLeftA->getKernelSample(i);
        blurKernelStep[i] = BlurKernelSample((to.offset - from.offset) / blockSize, (to.weight - from.weight) / blockSize);
      }
      blurLeftA->process(blurScratchA, blurScratchA, textureSizeRamp, blurKernelStep);
#else
      blurLeftA->setTextureSize(textureSizeLeft);
      blurLeftA->setBlur(blurSizeLeft, standardDeviation, blurBrightness);
      blurLeftA->process(blurScratchA, blurScratchA);
#endif
#else
#ifdef USE_BLUR_FEEDBACK
      blurLeftA->setFeedback(feedbackMagnitude, feedbackAngle);
      blurLeftB->setFeedback(feedbackMagnitude, feedbackAngle);
#endif
      // process both texture sizes
      blurLeftB->process(blurScratchA, blurScratchB);
      blurLeftA->process(blurScratchA, blurScratchA);

      // mix 
      blurScratchA.add(blurScratchB);
#endif

      // compress
      blurLeftCompressor.ProcessBlock(blurScratchA, blurScratchB, blurScratchA.getSize());
      blurScratchA.multiply(1.0f - compressionBlend);
      blurScratchB.multiply(compressionBlend);
      blurScratchA.add(blurScratchB);

      // upsample to the output
      if (downsamplingEnabled())
      {
        blurUpLeft->process(blurScratchA, outBlurLeft);
      }
      else
      {
        blurScratchA.copyTo(outBlurLeft);
      }
    }

    // right channel blur
    {
      // downsample and copy
      if (downsamplingEnabled())
      {
        blurDownRight->process(feedRight, blurScratchA);
      }
      else
      {
        feedRight.copyTo(blurScratchA);
      }

#ifdef FRACTIONAL_TEXTURE_SIZE
#ifdef USE_BLUR_FEEDBACK
      blurRightA->setFeedback(feedbackMagnitude, feedbackAngle);
#endif
#ifdef SMOOTH_ACROSS_BLOCK
      textureSizeRamp.ramp(prevTexRight, textureSizeRight);
      blurKernelStep.setGauss(blurSizeRight, standardDeviation, blurBrightness);
      blurKernelStep.blurSize = (blurSizeRight - blurRightA->getBlurSize()) / blockSize;
      for (int i = 0; i < blurKernelSize; ++i)
      {
        BlurKernelSample to = blurKernelStep[i];
        BlurKernelSample from = blurRightA->getKernelSample(i);
        blurKernelStep[i] = BlurKernelSample((to.offset - from.offset) / blockSize, (to.weight - from.weight) / blockSize);
      }
      blurRightA->process(blurScratchA, blurScratchA, textureSizeRamp, blurKernelStep);
#else
      blurRightA->setTextureSize(textureSizeRight);
      blurRightA->setBlur(blurSizeRight, standardDeviation, blurBrightness);
      blurRightA->process(blurScratchA, blurScratchA);
#endif
#else
#ifdef USE_BLUR_FEEDBACK
      blurRightA->setFeedback(feedbackMagnitude, feedbackAngle);
      blurRightB->setFeedback(feedbackMagnitude, feedbackAngle);
#endif
      // process both texture sizes
      blurRightB->process(blurScratchA, blurScratchB);
      blurRightA->process(blurScratchA, blurScratchA);

      // mix
      blurScratchA.add(blurScratchB);
#endif

      // compress
      blurRightCompressor.ProcessBlock(blurScratchA, blurScratchB, blurScratchA.getSize());
      blurScratchA.multiply(1.0f - compressionBlend);
      blurScratchB.multiply(compressionBlend);
      blurScratchA.add(blurScratchB);

      // upsample to the output
      if (downsamplingEnabled())
      {
        blurUpRight->process(blurScratchA, outBlurRight);
      }
      else
      {
        blurScratchA.copyTo(outBlurRight);
      }
    }

    if (downsamplingEnabled())
    {
      blurFilter->process(*blurBuffer, *blurBuffer);
    }

#ifndef USE_BLUR_FEEDBACK
    outBlurLeft.copyTo(feedLeft);
    outBlurRight.copyTo(feedRight);
#endif
    
    // do wet/dry mix with original signal
    float wet = getParameterValue(pid.inWetDry);
    float dry = 1.0f - wet;
    inLeft.multiply(dry);
    inRight.multiply(dry);
    outBlurLeft.multiply(wet);
    outBlurRight.multiply(wet);
    inLeft.add(outBlurLeft);
    inRight.add(outBlurRight);

    setParameterValue(pid.outLeftFollow, inLeftRms);
    setParameterValue(pid.outRightFollow, inRightRms);
    setButton(BUTTON_1, textureSize.skewEnabled());
    setButton(BUTTON_2, blurSize.skewEnabled());

#ifdef DEBUG
    setParameterValue(outLeftFollow, (textureSizeLeft - minTextureSize) / (maxTextureSize - minTextureSize));
    setParameterValue(outRightFollow, (textureSizeRight - minTextureSize) / (maxTextureSize - minTextureSize));

    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "texL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(textureSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " bL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeLeft, 10));

    debugCpy = stpcpy(debugCpy, " texR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(textureSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " bR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeRight, 10));

    debugCpy = stpcpy(debugCpy, " cR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(compressionAttack, 10));

    debugMessage(debugMsg);
#endif
  }
private:

  bool downsamplingEnabled() const
  {
    return blurResampleFactor > 1;
  }


};
