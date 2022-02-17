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
#include "basicmaths.h"
#include "custom_dsp.h" // for SoftLimit
#include <string.h>

class BlurPatch : public Patch
{
  static const PatchParameterId inTextureSize = PARAMETER_A;
  static const PatchParameterId inBlurSize    = PARAMETER_B;
  static const PatchParameterId inBlurTilt    = PARAMETER_C;
  static const PatchParameterId inFeedback    = PARAMETER_D;

  static const PatchParameterId inWetDry      = PARAMETER_AA;
  static const PatchParameterId inStandardDev = PARAMETER_AB;

  static const PatchParameterId outLeftFollow = PARAMETER_F;
  static const PatchParameterId outRightFollow = PARAMETER_G;

  static const int blurKernelSize = 7;
  static const int blurResampleStages = 3;
  static const int blurResampleFactor = 2;

  static const int minTextureSize = 32 / blurResampleFactor;
  static const int maxTextureSize = 512 / blurResampleFactor;
  const float maxBlurSamples = 11.0f / blurResampleFactor;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  // With 8 samples that is 1.75 and since parameters don't go all the way to 1,
  // we use 0.18f. The low end sounds about the same with smaller radii,
  // it's really only at larger texture sizes combined with larger radii
  // that you start to hear a difference when sweeping the standard deviation,
  // with the maximum value giving the smoothest sounding results.
  const float maxStandardDev = (blurKernelSize - 1) / 4.0f;
  const float minStandardDev = maxStandardDev / 3.0f;

  AudioBuffer* blurBuffer;
  AudioBuffer* feedbackBuffer;

  StereoDcBlockingFilter*     dcFilter;
  BiquadFilter*               feedbackFilterLeft;
  BiquadFilter*               feedbackFilterRight;

  FloatArray   blurScratchA;
  FloatArray   blurScratchB;
  DownSampler* blurDownLeft;
  DownSampler* blurDownRight;
  UpSampler*   blurUpLeft;
  UpSampler*   blurUpRight;

  GaussianBlurSignalProcessor* blurLeftA;
  GaussianBlurSignalProcessor* blurLeftB;
  GaussianBlurSignalProcessor* blurRightA;
  GaussianBlurSignalProcessor* blurRightB;

  bool textureSizeTiltLocked;
  bool blurSizeTiltLocked;

  SmoothFloat textureSizeLeft;
  SmoothFloat textureSizeRight;
  SmoothFloat textureSizeTilt;
  SmoothFloat blurSizeLeft;
  SmoothFloat blurSizeRight;
  SmoothFloat blurSizeTilt;
  SmoothFloat standardDeviation;
  SmoothFloat standardDeviationLeft;
  SmoothFloat standardDeviationRight;
  SmoothFloat feedback;

