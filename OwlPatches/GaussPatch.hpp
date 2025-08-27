#pragma once

#include "MonochromeScreenPatch.h"
#include "Gauss.h"

class GaussPatch : public MonochromeScreenPatch
{
  vessl::array<GaussSampleFrame> processArray;
  Gauss gauss;
  
public:
  GaussPatch() : processArray(new GaussSampleFrame[getBlockSize()], getBlockSize()),  gauss(getSampleRate(), getBlockSize())
  {
    registerParameter(PARAMETER_A, gauss.textureSize().getName());
    registerParameter(PARAMETER_B, gauss.blurSize().getName());
    registerParameter(PARAMETER_C, gauss.feedback().getName());
    registerParameter(PARAMETER_D, gauss.gain().getName());
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    gauss.textureSize() << getParameterValue(PARAMETER_A);
    gauss.blurSize() << getParameterValue(PARAMETER_B);
    gauss.feedback() << getParameterValue(PARAMETER_C);
    gauss.gain() << getParameterValue(PARAMETER_D)*12.0f;

    // @todo helper classes that implement source<stereo<float>> and sink<stereo<float>> for AudioBuffer.
    FloatArray inLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray inRight = audio.getSamples(RIGHT_CHANNEL);
    for (int i = 0; i < audio.getSize(); ++i)
    {
      GaussSampleFrame& sample = processArray[i];
      sample.left() = inLeft[i];
      sample.right() = inRight[i];
    }
    
    gauss.process(processArray, processArray);

    for (int i = 0; i < audio.getSize(); ++i)
    {
      GaussSampleFrame& sample = processArray[i];
      inLeft[i] = sample.left();
      inRight[i] = sample.right();
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.clear();
    screen.setCursor(0, 8);
    for (BlurKernelSample& sample : gauss.kernel())
    {
      screen.print("w: ");
      screen.print(sample.weight*100.f);
      screen.print(" o: ");
      screen.print(sample.offset);
      screen.print("\n");
    }
  }
};
