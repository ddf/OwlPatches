/**

AUTHOR:
    (c) 2023 Damien Quartz

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

#include "SpectralSynth.h"
#include "Diffuser.h"
#include "Reverb.h"
#include "Frequency.h"
#include "vessicle/vessl/vessl.h"

struct SpectralSympathiesParameterIds
{
  PatchParameterId inHarpFundamental; // = PARAMETER_A;
  PatchParameterId inHarpOctaves; // = PARAMETER_B;
  PatchParameterId inDensity; // = PARAMETER_C;
  PatchParameterId inTuning; // PARAMETER_D;
  PatchParameterId inDecay; // = PARAMETER_E;
  PatchParameterId inSpread; // = PARAMETER_F;
  PatchParameterId inBrightness; // = PARAMETER_G;
  PatchParameterId inCrush; // = PARAMETER_H;
  PatchParameterId inFeedback;

  PatchParameterId inWidth; // = PARAMETER_AA;
  PatchParameterId inReverbBlend; // = PARAMETER_AB;
  PatchParameterId inReverbTime; // = PARAMETER_AC;
  PatchParameterId inReverbTone; // = PARAMETER_AD;

  PatchParameterId outStrumX; // = PARAMETER_AE;
  PatchParameterId outStrumY; // = PARAMETER_AF;
};

#ifdef OWL_GENIUS
static const SpectralSympathiesParameterIds genius_params =
{
  .inHarpFundamental = PARAMETER_CA,
  .inHarpOctaves = PARAMETER_CB,
  .inDensity = PARAMETER_A,
  .inTuning = PARAMETER_E,
  .inDecay = PARAMETER_F,
  .inSpread = PARAMETER_B,
  .inBrightness = PARAMETER_G,
  .inCrush = PARAMETER_H,
  .inFeedback = PARAMETER_AB,

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
  .inHarpFundamental = PARAMETER_A,
  .inHarpOctaves = PARAMETER_B,
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

template<size_t SpectrumSize, bool ReverbEnabled>
class SpectralSympathiesBase : public MonochromeScreenPatch
{
  using sample_t = float;
  using complex_t = vessl::transform::complex<sample_t>;
  using SpectralGen = SpectralSynth<SpectrumSize, false>;
  using BitCrush = vessl::processors::bitcrush<sample_t, 24>;
  using ReverbProcessor = Reverb<sample_t>;
  using SampleArray = vessl::array<sample_t>;
  using ComplexArray = vessl::array<complex_t>;
  using Window = vessl::sample::windows::type;
  using FFT = vessl::transform::fft<sample_t>;

  SpectralSympathiesParameterIds params_;

  float spread_max_ = 1.0f;
  float decay_min_;
  float decay_max_;
  float decay_default_ = 0.5f;
  float density_min_ = 64;
  float density_max_ = static_cast<float>(SpectrumSize)/4;
  float crush_rate_min_ = 1000.0f;
  float string_animation_;

  int input_buffer_write_;
  SampleArray input_buffer_;
  SampleArray input_window_;
  SampleArray input_analyze_;
  ComplexArray input_spectrum_;
  ComplexArray feedback_spectrum_;
  FFT input_transform_;

  SpectralGen* spectral_gen_;
  Diffuser* diffuser_;
  ReverbProcessor* reverb_;

  BitCrush bit_crusher_;

  int        pluck_at_sample_;
  int        gate_on_at_sample_;
  int        gate_off_at_sample_;
  bool       gate_state_;
  StiffFloat band_first_;
  StiffFloat band_last_;
  SmoothFloat spread_;
  SmoothFloat decay_;
  SmoothFloat brightness_;
  SmoothFloat feedback_;
  SmoothFloat volume_;
  SmoothFloat crush_;
  SmoothFloat lin_log_lerp_;
  SmoothFloat band_density_;
  SmoothFloat stereo_width_;
  SmoothFloat reverb_time_;
  SmoothFloat reverb_tone_;
  SmoothFloat reverb_blend_;

  MidiMessage* midi_notes_;

public:
  explicit SpectralSympathiesBase() 
    : MonochromeScreenPatch()
#ifdef OWL_WITCH
    , params_(witch_params)
#endif
#ifdef OWL_GENIUS
    , params_(genius_params)
#endif
    , decay_min_(static_cast<float>(SpectrumSize)*0.5f / getSampleRate())
    , decay_max_(3.5f)
    , string_animation_(0)
    , input_buffer_write_(0)
    , input_buffer_(new sample_t[SpectrumSize], SpectrumSize)
    , input_window_(new sample_t[SpectrumSize], SpectrumSize)
    , input_analyze_(new sample_t[SpectrumSize], SpectrumSize)
    , input_spectrum_(new complex_t[SpectrumSize/2], SpectrumSize/2)
    , feedback_spectrum_(new complex_t[SpectrumSize/2], SpectrumSize/2)
    , input_transform_(SpectrumSize)
    , bit_crusher_(getSampleRate(), getSampleRate())
    , pluck_at_sample_(-1)
    , gate_on_at_sample_(-1)
    , gate_off_at_sample_(-1)
    , gate_state_(false)
  {
    band_first_.delta = 1.0f;
    band_last_.delta = 1.0f;
    
    spectral_gen_ = SpectralGen::create(getSampleRate());
    vessl::sample::windows::render(Window::hann, input_window_);

    if (ReverbEnabled)
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
    registerParameter(params_.inHarpFundamental, "Fundamentl");
    registerParameter(params_.inHarpOctaves, "Octaves");
    registerParameter(params_.inDensity, "Density");
    registerParameter(params_.inTuning, "Tuning");
    registerParameter(params_.inFeedback, "Feedback");
    if (ReverbEnabled)
    {
      registerParameter(params_.inWidth, "Width");
      registerParameter(params_.inReverbTime, "Verb Time");
      registerParameter(params_.inReverbTone, "Verb Tone");
      registerParameter(params_.inReverbBlend, "Verb Blend");
    }

    registerParameter(params_.outStrumX, "Strum X>");
    registerParameter(params_.outStrumY, "Strum Y>");

    setParameterValue(params_.inHarpFundamental, 0.0f);
    setParameterValue(params_.inHarpOctaves, 1.0f);
    setParameterValue(params_.inDecay, (decay_default_ - decay_min_) / (decay_max_ - decay_min_));
    setParameterValue(params_.inDensity, 1.0f);
    setParameterValue(params_.inSpread, 0.0f);
    setParameterValue(params_.inBrightness, 0.0f);
    setParameterValue(params_.inCrush, 0.0f);
    setParameterValue(params_.inTuning, 1.0f);
    setParameterValue(params_.inFeedback, 0.0f);

    if (ReverbEnabled)
    {
      setParameterValue(params_.inReverbTone, 1.0f);
    }
  }

  ~SpectralSympathiesBase() override
  {
    delete[] feedback_spectrum_.data();
    delete[] input_buffer_.data();
    delete[] input_analyze_.data();
    delete[] input_spectrum_.data();
    SpectralGen::destroy(spectral_gen_);
    if (ReverbEnabled)
    {
      Diffuser::destroy(diffuser_);
      ReverbProcessor::destroy(reverb_);
    }
    delete[] midi_notes_;
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    if ((bid == PUSHBUTTON || bid == BUTTON_1) && value == Patch::ON)
    {
      pluck_at_sample_ = samples;
    }

    if (bid == BUTTON_2)
    {
      if (value == Patch::ON)
      {
        gate_on_at_sample_ = samples;
      }
      else
      {
        gate_off_at_sample_ = samples;
      }
    }
  }

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
    SampleArray left(audio.getSamples(0), block_size);
    SampleArray right(audio.getSamples(1), block_size);
    
    constexpr float octaves_min = 0.5f;
    constexpr float octaves_max = 2.f;
    
    const float center  = vessl::math::lerp(100.f, 8000.f, getParameterValue(params_.inHarpFundamental));
    const float width = vessl::math::lerp(center * octaves_min, center * octaves_max, getParameterValue(params_.inHarpOctaves));
    band_first_ = spectral_gen_->get_band_frequency(2); // vessl::math::constrain(center - width, spectral_gen_->get_band_frequency(1), center);
    band_last_ = getSampleRate()*0.49f; // vessl::math::constrain(center + width, center, getSampleRate()*0.49f);
    //band_last_ = 20000.f - (1.f - getParameterValue(params_.inHarpOctaves))*fundamental*octaves_max_*64;
    float band_first_idx = spectral_gen_->get_band_index(band_first_.getValue());
    float band_last_idx = spectral_gen_->get_band_index(band_last_.getValue());
    band_density_ = vessl::math::lerp(density_min_, vessl::math::min(band_last_idx - band_first_idx, density_max_), getParameterValue(params_.inDensity));
    lin_log_lerp_ = getParameterValue(params_.inTuning);

    spread_ = vessl::math::interp<vessl::math::easing::quad::out>(0.f, 1.f, getParameterValue(params_.inSpread));
    decay_ = vessl::math::lerp(decay_min_, decay_max_, getParameterValue(params_.inDecay));
    brightness_ = getParameterValue(params_.inBrightness);
    feedback_ = getParameterValue(params_.inFeedback)*0.5f;
    crush_ = vessl::math::interp<vessl::math::easing::expo::out>(getSampleRate(), crush_rate_min_, getParameterValue(params_.inCrush));

    // reduce volume based on combination of decay, spread, and brightness parameters
    volume_ = vessl::math::interp<vessl::math::easing::expo::out>(1.0f, 0.5f, 0.6f*getParameterValue(params_.inDecay)
      + 0.2f*getParameterValue(params_.inSpread)
      + 0.2f*getParameterValue(params_.inBrightness));
    
    spread_max_ = vessl::math::lerp(SpectrumSize/4.f, SpectrumSize/64.f, getParameterValue(params_.inDensity));

    spectral_gen_->spread() = spread_.getValue();
    spectral_gen_->set_spread_bands_max(spread_max_);
    spectral_gen_->decay() = vessl::duration_t::from_seconds(decay_.getValue(), getSampleRate());
    spectral_gen_->brightness() = brightness_.getValue();
    spectral_gen_->volume() = volume_.getValue();
    bit_crusher_.rate() = crush_.getValue();

    // TODO: this needs to dynamically adjust to band density somehow,
    // but using band density directly doesn't work. it's more to do with 
    // the distance between bands that we are exciting.
    // when band step is small, attenuation needs to also be small.
    // probably this means applying attenuation to inputAnalyze instead of while we record.
    const int string_count = vessl::math::max(get_string_count(), 1);
    constexpr float mag_norm = 256.f / static_cast<float>(SpectrumSize);
    const float feed_scale = feedback_.getValue();
    for (int i = 0; i < block_size; ++i)
    {
      input_buffer_[input_buffer_write_++] = left[i];
      if (input_buffer_write_ == SpectrumSize)
      {
        // window the input and output to an analysis buffer
        // because running the fft messes up the input samples.
        input_window_.multiply(input_buffer_, input_analyze_);
        input_transform_.forward(input_analyze_, input_spectrum_);
        
        // transfer spectrum data from input analysis to spectral_gen
        // by sampling only those frequencies represented by our strings.
        // i.e. comb filter it.
        for (int si = 0; si < string_count; ++si)
        {
          const float freq = frequency_of_string(si);
          const int bi = spectral_gen_->get_band_index(freq);
          if (bi > 0 && bi < input_spectrum_.size())
          {
            complex_t input = input_spectrum_[bi];
            const int fi = spectral_gen_->get_band_index(freq*0.5f);
            if (fi > 0 && fi < input_spectrum_.size())
            {
              complex_t feed = input*feedback_spectrum_[fi];
              input = vessl::math::lerp(input, feed, feed_scale);
            }
            const float in_mag = input.magnitude() * mag_norm;
            const float in_phase = 0; // input_spectrum_[b].phase();
            spectral_gen_->excite(bi, in_mag, in_phase);
          }
        }
        
        input_spectrum_.copy_to(feedback_spectrum_);
        
        // map the full spectrum to our selected strings
        // for (int ii = 1; ii < input_spectrum_.size(); ++ii)
        // {
        //   float it = static_cast<float>(ii - 1) / (input_spectrum_.size()-1);
        //   int si = vessl::math::round(it*string_count);
        //   const float freq = frequency_of_string(si);
        //   int gi = spectral_gen_->get_band_index(freq);
        //   if (gi > 0 && gi < input_spectrum_.size())
        //   {
        //     const float in_mag = input_spectrum_[ii].magnitude() * mag_norm;
        //     //spectral_gen_->excite(gi, in_mag, 0);
        //     auto& band = spectral_gen_->get_band(freq);
        //     if (band.amplitude*band.decay < in_mag)
        //     {
        //       spectral_gen_->pluck(freq, in_mag);
        //     }
        //   }
        // }
        
        // copy the back half of the array to the front half
        // continue recording input from the middle of the array.
        // doing this means we can update the spectral data for sound generation every overlap.
        input_buffer_write_ = SpectrumSize / 2;
        SampleArray input_buffer_back(input_buffer_.data() + input_buffer_write_, input_buffer_write_);
        input_buffer_back.copy_to(input_buffer_);
      }
    }

    spectral_gen_->generate(left);

    // vessl::array<float> bcp(left.getData(), left.getSize());
    // bit_crusher_.process(bcp, bcp);

    left.copy_to(right);

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

    //setParameterValue(params.outStrumX, strumX);
    //setParameterValue(params.outStrumY, strumY);
  }

#ifdef OWL_GENIUS
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    const int top = 8;
    const int bottom = screen.getHeight() - 18;
    const int height = bottom - top;
    const int numBands = get_string_count();
    for (int b = 0; b < numBands; ++b)
    {
      float freq = frequency_of_string(b);
      float x = vessl::math::lerp(0, screen.getWidth() - 1, (float)b / (numBands - 1));
      auto& band = spectral_gen_->get_band(freq);
      band.phase += string_animation_;

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
        float s1 = (float)y / height * M_PI * band.amplitude * 600 + band.phase;
        if (fabsf(band.amplitude*vessl::math::sin<float>(s1)) > 0.004f)
        {
          screen.setPixel(x, y, WHITE);
        }
      }
    }

    char* bandFirstStr = msg_itoa((int)band_first_, 10);
    screen.setCursor(0, top);
    screen.print(bandFirstStr);
    screen.print(" Hz");

    char* bandLastStr = msg_itoa((int)band_last_, 10);
    screen.setCursor(screen.getWidth() - 6 * (strlen(bandLastStr) + 3), top);
    screen.print(bandLastStr);
    screen.print(" Hz");

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

protected:
  // get the current string count based on the density setting
  int get_string_count()
  {
    return static_cast<int>(band_density_.getValue() + 0.5f);
  }

  float frequency_of_string(const int string_num)
  {
    const float t = static_cast<float>(string_num) / get_string_count();
    // convert first and last bands to midi notes and then do a linear interp, converting back to Hz at the end.
    const Frequency low_freq = Frequency::ofHertz(band_first_.getValue());
    const Frequency hi_freq = Frequency::ofHertz(band_last_.getValue());
    const float lin_freq = vessl::math::lerp(low_freq.asHz(), hi_freq.asHz(), t);
    const float midi_note = vessl::math::lerp(low_freq.asMidiNote(), hi_freq.asMidiNote(), t);
    const float log_freq = Frequency::ofMidiNote(midi_note).asHz();
    // we lerp from logFreq up to linFreq because log spacing clusters frequencies
    // towards the bottom of the range, which means that when holding down the mouse on a string
    // and lowering this param, you'll hear the pitch drop, which makes more sense than vice-versa.
    return vessl::math::lerp(log_freq, lin_freq, lin_log_lerp_.getValue());
  }

private:
  //void pluck(SpectralGen* spectrum, float location, float amp)
  //{
  //  const int   numBands = getStringCount();
  //  const int   band = roundf(Interpolator::linear(0, numBands, location));
  //  const float freq = frequencyOfString(band);
  //  spectrum->pluck(freq, amp);
  //}

  //void pluck(SpectralGen* spectrum, MidiMessage msg)
  //{
  //  float freq = Frequency::ofMidiNote(msg.getNote()).asHz();
  //  float amp = msg.getVelocity() / 127.0f;
  //  spectrum->pluck(freq, amp);
  //}
};

#ifdef OWL_WITCH
typedef SpectralSympathiesBase<2048,false> SpectralSympathiesPatch;
#endif

#ifdef OWL_GENIUS
typedef SpectralSympathiesBase<2048,false> SpectralSympathiesPatch;
#endif