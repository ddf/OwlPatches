#pragma once

#include "vessl/vessl.h"
#include "MarkovChain.hpp"

using vessl::unit;
using vessl::unitGenerator;

// T is the value type we can listen to and then subsequently generate
// H is a functor type that can generate an uint32_t hash key from a value of T.
template<typename T, typename H = KeyFunc<T, uint32_t>>
class MarkovGenerator final : public unitGenerator<T>, protected vessl::plist<0>
{
public:
  typedef MarkovChain<T, uint32_t, H> Chain;

private:
  Chain markovChain;
  
public:
  explicit MarkovGenerator(vessl::size_t memorySize) : unitGenerator<T>(), markovChain(memorySize) {}
  
  Chain& chain() { return markovChain; }
  const Chain& chain() const { return markovChain;}
  void learn(const T& value) { markovChain.learn(value); }
  T generate() override { return markovChain.generate(); }
  
  const parameters& getParameters() const override { return *this; }
  
protected:
  vessl::parameter elementAt(vessl::size_t index) const override { return vessl::parameter::none(); }
};
