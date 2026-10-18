/**

AUTHOR:
    (c) 2026 Damien Quartz

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

#pragma once

#include "PatchBase.h"
#include "PatchParameterIds.h"
#include "PatchParameterDescription.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Cutup.h"

class CutupPatch : public PatchBase
{
  // 4 seconds - one full bar at 60 bpm.
  static constexpr vessl::size_t buffer_size = 48000*4;
  static constexpr IntPatchParameterDescription rate_vary_desc = { "rate vary", 1, 9, 1, 0.f, 0.f };
  static constexpr IntPatchParameterDescription slice_vary_desc = { "slice vary", 0, 4, 0, 0.f, 0.f };

  using binary_t  = vessl::binary_t;
  using sample_t  = vessl::sample::frame<float, 2>;
  using CutupProc = Cutup<sample_t, buffer_size>;

  CutupProc* cutup_;
  binary_t   enabled_ = false;

  struct
  {
    InputGateId clock = InputGateId::A;
    InputGateId slice = InputGateId::B;
  } bin_;

  struct
  {
  } bout_;

  struct
  {
    PatchParameter<int> slice_vary;
    PatchParameter<int> rate_vary;
  } pin_;
  
  int clock_pulse_  = -1;
  int toggle_countdown_ = -1;
  bool toggle_slice_ = 0;

public:
  explicit CutupPatch()
  : cutup_(nullptr)
  {
    cutup_ = new CutupProc(getSampleRate());
    cutup_->rate_min() = 1;

    pin_.slice_vary = slice_vary_desc.registerParameter(this);
    pin_.rate_vary = rate_vary_desc.registerParameter(this);
  }

  virtual ~CutupPatch()
  {
    delete cutup_;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == bin_.clock.id && value == ON)
    {
      clock_pulse_ = samples;
      cutup_->clock(samples);
    }

    if (bid == bin_.slice.id && value == ON)
    {
      toggle_countdown_ = samples;
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    int slice_vary = pin_.slice_vary.getValue();
    int rate_vary = pin_.rate_vary.getValue();
    cutup_->slice_min() = vessl::math::max(0, 4 - slice_vary);
    cutup_->slice_max() = 4 + slice_vary;
    cutup_->rate_min() = rate_vary > 1 ? 0 : 1;
    cutup_->rate_max() = rate_vary;

    AudioBufferReader<2> reader(audio);
    AudioBufferWriter<2> writer(audio);
    while(reader)
    {
      if (toggle_countdown_ > -1)
      {
        --toggle_countdown_;
        toggle_slice_ = toggle_countdown_ == -1;
      }

      if (clock_pulse_ > -1)
      {
        --clock_pulse_;
        if (clock_pulse_ == -1 && toggle_slice_)
        {
          enabled_ = !enabled_;
          cutup_->enabled() = enabled_;
          toggle_slice_ = false;
        }
      }
      
      sample_t s = reader.read();
      writer << cutup_->process(s);
    }

    setButton(bin_.slice, enabled_);
  }
};