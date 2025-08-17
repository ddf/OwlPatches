#pragma once

#include "vessl/vessl.h"
#include "MarkovChain.hpp"

using vessl::unit;
using vessl::unitGenerator;

// T is the value type we can listen to and then subsequently generate
// H is a functor type that can generate an size_t hash key from a value of T.
template<typename T, typename H = KeyFunc<T, uint32_t>>
class MarkovGenerator final : public unitGenerator<T>
{
public:
  typedef MarkovChain<T, uint32_t, H> Chain;

private:
  unit::init<0> init = {
    "markov",
    {}
  };
  Chain markovChain;

public:
  explicit MarkovGenerator(float sampleRate, size_t memorySize) : unitGenerator<T>(init, sampleRate), markovChain(memorySize)
  {

  }
  
  Chain& chain() { return markovChain; }
  
  void learn(const T& value)
  {
    markovChain.learn(value);
  }

  T generate() override
  {
    return markovChain.generate();
  }
};
