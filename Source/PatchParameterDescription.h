#pragma once

#include "Patch.h"
#include "PatchParameter.h"
#include "OpenWareMidiControl.h"

enum class InputParameterSkew : uint8_t
{
  Exponential,
  Linear,
  Logarithmic
};

template<typename T>
struct InputParameterDescription
{
  const char* name;
  T minValue = 0;
  T maxValue = 1;
  T defaultValue = 0;
  float lambda = 0;
  float delta = 0;
  InputParameterSkew skew = InputParameterSkew::Linear;

  PatchParameter<T> registerParameter(Patch* withPatch) const;

  float skewToFloat() const
  {
    switch (skew)
    {
      case InputParameterSkew::Exponential: return Patch::EXP;
      case InputParameterSkew::Linear: return Patch::LIN;
      case InputParameterSkew::Logarithmic: return Patch::LOG;
    }

    return 0;
  }
};

// explicit instantiation declarations to suppress clang warnings
template<>
PatchParameter<float>::PatchParameter();

template<>
PatchParameter<float>& PatchParameter<float>::operator=(const PatchParameter<float>& other);  // NOLINT(clang-diagnostic-deprecated-copy-with-user-provided-copy)

template<>
inline PatchParameter<float> InputParameterDescription<float>::registerParameter(Patch* withPatch) const
{
  return withPatch->getFloatParameter(name, minValue, maxValue, defaultValue, lambda, delta, skewToFloat());
}

template<>
PatchParameter<int>::PatchParameter();

template<>
PatchParameter<int>& PatchParameter<int>::operator=(const PatchParameter<int>& other);   // NOLINT(clang-diagnostic-deprecated-copy-with-user-provided-copy)

template<>
inline PatchParameter<int> InputParameterDescription<int>::registerParameter(Patch* withPatch) const
{
  return withPatch->getIntParameter(name, minValue, maxValue, defaultValue, lambda, delta, skewToFloat());
}

typedef InputParameterDescription<float> FloatPatchParameterDescription;
typedef InputParameterDescription<int> IntPatchParameterDescription;

struct OutputParameterDescription
{
  const char* name;
  PatchParameterId pid;
};

class OutputParameter
{
  char name[32];
  PatchParameterId pid;
  Patch* owner;

public:
  OutputParameter(Patch* owningPatch, const OutputParameterDescription& desc) : owner(owningPatch)
  {
    pid = desc.pid;
    const size_t copyLen = min(strlen(desc.name), (sizeof(name)/sizeof(char))-2);
    // ReSharper disable once CppDeprecatedEntity
    strncpy(name, desc.name, copyLen);  // NOLINT(clang-diagnostic-deprecated-declarations)
    name[copyLen] = '>';
    name[copyLen+1] = 0;
    owningPatch->registerParameter(pid, name);
    owningPatch->setParameterValue(pid, 0);
  }

  void setValue(const float value) const
  {
    owner->setParameterValue(pid, value);
  }
};

// when using the gate outs on Genius we can't use F and G as output parameters
// so we define standard output parameter names here based on the module we are building for
#if defined(OWL_GENIUS)
#define OUT_PARAMETER_A PARAMETER_CA
#define OUT_PARAMETER_B PARAMETER_CB
#define OUT_GATE_1 BUTTON_1
#define OUT_GATE_2 BUTTON_2
#elif defined(OWL_LICH)
#define OUT_PARAMETER_A PARAMETER_F
#define OUT_PARAMETER_B PARAMETER_G
#define OUT_GATE_1 PUSHBUTTON
// Lich doesn't have a second output gate, so cause an error
#define OUT_GATE_2 ERROR 
#else
#define OUT_PARAMETER_A PARAMETER_CA
#define OUT_PARAMETER_B PARAMETER_CB
#define OUT_GATE_1 BUTTON_1
#define OUT_GATE_2 BUTTON_2
#endif