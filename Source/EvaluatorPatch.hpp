#pragma once

#include "PatchBase.h"
#include "PatchParameterIds.h"
#include "AudioBufferSourceSink.h"
#include "vessicle/Evaluator.h"

/** 
 * @todo 6 channels for evaluator so it can output triggers and CV.
 */ 
class EvaluatorPatch : public PatchBase
{
  using Eval = Evaluator<float, 2>;
  using Smoother = vessl::math::easing::smoother<float>;
  static constexpr float kBitsMin = 4;
  static constexpr float kBitsMax = 24;
  static constexpr float kBitsDefault = 12;
  static constexpr size_t kInitPid = -1;
  static constexpr char kInitProgram[] = "[*] = t*(42&t>>10);";

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
  StiffFloat bits_;
  size_t loaded_pid_;
  size_t load_request_pid_;
  bool program_is_valid_; // true if the most recently compiled program is valid
  Program::Compiler* compiler_ = nullptr;

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
  , loaded_pid_(kInitPid)
  , preset_count_(0)
  {
    bits_.delta = 0.0025f;
    program_is_valid_ = evaluator_.compile(kInitProgram);
    evaluator_.bits() = kBitsDefault;

    registerParameter(params_.program, "Program");
    registerParameter(params_.bits, "Bits");
    setParameterValue(params_.bits, (kBitsDefault - kBitsMin) / static_cast<float>(kBitsMax - kBitsMin));
    registerParameter(params_.rate, "Rate");
    setParameterValue(params_.rate, 0.5f);

    registerParameter(params_.v0, "V0");
    registerParameter(params_.v1, "V1");
    registerParameter(params_.v2, "V2");
    registerParameter(params_.v3, "V3");
    registerParameter(params_.v4, "V4");
    registerParameter(params_.v5, "V5");
    registerParameter(params_.v6, "V6");
    registerParameter(params_.v7, "V7");

    loadPrograms();
    load_request_pid_ = preset_count_;
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
      //program_is_valid_ = evaluator_.compile(data);
      compiler_ = Eval::make_compiler(data);
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
  }

  size_t getSelectedProgram()
  {
    return vessl::math::constrain(static_cast<size_t>(getParameterValue(PARAMETER_A)*preset_count_), 0u, preset_count_ - 1);
  }

  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override 
  {
    if (bid == BUTTON_2 && value == ON)
    {
      load_request_pid_ = getSelectedProgram();
    }
  }
  
  void processAudio(AudioBuffer &audio) override
  {
    bits_ = getParameterValue(params_.bits);
    float bits = vessl::math::lerp(kBitsMin, kBitsMax, bits_.getValue());
    evaluator_.bits() = bits; // (int)(bits*100)/100;
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
    // remove default parameter UI
    screen.clear();

    screen.setCursor(0, 10);
    if (load_request_pid_ < preset_count_)
    {
      screen.print("loading:");
      screen.print(presets_[load_request_pid_].name);
      if (compiler_ == nullptr)
      {
        load(presets_[load_request_pid_]);
      }
      else
      {
        if (compiler_->IsDone())
        {
          program_is_valid_ = evaluator_.load(compiler_);
          Program::Destroy(compiler_);
          compiler_ = nullptr;

          loaded_pid_ = load_request_pid_;
          load_request_pid_ = preset_count_;
        }
        else
        {
          compiler_->Tick();

          static char anim[][2] = {"|","/","-","\\" };
          static size_t f = 0;
          screen.print(" ");
          screen.print(anim[f]);
          f = (f+1)%4;
        }
      }
    }
    else if (program_is_valid_)
    {
      if (loaded_pid_ == kInitPid)
      {
        screen.print("init");
      }
      else
      {
        screen.print(presets_[loaded_pid_].name);
      }
      screen.print(" (IC:");
      screen.print((int)evaluator_.instruction_count());
      screen.print(")");
      screen.setCursor(0, 20);
      if (Program::RuntimeError err = evaluator_.last_runtime_error())
      {
        screen.print("Error: ");
        screen.print(Program::GetErrorString(err));
        screen.print("\nData: ");
        screen.print((int)err.value);
      }
      else
      {
        screen.print("t:");
        screen.print((int)evaluator_.get('t'));
        
        screen.setCursor(0, 30);
        screen.print("m:");
        screen.print((int)evaluator_.get('m'));
        screen.setCursor(screen.getWidth()/2, 30);
        screen.print("q:");
        screen.print((int)evaluator_.get('q'));

        screen.setCursor(0, 40);
        screen.print("w:");
        screen.print((int)evaluator_.get('w'));
        screen.setCursor(screen.getWidth()/2, 40);
        screen.print("~:");
        screen.print((int)(evaluator_.get('~')*evaluator_.rate().read_analog()));
      }
    }
    else
    {
      screen.print(Program::GetErrorString(evaluator_.last_compile_error()));
      screen.print("\n Loc:");
      screen.print(evaluator_.last_compile_error_position());
    }
    
    if (load_request_pid_ == preset_count_)
    {
      size_t pid = getSelectedProgram();
      screen.setCursor(0, screen.getHeight());
      screen.print("load: ");
      screen.print(presets_[pid].name);
    }
  }
};