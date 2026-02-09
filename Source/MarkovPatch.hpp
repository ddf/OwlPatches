/**

AUTHOR:
    (c) 2022-2025 Damien Quartz

LICENSE:
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


DESCRIPTION:
    Markov is essentially a "smart" granular synthesizer that plays 
    only one grain at a time and chooses the starting sample of each 
    grain based on the last sample of the previous grain.

    Teach the patch how to generate sound by engaging Listen mode
    by pressing Button 1 or sending a trigger to Gate 1. 
    While listening, the patch records to a four second "memory" 
    and analyzes it using a 1-dimensional Markov Chain. The patch
    will "forget" old sound when more than four seconds are recorded.

    Sound is continuously generated based on what has been learned
    with control over the "word" size, which is analogous to grain size 
    in a typical granular synthesizer.  Each word has an envelope 
    applied to it, which can be morphed from an exponential decay, 
    to a linear decay, to a box car.  When the envelope shape parameter 
    is turned all the way up, the envelope is kept open at all times, 
    putting the patch into a kind of pure synthesis mode where word size 
    becomes less obvious.

    The default word size with the word size parameter at 0.5 is half 
    a second and can be increased to two seconds and decreased to 
    an eighth of a second. The word size can also be set by tapping 
    a tempo on Button 2 or by sending clock to Gate 2. While receiving 
    clock at Gate 2, the word size parameter will divide or multiply 
    the word size by musical durations.

    Random variation can be added to the word size with the word variation
    parameter. Below 0.5 only musical divisions and multiplications
    are allowed, increasing in range as the parameter moves towards zero.
    Above 0.5 the variation is totally random, increasing in range 
    as the parameter moves towards one.
*/

#pragma once

#include "MonochromeScreenPatch.h"
#include "PatchParameterDescription.h"
#include "DcBlockingFilter.h"
#include "Markov.h"

static constexpr PatchButtonId IN_TOGGLE_LISTEN = BUTTON_1;
static constexpr PatchButtonId IN_CLOCK = BUTTON_2;
static constexpr PatchButtonId OUT_WORD_STARTED_LEFT = OUT_GATE_1;
static constexpr PatchButtonId OUT_WORD_STARTED_RIGHT = OUT_GATE_2;

static constexpr PatchParameterId IN_WORD_SIZE = PARAMETER_A;
static constexpr PatchParameterId IN_DECAY = PARAMETER_B;
static constexpr PatchParameterId IN_WORD_SIZE_VARIATION = PARAMETER_C;
static constexpr PatchParameterId IN_DRY_WET = PARAMETER_D;

static constexpr PatchParameterId OUT_WORD_PROGRESS_LEFT = OUT_PARAMETER_A;
static constexpr PatchParameterId OUT_WORD_PROGRESS_RIGHT = OUT_PARAMETER_B;

class MarkovPatch final : public MonochromeScreenPatch  // NOLINT(cppcoreguidelines-special-member-functions)
{
  struct KeyFuncStereo
  {
    uint32_t operator()(const ComplexFloat& value) const
    {
      // generate a unique key for this stereo frame
      // not sure what's best here - if frames are too unique, we wind up restarting words at zero all the time
      // what I really want to be able to do is modulate this
      static constexpr double SCALE = (1<<12) / (2.0f * M_PI);
      return static_cast<uint32_t>((value.getPhase()+M_PI)*SCALE);
    }
  };

  struct KeyFuncMono
  {
    uint32_t operator()(const float& value) const
    {
      return static_cast<uint32_t>((value*0.5f + 0.5f)*UINT16_MAX);
    }
  };
  
  //using MarkovProcessor = Markov<ComplexFloat, KeyFuncStereo>;
  using MarkovProcessor = Markov<float, KeyFuncMono>;
  
  StereoDcBlockingFilter* dcBlockingFilter;
  MarkovProcessor* markovLeft;
  MarkovProcessor* markovRight;
  
