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
    Synthesizes sound by using overlap-add IFFT synthesis of spectral data.
    Send audio to L In to excite a portion of the spectrum using the 
    Fundamental, Octaves, Density, and Tuning settings. Fundamental
    and Octaves are used to determine the portion of the spectrum that
    L In excites (shown at the top of the screen in Hz). Density determines
    how many "strings" are available in that range, acting like a kind of
    comb filter on the input. Tuning determines how strings are spaced within
    the frequency range from logarithmic to linear. Decay controls how 
    quickly strings decay to silence after being excited, Spread will excite 
    nearby strings, Brightness fades in overtones of excited strings, and Crush
    reduces the sample rate of the output. Width stereoizes the output with a diffuser,
    which is followed by reverb with controls for blend, time, and tone.
*/

#pragma once

#define USE_MIDI_CALLBACK

#include "MonochromeScreenPatch.h"
#include "SmoothValue.h"
#include "MidiMessage.h"

#include "Diffuser.h"
#include "Reverb.h"
#include "vessicle/Condolences.h"

struct CondolencesParameterIds
{
  PatchParameterId inDensity; // = PARAMETER_C;
  PatchParameterId inTuning; // PARAMETER_D;
  PatchParameterId inDecay; // = PARAMETER_E;
  PatchParameterId inSpread; // = PARAMETER_F;
  PatchParameterId inBrightness; // = PARAMETER_G;
  PatchParameterId inCrush; // = PARAMETER_H;
  PatchParameterId inFeedback;
  PatchParameterId inMix;

  PatchParameterId inWidth; // = PARAMETER_AA;
  PatchParameterId inReverbBlend; // = PARAMETER_AB;
  PatchParameterId inReverbTime; // = PARAMETER_AC;
  PatchParameterId inReverbTone; // = PARAMETER_AD;

  PatchParameterId outStrumX; // = PARAMETER_AE;
  PatchParameterId outStrumY; // = PARAMETER_AF;
};

static constexpr vessl::size_t condolences_spectrum_size = 2048;

#ifdef OWL_GENIUS
static constexpr CondolencesParameterIds genius_params =
{
  .inDensity = PARAMETER_A,
  .inTuning = PARAMETER_E,
  .inDecay = PARAMETER_F,
  .inSpread = PARAMETER_B,
  .inBrightness = PARAMETER_G,
  .inCrush = PARAMETER_H,
  .inFeedback = PARAMETER_AB,
  .inMix = PARAMETER_AA,

  .inWidth = PARAMETER_DA,
  .inReverbBlend = PARAMETER_DB,
  .inReverbTime = PARAMETER_DC,
  .inReverbTone = PARAMETER_DD,

  .outStrumX = PARAMETER_AE,
  .outStrumY = PARAMETER_AF,
};
#endif

#ifdef OWL_WITCH
static constexpr SpectralSympathiesParameterIds witch_params =
{
  .inDensity = PARAMETER_C,
  .inTuning = PARAMETER_D,
  .inDecay = PARAMETER_E,
  .inSpread = PARAMETER_AA,
  .inBrightness = PARAMETER_AB,
  .inCrush = PARAMETER_AC,

  .inWidth = PARAMETER_BA,
  .inReverbBlend = PARAMETER_BB,
  .inReverbTime = PARAMETER_BC,
  .inReverbTone = PARAMETER_BD,

  .outStrumX = PARAMETER_F,
  .outStrumY = PARAMETER_G,
};
#endif

class CondolencesPatch : public MonochromeScreenPatch
{
  static constexpr bool ReverbEnabled = false;
  
  using sample_t = float;
  using complex_t = vessl::transform::complex<sample_t>;
  using BitCrush = vessl::processors::bitcrush<sample_t, 24>;
  using SpectralGen = Condolences<sample_t, condolences_spectrum_size>;
  using ReverbProcessor = Reverb<sample_t>;
  using SampleArray = vessl::array<sample_t>;
  using ComplexArray = vessl::array<complex_t>;
  using Window = vessl::sample::windows::type;
  using FFT = vessl::transform::fft<sample_t>;

  CondolencesParameterIds params_;

  float spread_max_ = 1.0f;
  float decay_min_;
  float decay_max_;
  float decay_default_ = 0.5f;
  float crush_rate_min_ = 1000.0f;
  float string_animation_;
  
  AudioBuffer* output_buffer_;
  SpectralGen* spectral_gen_;
  Diffuser* diffuser_;
  ReverbProcessor* reverb_;

  BitCrush bit_crusher_;
  
  SmoothFloat mix_;
  SmoothFloat crush_;
  SmoothFloat stereo_width_;
  SmoothFloat reverb_time_;
  SmoothFloat reverb_tone_;
  SmoothFloat reverb_blend_;

