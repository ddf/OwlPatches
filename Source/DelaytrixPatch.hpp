// ReSharper disable CppClangTidyClangDiagnosticSwitch
#pragma once

#include "MonochromeScreenPatch.h"
#include "DelayMatrix.h"
#include "Noise.hpp"

// for building param names
#include <cstring>

#ifndef ARM_CORTEX
inline char * stpcpy(char *s1, const char *s2) { return strcat(s1, s2); }  // NOLINT(clang-diagnostic-deprecated-declarations)
#endif

enum : uint8_t
{
  DELAY_COUNT = 4
};

struct DelaytrixParamIds
{
  PatchParameterId time;
  PatchParameterId spread;
  PatchParameterId feedback;
  PatchParameterId dryWet;
  PatchParameterId skew;
  PatchParameterId lfoOut;
  PatchParameterId rndOut;
  PatchParameterId modIndex;
};

class DelaytrixPatch : public MonochromeScreenPatch
{
  struct DelayLineParamIds
  {
    PatchParameterId input;    // amount of input fed into the delay
    PatchParameterId cutoff;   // cutoff for the filter
    PatchParameterId feedback[DELAY_COUNT];     // amount of wet signal sent to other delays
  };
  
  using Delaytrix = DelayMatrix<DELAY_COUNT>;
  using FreezeState = Delaytrix::FreezeState;
  using Tap = Delaytrix::TapDelayLength;
  
  DelaytrixParamIds patchParams;
  DelayLineParamIds delayParams[DELAY_COUNT];
  