  array<float> markovBuffer;

public: 
  MarkovPatch() : dcBlockingFilter(nullptr), markovBuffer(new float[getBlockSize()], getBlockSize())
  {
    dcBlockingFilter = StereoDcBlockingFilter::create(0.995f);
    markovLeft = new MarkovProcessor(getSampleRate(), static_cast<size_t>(getSampleRate()*2));
    markovRight = new MarkovProcessor(getSampleRate(), static_cast<size_t>(getSampleRate())*2);

    // registration order matters for which parameters are assign to CV 1 and 2 on Genius on startup
    registerParameter(IN_WORD_SIZE, "Word Size");
    registerParameter(IN_DECAY, "Decay");
    registerParameter(IN_WORD_SIZE_VARIATION, "Word Size Variation");
    registerParameter(IN_DRY_WET, "Dry/Wet");
    registerParameter(OUT_WORD_PROGRESS_LEFT, "Word L>");
    registerParameter(OUT_WORD_PROGRESS_RIGHT, "Word R>");

    setParameterValue(IN_WORD_SIZE, 0.5f);
    setParameterValue(IN_WORD_SIZE_VARIATION, 0.5f);
  }

  ~MarkovPatch() override
  {
    delete[] markovBuffer.getData();
    delete markovLeft;
    delete markovRight;
    StereoDcBlockingFilter::destroy(dcBlockingFilter);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == IN_TOGGLE_LISTEN && value == ON)
    {
      // @todo use sample delay again when possible
      if (markovLeft->listen())
      {
        markovLeft->listen() = false; 
      }
      else
      {
        markovLeft->listen() = true;
      }

      if (markovRight->listen())
      {
        markovRight->listen() = false;
      }
      else
      {
        markovRight->listen() = true;
      }
    }
    else if (bid == IN_CLOCK && value == ON)
    {
      markovLeft->clock();
      markovRight->clock();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int inSize = audio.getSize();
    array<float> inLeft(audio.getSamples(0), inSize);
    array<float> inRight(audio.getSamples(1), inSize);

    dcBlockingFilter->process(audio, audio);

    float wsz = getParameterValue(IN_WORD_SIZE);
    float wszv = getParameterValue(IN_WORD_SIZE_VARIATION);
    float dcy = getParameterValue(IN_DECAY);

    // set the parameters
    markovLeft->wordSize() = wsz;
    markovLeft->variation() = wszv;
    markovLeft->decay() = dcy;

    markovRight->wordSize() = wsz;
    markovRight->variation() = wszv;
    markovRight->decay() = dcy;

    // // copy our input into a processing array
    // for (int i = 0; i < inSize; ++i)
    // {
    //   ComplexFloat& c = markovBuffer[i];
    //   c.re = inLeft[i];
    //   c.im = inRight[i];
    // }
    //
    // // and process
    // markov->process(markovBuffer, markovBuffer);
    
    float wetMix = vessl::math::constrain(getParameterValue(IN_DRY_WET)*1.02f, 0.0f, 1.0f);
    float dryMix = 1.0f - wetMix;
    
    inLeft >> *markovLeft >> markovBuffer;
    inLeft.scale(dryMix).add(markovBuffer.scale(wetMix));
    
    inRight >> *markovRight >> markovBuffer;
    inRight.scale(dryMix).add(markovBuffer.scale(wetMix));
    
    // @todo use this again when we can
    uint32_t wordStartDelay = 0;
    bool wordState = markovLeft->wordStarted().readBinary();
    setButton(OUT_WORD_STARTED_LEFT, wordState, static_cast<uint16_t>(wordStartDelay));
    
    wordState = markovRight->wordStarted().readBinary();
    setButton(OUT_WORD_STARTED_RIGHT, wordState, static_cast<uint16_t>(wordStartDelay));
    
    setParameterValue(OUT_WORD_PROGRESS_LEFT, markovLeft->progress().read<float>());
    setParameterValue(OUT_WORD_PROGRESS_RIGHT, markovLeft->progress().read<float>());
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const auto stats = markovLeft->getChainStats();
    screen.setCursor(0, 8);
    screen.print("keys ");
    screen.print(stats.chainCount);
    screen.print("\n min len ");
    screen.print(stats.minChainLength);
    screen.print(" (");
    screen.print(stats.minChainCount);
    screen.print(")\n max len ");
    screen.print(stats.maxChainLength);
    screen.print(" (");
    screen.print(stats.maxChainCount);
    screen.print(")\n avg len ");
    screen.print(stats.avgChainLength);
    screen.print("\n Wms " );
    screen.print(markovLeft->wordSizeMs());
    screen.print("\n BPM ");
    screen.print(markovLeft->getBpm());
  }
};
