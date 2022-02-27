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

  static const PatchParameterId inStandardDev = PARAMETER_AA;
  static const PatchParameterId inCompressionThreshold = PARAMETER_AB;
  static const PatchParameterId inCompressionRatio = PARAMETER_AC;
  static const PatchParameterId inCompensationSpeed = PARAMETER_AD;

  static const PatchParameterId outLeftFollow = PARAMETER_F;
  static const PatchParameterId outRightFollow = PARAMETER_G;

  static const int blurKernelSize     = 7;
  static const int blurResampleStages = 3;
  static const int blurResampleFactor = 2;

  static const int minTextureSize = 16 / blurResampleFactor;
  static const int maxTextureSize = 256 / blurResampleFactor;
  const  float maxBlurSamples     = 31.0f / blurResampleFactor;
  const  float minBlurSize        = 0.02f;
  const  float maxBlurSize        = 0.5f;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  // The minimum value here sounds about the same with smaller radii,
  // it's really only at larger texture sizes combined with larger radii
  // that you start to hear a difference when sweeping the standard deviation,
  // with the maximum value giving the smoothest sounding results.
  const float maxStandardDev = (blurKernelSize - 1) / 4.0f;
  const float minStandardDev = maxStandardDev / 3.0f;

  const float compressorRatioMin = 1.0f;
  const float compressorRatioMax = 40.0f;
  const float compressorRatioDefault = compressorRatioMin;

  const float compesationSpeedMin = 0.99f;
  const float compensationSpeedMax = 0.1f;
  const float compensationSpeedDefault = compensationSpeedMax;

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
  SmoothFloat standardDeviationLeft;
  SmoothFloat standardDeviationRight;
  SmoothFloat feedback;

  SmoothFloat inLeftRms;
  SmoothFloat inRightRms;
  SmoothFloat blurLeftRms;
  SmoothFloat blurRightRms;
  SmoothFloat blurLeftGain;
  SmoothFloat blurRightGain;
  SmoothFloat compressionRatio;

  Compressor blurLeftCompressor;
  Compressor blurRightCompressor;

public:
  BlurPatch() 
    : textureSize(0), blurSize(0)
    , textureSizeLeft(0.1f, minTextureSize), textureSizeRight(0.1f, minTextureSize)
    , blurSizeLeft(0.1f, 0.0f), blurSizeRight(0.1f, 0.0f)
    , standardDeviation(0.9f, minStandardDev)
    , standardDeviationLeft(0.75f, minStandardDev), standardDeviationRight(0.75f, minStandardDev)
    , blurLeftGain(0.99f, 1), blurRightGain(0.99f, 1), compressionRatio(0.9f, compressorRatioDefault)
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inFeedback, "Feedback");
    registerParameter(inWetDry, "Dry/Wet");
    registerParameter(inStandardDev, "Standard Deviation");
    registerParameter(inCompressionThreshold, "Blur Gain Compensation");
    registerParameter(inCompressionRatio, "Blur Compressor Ratio");
    registerParameter(inCompensationSpeed, "Blur Gain Max Compensation Speed");

    registerParameter(outLeftFollow, "Left Follow>");
    registerParameter(outRightFollow, "Right Follow>");

    setParameterValue(inTextureSize, 0.0f);
    setParameterValue(inBlurSize,    0.0f);
    setParameterValue(inStandardDev, 1.0f);
    setParameterValue(inFeedback, 0.0f);
    setParameterValue(inWetDry, 1);
    setParameterValue(inCompressionThreshold, 0);
    setParameterValue(inCompressionRatio, (compressorRatioDefault - compressorRatioMin) / (compressorRatioMax  - compressorRatioMin));
    setParameterValue(inCompensationSpeed, (compensationSpeedDefault - compesationSpeedMin) / (compensationSpeedMax - compesationSpeedMin));
    setParameterValue(outLeftFollow, 0);
    setParameterValue(outRightFollow, 0);

    dcFilter = StereoDcBlockingFilter::create();
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    blurDownLeft  = DownSampler::create(blurResampleStages, blurResampleFactor);
    blurDownRight = DownSampler::create(blurResampleStages, blurResampleFactor);
    blurUpLeft    = UpSampler::create(blurResampleStages, blurResampleFactor);
    blurUpRight   = UpSampler::create(blurResampleStages, blurResampleFactor);

    blurScratchA = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurLeftA = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviationLeft, blurKernelSize);
    blurRightA = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviationRight, blurKernelSize);

