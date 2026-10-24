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

/**
 * @todo rather than using a freeze unit in Cutup, write into a plain delay line.
 *       every clock pulse, record into a separate array where the record head is currently located.
 *       keep track of the last X clock pulses (probably 16, but could be configurable).
 *       when cutup is engaged, select slices from the saved-off locations.
 *       probably want to keep more information than just the sample position.
 *       e.g. length of a clock pulse in samples at time of recording, peak value (for skipping "silent" ticks)
 */

class CutupPatch : public PatchBase
{
  static constexpr IntPatchParameterDescription rate_vary_desc = { "rate vary", 0, 17, 1 };
  static constexpr IntPatchParameterDescription slice_vary_desc = { "slice vary", 0, 4, 0 };
  static constexpr FloatPatchParameterDescription crush_vary_desc = { "crush vary", 0.f, 1.f, 0.f };
  static constexpr FloatPatchParameterDescription damp_vary_desc = { "damp vary", 0.f, 0.5f, 0.f };
  static constexpr FloatPatchParameterDescription slice_len_desc = { "slice len", 0.f, 1.f, 0.f };

  using binary_t  = vessl::binary_t;
  using sample_t  = vessl::sample::frame<float, 2>;
  using CutupProc = Cutup<sample_t>;

  // 4 seconds - one full bar at 60 bpm.
  static constexpr vessl::size_t cutup_buffer_size = 48000*4;
  sample_t cutup_buffer_[cutup_buffer_size];

  CutupProc  cutup_;
  binary_t   enabled_ = false;
  int        slice_trigger_ = 0;

  struct
  {
    InputGateId clock = InputGateId::A;
    InputGateId cut   = InputGateId::C;
    InputGateId slice = InputGateId::D;
  } bin_;

  struct
  {
    OutputGateId sliced = OutputGateId::B;
  } bout_;

  struct
  {
    IntParameter   slice_vary;
    IntParameter   rate_vary;
    FloatParameter crush_vary;
    FloatParameter damp_vary;
    FloatParameter slice_len_vary;
  } pin_;
  
  int clock_pulse_  = -1;
  int toggle_countdown_ = -1;
  bool toggle_slice_ = 0;

public:
  explicit CutupPatch()
  : cutup_(getSampleRate(), cutup_buffer_, cutup_buffer_size)
  {
    //cutup_ = new CutupProc(getSampleRate());
    cutup_.rate_min() = 1;
    cutup_.crush_min() = 0;

    pin_.slice_vary = slice_vary_desc.registerParameter(this);
    pin_.rate_vary = rate_vary_desc.registerParameter(this);
    pin_.crush_vary = crush_vary_desc.registerParameter(this);
    pin_.damp_vary = crush_vary_desc.registerParameter(this);
    pin_.slice_len_vary = slice_len_desc.registerParameter(this);
  }

  virtual ~CutupPatch()
  {
    //delete cutup_;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if (bid == bin_.clock.id && value == ON)
    {
      clock_pulse_ = samples;
      cutup_.clock(samples);
    }

    if (bid == bin_.slice.id && value == ON)
    {
      toggle_countdown_ = samples;
    }

    if (bid == bin_.cut.id && value == ON)
    {
      cutup_.cut();
    }
  }

  void processAudio(AudioBuffer& audio) override
  {
    int slice_vary = pin_.slice_vary.getValue();
    int rate_vary = pin_.rate_vary.getValue();
    cutup_.slice_min() = vessl::math::max(0, 4 - slice_vary);
    cutup_.slice_max() = 2 + slice_vary;
    cutup_.slice_vary() = pin_.slice_len_vary.getValue();
    cutup_.rate_min() = 8 - rate_vary;
    cutup_.rate_max() = 8 + rate_vary;
    cutup_.crush_max() = pin_.crush_vary.getValue();
    cutup_.damp_max() = pin_.damp_vary.getValue();

    bool slice_started = false;
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
          cutup_.enabled() = enabled_;
          toggle_slice_ = false;
        }
      }

      sample_t in = reader.read();
      writer << cutup_.process(in);
      slice_started |= cutup_.sliced();
    }
    if (slice_started)
    {
      slice_trigger_ = 2;
    }
    else if (slice_trigger_ > 0)
    {
      --slice_trigger_;
    }

    setButton(bin_.slice, enabled_ ? ON : OFF);
    setButton(bout_.sliced, slice_trigger_ ? ON : OFF);
  }
};