  Delaytrix delayMatrix;
  float dryWetAnim = 0;

public:
  DelaytrixPatch() 
  : patchParams({ PARAMETER_A, PARAMETER_C, PARAMETER_B, PARAMETER_D, PARAMETER_E, PARAMETER_F, PARAMETER_G, PARAMETER_H })
  , delayMatrix(getSampleRate(), getBlockSize())
  {
    registerParameter(patchParams.time, "Time");
    registerParameter(patchParams.feedback, "Feedback");
    registerParameter(patchParams.spread, "Spread");
    registerParameter(patchParams.skew, "Skew");
    registerParameter(patchParams.dryWet, "Dry/Wet");
    registerParameter(patchParams.lfoOut, "LFO>");
    registerParameter(patchParams.rndOut, "RND>");
    registerParameter(patchParams.modIndex, "Mod");
    // 0.5f is "off" because turning left sends smooth noise to delay time, and turning right sends sine lfo
    setParameterValue(patchParams.modIndex, 0.5f);
    
    char pname[16];
    for (int i = 0; i < DELAY_COUNT; ++i)
    {
      DelayLineParamIds& params = delayParams[i];
      params.input = static_cast<PatchParameterId>(PARAMETER_AA + i);
      char* p = stpcpy(pname, "Gain ");
      stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.input, pname);
      setParameterValue(params.input, 0.99f);

      params.cutoff = static_cast<PatchParameterId>(PARAMETER_AE + i);
      p = stpcpy(pname, "Color ");
      stpcpy(p, msg_itoa(i + 1, 10));
      registerParameter(params.cutoff, pname);
      setParameterValue(params.cutoff, 0.99f);

      for (int f = 0; f < DELAY_COUNT; ++f)
      {
        params.feedback[f] = static_cast<PatchParameterId>(PARAMETER_BA + f * 4 + i);
        p = stpcpy(pname, "Fdbk ");
        p = stpcpy(p, msg_itoa(f + 1, 10));
        p = stpcpy(p, "->");
        stpcpy(p, msg_itoa(i + 1, 10));
        registerParameter(params.feedback[f], pname);
        // initialize the matrix so it sounds like 3 delays in parallel
        // when the global feedback param is turned up
        if (i == f) { setParameterValue(params.feedback[f], 0.99f); }
        else { setParameterValue(params.feedback[f], 0.5f); }
      }
    }
  }
  
  void processAudio(AudioBuffer& audio) override
  {
    delayMatrix.time() = getParameterValue(patchParams.time);
    delayMatrix.spread() = getParameterValue(patchParams.spread);
    delayMatrix.feedback() = getParameterValue(patchParams.feedback);
    delayMatrix.dryWet() = getParameterValue(patchParams.dryWet);
    delayMatrix.skew() = getParameterValue(patchParams.skew);
    delayMatrix.mod() = getParameterValue(patchParams.modIndex);
    
    for (int i = 0; i < DELAY_COUNT; ++i)
    {
      auto& dlp = delayParams[i];
      auto& delay = delayMatrix.delay(i);
      delay.input.value = getParameterValue(dlp.input);
      delay.cutoff.value = getParameterValue(dlp.cutoff);
      for (int f = 0; f < DELAY_COUNT; ++f)
      {
        delay.feedback[f].value = getParameterValue(dlp.feedback[f]);
      }
    }
    
    Array audioLeft(audio.getSamples(LEFT_CHANNEL), audio.getSize());
    Array audioRight(audio.getSamples(RIGHT_CHANNEL), audio.getSize());
    delayMatrix.processStereo(audioLeft, audioRight);
    
    float lfoGen = delayMatrix.lfo().readAnalog();
    float rndGen = delayMatrix.rnd().readAnalog();
    setParameterValue(patchParams.lfoOut, vessl::math::constrain(lfoGen*0.5f + 0.5f, 0.f, 1.f));
    setParameterValue(patchParams.rndOut, vessl::math::constrain(rndGen, 0.f, 1.f));
    
    setButton(PUSHBUTTON, delayMatrix.gate().readBinary());
    
    FreezeState freezeState = delayMatrix.freeze().read<FreezeState>();
    setButton(BUTTON_2, freezeState == FreezeState::FreezeOn ? 1 : 0);
    // this is the second gate output on the Witch
    setButton(BUTTON_6, freezeState == FreezeState::FreezeOn ? 1 : 0);
  }
  
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override  // NOLINT(portability-template-virtual-member-function)
  {
    if (bid == BUTTON_1)
    {
      if (value == Patch::ON)
      {
        delayMatrix.tap(samples);
      }
    }
    
    if (bid == BUTTON_2 && value == Patch::ON)
    {
      delayMatrix.toggleFreeze();
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override 
  {
    screen.clear();

    constexpr uint16_t matrixTop = 17;
    constexpr uint16_t knobRadius = 4;
    uint16_t x = 0;

    constexpr uint16_t headingY = matrixTop - knobRadius * 2 - 1;
    screen.setCursor(x, headingY);
    if (delayMatrix.isClocked())
    {
      screen.print("Q=");
      screen.print(static_cast<int>(delayMatrix.getBpm()));
    }
    else
    {
      screen.print("TIME");
    }
    x += 39;
    screen.setCursor(x, headingY);
    screen.print("IN");
    x += 14;
    screen.setCursor(x, headingY);
    screen.print("LP");
    x += 14;

    const Delaytrix::DelayLineData& lastData = delayMatrix.getDelayData(DELAY_COUNT - 1);
    FreezeState freezeState = delayMatrix.freeze().read<FreezeState>();
    float lastTime = lastData.time.value;
    const float lastMaxFreezePosition = min(lastTime * 8 - lastTime - lastData.skew, static_cast<float>(lastData.delayLength) - lastTime - lastData.skew);
    const float maxFreezeSize = lastMaxFreezePosition + lastTime + lastData.skew;
    if (freezeState == FreezeState::FreezeOn)
    {
      screen.setCursor(x, headingY);
      screen.print("/");
      screen.print(ftoa(maxFreezeSize / getSampleRate(), 10));
      screen.print("s\\");
    }

    for (uint16_t i = 0; i < DELAY_COUNT; ++i)
    {
      constexpr uint16_t rowSpacing = 12;
      const Delaytrix::DelayLineData& data = delayMatrix.getDelayData(i);
      uint16_t rowY = matrixTop + rowSpacing * i;
      uint16_t knobY = rowY - knobRadius - 1;
      x = 1;
      if (delayMatrix.isClocked())
      {
        int clockMult = delayMatrix.clockMult();
        int spreadDivMult = delayMatrix.spreadMult();
        int tapFirst = Tap::Quarter / clockMult;
        int spreadInc = spreadDivMult < 0 ? tapFirst / -spreadDivMult : tapFirst * spreadDivMult;
        Tap tap = static_cast<Tap>(tapFirst + spreadInc * i);
        screen.setCursor(x, rowY);
        switch (tap)  // NOLINT(clang-diagnostic-switch-enum)
        {
          // for easy debug
          case 0: screen.print(ftoa(data.time.value / getSampleRate(), 10)); break;
#define QUAV ""
#define DOT2 "."
#define DOT4 ","
#define DOT8 ";"
          case Tap::Whole: screen.print("W"); break;
          case Tap::Half: screen.print("H"); break;
          case Tap::Quarter: screen.print("Q"); break;
          case Tap::One8: screen.print(QUAV "8"); break;
          case Tap::One16: screen.print(QUAV "16"); break;
          case Tap::One32: screen.print(QUAV "32"); break;
          case Tap::One64: screen.print(QUAV "64"); break;
          case Tap::One128: screen.print(QUAV "128"); break;
          case Tap::One256: screen.print(QUAV "256"); break;
          case Tap::One512: screen.print(QUAV "512"); break;

          case Tap::WholeT: screen.print("WT"); break;
          case Tap::HalfT: screen.print("HT"); break;
          case Tap::QuarterT: screen.print("QT"); break;
          case Tap::One8T: screen.print(QUAV "8T"); break;
          case Tap::One16T: screen.print(QUAV "16T"); break;
          case Tap::One32T: screen.print(QUAV "32T"); break;
          case Tap::One64T: screen.print(QUAV "64T"); break;
          case Tap::One128T: screen.print(QUAV "128T"); break;
          case Tap::One256T: screen.print(QUAV "256T"); break;
          case Tap::One512T: screen.print(QUAV "512T"); break;
          case Tap::One1028T: screen.print(QUAV "1028T"); break;

          case Tap::WholeTT: screen.print("WTT"); break;
          case Tap::HalfTT: screen.print("HTT"); break;
          case Tap::QuarterTT: screen.print("QTT"); break;
          case Tap::One8TT: screen.print(QUAV "8TT"); break;
          case Tap::One16TT: screen.print(QUAV "16TT"); break;
          case Tap::One32TT: screen.print(QUAV "32TT"); break;
          case Tap::One64TT: screen.print(QUAV "64TT"); break;
          case Tap::One128TT: screen.print(QUAV "128TT"); break;
          case Tap::One256TT: screen.print(QUAV "256TT"); break;
          case Tap::One512TT: screen.print(QUAV "512TT"); break;
          case Tap::One1028TT: screen.print(QUAV "1028TT"); break;

          case Tap::Whole     + Tap::One8: screen.print("W" DOT8); break;
          case Tap::Half      + Tap::One16: screen.print("H" DOT8); break;
          case Tap::Quarter   + Tap::One32: screen.print("Q" DOT8); break;
          case Tap::One8      + Tap::One64: screen.print(QUAV "8" DOT8); break;
          case Tap::One16     + Tap::One128: screen.print(QUAV "16" DOT8); break;
          case Tap::One32     + Tap::One256: screen.print(QUAV "32" DOT8); break;
          case Tap::One64     + Tap::One512: screen.print(QUAV "64" DOT8); break;
          case Tap::One128    + Tap::One1028: screen.print(QUAV "128" DOT8); break;

          case Tap::Whole     + Tap::Quarter: screen.print("W" DOT4); break;
          case Tap::Half      + Tap::One8: screen.print("H" DOT4); break;
          case Tap::Quarter   + Tap::One16: screen.print("Q" DOT4); break;
          case Tap::One8      + Tap::One32: screen.print(QUAV "8" DOT4); break;
          case Tap::One16     + Tap::One64: screen.print(QUAV "16" DOT4); break;
          case Tap::One32     + Tap::One128: screen.print(QUAV "32" DOT4); break;
          case Tap::One64     + Tap::One256: screen.print(QUAV "64" DOT4); break;
          case Tap::One128    + Tap::One512: screen.print(QUAV "128" DOT4); break;

          case Tap::Whole     + Tap::Quarter  + Tap::One16: screen.print("W" DOT4 DOT4); break;
          case Tap::Half      + Tap::One8     + Tap::One32: screen.print("H" DOT4 DOT4); break;
          case Tap::Quarter   + Tap::One16    + Tap::One64: screen.print("Q" DOT4 DOT4); break;
          case Tap::One8      + Tap::One32    + Tap::One128: screen.print(QUAV "8" DOT4 DOT4); break;
          case Tap::One16     + Tap::One64    + Tap::One256: screen.print(QUAV "16" DOT4 DOT4); break;
          case Tap::One32     + Tap::One128   + Tap::One512: screen.print(QUAV "32" DOT4 DOT4); break;
          case Tap::One64     + Tap::One256   + Tap::One1028: screen.print(QUAV "64" DOT4 DOT4); break;

          case Tap::WholeT    + Tap::QuarterT: screen.print("WT" DOT4); break;
          case Tap::HalfT     + Tap::One8T: screen.print("HT" DOT4); break;
          case Tap::QuarterT  + Tap::One16T: screen.print("QT" DOT4); break;
          case Tap::One8T     + Tap::One32T: screen.print(QUAV "8T" DOT4); break;
          case Tap::One16T    + Tap::One64T: screen.print(QUAV "16T" DOT4); break;
          case Tap::One32T    + Tap::One128T: screen.print(QUAV "32T" DOT4); break;
          case Tap::One64T    + Tap::One256T: screen.print(QUAV "64T" DOT4); break;
          case Tap::One128T   + Tap::One512T: screen.print(QUAV "128T" DOT4); break;

          case Tap::WholeTT   + Tap::QuarterTT: screen.print("WTT" DOT4); break;
          case Tap::HalfTT    + Tap::One8TT: screen.print("HTT" DOT4); break;
          case Tap::QuarterTT + Tap::One16TT: screen.print("QTT" DOT4); break;
          case Tap::One8TT    + Tap::One32TT: screen.print(QUAV "8TT" DOT4); break;
          case Tap::One16TT   + Tap::One64TT: screen.print(QUAV "16TT" DOT4); break;
          case Tap::One32TT   + Tap::One128TT: screen.print(QUAV "32TT" DOT4); break;
          case Tap::One64TT   + Tap::One256TT: screen.print(QUAV "64TT" DOT4); break;
          case Tap::One128TT  + Tap::One512TT: screen.print(QUAV "128TT" DOT4); break;

          case Tap::Whole     + Tap::Half: screen.print("W" DOT2); break;
          case Tap::Half      + Tap::Quarter: screen.print("H" DOT2); break;
          case Tap::Quarter   + Tap::One8: screen.print("Q" DOT2); break;
          case Tap::One8      + Tap::One16: screen.print(QUAV "8" DOT2); break;
          case Tap::One16     + Tap::One32: screen.print(QUAV "16" DOT2); break;
          case Tap::One32     + Tap::One64: screen.print(QUAV "32" DOT2); break;
          case Tap::One64     + Tap::One128: screen.print(QUAV "64" DOT2); break;
          case Tap::One128    + Tap::One256: screen.print(QUAV "128" DOT2); break;
          case Tap::One256    + Tap::One512: screen.print(QUAV "256" DOT2); break;

          case Tap::Whole     + Tap::Half      + Tap::Quarter: screen.print("W" DOT2 DOT2); break;
          case Tap::Half      + Tap::Quarter   + Tap::One8: screen.print("H" DOT2 DOT2); break;
          case Tap::Quarter   + Tap::One8      + Tap::One16: screen.print("Q" DOT2 DOT2); break;
          case Tap::One8      + Tap::One16     + Tap::One32: screen.print(QUAV "8" DOT2 DOT2); break;
          case Tap::One16     + Tap::One32     + Tap::One64: screen.print(QUAV "16" DOT2 DOT2); break;
          case Tap::One32     + Tap::One64     + Tap::One128: screen.print(QUAV "32" DOT2 DOT2); break;
          case Tap::One64     + Tap::One128    + Tap::One256: screen.print(QUAV "64" DOT2 DOT2); break;
          case Tap::One128    + Tap::One256    + Tap::One512: screen.print(QUAV "128" DOT2 DOT2); break;

          case Tap::WholeT    + Tap::HalfT     + Tap::QuarterT: screen.print("WT" DOT2 DOT2); break;
          case Tap::QuarterT  + Tap::One8T     + Tap::One16T: screen.print("QT" DOT2 DOT2); break;
          case Tap::HalfT     + Tap::QuarterT  + Tap::One8T: screen.print("HT" DOT2 DOT2); break;
          case Tap::One8T     + Tap::One16T    + Tap::One32T: screen.print(QUAV "8T"  DOT2 DOT2); break;
          case Tap::One16T    + Tap::One32T    + Tap::One64T: screen.print(QUAV "16T" DOT2 DOT2); break;
          case Tap::One32T    + Tap::One64T    + Tap::One128T: screen.print(QUAV "32T" DOT2 DOT2); break;
          case Tap::One64T    + Tap::One128T   + Tap::One256T: screen.print(QUAV "64T" DOT2 DOT2); break;
          case Tap::One128T   + Tap::One256T   + Tap::One512T: screen.print(QUAV "128T" DOT2 DOT2); break;

          case Tap::WholeTT   + Tap::HalfTT    + Tap::QuarterTT: screen.print("WTT" DOT2 DOT2); break;
          case Tap::QuarterTT + Tap::One8TT    + Tap::One16TT: screen.print("QTT" DOT2 DOT2); break;
          case Tap::HalfTT    + Tap::QuarterTT + Tap::One8TT: screen.print("HTT" DOT2 DOT2); break;
          case Tap::One8TT    + Tap::One16TT   + Tap::One32TT: screen.print(QUAV "8TT"  DOT2 DOT2); break;
          case Tap::One16TT   + Tap::One32TT   + Tap::One64TT: screen.print(QUAV "16TT" DOT2 DOT2); break;
          case Tap::One32TT   + Tap::One64TT   + Tap::One128TT: screen.print(QUAV "32TT" DOT2 DOT2); break;
          case Tap::One64TT   + Tap::One128TT  + Tap::One256TT: screen.print(QUAV "64TT" DOT2 DOT2); break;
          case Tap::One128TT  + Tap::One256TT  + Tap::One512TT: screen.print(QUAV "128TT" DOT2 DOT2); break;

          case Tap::Whole     + Tap::Half      + Tap::One8: screen.print("W" DOT2 DOT4); break;
          case Tap::Half      + Tap::Quarter   + Tap::One16: screen.print("H" DOT2 DOT4); break;
          case Tap::Quarter   + Tap::One8      + Tap::One32: screen.print("Q" DOT2 DOT4); break;
          case Tap::One8      + Tap::One16     + Tap::One64: screen.print(QUAV "8" DOT2 DOT4); break;
          case Tap::One16     + Tap::One32     + Tap::One128: screen.print(QUAV "16" DOT2 DOT4); break;
          case Tap::One32     + Tap::One64     + Tap::One256: screen.print(QUAV "32" DOT2 DOT4); break;
          case Tap::One64     + Tap::One128    + Tap::One512: screen.print(QUAV "64" DOT2 DOT4); break;
          case Tap::One128    + Tap::One256    + Tap::One1028: screen.print(QUAV "128" DOT2 DOT4); break;

          case Tap::WholeT    + Tap::HalfT     + Tap::One8T: screen.print("WT" DOT2 DOT4); break;
          case Tap::HalfT     + Tap::QuarterT  + Tap::One16T: screen.print("HT" DOT2 DOT4); break;
          case Tap::QuarterT  + Tap::One8T     + Tap::One32T: screen.print("QT" DOT2 DOT4); break;
          case Tap::One8T     + Tap::One16T    + Tap::One64T: screen.print(QUAV "8T" DOT2 DOT4); break;
          case Tap::One16T    + Tap::One32T    + Tap::One128T: screen.print(QUAV "16T" DOT2 DOT4); break;
          case Tap::One32T    + Tap::One64T    + Tap::One256T: screen.print(QUAV "32T" DOT2 DOT4); break;
          case Tap::One64T    + Tap::One128T   + Tap::One512T: screen.print(QUAV "64T" DOT2 DOT4); break;
          case Tap::One128T   + Tap::One256T   + Tap::One1028T: screen.print(QUAV "128T" DOT2 DOT4); break;

          default: screen.print(tap); break;
        }

        //screen.print((int)TapTempo::samplePeriodToBpm(data.time, getSampleRate()));
        //screen.print((int)data.time);
      }
      else
      {
        screen.setCursor(x, static_cast<uint16_t>(rowY));
        screen.print(ftoa(data.time.value / getSampleRate(), 10));
        screen.print("s");
      }
      x += 44;
      drawKnob(data.input.value, screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 4;
      drawKnob((data.cutoff.value - Delaytrix::MIN_CUTOFF) / (Delaytrix::MAX_CUTOFF - Delaytrix::MIN_CUTOFF), screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 6;

      //screen.setCursor(x, rowY);
      //screen.print(ftoa(data.delayLength / getSampleRate(), 10));

      if (freezeState == FreezeState::FreezeOn)
      {
        const float windowStart = 1.0f - ((delayMatrix.freezePosition(i) + data.time.value) / maxFreezeSize);
        const float windowSize = min(data.time.value / maxFreezeSize, 1.0f);
        const int freezeX = x - knobRadius;
        const int freezeY = knobY - knobRadius;
        constexpr float freezeW = (knobRadius * 2 + 4)*DELAY_COUNT - 1;
        //screen.setCursor(freezeX, rowY);
        //screen.print(delays[i]->getPosition()/getSampleRate());
        screen.drawRectangle(freezeX-1, freezeY, static_cast<int>(freezeW+1), 8, WHITE);
        screen.fillRectangle(static_cast<int>(freezeW * windowStart + static_cast<float>(freezeX)), freezeY, static_cast<int>(vessl::math::max(freezeW * windowSize, 1.f)), 8, WHITE);
      }
      else
      {
        for (int f = 0; f < DELAY_COUNT; ++f)
        {
          drawFeedLabel(screen, x - knobRadius, headingY, f + 1);
          float fbk = getParameterValue(delayParams[f].feedback[i]);
          drawKnob(fbk, screen, x+1, knobY, knobRadius);
          x += knobRadius * 2 + 4;
        }
      }
    }

    constexpr int horizBarHeight = 8;
    const int barY = screen.getHeight() - 1;

    x = 0;
    drawMod(screen, x, barY, 37, horizBarHeight, delayMatrix.modValue());

    x += 40;
    drawSkew(screen, x, barY, 22, horizBarHeight, static_cast<float>(delayMatrix.skew()));

    x += 26;
    drawFeedback<true>(screen, x, barY, 48, horizBarHeight, static_cast<float>(delayMatrix.feedback()));

    x += 52;
    drawDryWet(screen, x, barY, horizBarHeight, barY - matrixTop + 8, static_cast<float>(delayMatrix.dryWet()));

    //x += 9;
    ////drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    //drawSkew(screen, x, barY, barW, skew);

    //x += 9;
    ////drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    //drawSkew(screen, x, barY, barW, skew);
  }
private:
  static void drawFeedLabel(MonochromeScreenBuffer& screen, uint16_t x, uint16_t y, int num)
  {
    const int ac = y - 5;
    screen.drawLine(x, ac, x + 3, ac, WHITE);
    screen.drawLine(x + 2, ac - 2, x + 4, ac, WHITE);
    screen.drawLine(x + 2, ac + 2, x + 4, ac, WHITE);
    screen.setCursor(x + 5, y);
    screen.print(num);
    //switch (num)
    //{
    //  case 4: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 3: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 2: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 1: screen.fillRectangle(x + 6, y - 3 * num, 2, 2, WHITE); break;
    //  default: break;
    //}
  }

  static void drawKnob(float value, MonochromeScreenBuffer& screen, uint16_t x, uint16_t y, uint16_t radius)
  {
    static constexpr float PI_4 = vessl::math::pi<float>() / 4.f;
    float angle = vessl::easing::lerp(-3.1f*PI_4, 3.1f*PI_4, value);
    float dirX = vessl::math::sin(angle);
    float dirY = -vessl::math::cos(angle);
    float x1 = static_cast<float>(x) + dirX * static_cast<float>(radius);
    float y1 = static_cast<float>(y) + dirY * static_cast<float>(radius);
    screen.drawCircle(x, y, radius+1, WHITE);
    screen.drawLine(x, y, static_cast<int>(x1), static_cast<int>(y1), WHITE);

    // hack to fix "pointy" circle sides
    screen.setPixel(x - radius -1, y, BLACK);
    screen.setPixel(x - radius, y, WHITE);
    screen.setPixel(x + radius + 1, y, BLACK);
    screen.setPixel(x + radius, y, WHITE);
    screen.setPixel(x, y + radius + 1, BLACK);
    screen.setPixel(x, y + radius, WHITE);
    screen.setPixel(x, y - radius - 1, BLACK);
    screen.setPixel(x, y - radius, WHITE);
  }


  static void drawMod(MonochromeScreenBuffer& screen, int x, int y, int w, int h, float amt)
  {
    screen.drawRectangle(x, y - h, w, h, WHITE);
    int fw = static_cast<int>(static_cast<float>(w) * amt);
    int c = w / 2;
    screen.drawLine(x + c + fw, y - h, x + c + fw, y -1, WHITE);
    screen.drawLine(x + c, y - h, x + c, y - h + 1, WHITE);
    screen.drawLine(x + c, y - 1, x + c, y - 2, WHITE);
  }


  template<bool pointLeft>
  void drawFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int w, const int h, const float amt) const
  {
    const int iconY = y-2;
    const int iconDim = h-2;

    FreezeState freezeState = delayMatrix.freeze().read<FreezeState>();
    if (freezeState == FreezeState::FreezeOn)
    {
      screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
      screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
      screen.drawLine(x, iconY - iconDim + 2, x + 2, iconY - iconDim + 2, WHITE);
      screen.drawRectangle(x + iconDim - 3, iconY - 3, 3, 3, WHITE);
      screen.drawLine(x + iconDim - 3, iconY, x + iconDim - 3, iconY - 3, WHITE);
    }
    else
    {
      screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
      screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
      screen.drawLine(x + iconDim, iconY - iconDim, x + iconDim, iconY, WHITE);

      if (pointLeft)
      {
        screen.drawLine(x + iconDim, iconY, x + 2, iconY, WHITE);
        screen.drawLine(x + 2, iconY, x + 4, iconY - 2, WHITE);
        screen.drawLine(x + 2, iconY, x + 4, iconY + 2, WHITE);
      }
      else
      {
        screen.drawLine(x, iconY, x + iconDim - 2, iconY, WHITE);
        screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY - 2, WHITE);
        screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY + 2, WHITE);
      }
    }

    uint16_t barWidth = w - iconDim - 2;
    screen.drawRectangle(x+iconDim+2, y - h, barWidth, h, WHITE);
    screen.fillRectangle(x+iconDim+2, y - h, barWidth*amt, h, WHITE);
  }

  static void drawSkew(MonochromeScreenBuffer& screen, const int x, const int y, const int w, const int h, const float amt)
  {
    int tx = x;
    int tw = h;
    int ty = y - h;
    screen.drawLine(tx, ty, tx + tw, ty, WHITE);
    screen.drawLine(tx + tw, ty, tx + tw / 2, y, WHITE);
    screen.drawLine(tx + tw / 2, y, tx, ty, WHITE);

    const int barWidth = w - tw - 1;
    screen.drawRectangle(x+tw+2, y - h, barWidth, h, WHITE);
    screen.fillRectangle(x+tw+2, y - h, static_cast<int>(static_cast<float>(barWidth) * amt), h, WHITE);
  }

  void drawDryWet(MonochromeScreenBuffer& screen, int x, const int y, const int w, const int h, float amt)
  {
    dryWetAnim += amt;
    if (dryWetAnim >= 256)
    {
      dryWetAnim -= 256;
    }
    for (int ix = 0; ix < w; ++ix)
    {
      for (int iy = 0; iy < w; ++iy)
      {
        screen.setPixel(x + ix, y - iy, noise2(ix, iy+dryWetAnim) > 224);
      }
    }

    const int barHeight = h - w - 2;
    const int barFill = static_cast<int>(static_cast<float>(barHeight) * amt);
    screen.drawRectangle(x, y - h, w, barHeight, WHITE);
    screen.fillRectangle(x, y - w - 2 - barFill, w, barFill, WHITE);
  }

  // copied from message.cpp and modified to give 3 decimal points
  static constexpr char HEXNUMERALS[] = "0123456789abcdef";

  static char* ftoa(float val, int base)
  {
    static char buf[16] = { 0 };
    int i = 14;
    // print 3 decimal points
    unsigned int part = vessl::math::abs(static_cast<int>((val - vessl::math::floor(val)) * 1000));
    do {
      buf[i--] = HEXNUMERALS[part % base];
      part /= base;
    } while (i > 11);
    buf[i--] = '.';
    part = vessl::math::abs(static_cast<int>(val));
    do {
      buf[i--] = HEXNUMERALS[part % base];
      part /= base;
    } while (part && i);
    if (val < 0.0f)
    {
      buf[i--] = '-';
    }
    return &buf[i + 1];
  }

};
