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
#include "Interpolator.h"
#include "BlurSignalProcessor.h"
#include "basicmaths.h"
#include <string.h>

class BlurPatch : public Patch
{
  static const PatchParameterId inTextureSize = PARAMETER_A;
  static const PatchParameterId inBlurSize    = PARAMETER_B;
  static const PatchParameterId inStandardDev = PARAMETER_C;
  static const PatchParameterId inBlurTilt    = PARAMETER_D;

  static const PatchParameterId inWetDry = PARAMETER_AA;
  static const PatchParameterId inBlurSamples = PARAMETER_AB;

  static const PatchParameterId outNoise1 = PARAMETER_F;
  static const PatchParameterId outNoise2 = PARAMETER_G;

  static const int minTextureSize = 32;
  static const int maxTextureSize = 512;

  const float minStandardDev = 0.05f;
  const float maxStandardDev = 0.18f;

  AudioBuffer* blurBuffer;
  BlurKernel blurKernelLeft;
  BlurKernel blurKernelRight;

  StereoDcBlockingFilter*     dcFilter;
  BlurSignalProcessor<AxisX>* blurLeftX;
  BlurSignalProcessor<AxisY>* blurLeftY;
  BlurSignalProcessor<AxisX>* blurRightX;
  BlurSignalProcessor<AxisY>* blurRightY;

  SmoothFloat textureSize;
  SmoothFloat blurSizeLeft;
  SmoothFloat blurSizeRight;
  SmoothFloat standardDeviation;
  SmoothFloat standardDeviationLeft;
  SmoothFloat standardDeviationRight;

  SmoothFloat inLeftRms;
  SmoothFloat inRightRms;
  SmoothFloat blurLeftRms;
  SmoothFloat blurRightRms;

public:
  BlurPatch() 
    : textureSize(0.99f, minTextureSize), standardDeviation(0.99f, minStandardDev)
    , standardDeviationLeft(0.75f, minStandardDev), standardDeviationRight(0.75f, minStandardDev)
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inStandardDev, "Standard Deviation");
    registerParameter(inBlurSamples, "Blur Samples");
    registerParameter(inBlurTilt, "Blur Tilt");

    registerParameter(outNoise1, "Noise 1>");
    registerParameter(outNoise2, "Noise 2>");

    setParameterValue(inTextureSize, 0);
    setParameterValue(inBlurSize,    0.2f);
    setParameterValue(inStandardDev, 0.0f);
    setParameterValue(inBlurSamples, 0);
    setParameterValue(inBlurTilt, 0.5f);
    setParameterValue(inWetDry, 1);
    setParameterValue(outNoise1, 0);
    setParameterValue(outNoise2, 0);

    dcFilter = StereoDcBlockingFilter::create();

    blurBuffer = AudioBuffer::create(2, getBlockSize());
    blurKernelLeft = BlurKernel::create(8);
    blurKernelRight = BlurKernel::create(8);

    blurLeftX = BlurSignalProcessor<AxisX>::create(maxTextureSize, blurKernelLeft);
    blurLeftY = BlurSignalProcessor<AxisY>::create(maxTextureSize, blurKernelLeft);
    blurRightX = BlurSignalProcessor<AxisX>::create(maxTextureSize, blurKernelRight);
    blurRightY = BlurSignalProcessor<AxisY>::create(maxTextureSize, blurKernelRight);
  }

  ~BlurPatch()
  {
    AudioBuffer::destroy(blurBuffer);
    BlurKernel::destroy(blurKernelLeft);
    BlurKernel::destroy(blurKernelRight);

    StereoDcBlockingFilter::destroy(dcFilter);

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
    const int blockSize = getBlockSize();

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

    textureSize       = Interpolator::linear(minTextureSize, maxTextureSize, getParameterValue(inTextureSize));
    standardDeviation = Interpolator::linear(minStandardDev, maxStandardDev, getParameterValue(inStandardDev));
    blurSizeRight     = Interpolator::linear(0.0f, 0.33f, std::clamp(blurSizeParam + blurTilt, 0.0f, 1.0f));
    blurSizeLeft      = Interpolator::linear(0.0f, 0.33f, std::clamp(blurSizeParam - blurTilt, 0.0f, 1.0f));

    if (isButtonPressed(BUTTON_1))
    {
      standardDeviationLeft = standardDeviation + inLeft.getStandardDeviation();
      standardDeviationRight = standardDeviation + inRight.getStandardDeviation();
    }
    else
    {
      standardDeviationLeft  = standardDeviation;
      standardDeviationRight = standardDeviation;
    }
    blurKernelLeft.setGauss(blurSizeLeft, standardDeviationLeft);
    blurKernelRight.setGauss(blurSizeRight, standardDeviationRight);

    dcFilter->process(audio, audio);

    blurLeftX->setKernel(blurKernelLeft);
    blurLeftY->setKernel(blurKernelLeft);
    blurRightX->setKernel(blurKernelRight);
    blurRightY->setKernel(blurKernelRight);

    int texSize = int(textureSize);
    blurLeftX->setTextureSize(texSize);
    blurLeftY->setTextureSize(texSize);
    blurRightX->setTextureSize(texSize);
    blurRightY->setTextureSize(texSize);

    for (int i = 0; i < blockSize; ++i)
    {
      blurLeft[i] = blurLeftY->process(blurLeftX->process(inLeft[i]));
      blurRight[i] = blurRightY->process(blurRightX->process(inRight[i]));
    }

    // do wet/dry mix with original signal applying makeup gain to the blurred signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    inLeftRms = inLeft.getRms();
    inRightRms = inRight.getRms();
    blurLeftRms = blurLeft.getRms();
    blurRightRms = blurRight.getRms();
    float leftGain = (blurLeftRms > 0.0f ? inLeftRms / blurLeftRms : 1) * wet;
    float rightGain = (blurRightRms > 0.0f ? inRightRms / blurRightRms : 1) * wet;
    for (int i = 0; i < blockSize; ++i)
    {
      inLeft[i]  = (inLeft[i] * dry + blurLeft[i] * leftGain);
      inRight[i] = (inRight[i] * dry + blurRight[i] * rightGain);
    }

    setParameterValue(outNoise1, standardDeviationLeft);
    setParameterValue(outNoise2, standardDeviationRight);

    char debugMsg[64];
    char* debugCpy = stpcpy(debugMsg, "tex ");
    debugCpy = stpcpy(debugCpy, msg_itoa(textureSize, 10));
    debugCpy = stpcpy(debugCpy, " bL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeLeft, 10));
    debugCpy = stpcpy(debugCpy, " bR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(blurSizeRight, 10));
    debugCpy = stpcpy(debugCpy, " stDevL ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationLeft, 10));
    debugCpy = stpcpy(debugCpy, " stDevR ");
    debugCpy = stpcpy(debugCpy, msg_ftoa(standardDeviationRight, 10));
    debugMessage(debugMsg);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
  }

};
