#pragma once

#include "PatchBase.h"
#include "PatchParameterIds.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Evaluator.h"

/** 
 * @todo 6 channels for evaluator so it can output triggers and CV.
 * @todo parameters for controlling VCs.
 */ 
class EvaluatorPatch : public PatchBase
{
  using Eval = Evaluator<float, 2>;
  static constexpr uint8_t kBitsMin = 4;
  static constexpr uint8_t kBitsMax = 24;
  static constexpr uint8_t kBitsDefault = 8;

  struct 
  {
    InputParameterId program = InputParameterId::A;
    InputParameterId bits    = InputParameterId::B;
    InputParameterId rate    = InputParameterId::C;

    InputParameterId v0      = InputParameterId::AA;
    InputParameterId v1      = InputParameterId::AB;
    InputParameterId v2      = InputParameterId::AC;
    InputParameterId v3      = InputParameterId::AD;
    InputParameterId v4      = InputParameterId::AE;
    InputParameterId v5      = InputParameterId::AF;
    InputParameterId v6      = InputParameterId::AG;
    InputParameterId v7      = InputParameterId::AH;
  } params_;
  
  Eval evaluator_;
  bool program_is_valid_;

  struct Preset
  {
    char name[32] = {'\0'};
  };

  static constexpr size_t preset_max_ = 20;
  Preset  presets_[preset_max_];
  size_t  preset_count_;

public:
  EvaluatorPatch() : PatchBase()
  , evaluator_(getSampleRate())
  , preset_count_(0)
  {
    program_is_valid_ = evaluator_.compile("[*] = t*(42&t>>10);");
    evaluator_.bits() = 8;

    registerParameter(params_.program, "Program");
    registerParameter(params_.bits, "Bits");
    setParameterValue(params_.bits, (kBitsDefault - kBitsMin) / static_cast<float>(kBitsMax - kBitsMin));
    registerParameter(params_.rate, "Rate");
    setParameterValue(params_.rate, 1.0f);

    registerParameter(params_.v0, "V0");
    registerParameter(params_.v1, "V1");
    registerParameter(params_.v2, "V2");
    registerParameter(params_.v3, "V3");
    registerParameter(params_.v4, "V4");
    registerParameter(params_.v5, "V5");
    registerParameter(params_.v6, "V6");
    registerParameter(params_.v7, "V7");

    loadPrograms();
  }
  
  ~EvaluatorPatch() = default;

  void load(const Preset& preset)
  {
    char file_name[48];
    size_t len = strlen(preset.name);
    strncpy(file_name, preset.name, len);
    strncpy(file_name + len, ".txt\0", 5);
    if (Resource* resource = getResource(file_name))
    {
      const char* data = static_cast<const char*>(resource->getData());
      program_is_valid_ = evaluator_.compile(data);
    }
  }

  void loadPrograms()
  {
    preset_count_ = 0;

    if (Resource* programs = getResource("programs.txt"))
    {
      const char* data = static_cast<const char*>(programs->getData());
      size_t data_size = programs->getSize() * sizeof(char);
      const char* data_end = data + data_size;
      while(preset_count_ < preset_max_ && data < data_end)
      {
        while(isspace(*data) && data < data_end)
        {
          ++data;
        }

        size_t name_len = 0;
        while(name_len < 31 && data < data_end)
        {
          char c = *data;
          if (isspace(c))
          {
            presets_[preset_count_++].name[name_len] = '\0';
            name_len = 32; // to break out of the loop
          }
          else
          {
            presets_[preset_count_].name[name_len++] = c;
          }
          ++data;
        }
      }
      Resource::destroy(programs);
    }

    if (preset_count_ > 0)
    {
      load(presets_[0]);
    }
  }

  size_t getSelectedProgram()
  {
    return vessl::math::constrain(static_cast<size_t>(getParameterValue(PARAMETER_A)*preset_count_), 0u, preset_count_ - 1);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override 
  {
    if (bid == BUTTON_2 && value == ON)
    {
      size_t pid = getSelectedProgram();
      load(presets_[pid]);
    }
  }
  
  void processAudio(AudioBuffer &audio) override
  {
    uint8_t bits = static_cast<uint8_t>(vessl::math::lerp(kBitsMin, kBitsMax, getParameterValue(params_.bits)));
    evaluator_.bits() = bits;
    evaluator_.rate() = getParameterValue(params_.rate);
    for(size_t v = 0; v < 8; ++v)
    {
      evaluator_.v(v) = getParameterValue(static_cast<PatchParameterId>(params_.v0.id + v));
    }

    if (program_is_valid_)
    {
      AudioBufferReader<2> r(audio);
      AudioBufferWriter<2> w(audio);
      while(r && w)
      {
        w << evaluator_.process(r.read());
      }
      // just cuz this shit tends to be really loud
      audio.multiply(0.5f);
    }
  }

  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.setCursor(0, 10);
    if (program_is_valid_)
    {
      size_t pid = getSelectedProgram();
      screen.print(presets_[pid].name);
      screen.setCursor(0, 20);
      Program::RuntimeError err = evaluator_.last_runtime_error();
      if (err.code)
      {
        screen.print("Error: ");
        screen.print(Program::GetErrorString(err));
        screen.print("\nData: ");
        screen.print((int)err.value);
      }
      else
      {
        screen.print("t=");
        screen.print((int)evaluator_.get('t'));
      }
    }
    else
    {
      screen.print(Program::GetErrorString(evaluator_.last_compile_error()));
    }
  }
};