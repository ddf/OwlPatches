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

#define FRACTIONAL_TEXTURE_SIZE
//#define SMOOTH_ACROSS_BLOCK

typedef daisysp::Compressor Compressor;

#ifdef FRACTIONAL_TEXTURE_SIZE
typedef GaussianBlurSignalProcessor<float> GaussianBlur;
#else
typedef GaussianBlurSignalProcessor<size_t> GaussianBlur;
#endif

class BlurPatch : public Patch
{
  static const PatchParameterId inTextureSize = PARAMETER_A;
  static const PatchParameterId inBlurSize    = PARAMETER_B;
  static const PatchParameterId inFeedback    = PARAMETER_C;
  static const PatchParameterId inWetDry      = PARAMETER_D;

  // attenuate or boost the input signal during the blur
  static const PatchParameterId inBlurBrightness = PARAMETER_AA;

  // compressor parameters, which work as you'd expect
  static const PatchParameterId inCompressionThreshold  = PARAMETER_AB;
  static const PatchParameterId inCompressionRatio      = PARAMETER_AC;
  static const PatchParameterId inCompressionMakeupGain = PARAMETER_AD;
  static const PatchParameterId inCompressionAttack     = PARAMETER_AE;
  static const PatchParameterId inCompressionRelease    = PARAMETER_AF;

  // unused, but keeping it around in case I want to quickly hook it up and tweak it for some reason
  static const PatchParameterId inStandardDev = PARAMETER_AH;

  static const PatchParameterId outLeftFollow = PARAMETER_F;
  static const PatchParameterId outRightFollow = PARAMETER_G;

  static const int blurKernelSize     = 7;
  static const int blurResampleStages = 4;
  static const int blurResampleFactor = 4;

  static const int minTextureSize = 16 / blurResampleFactor;
  static const int maxTextureSize = 256 / blurResampleFactor;
  const  float maxBlurSamples     = 31.0f / blurResampleFactor;
  const  float minBlurSize        = 0.15f;
  const  float maxBlurSize        = 0.95f;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  // The minimum value here sounds about the same with smaller radii,
  // it's really only at larger texture sizes combined with larger radii
  // that you start to hear a difference when sweeping the standard deviation,
  // with the maximum value giving the smoothest sounding results.
  const float maxStandardDev = (blurKernelSize - 1) / 4.0f;
  const float minStandardDev = maxStandardDev / 3.0f;

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

  FloatArray   blurScratchA;
  GaussianBlur* blurLeftA;
  GaussianBlur* blurRightA;

#ifdef FRACTIONAL_TEXTURE_SIZE
  FloatArray textureSizeRamp;
  BlurKernel blurKernelStep;
#else
  FloatArray   blurScratchB;
  GaussianBlur* blurLeftB;
  GaussianBlur* blurRightB;
#endif

  SkewedFloat textureSize;
  SkewedFloat blurSize;

  // actual sizes used for textures based on size and tilt values
  SmoothFloat textureSizeLeft;
  SmoothFloat textureSizeRight;
  SmoothFloat blurSizeLeft;
  SmoothFloat blurSizeRight;
  SmoothFloat standardDeviation;
  SmoothFloat feedback;

  SmoothFloat inLeftRms;
  SmoothFloat inRightRms;

  SmoothFloat compressionThreshold;
  SmoothFloat compressionRatio;
  SmoothFloat compressionAttack;
  SmoothFloat compressionRelease;
  SmoothFloat compressionMakeupGain;

