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
#include "Interpolator.h"
#include "BlurSignalProcessor.h"
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

  static const PatchParameterId outNoise1 = PARAMETER_F;
  static const PatchParameterId outNoise2 = PARAMETER_G;

  static const int minTextureSize = 32;
  static const int maxTextureSize = 512;

  static const int blurKernelSize = 8;

  // maximum standard deviation was chosen based on the recommendation here:
  // https://dsp.stackexchange.com/questions/10057/gaussian-blur-standard-deviation-radius-and-kernel-size
  // where standard deviation should equal (sampleCount - 1)/4.
  // With 8 samples that is 1.75 and since parameters don't go all the way to 1,
  // we use 0.18f. The low end sounds about the same with smaller radii,
  // it's really only at larger texture sizes combined with larger radii
  // that you start to hear a difference when sweeping the standard deviation,
  // with the maximum value giving the smoothest sounding results.
  const float minStandardDev = 0.06f;
  const float maxStandardDev = 0.18f;

  AudioBuffer* blurBuffer;
  AudioBuffer* feedbackBuffer;

  BlurKernel blurKernelLeft;
  BlurKernel blurKernelRight;

  StereoDcBlockingFilter*     dcFilter;
  BiquadFilter*               feedbackFilterLeft;
  BiquadFilter*               feedbackFilterRight;

  BlurSignalProcessor<AxisX>* blurLeftX;
  BlurSignalProcessor<AxisY>* blurLeftY;
  BlurSignalProcessor<AxisX>* blurRightX;
  BlurSignalProcessor<AxisY>* blurRightY;

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

