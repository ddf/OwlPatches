#pragma once

#include <cstdint>
#include "OpenWareMidiControl.h"

// a fake enumeration to handle the fact that on Lich PARAMETER_F and PARAMETER_G are reserved as output parameters.
struct InputParameterId
{
  PatchParameterId id;
  static const InputParameterId
    A,  B,  C,  D,  E,  F,  G,  H,
    AA, AB, AC, AD, AE, AF, AG, AH,
    BA, BB, BC, BD, BE, BF, BG, BH,
    CA, CB, CC, CD, CE, CF, CG, CH;

  constexpr InputParameterId next() const
  {
#if defined(OWL_LICH)
    if (id == PARAMETER_E) return {PARAMETER_H};
#endif
    return {static_cast<PatchParameterId>(id+1)};  
  }
  
  operator PatchParameterId() const { return id; }
};

constexpr InputParameterId
  InputParameterId::A {PARAMETER_A},
  InputParameterId::B = A.next(),
  InputParameterId::C = B.next(),
  InputParameterId::D = C.next(),
  InputParameterId::E = D.next(),
  InputParameterId::F = E.next(),
  InputParameterId::G = F.next(),
  InputParameterId::H = G.next(),

  InputParameterId::AA = H.next(),
  InputParameterId::AB = AA.next(),
  InputParameterId::AC = AB.next(),
  InputParameterId::AD = AC.next(),
  InputParameterId::AE = AD.next(),
  InputParameterId::AF = AE.next(),
  InputParameterId::AG = AF.next(),
  InputParameterId::AH = AG.next(),

  InputParameterId::BA = AH.next(),
  InputParameterId::BB = BA.next(),
  InputParameterId::BC = BB.next(),
  InputParameterId::BD = BC.next(),
  InputParameterId::BE = BD.next(),
  InputParameterId::BF = BE.next(),
  InputParameterId::BG = BF.next(),
  InputParameterId::BH = BG.next(),

  InputParameterId::CA = BH.next(),
  InputParameterId::CB = CA.next(),
  InputParameterId::CC = CB.next(),
  InputParameterId::CD = CC.next(),
  InputParameterId::CE = CD.next(),
  InputParameterId::CF = CE.next(),
  InputParameterId::CG = CF.next(),
  InputParameterId::CH = CG.next();

struct OutputParameterId
{
  PatchParameterId id;
  
  static const OutputParameterId A, B;

  operator PatchParameterId() const { return id; }
};

#if defined(OWL_LICH)
constexpr OutputParameterId OutputParameterId::A {PARAMETER_F}, OutputParameterId::B = {PARAMETER_G};
#else
constexpr OutputParameterId OutputParameterId::A {PARAMETER_DA}, OutputParameterId::B = {PARAMETER_DB};
#endif

struct OutputGateId
{
  PatchButtonId id;

  static const OutputGateId A, B;

  operator PatchButtonId() const { return id; }
};

#if defined(OWL_LICH)
constexpr OutputGateId OutputGateId::A {PUSHBUTTON}, OutputGateId::B = {PUSHBUTTON};
#else
constexpr OutputGateId OutputGateId::A {BUTTON_1}, OutputGateId::B = {BUTTON_2};
#endif