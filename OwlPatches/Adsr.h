#pragma once

#include "AdsrEnvelope.h"

// extension of OWL ADSR
template<bool LINEAR>
class Adsr final : public AdsrEnvelope<LINEAR>
{
public:
  explicit Adsr(float sr) : AdsrEnvelope<LINEAR>(sr) {}
  
  bool isIdle() const { return AdsrEnvelope<LINEAR>::stage == AdsrEnvelope<LINEAR>::kIdle; }
};

typedef Adsr<true> LinearAdsr;
typedef Adsr<false> ExponentialAdsr;