public:
  BlurPatch() 
    : textureSizeLeft(0.99f, minTextureSize), textureSizeRight(0.99f, minTextureSize)
    , standardDeviation(0.99f, minStandardDev) 
    , standardDeviationLeft(0.75f, minStandardDev), standardDeviationRight(0.75f, minStandardDev)
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inBlurTilt, "Blur Tilt");
    registerParameter(inFeedback, "Feedback");

    registerParameter(inWetDry, "Dry/Wet");
    registerParameter(inStandardDev, "Standard Deviation");

    registerParameter(outNoise1, "Noise 1>");
    registerParameter(outNoise2, "Noise 2>");

    setParameterValue(inTextureSize, 0);
    setParameterValue(inBlurSize,    0.0f);
    setParameterValue(inStandardDev, 1.0f);
    setParameterValue(inBlurTilt, 0.5f);
    setParameterValue(inFeedback, 0.0f);
    setParameterValue(inWetDry, 1);
    setParameterValue(outNoise1, 0);
    setParameterValue(outNoise2, 0);

    dcFilter = StereoDcBlockingFilter::create();
    feedbackFilterLeft = BiquadFilter::create(getSampleRate());
    feedbackFilterRight = BiquadFilter::create(getSampleRate());

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    feedbackBuffer = AudioBuffer::create(2, getBlockSize());

    blurKernelLeft = BlurKernel::create(blurKernelSize);
    blurKernelRight = BlurKernel::create(blurKernelSize);

    blurLeftX = BlurSignalProcessor<AxisX>::create(maxTextureSize, blurKernelLeft);
    blurLeftY = BlurSignalProcessor<AxisY>::create(maxTextureSize, blurKernelLeft);
    blurRightX = BlurSignalProcessor<AxisX>::create(maxTextureSize, blurKernelRight);
    blurRightY = BlurSignalProcessor<AxisY>::create(maxTextureSize, blurKernelRight);
  }

  ~BlurPatch()
  {
    AudioBuffer::destroy(blurBuffer);
    AudioBuffer::destroy(feedbackBuffer);

    BlurKernel::destroy(blurKernelLeft);
    BlurKernel::destroy(blurKernelRight);

    StereoDcBlockingFilter::destroy(dcFilter);
    BiquadFilter::destroy(feedbackFilterLeft);
    BiquadFilter::destroy(feedbackFilterRight);

    BlurSignalProcessor<AxisX>::destroy(blurLeftX);
    BlurSignalProcessor<AxisX>::destroy(blurRightX);
    BlurSignalProcessor<AxisY>::destroy(blurLeftY);
    BlurSignalProcessor<AxisY>::destroy(blurRightY);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray blurLeft = blurBuffer->getSamples(0);
    FloatArray blurRight = blurBuffer->getSamples(1);
    FloatArray feedLeft = feedbackBuffer->getSamples(0);
    FloatArray feedRight = feedbackBuffer->getSamples(1);

    const int blockSize = getBlockSize();

    float textureSizeParam = getParameterValue(inTextureSize);
    float blurSizeParam = getParameterValue(inBlurSize);
    float blurTiltParam = getParameterValue(inBlurTilt);
    float blurTilt = 0;
    if (blurTiltParam >= 0.53f)
    {
      blurTilt = (blurTiltParam - 0.53f) * 1.06f;
    }
    else if (blurTiltParam <= 0.47f)
    {
      blurTilt = (0.47f - blurTiltParam) * -1.06f;
    }

    textureSizeLeft   = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSizeParam + blurTilt, 0.0f, 1.0f));
    textureSizeRight  = Interpolator::linear(minTextureSize, maxTextureSize, std::clamp(textureSizeParam - blurTilt, 0.0f, 1.0f));
    // try scaling max blur down based on the current texture size,
    // such that at the smallest texture size we have a max blur of ~0.33
    float maxBlurL    = 11.0f / textureSizeLeft;
    float maxBlurR    = 11.0f / textureSizeRight;
    blurTilt = 0;
    blurSizeLeft      = Interpolator::linear(0.0f, maxBlurL, std::clamp(blurSizeParam - blurTilt, 0.0f, 1.0f));
    blurSizeRight     = Interpolator::linear(0.0f, maxBlurR, std::clamp(blurSizeParam + blurTilt, 0.0f, 1.0f));
    standardDeviation = Interpolator::linear(minStandardDev, maxStandardDev, getParameterValue(inStandardDev));
    feedback          = getParameterValue(inFeedback);

    standardDeviationLeft  = standardDeviation;
    standardDeviationRight = standardDeviation;

    blurKernelLeft.setGauss(blurSizeLeft, standardDeviationLeft);
    blurKernelRight.setGauss(blurSizeRight, standardDeviationRight);

    dcFilter->process(audio, audio);

    blurLeftX->setKernel(blurKernelLeft);
    blurLeftY->setKernel(blurKernelLeft);
    blurRightX->setKernel(blurKernelRight);
    blurRightY->setKernel(blurKernelRight);

    blurLeftX->setTextureSize(textureSizeLeft);
    blurLeftY->setTextureSize(textureSizeLeft);
    blurRightX->setTextureSize(textureSizeRight);
    blurRightY->setTextureSize(textureSizeRight);

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

    blurLeftX->process(feedLeft, blurLeft);
    blurLeftY->process(blurLeft, blurLeft);
    blurRightX->process(feedRight, blurRight);
    blurRightY->process(blurRight, blurRight);

    // do wet/dry mix with original signal applying makeup gain to the blurred signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    inLeftRms = inLeft.getRms();
    inRightRms = inRight.getRms();
    blurLeftRms = blurLeft.getRms();
    blurRightRms = blurRight.getRms();
    const float rmsThreshold = 0.0001f;
    float leftGain  = (inLeftRms > rmsThreshold && blurLeftRms > rmsThreshold ? inLeftRms / blurLeftRms : 1);
    float rightGain = (inRightRms > rmsThreshold && blurRightRms > rmsThreshold ? inRightRms / blurRightRms : 1);
    blurLeft.multiply(leftGain);
    blurRight.multiply(rightGain);
    blurLeft.copyTo(feedLeft);
    blurRight.copyTo(feedRight);
    for (int i = 0; i < blockSize; ++i)
    {
      inLeft[i]  = (inLeft[i] * dry + blurLeft[i] * wet);
      inRight[i] = (inRight[i] * dry + blurRight[i] * wet);
    }

    setParameterValue(outNoise1, standardDeviationLeft);
    setParameterValue(outNoise2, standardDeviationRight);

    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "texL ");
    debugCpy = stpcpy(debugCpy, msg_itoa(textureSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " bL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " stDevL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationLeft, 10));

    debugCpy = stpcpy(debugCpy, " texR ");
    debugCpy = stpcpy(debugCpy, msg_itoa(textureSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " bR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " stDevR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationRight, 10));
    debugMessage(debugMsg);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
  }

};
