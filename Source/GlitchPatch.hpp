/**

AUTHOR:
    (c) 2021-2025 Damien Quartz

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
    A clockable freeze / bitcrush / glitch effect.

    Parameter A controls the length of the freeze buffer.

    Parameter B controls the amount of bitcrush,
    which is a mix of bit reduction and rate reduction.

    Parameter C controls a glitch effect,
    which mangles the result of frozen, bitcrushed input
    at regular intervals based on the clock,
    increasing in frequency as the parameter is increased.

    Parameter D controls the mix of a wave shaping effect that
    reinterprets the result of the glitch effect as if it is a
    wave table, using the dry input as phase, modulated by
    an envelope follower that tracks the dry input signal level.

    Button 1 and Gate A enable freeze.
    Button 2 is for tap tempo and Gate B for external clock.
    Gate Out is the internal clock for the freeze loop,
    which is influenced by Parameter A even when freeze is not activated.

    CV Out 1 is the envelope follower sampled at block rate.
    CV Out 2 is the random value used to determine when the engage glitch.

*/
#pragma once

#include "DcBlockingFilter.h"
#include "Patch.h"
#include "PatchParameterDescription.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Glitch.h"

namespace glitch_inputs
{
constexpr PatchButtonId clock = BUTTON_1;
constexpr PatchButtonId freeze = BUTTON_2;
constexpr PatchButtonId glitch_enabled = BUTTON_4;
constexpr FloatPatchParameterDescription repeats = { "Repeats", 0, 1, 0.5f, 0.0f, 0.01f };
constexpr FloatPatchParameterDescription shape = { "Shape", 0, 1, 0.0f };
constexpr FloatPatchParameterDescription crush = { "Crush", 0, 1, 0.0f };
constexpr FloatPatchParameterDescription glitch = { "Glitch", 0, 1, 0 };
constexpr FloatPatchParameterDescription play_rate = { "PlayRate", -1.f, 1.f, 0 };
constexpr FloatPatchParameterDescription mix = {"Mix", 0, 1, 0 };
}

namespace glitch_outputs
{
constexpr PatchButtonId glitch_rand_gate = OUT_GATE_1;
constexpr PatchButtonId glitching_gate   = OUT_GATE_2;
constexpr OutputParameterDescription env = { "Env", OUT_PARAMETER_A };
constexpr OutputParameterDescription rand = { "Rand", OUT_PARAMETER_B };
}


constexpr uint32_t GlitchBufferSize = 1 << 17;

class GlitchPatch final : public Patch  // NOLINT(cppcoreguidelines-special-member-functions)
{
  using GlitchProcessor = Glitch<GlitchBufferSize>;
  FloatParameter pin_repeats_;
  FloatParameter pin_glitch_;
  FloatParameter pin_shape_;
  FloatParameter pin_crush_;
  FloatParameter pin_play_rate_;
  FloatParameter pin_mix_;
  OutputParameter pout_env_;
  OutputParameter pout_rand_;

  StereoDcBlockingFilter* dc_filter_;
  GlitchProcessor* glitch_processor_;
  
  bool glitch_enabled_;
  
  vessl::array<GlitchSampleType> process_buffer_;

public:
  GlitchPatch() : Patch()
  , pout_env_(this, glitch_outputs::env)
  , pout_rand_(this, glitch_outputs::rand)
  , process_buffer_(new GlitchSampleType[getBlockSize()], getBlockSize())
  {
    // order of registration determines parameter assignment, starting from PARAMETER_A
    pin_repeats_ = glitch_inputs::repeats.registerParameter(this);
    pin_play_rate_ = glitch_inputs::play_rate.registerParameter(this);
    pin_crush_ = glitch_inputs::crush.registerParameter(this);
    pin_glitch_ = glitch_inputs::glitch.registerParameter(this);
    pin_shape_ = glitch_inputs::shape.registerParameter(this);
    pin_mix_ = glitch_inputs::mix.registerParameter(this);

    dc_filter_ = StereoDcBlockingFilter::create(0.995f);
    glitch_processor_ = new GlitchProcessor(getSampleRate(), getBlockSize());
#ifdef OWL_WITCH
    glitch_enabled_ = true;
#else
    glitch_enabled_ = false;
#endif
  }

  ~GlitchPatch() override
  {
    StereoDcBlockingFilter::destroy(dc_filter_);
    delete[] process_buffer_.data();
    delete glitch_processor_;
  }

  void processAudio(AudioBuffer& audio) override
  {
    glitch_processor_->repeats() = pin_repeats_.getValue();
    glitch_processor_->crush() = pin_crush_.getValue();
    glitch_processor_->glitch() = pin_glitch_.getValue();
#ifdef OWL_WITCH
    glitch_processor_->glitch_enabled() = glitch_enabled_;
#else
    glitch_processor_->glitch_enabled() = pin_glitch_.getValue() > 0.001f;
#endif
    glitch_processor_->shape() = pin_shape_.getValue();
    
    float play_param = pin_play_rate_.getValue();
    constexpr float play_dead = 0.1f;
    constexpr float play_rate_max = 8.f;
    float play_rate = 1.0f;
    if (play_param < -play_dead)
    {
      float t = play_param*-1.1f - play_dead;
      play_rate = -vessl::math::lerp(play_rate, play_rate_max, t);
    }
    else
    {
      float t = play_param*1.1f - play_dead;
      play_rate = vessl::math::lerp(play_rate, play_rate_max, t);
    }
    glitch_processor_->play_rate() = play_rate;

    dc_filter_->process(audio, audio);

    AudioBufferReader<2> reader(audio);
    auto pbw = process_buffer_.make_writer();
    while (reader)
    {
      pbw << reader.read();
    }
    
    glitch_processor_->process(process_buffer_, process_buffer_);
    
    AudioBufferWriter<2> writer(audio);
    auto pbr = process_buffer_.make_reader();
    while (pbr)
    {
      writer.write(pbr.read());
    }
    
    pout_env_.setValue(glitch_processor_->envelope());
    pout_rand_.setValue(glitch_processor_->glitch_rand());
    setButton(glitch_outputs::glitching_gate, glitch_processor_->is_glitching() ? ON : OFF);
    setButton(glitch_outputs::glitch_rand_gate, glitch_processor_->glitch_rand() > 0.5f ? ON : OFF);
    setButton(glitch_inputs::glitch_enabled, glitch_enabled_ ? ON : OFF);
  }


  void buttonChanged(const PatchButtonId bid, const uint16_t value, const uint16_t samples) override
  {
    if (bid == glitch_inputs::freeze)
    {
      if (value == ON)
      {
        glitch_processor_->freeze() = true;
      }
      else
      {
        glitch_processor_->freeze() = false;
      }
    }

    if (bid == glitch_inputs::clock && value == ON)
    {
      glitch_processor_->clock(samples);
    }
    
    if (bid == glitch_inputs::glitch_enabled && value == ON)
    {
      glitch_enabled_ = !glitch_enabled_;
    }
  }

};