#ifndef FRACTIONAL_TEXTURE_SIZE
    blurScratchB = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurLeftB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviationLeft, blurKernelSize);
    blurRightB = GaussianBlur::create(maxTextureSize, 0.0f, standardDeviationRight, blurKernelSize);
#else
    textureSizeRamp = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurKernelStep = BlurKernel::create(blurKernelSize);
#endif

    blurLeftCompressor.Init(getSampleRate());
    blurRightCompressor.Init(getSampleRate());

    blurLeftCompressor.SetRatio(compressorRatioDefault);
    blurRightCompressor.SetRatio(compressorRatioDefault);
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
    blurSizeLeft      = Interpolator::linear(minBlurSize, maxBlurSize /* maxBlurSamples / textureSizeLeft */, std::clamp(blurSize.getLeft(), 0.0f, 1.0f));
    blurSizeRight     = Interpolator::linear(minBlurSize, maxBlurSize /* maxBlurSamples / textureSizeRight */, std::clamp(blurSize.getRight(), 0.0f, 1.0f));
    standardDeviation = Interpolator::linear(minStandardDev, maxStandardDev, getParameterValue(inStandardDev));
    feedback          = getParameterValue(inFeedback);

    standardDeviationLeft  = standardDeviation;
    standardDeviationRight = standardDeviation;

#ifndef FRACTIONAL_TEXTURE_SIZE
    int texLeftA = (int)textureSizeLeft;
    int texLeftB = texLeftA + 1;
    float texLeftBlend = textureSizeLeft - texLeftA;

    int texRightA = (int)textureSizeRight;
    int texRightB = texRightA + 1;
    float texRightBlend = textureSizeRight - texRightA;

    blurLeftA->setBlur(blurSizeLeft, standardDeviationLeft, (1.0f - texLeftBlend));
    blurLeftA->setTextureSize(texLeftA);
    blurLeftB->setBlur(blurSizeLeft, standardDeviationLeft, texLeftBlend);
    blurLeftB->setTextureSize(texLeftB);

    blurRightA->setBlur(blurSizeRight, standardDeviationRight, (1.0f - texRightBlend));
    blurRightA->setTextureSize(texRightA);
    blurRightB->setBlur(blurSizeRight, standardDeviationRight, texRightBlend);
    blurRightB->setTextureSize(texRightB);
