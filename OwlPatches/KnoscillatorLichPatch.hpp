
#include "Patch.h"
#include "SineOscillator.h"

class KnoscillatorLichPatch : public Patch 
{
private:
  SineOscillator* osc;

public:
  KnoscillatorLichPatch()
  {
    osc = SineOscillator::create(getSampleRate());
    osc->setFrequency(440);
  }

  ~KnoscillatorLichPatch()
  {
    SineOscillator::destroy(osc);
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray left = audio.getSamples(LEFT_CHANNEL);
    FloatArray right = audio.getSamples(RIGHT_CHANNEL);
    osc->getSamples(left);
    left.copyTo(right);
  }
};