  Compressor blurLeftCompressor;
  Compressor blurRightCompressor;

public:
  BlurPatch()
    : textureSize(0), blurSize(0)
    , textureSizeLeft(0.9f, minTextureSize), textureSizeRight(0.9f, minTextureSize)
    , blurSizeLeft(0.9f, 0.0f), blurSizeRight(0.9f, 0.0f)
    , standardDeviation(0.9f, maxStandardDev)
    , compressionThreshold(0.9f, compressorThresholdDefault), compressionRatio(0.9f, compressorRatioDefault)
    , compressionAttack(0.9f, compressorResponseDefault), compressionRelease(0.9f, compressorResponseDefault)
    , compressionMakeupGain(0.9f, compressorMakeupGainDefault)
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inFeedback, "Feedback");
    registerParameter(inWetDry, "Dry/Wet");
    registerParameter(inBlurBrightness, "Blur Brightness");
    registerParameter(inCompressionThreshold, "Blur Compressor Threshold");
    registerParameter(inCompressionRatio, "Blur Compressor Ratio");
    registerParameter(inCompressionAttack, "Blur Compressor Attack");
    registerParameter(inCompressionRelease, "Blur Compressor Release");
    registerParameter(inCompressionMakeupGain, "Blur Compressor Makeup Gain");

    //registerParameter(inStandardDev, "Standard Deviation");

    registerParameter(outLeftFollow, "Left Follow>");
    registerParameter(outRightFollow, "Right Follow>");

    setParameterValue(inTextureSize, 0.0f);
    setParameterValue(inBlurSize,    0.0f);
    setParameterValue(inFeedback, 0.0f);
    setParameterValue(inWetDry, 1);
    setParameterValue(inBlurBrightness, (blurBrightnessDefault - blurBrightnessMin) / (blurBrightnessMax - blurBrightnessMin));
    setParameterValue(inCompressionThreshold, (compressorThresholdDefault - compressorThresholdMin) / (compressorThresholdMax - compressorThresholdMin));
    setParameterValue(inCompressionRatio, (compressorRatioDefault - compressorRatioMin) / (compressorRatioMax  - compressorRatioMin));
    setParameterValue(inCompressionAttack, (compressorResponseDefault - compressorResponseMin) / (compressorResponseMax - compressorResponseMin));
    setParameterValue(inCompressionRelease, (compressorResponseDefault - compressorResponseMin) / (compressorResponseMax - compressorResponseMin));
    setParameterValue(inCompressionMakeupGain, (compressorMakeupGainDefault - compressorMakeupGainMin) / (compressorMakeupGainMax - compressorMakeupGainMin));

    //setParameterValue(inStandardDev, 1.0f);

    setParameterValue(outLeftFollow, 0);
    setParameterValue(outRightFollow, 0);

    dcFilter = StereoDcBlockingFilter::create();
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    blurDownLeft  = DownSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
    blurDownRight = DownSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
    blurUpLeft    = UpSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);
    blurUpRight   = UpSampler::create(getSampleRate(), blurResampleStages, blurResampleFactor);

    blurScratchA = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurLeftA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize);
    blurRightA = GaussianBlur::create(maxTextureSize, maxBlurSize, standardDeviation, blurKernelSize);

    blurLeftA->setBlur(minBlurSize, standardDeviation);
    blurRightA->setBlur(minBlurSize, standardDeviation);

#ifndef FRACTIONAL_TEXTURE_SIZE
    blurScratchB = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurLeftB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviation, blurKernelSize);
    blurRightB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviation, blurKernelSize);
#else
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

    DownSampler::destroy(blurDownLeft);
    DownSampler::destroy(blurDownRight);
    UpSampler::destroy(blurUpLeft);
    UpSampler::destroy(blurUpRight);

    StereoDcBlockingFilter::destroy(dcFilter);
    BiquadFilter::destroy(feedbackFilterLeft);
    BiquadFilter::destroy(feedbackFilterRight);

    FloatArray::destroy(blurScratchA);
    GaussianBlur::destroy(blurLeftA);
    GaussianBlur::destroy(blurRightA);

#ifndef FRACTIONAL_TEXTURE_SIZE
    FloatArray::destroy(blurScratchB);
    GaussianBlur::destroy(blurLeftB);
    GaussianBlur::destroy(blurRightB);
#else
    FloatArray::destroy(textureSizeRamp);
    BlurKernel::destroy(blurKernelStep);
#endif
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      textureSize.toggleSkew();
      if (textureSize.skewEnabled())
      {
        textureSize.resetSkew();
      }
    }

    if (bid == BUTTON_2 && value == ON)
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
    
    textureSize = getParameterValue(inTextureSize);
    blurSize = getParameterValue(inBlurSize);

#ifdef FRACTIONAL_TEXTURE_SIZE
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

    float brightnessParam = getParameterValue(inBlurBrightness);
    float blurBrightness = blurBrightnessDefault;
    if (brightnessParam >= 0.53f)
    {
      blurBrightness = Interpolator::linear(blurBrightnessDefault, blurBrightnessMax, (brightnessParam - 0.53f) * 2.12f);
    }
    else if (brightnessParam <= 0.47f)
    {
      blurBrightness = Interpolator::linear(blurBrightnessDefault, blurBrightnessMin, (0.47f - brightnessParam) * 2.12f);
    }

    feedback          = getParameterValue(inFeedback);

    //standardDeviation = Interpolator::linear(minStandardDev, maxStandardDev, getParameterValue(inStandardDev));

#ifndef FRACTIONAL_TEXTURE_SIZE
    int texLeftA = (int)textureSizeLeft;
    int texLeftB = texLeftA + 1;
    float texLeftBlend = textureSizeLeft - texLeftA;

    int texRightA = (int)textureSizeRight;
    int texRightB = texRightA + 1;
    float texRightBlend = textureSizeRight - texRightA;

    blurLeftA->setBlur(blurSizeLeft, standardDeviation, (1.0f - texLeftBlend));
    blurLeftA->setTextureSize(texLeftA);
    blurLeftB->setBlur(blurSizeLeft, standardDeviation, texLeftBlend);
    blurLeftB->setTextureSize(texLeftB);

    blurRightA->setBlur(blurSizeRight, standardDeviation, (1.0f - texRightBlend));
    blurRightA->setTextureSize(texRightA);
    blurRightB->setBlur(blurSizeRight, standardDeviation, texRightBlend);
    blurRightB->setTextureSize(texRightB);
