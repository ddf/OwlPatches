#pragma once

#include "PatchBase.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Evaluator.h"


/** 
 * @todo loading program text from Resources
 * @todo 6 channels for evaluator so it can output triggers and CV.
 * @todo parameters for controlling VCs.
 */ 
class EvaluatorPatch : public PatchBase
{
  using Eval = Evaluator<float, 2>;
  static constexpr size_t kEvalMemSize = 4096;
  static constexpr uint8_t kBitsMin = 4;
  static constexpr uint8_t kBitsMax = 24;
  static constexpr uint8_t kBitsDefault = 8;
  
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
    program_is_valid_ = evaluator_.compile("[*] = t*(42&t>>10);", kEvalMemSize);
    evaluator_.bits() = 8;

    registerParameter(PARAMETER_A, "Program");
    registerParameter(PARAMETER_B, "Bits");
    setParameterValue(PARAMETER_B, (kBitsDefault - kBitsMin) / static_cast<float>(kBitsMax - kBitsMin));
    registerParameter(PARAMETER_C, "Rate");
    setParameterValue(PARAMETER_C, 1.0f);

    loadPrograms();
  }
  
  ~EvaluatorPatch() = default;

  void load(const Preset& preset)
  {
    char file_name[48];
    sprintf(file_name, "%s.txt", preset.name);
    if (Resource* resource = getResource(file_name))
    {
      const char* data = static_cast<const char*>(resource->getData());
      program_is_valid_ = evaluator_.compile(data, kEvalMemSize);
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
    uint8_t bits = static_cast<uint8_t>(vessl::math::lerp(kBitsMin, kBitsMax, getParameterValue(PARAMETER_B)));
    evaluator_.bits() = bits;
    evaluator_.rate() = getParameterValue(PARAMETER_C);

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
    }
    else
    {
      screen.print(Program::GetErrorString(evaluator_.last_compile_error()));
    }
  }
};