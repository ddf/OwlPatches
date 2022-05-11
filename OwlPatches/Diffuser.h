// Based on the Diffuser from Clouds: https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/fx/diffuser.h

#include "SignalProcessor.h"
#include "AudioBuffer.h"
#include "AllpassNetwork.h"

class Diffuser : public MultiSignalProcessor
{
  using Allpass = daisysp::Allpass;
  static const int kBufferSize = 2048;

  AllpassNetwork* apl;
  AllpassNetwork* apr;

  Diffuser(AllpassNetwork* apl, AllpassNetwork* apr)
    : apl(apl), apr(apr)
  {
  }

public:
  void setAmount(float amt)
  {
    apl->setAmount(amt);
    apr->setAmount(amt);
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    int size = input.getSize();
    FloatArray inL = input.getSamples(0);
    FloatArray inR = input.getSamples(1);
    FloatArray outL = output.getSamples(0);
    FloatArray outR = output.getSamples(1);

    apl->process(inL, outL);
    apr->process(inR, outR);
  }

  static Diffuser* create()
  {
    static size_t leftLen[4]{ 126, 180, 269, 444 };
    static size_t rightLen[4]{ 151, 205, 245, 405 };
    AllpassNetwork* apl = AllpassNetwork::create(leftLen, 4, 0.625f);
    AllpassNetwork* apr = AllpassNetwork::create(rightLen, 4, 0.625f);
    return new Diffuser(apl, apr);
  }

  static void destroy(Diffuser* diffuser)
  {
    AllpassNetwork::destroy(diffuser->apl);
    AllpassNetwork::destroy(diffuser->apr);
    delete diffuser;
  }
};
