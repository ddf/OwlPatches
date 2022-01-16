/**

AUTHOR:
    (c) 2022 Damien Quartz

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

*/

#include "Patch.h"
#include "MarkovChain.hpp"

class MarkovPatch : public Patch 
{
  MarkovChain* markov;
  uint16_t listening;
  uint16_t generating;

  AudioBuffer* genBuffer;

  float lastLearnLeft, lastLearnRight;
  float lastGenLeft, lastGenRight;

  static const PatchButtonId inToggleListen = BUTTON_1;
  static const PatchButtonId inToggleGenerate = BUTTON_2;

  static const PatchParameterId inDryWet = PARAMETER_A;

public: 
  MarkovPatch()
  : listening(OFF), generating(ON), lastLearnLeft(0), lastLearnRight(0)
  , genBuffer(0), lastGenLeft(0), lastGenRight(0)
  {
    markov = MarkovChain::create();
    genBuffer = AudioBuffer::create(2, getBlockSize());

    registerParameter(inDryWet, "Dry/Wet");

  }

  ~MarkovPatch()
  {
    MarkovChain::destroy(markov);
    AudioBuffer::destroy(genBuffer);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == inToggleListen && value == ON)
    {
      listening = listening == ON ? OFF : ON;
      if (listening)
      {
        lastLearnLeft = 0;
        lastLearnRight = 0;
      }
    }
    else if (bid == inToggleGenerate && value == ON)
    {
      generating = generating == ON ? OFF : ON;
      if (generating)
      {
        lastGenLeft = 0;
        lastGenRight = 0;
      }
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    FloatArray inLeft = audio.getSamples(0);
    FloatArray inRight = audio.getSamples(1);
    FloatArray genLeft = genBuffer->getSamples(0);
    FloatArray genRight = genBuffer->getSamples(1);

    if (listening)
    {
      markov->setLastLearn(lastLearnLeft);
      markov->learn(inLeft);
      lastLearnLeft = inLeft[inLeft.getSize() - 1];

      //markov->setLastLearn(lastLearnRight);
      //markov->learn(right);
      //lastLearnRight = right[right.getSize() - 1];
    }

    if (generating)
    {
      markov->setLastGenerate(lastGenLeft);
      markov->generate(genLeft);
      lastGenLeft = genLeft[genLeft.getSize()-1];

      //markov->setLastGenerate(lastGenRight);
      //markov->generate(right);
      //lastGenRight = right[right.getSize() - 1];
    }

    float dryWet = getParameterValue(inDryWet);
    const float wetAmt = dryWet;
    const float dryAmt = 1.0f - wetAmt;
    inLeft.multiply(dryAmt);
    genLeft.multiply(wetAmt);
    inLeft.add(genLeft);
    inLeft.copyTo(inRight);

    setButton(inToggleListen, listening);
    setButton(inToggleGenerate, generating);
  }
  
};