#endif

    const float compressionThreshold = Interpolator::linear(0, -80, getParameterValue(inCompressionThreshold));
    blurLeftCompressor.SetThreshold(compressionThreshold);
    blurRightCompressor.SetThreshold(compressionThreshold);

    compressionRatio = Interpolator::linear(compressorRatioMin, compressorRatioMax, getParameterValue(inCompressionRatio));
    blurLeftCompressor.SetRatio(compressionRatio);
    blurRightCompressor.SetRatio(compressionRatio);

    const float blurGainMax = compressionRatio;
    const float rmsMin = pow10f(-compressorRatioMax / 20.f);
    const float compensationSpeed = Interpolator::linear(compesationSpeedMin, compensationSpeedMax, getParameterValue(inCompensationSpeed));

    dcFilter->process(audio, audio);

    inLeftRms = inLeft.getRms();
    inRightRms = inRight.getRms();

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

    // left channel blur
    {
      // downsample and copy
      blurDownLeft->process(feedLeft, blurScratchA);

#ifdef FRACTIONAL_TEXTURE_SIZE
      textureSizeRamp.ramp(prevTexLeft, textureSizeLeft);
      blurKernelStep.setGauss(blurSizeLeft, standardDeviationLeft);
      blurKernelStep.blurSize = (blurSizeLeft - blurLeftA->getBlurSize()) / blockSize;
      for (int i = 0; i < blurKernelSize; ++i)
      {
        BlurKernelSample to = blurKernelStep[i];
        BlurKernelSample from = blurLeftA->getKernelSample(i);
        blurKernelStep[i] = BlurKernelSample((to.offset - from.offset) / blockSize, (to.weight - from.weight) / blockSize);
      }
      blurLeftA->process(blurScratchA, blurScratchA, textureSizeRamp, blurKernelStep);
#else
      // process both texture sizes
      blurLeftB->process(blurScratchA, blurScratchB);
      blurLeftA->process(blurScratchA, blurScratchA);

      // mix 
      blurScratchA.add(blurScratchB);
#endif

      // compress
      //blurLeftCompressor.ProcessBlock(blurScratchA, blurScratchA, blurScratchA.getSize());

      // attempt to match blur volume to input volume
      blurLeftRms = max(blurScratchA.getRms(), rmsMin);
      float leftGain = min(pow10f(log10f(max(inLeftRms.getValue(), rmsMin)) - log10f(blurLeftRms)), blurGainMax);
      // set reactiveness based on how much the gain changed
      blurLeftGain.lambda = Interpolator::linear(compesationSpeedMin, compensationSpeed, fabsf(blurLeftGain - leftGain) / blurGainMax);
      blurLeftGain = leftGain;
      blurScratchA.multiply(blurLeftGain);

      // upsample to the output
      blurUpLeft->process(blurScratchA, outBlurLeft);
    }

    // right channel blur
    {
      // downsample and copy
      blurDownRight->process(feedRight, blurScratchA);

#ifdef FRACTIONAL_TEXTURE_SIZE
      textureSizeRamp.ramp(prevTexRight, textureSizeRight);
      blurKernelStep.setGauss(blurSizeRight, standardDeviationRight);
      blurKernelStep.blurSize = (blurSizeRight - blurRightA->getBlurSize()) / blockSize;
      for (int i = 0; i < blurKernelSize; ++i)
      {
        BlurKernelSample to = blurKernelStep[i];
        BlurKernelSample from = blurRightA->getKernelSample(i);
        blurKernelStep[i] = BlurKernelSample((to.offset - from.offset) / blockSize, (to.weight - from.weight) / blockSize);
      }
      blurRightA->process(blurScratchA, blurScratchA, textureSizeRamp, blurKernelStep);
#else
      // process both texture sizes
      blurRightB->process(blurScratchA, blurScratchB);
      blurRightA->process(blurScratchA, blurScratchA);

      // mix
      blurScratchA.add(blurScratchB);
#endif

      // compress
      //blurRightCompressor.ProcessBlock(blurScratchA, blurScratchA, blurScratchA.getSize());

      // attempt to match blur volume to input volume
      blurRightRms = max(blurScratchA.getRms(), rmsMin);
      float rightGain = min(pow10f(log10f(max(inRightRms.getValue(), rmsMin)) - log10f(blurRightRms)), blurGainMax);
      blurRightGain.lambda = Interpolator::linear(compesationSpeedMin, compensationSpeed, fabsf(blurRightGain - rightGain) / blurGainMax);
      blurRightGain = rightGain;
      blurScratchA.multiply(blurRightGain);

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

    setParameterValue(outLeftFollow, inLeftRms*4);
    //setParameterValue(outRightFollow, inRightRms*4);
    setParameterValue(outRightFollow, blurLeftGain / blurGainMax);
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

    debugCpy = stpcpy(debugCpy, " cP ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(compensationSpeed, 10));

    debugCpy = stpcpy(debugCpy, " csL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurLeftGain.lambda, 10));
    debugMessage(debugMsg);
  }
};