  MidiMessage* midi_notes_;

public:
  explicit CondolencesPatch() 
    : MonochromeScreenPatch()
#ifdef OWL_WITCH
    , params_(witch_params)
#endif
#ifdef OWL_GENIUS
    , params_(genius_params)
#endif
    , decay_min_(static_cast<float>(condolences_spectrum_size)*0.5f / getSampleRate())
    , decay_max_(10.f)
    , string_animation_(0)
    , bit_crusher_(getSampleRate(), getSampleRate())
    , mix_(0.99f, 0.5f)
  {
    output_buffer_ = AudioBuffer::create(2, getBlockSize());
    spectral_gen_ = SpectralGen::create(getSampleRate());

    if constexpr (ReverbEnabled)
    {
      diffuser_ = Diffuser::create();
      reverb_ = ReverbProcessor::create(getSampleRate());
    }
    
    midi_notes_ = new MidiMessage[128];
    memset(midi_notes_, 0, sizeof(MidiMessage) * 128);

    // register Decay and Spread first
    // so that these wind up as the default CV A and B parameters on Genius
    registerParameter(params_.inDecay, "Decay");
    registerParameter(params_.inSpread, "Spread");
    registerParameter(params_.inBrightness, "Brightness");
    registerParameter(params_.inCrush, "Crush");
    registerParameter(params_.inDensity, "Density");
    registerParameter(params_.inTuning, "Tuning");
    registerParameter(params_.inFeedback, "Feedback");
    registerParameter(params_.inMix, "Mix");
    if constexpr (ReverbEnabled)
    {
      registerParameter(params_.inWidth, "Width");
      registerParameter(params_.inReverbTime, "Verb Time");
      registerParameter(params_.inReverbTone, "Verb Tone");
      registerParameter(params_.inReverbBlend, "Verb Blend");
    }

    registerParameter(params_.outStrumX, "Strum X>");
    registerParameter(params_.outStrumY, "Strum Y>");
    setParameterValue(params_.inDecay, (decay_default_ - decay_min_) / (decay_max_ - decay_min_));
    setParameterValue(params_.inDensity, 1.0f);
    setParameterValue(params_.inSpread, 0.0f);
    setParameterValue(params_.inBrightness, 0.0f);
    setParameterValue(params_.inCrush, 0.0f);
    setParameterValue(params_.inTuning, 1.0f);
    setParameterValue(params_.inFeedback, 0.0f);
    setParameterValue(params_.inMix, mix_.getValue());

    if constexpr (ReverbEnabled)
    {
      setParameterValue(params_.inReverbTone, 1.0f);
    }
  }

  ~CondolencesPatch() override
  {
    SpectralGen::destroy(spectral_gen_);
    AudioBuffer::destroy(output_buffer_);
    if constexpr (ReverbEnabled)
    {
      Diffuser::destroy(diffuser_);
      ReverbProcessor::destroy(reverb_);
    }
    delete[] midi_notes_;
  }

  // void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  // {
  //   if ((bid == PUSHBUTTON || bid == BUTTON_1) && value == Patch::ON)
  //   {
  //     pluck_at_sample_ = samples;
  //   }
  //
  //   if (bid == BUTTON_2)
  //   {
  //     if (value == Patch::ON)
  //     {
  //       gate_on_at_sample_ = samples;
  //     }
  //     else
  //     {
  //       gate_off_at_sample_ = samples;
  //     }
  //   }
  // }

  void processMidi(MidiMessage msg) override
  {
    //if (msg.isNote())
    //{
    //  midiNotes[msg.getNote()] = msg;

    //  if (msg.isNoteOn())
    //  {
    //    pluck(spectralGen, msg);
    //  }
    //}
  }