  SmoothFloat inLeftRms;
  SmoothFloat inRightRms;
  SmoothFloat blurLeftRms;
  SmoothFloat blurRightRms;

public:
  BlurPatch() 
    : textureSizeLeft(0.99f, minTextureSize), textureSizeRight(0.99f, minTextureSize)
    , textureSizeTiltLocked(false), blurSizeTiltLocked(false)
    , standardDeviation(0.99f, minStandardDev) 
    , standardDeviationLeft(0.75f, minStandardDev), standardDeviationRight(0.75f, minStandardDev)
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inBlurTilt, "Blur Tilt");
    registerParameter(inFeedback, "Feedback");

    registerParameter(inWetDry, "Dry/Wet");
    registerParameter(inStandardDev, "Standard Deviation");

    registerParameter(outLeftFollow, "Left Follow>");
    registerParameter(outRightFollow, "Right Follow>");

    setParameterValue(inTextureSize, 0);
    setParameterValue(inBlurSize,    0.0f);
    setParameterValue(inStandardDev, 1.0f);
    setParameterValue(inBlurTilt, 0.5f);
    setParameterValue(inFeedback, 0.0f);
    setParameterValue(inWetDry, 1);
    setParameterValue(outLeftFollow, 0);
    setParameterValue(outRightFollow, 0);

    dcFilter = StereoDcBlockingFilter::create();
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    blurScratchA  = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurScratchB = FloatArray::create(getBlockSize() / blurResampleFactor);
    blurDownLeft  = DownSampler::create(blurResampleStages, blurResampleFactor);
    blurDownRight = DownSampler::create(blurResampleStages, blurResampleFactor);
    blurUpLeft    = UpSampler::create(blurResampleStages, blurResampleFactor);
    blurUpRight   = UpSampler::create(blurResampleStages, blurResampleFactor);

    blurLeftA = GaussianBlurSignalProcessor::create(maxTextureSize, 0.0f, standardDeviationLeft, blurKernelSize);
    blurLeftB = GaussianBlurSignalProcessor::create(maxTextureSize, 0.0f, standardDeviationLeft, blurKernelSize);
    blurRightA = GaussianBlurSignalProcessor::create(maxTextureSize, 0.0f, standardDeviationRight, blurKernelSize);
    blurRightB = GaussianBlurSignalProcessor::create(maxTextureSize, 0.0f, standardDeviationRight, blurKernelSize);
  }

  ~BlurPatch()
  {
    AudioBuffer::destroy(blurBuffer);
    AudioBuffer::destroy(feedbackBuffer);

    FloatArray::destroy(blurScratchA);
    FloatArray::destroy(blurScratchB);
    DownSampler::destroy(blurDownLeft);
    DownSampler::destroy(blurDownRight);
    UpSampler::destroy(blurUpLeft);
    UpSampler::destroy(blurUpRight);

    StereoDcBlockingFilter::destroy(dcFilter);
    BiquadFilter::destroy(feedbackFilterLeft);
    BiquadFilter::destroy(feedbackFilterRight);

    GaussianBlurSignalProcessor::destroy(blurLeftA);
    GaussianBlurSignalProcessor::destroy(blurLeftB);
    GaussianBlurSignalProcessor::destroy(blurRightA);
    GaussianBlurSignalProcessor::destroy(blurRightB);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == BUTTON_1 && value == ON)
    {
      textureSizeTiltLocked = !textureSizeTiltLocked;
    }

    if (bid == BUTTON_2 && value == ON)
    {
      blurSizeTiltLocked = !blurSizeTiltLocked;
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

    float textureSizeParam = getParameterValue(inTextureSize);
    float blurSizeParam = getParameterValue(inBlurSize);
    float blurTiltParam = getParameterValue(inBlurTilt);
    float tilt = 0;
    if (blurTiltParam >= 0.53f)
    {
      tilt = (blurTiltParam - 0.53f) * 1.06f;
    }
    else if (blurTiltParam <= 0.47f)
    {
      tilt = (0.47f - blurTiltParam) * -1.06f;
    }

    if (!textureSizeTiltLocked)
    {
      textureSizeTilt = tilt;
    }
    textureSizeLeft   = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSizeParam + textureSizeTilt, 0.0f, 1.0f));
    textureSizeRight  = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSizeParam - textureSizeTilt, 0.0f, 1.0f));
    // scale max blur down so we never blur more than a maximum number of samples away
    float maxBlurL    = maxBlurSamples / textureSizeLeft;
    float maxBlurR    = maxBlurSamples / textureSizeRight;
    if (!blurSizeTiltLocked)
    {
      blurSizeTilt = tilt;
    }
    blurSizeLeft      = Interpolator::linear(0.0f, maxBlurL, std::clamp(blurSizeParam - blurSizeTilt, 0.0f, 1.0f));
    blurSizeRight     = Interpolator::linear(0.0f, maxBlurR, std::clamp(blurSizeParam + blurSizeTilt, 0.0f, 1.0f));
    standardDeviation = Interpolator::linear(minStandardDev, maxStandardDev, getParameterValue(inStandardDev));
    feedback          = getParameterValue(inFeedback);

    standardDeviationLeft  = standardDeviation;
    standardDeviationRight = standardDeviation;

    int texLeftA = (int)textureSizeLeft;
    int texLeftB = texLeftA + 1;
    float texLeftBlend = textureSizeLeft - texLeftA;

    int texRightA = (int)textureSizeRight;
    int texRightB = texRightA + 1;
    float texRightBlend = textureSizeRight - texRightA;

    blurLeftA->setBlur(blurSizeLeft, standardDeviationLeft);
    blurLeftA->setTextureSize(texLeftA);
    blurLeftB->setBlur(blurSizeLeft, standardDeviationLeft);
    blurLeftB->setTextureSize(texLeftB);

    blurRightA->setBlur(blurSizeRight, standardDeviationRight);
    blurRightA->setTextureSize(texRightA);
    blurRightB->setBlur(blurSizeRight, standardDeviationRight);
    blurRightB->setTextureSize(texRightB);

    dcFilter->process(audio, audio);

    feedLeft.multiply(feedback);
    feedRight.multiply(feedback);

    feedLeft.add(inLeft);
    feedRight.add(inRight);

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

    // downsample and copy
    blurDownLeft->process(feedLeft, blurScratchA);
    blurScratchA.copyTo(blurScratchB);

    // process both texture sizes
    blurLeftA->process(blurScratchA, blurScratchA);
    blurLeftB->process(blurScratchB, blurScratchB);

    // mix based on the blend
    blurScratchA.multiply(1.0f - texLeftBlend);
    blurScratchB.multiply(texLeftBlend);
    blurScratchA.add(blurScratchB);

    // upsample to the output
    blurUpLeft->process(blurScratchA, outBlurLeft);

    // downsample and copy
    blurDownRight->process(feedRight, blurScratchA);
    blurScratchA.copyTo(blurScratchB);

    // process both texture sizes
    blurRightA->process(blurScratchA, blurScratchA);
    blurRightB->process(blurScratchB, blurScratchB);

    // mix based on the blend
    blurScratchA.multiply(1.0f - texRightBlend);
    blurScratchB.multiply(texRightBlend);
    blurScratchA.add(blurScratchB);

    // upsample to the output
    blurUpRight->process(blurScratchA, outBlurRight);

    // do wet/dry mix with original signal applying makeup gain to the blurred signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    inLeftRms = inLeft.getRms();
    inRightRms = inRight.getRms();
    blurLeftRms = outBlurLeft.getRms();
    blurRightRms = outBlurRight.getRms();
    const float rmsThreshold = 0.0001f;
    float leftGain  = (inLeftRms > rmsThreshold && blurLeftRms > rmsThreshold ? inLeftRms / blurLeftRms : 1);
    float rightGain = (inRightRms > rmsThreshold && blurRightRms > rmsThreshold ? inRightRms / blurRightRms : 1);
    outBlurLeft.multiply(leftGain);
    outBlurRight.multiply(rightGain);
    outBlurLeft.copyTo(feedLeft);
    outBlurRight.copyTo(feedRight);
    for (int i = 0; i < blockSize; ++i)
    {
      inLeft[i]  = (inLeft[i] * dry + outBlurLeft[i] * wet);
      inRight[i] = (inRight[i] * dry + outBlurRight[i] * wet);
    }

    setParameterValue(outLeftFollow, inLeftRms*4);
    setParameterValue(outRightFollow, inRightRms*4);
    setButton(BUTTON_1, textureSizeTiltLocked);
    setButton(BUTTON_2, blurSizeTiltLocked);

    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "texL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(textureSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " bL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " stDevL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationLeft, 10));

    debugCpy = stpcpy(debugCpy, " texR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(textureSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " bR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " stDevR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationRight, 10));
    debugMessage(debugMsg);
  }

};
