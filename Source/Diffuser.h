// Based on the Diffuser from Clouds: https://github.com/pichenettes/eurorack/blob/master/clouds/dsp/fx/diffuser.h
#pragma once

#include "SignalProcessor.h"
#include "AudioBuffer.h"
#include "AllpassNetwork.h"

class Diffuser : public MultiSignalProcessor
{
  static const int kBufferSize = 2048;
  using Apn = AllpassNetwork<float,4>;
  using Array = vessl::array<float>;

  Apn* apl;
  Apn* apr;

  Diffuser(Apn* apl, Apn* apr)
    : apl(apl), apr(apr)
  {
  }

public:
  void setAmount(float amt)
  {
    apl->amount() = amt;
    apr->amount() = amt;
  }

  void process(AudioBuffer& input, AudioBuffer& output) override
  {
    int size = input.getSize();
    Array inL(input.getSamples(0), size);
    Array inR(input.getSamples(1), size);
    Array outL(output.getSamples(0), size);
    Array outR(output.getSamples(1), size);

    inL >> *apl >> outL;
    inR >> *apr >> outR;
  }

  static Diffuser* create()
  {
    static vessl::size_t left_len[4]{ 126, 180, 269, 444 };
    static vessl::size_t right_len[4]{ 151, 205, 245, 405 };
    Apn* apl = Apn::create(left_len, 0.625f);
    Apn* apr = Apn::create(right_len, 0.625f);
    return new Diffuser(apl, apr);
  }

  static void destroy(const Diffuser* diffuser)
  {
    Apn::destroy(diffuser->apl);
    Apn::destroy(diffuser->apr);
    delete diffuser;
  }
};