#endif

    compressionThreshold = Interpolator::linear(0, -80, getParameterValue(inCompressionThreshold));
    blurLeftCompressor.SetThreshold(compressionThreshold);
    blurRightCompressor.SetThreshold(compressionThreshold);

    compressionRatio = Interpolator::linear(compressorRatioMin, compressorRatioMax, getParameterValue(inCompressionRatio));
    blurLeftCompressor.SetRatio(compressionRatio);
    blurRightCompressor.SetRatio(compressionRatio);

    compressionAttack = Interpolator::linear(compressorResponseMin, compressorResponseMax, getParameterValue(inCompressionAttack));
    blurLeftCompressor.SetAttack(compressionAttack);
    blurRightCompressor.SetAttack(compressionAttack);

    compressionRelease = Interpolator::linear(compressorResponseMin, compressorResponseMax, getParameterValue(inCompressionRelease));
    blurLeftCompressor.SetRelease(compressionRelease);
    blurRightCompressor.SetRelease(compressionRelease);

    compressionMakeupGain = Interpolator::linear(compressorMakeupGainMin, compressorMakeupGainMax, getParameterValue(inCompressionMakeupGain));
    blurLeftCompressor.SetMakeup(compressionMakeupGain);
    blurRightCompressor.SetMakeup(compressionMakeupGain);

    dcFilter->process(audio, audio);

    inLeftRms = inLeft.getRms() * blurBrightness;
    inRightRms = inRight.getRms() * blurBrightness;

    // Note: the way feedback is applied is based on how Clouds does it
    float cutoff = (20.0f + 100.0f * feedback * feedback);
    feedbackFilterLeft->setHighPass(cutoff, 1);
    feedbackFilterLeft->process(feedLeft);
    feedbackFilterRight->setHighPass(cutoff, 1);
    feedbackFilterRight->process(feedRight);
    float softLimitCoeff = feedback * 1.4f;
    for (int i = 0; i < blockSize; ++i)
    {
      float left = inLeft[i];
      float right = inRight[i];
      feedLeft[i] = left + feedback * (daisysp::SoftLimit(softLimitCoeff * feedLeft[i] + left) - left);
      feedRight[i] = right + feedback * (daisysp::SoftLimit(softLimitCoeff * feedRight[i] + right) - right);
    }

    // scale down brightness to compensate for over amplification by the resampling filter
    blurBrightness *= 0.375f;

    // left channel blur
    {
      // downsample and copy
      blurDownLeft->process(feedLeft, blurScratchA);

#ifdef FRACTIONAL_TEXTURE_SIZE
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
      blurLeftA->setBlur(blurSizeLeft, standardDeviation);
      blurLeftA->process(blurScratchA, blurScratchA);
#endif
#else
      // process both texture sizes
      blurLeftB->process(blurScratchA, blurScratchB);
      blurLeftA->process(blurScratchA, blurScratchA);

      // mix 
      blurScratchA.add(blurScratchB);
#endif

      // compress
      blurLeftCompressor.ProcessBlock(blurScratchA, blurScratchA, blurScratchA.getSize());

      // upsample to the output
      blurUpLeft->process(blurScratchA, outBlurLeft);
    }

    // right channel blur
    {
      // downsample and copy
      blurDownRight->process(feedRight, blurScratchA);

#ifdef FRACTIONAL_TEXTURE_SIZE
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
      blurRightA->setBlur(blurSizeRight, standardDeviation);
      blurRightA->process(blurScratchA, blurScratchA);
#endif
#else
      // process both texture sizes
      blurRightB->process(blurScratchA, blurScratchB);
      blurRightA->process(blurScratchA, blurScratchA);

      // mix
      blurScratchA.add(blurScratchB);
#endif

      // compress
      blurRightCompressor.ProcessBlock(blurScratchA, blurScratchA, blurScratchA.getSize());

      // upsample to the output
      blurUpRight->process(blurScratchA, outBlurRight);
    }

    outBlurLeft.copyTo(feedLeft);
    outBlurRight.copyTo(feedRight);
    
    // do wet/dry mix with original signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    inLeft.multiply(dry);
    inRight.multiply(dry);
    outBlurLeft.multiply(wet);
    outBlurRight.multiply(wet);
    inLeft.add(outBlurLeft);
    inRight.add(outBlurRight);

    //for (int i = 0; i < blockSize; ++i)
    //{
    //  inLeft[i]  = (inLeft[i] * dry + outBlurLeft[i] * wet);
    //  inRight[i] = (inRight[i] * dry + outBlurRight[i] * wet);
    //}

    setParameterValue(outLeftFollow, inLeftRms);
    setParameterValue(outRightFollow, inRightRms);
    //setParameterValue(outLeftFollow, (textureSizeLeft - minTextureSize) / (maxTextureSize - minTextureSize));
    //setParameterValue(outRightFollow, (textureSizeRight - minTextureSize) / (maxTextureSize - minTextureSize));
    setButton(BUTTON_1, textureSize.skewEnabled());
    setButton(BUTTON_2, blurSize.skewEnabled());

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
  }
};
