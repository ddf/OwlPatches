#pragma once

#include "PatchBase.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Evaluator.h"

/** 
 * @todo loading program text from Resources
 * @todo 6 channels for evaluator so it can output triggers and CV.
 * @todo parameters for controlling VCs.
 */ 
class EvaluatorPatch : public PatchBase
{
  using Eval = Evaluator<float, 2>;
  
  Eval evaluator_;

public:
  EvaluatorPatch() : PatchBase()
  , evaluator_(getSampleRate())
  {
    evaluator_.compile("[*] = t*(42&t>>10);", 4096);
    evaluator_.bits() = 8;

    registerParameter(PARAMETER_A, "Bits");
    setParameterValue(PARAMETER_A, (8-4.f)/(32-4.f));
    registerParameter(PARAMETER_B, "Rate");
    setParameterValue(PARAMETER_B, 1.0f);
  }
  
  ~EvaluatorPatch()
  {
    
  }
  
  void processAudio(AudioBuffer &audio) override
  {
    uint8_t bits = static_cast<uint8_t>(vessl::math::lerp(4.f, 32.f, getParameterValue(PARAMETER_A)));
    evaluator_.bits() = bits;
    evaluator_.rate() = getParameterValue(PARAMETER_B);

    AudioBufferReader<2> r(audio);
    AudioBufferWriter<2> w(audio);
    while(r && w)
    {
      w << evaluator_.process(r.read());
    }
    // just cuz this shit tends to be really loud
    audio.multiply(0.5f);
  }
};