  void processAudio(AudioBuffer& audio) override
  {
    const int block_size = audio.getSize();
    SampleArray audio_left(audio.getSamples(0), block_size);
    SampleArray audio_right(audio.getSamples(1), block_size);
    SampleArray wet_left(output_buffer_->getSamples(0), block_size);
    SampleArray wet_right(output_buffer_->getSamples(1), block_size);
    
    constexpr float octaves_min = 0.5f;
    constexpr float octaves_max = 2.f;
    
    spectral_gen_->density() = getParameterValue(params_.inDensity);
    spectral_gen_->spacing() = getParameterValue(params_.inTuning);
    spectral_gen_->spread() = getParameterValue(params_.inSpread);
    spectral_gen_->decay() = vessl::math::lerp(
      decay_min_, decay_max_, getParameterValue(params_.inDecay)
      );
    spectral_gen_->brightness() = getParameterValue(params_.inBrightness);

    crush_ = vessl::math::interp<vessl::math::easing::expo::out>(
      getSampleRate(), crush_rate_min_, getParameterValue(params_.inCrush)
      );
    bit_crusher_.rate() = crush_.getValue();
    

    spectral_gen_->process(audio_left, wet_left);
    wet_left.copy_to(wet_right);

    // vessl::array<float> bcp(left.getData(), left.getSize());
    // bit_crusher_.process(bcp, bcp);

    // if (ReverbEnabled)
    // {
    //   stereo_width_ = getParameterValue(params_.inWidth);
    //   reverb_time_ = 0.35f + 0.6f*getParameterValue(params_.inReverbTime);
    //   reverb_tone_ = Interpolator::linear(0.2f, 0.97f, getParameterValue(params_.inReverbTone));
    //   reverb_blend_ = getParameterValue(params_.inReverbBlend) * 0.56f;
    //
    //   diffuser_->setAmount(stereo_width_);
    //   diffuser_->process(audio, audio);
    //
    //   float meanSpectralMagnitude = spectral_gen_->get_magnitude_mean();
    //   float reverbInputGain = clamp(0.2f - meanSpectralMagnitude, 0.05f, 1.0f);
    //
    //   reverb_->diffusion() = (0.7f);
    //   reverb_->input_gain() = (reverbInputGain);
    //   reverb_->reverb_time() = (reverb_time_);
    //   reverb_->low_pass() = (reverb_tone_);
    //   reverb_->wet_mix() = (reverb_blend_);
    //   // @todo fix this
    //   reverb_->process(audio, audio);
    // }
    
    mix_ = getParameterValue(params_.inMix);
    vessl::sample::mix(audio_left, wet_left, mix_.getValue(), audio_left);
    vessl::sample::mix(audio_right, wet_right, mix_.getValue(), audio_right);

    //setParameterValue(params.outStrumX, strumX);
    //setParameterValue(params.outStrumY, strumY);
  }

#ifdef OWL_GENIUS
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int top = 8;
    const int bottom = screen.getHeight() - 18;
    const int height = bottom - top;
    const int numBands = spectral_gen_->get_string_count();
    for (int b = 0; b < numBands; ++b)
    {
      float freq = spectral_gen_->frequency_of_string(b);
      float x = vessl::math::lerp(0, screen.getWidth() - 1, (float)b / (numBands - 1));
      auto band = spectral_gen_->get_band(freq);
      //band.phase += string_animation_;

      // solid line animation that wobbles back and forth based on amplitude
      //float w = Interpolator::linear(0, 2, band.amplitude);
      //int segments = w > 0 ? 32 : 1;
      //float segLength = (float)height / segments;
      //float py0 = 0;
      //float px0 = x + w * sinf(band.phase);
      //for (int i = 0; i < segments + 1; ++i)
      //{
      //  float py1 = i * segLength;
      //  float s1 = py1 / height * (float)M_PI * 8 + band.phase;
      //  float px1 = x + w * sinf(s1);
      //  screen.drawLine(px0, py0, px1, py1, WHITE);
      //  px0 = px1;
      //  py0 = py1;
      //}

      // same animation, viewed from the side with "pegs" at top and bottom
      screen.drawLine(x, top, x, top + 1, WHITE);
      screen.drawLine(x, bottom - 1, x, bottom, WHITE);
      for (int y = top + 2; y < bottom - 1; ++y)
      {
        float s1 = (float)y / height * M_PI * band.magnitude() * 600; // + band.phase;
        if (fabsf(band.magnitude()*vessl::math::sin<float>(s1)) > 0.004f)
        {
          screen.setPixel(x, y, WHITE);
        }
      }
    }
    //
    // char* bandFirstStr = msg_itoa((int)band_first_, 10);
    // screen.setCursor(0, top);
    // screen.print(bandFirstStr);
    // screen.print(" Hz");
    //
    // char* bandLastStr = msg_itoa((int)band_last_, 10);
    // screen.setCursor(screen.getWidth() - 6 * (strlen(bandLastStr) + 3), top);
    // screen.print(bandLastStr);
    // screen.print(" Hz");

    screen.setCursor(screen.getWidth() / 2 - 16, top);
    //screen.print(highElapsedTime);
    //screen.print(spectralGen->getMagnitudeMean());

    const float dt = 1.0f / 60.0f;
    string_animation_ += dt * M_PI * 4;
    if (string_animation_ > M_PI * 2)
    {
      string_animation_ -= M_PI * 2;
    }
  }
#else
  void processScreen(MonochromeScreenBuffer& screen) override {}
#endif
};