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
    a guassian blur to it.

*/

#include "Patch.h"
#include "DcBlockingFilter.h"
#include "Interpolator.h"
#include "BlurSignalProcessor.h"

class BlurPatch : public Patch
{
  static const PatchParameterId inTextureSize = PARAMETER_A;
  static const PatchParameterId inBlurSize    = PARAMETER_B;
  static const PatchParameterId inStandardDev = PARAMETER_C;

  static const PatchParameterId inWetDry = PARAMETER_AA;
  static const PatchParameterId inBlurSamples = PARAMETER_AB;

  static const PatchParameterId outNoise1 = PARAMETER_F;
  static const PatchParameterId outNoise2 = PARAMETER_G;

  static const int maxTextureSize = 512;

  AudioBuffer* blurBuffer;

  StereoDcBlockingFilter*     dcFilter;
  BlurSignalProcessor<AxisX>* blurLeftX;
  BlurSignalProcessor<AxisY>* blurLeftY;
  BlurSignalProcessor<AxisX>* blurRightX;
  BlurSignalProcessor<AxisY>* blurRightY;

  SmoothFloat textureSize;
  SmoothFloat blurSize;
  SmoothFloat standardDeviation;

public:
  BlurPatch()
  {
    registerParameter(inTextureSize, "Texture Size");
    registerParameter(inBlurSize, "Blur Size");
    registerParameter(inStandardDev, "Standard Deviation");
    registerParameter(inBlurSamples, "Blur Samples");

    registerParameter(outNoise1, "Noise 1>");
    registerParameter(outNoise2, "Noise 2>");

    setParameterValue(inTextureSize, 0);
    setParameterValue(inBlurSize,    0.2f);
    setParameterValue(inStandardDev, 0);
    setParameterValue(inBlurSamples, 0);
    setParameterValue(inWetDry, 1);
    setParameterValue(outNoise1, 0);
    setParameterValue(outNoise2, 0);

    blurBuffer = AudioBuffer::create(2, getBlockSize());

    dcFilter = StereoDcBlockingFilter::create();

    blurLeftX = BlurSignalProcessor<AxisX>::create(maxTextureSize);
    blurLeftY = BlurSignalProcessor<AxisY>::create(maxTextureSize);
    blurRightX = BlurSignalProcessor<AxisX>::create(maxTextureSize);
    blurRightY = BlurSignalProcessor<AxisY>::create(maxTextureSize);
  }

  ~BlurPatch()
  {
    AudioBuffer::destroy(blurBuffer);

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

    textureSize = Interpolator::linear(16, maxTextureSize, getParameterValue(inTextureSize));
    blurSize = Interpolator::linear(0.0f, 0.5f, getParameterValue(inBlurSize));
    standardDeviation = Interpolator::linear(0.01f, 0.1f, getParameterValue(inStandardDev));

    dcFilter->process(audio, audio);

    blurLeftX->setTextureSize(textureSize);
    blurLeftX->setSizeAndStandardDeviation(blurSize, standardDeviation);
    blurLeftY->setTextureSize(textureSize);
    blurLeftY->setSizeAndStandardDeviation(blurSize, standardDeviation);
    blurRightX->setTextureSize(textureSize);
    blurRightX->setSizeAndStandardDeviation(blurSize, standardDeviation);
    blurRightY->setTextureSize(textureSize);
    blurRightY->setSizeAndStandardDeviation(blurSize, standardDeviation);

    blurLeftX->process(inLeft, blurLeft);
    blurLeftY->process(blurLeft, blurLeft);
    blurRightX->process(inRight, blurRight);
    blurRightY->process(blurRight, blurRight);

    // do wet/dry mix with original signal
    float wet = getParameterValue(inWetDry);
    float dry = 1.0f - wet;
    for (int i = 0; i < getBlockSize(); ++i)
    {
      inLeft[i] = inLeft[i] * dry + blurLeft[i] * wet;
      inRight[i] = inRight[i] * dry + blurRight[i] * wet;
    }

    //setParameterValue(outNoise1, sampledNoise1);
    //setParameterValue(outNoise2, sampledNoise2);
  }


  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
  }

};
