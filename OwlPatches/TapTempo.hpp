/**
  This class originally came from https://github.com/Befaco/Lich_Patches/blob/main/PingPong/TapTempo.hpp
**/

#ifndef __TapTempo_hpp__
#define __TapTempo_hpp__

// #define TAP_THRESHOLD     64// 256 // 78Hz at 20kHz sampling rate, or 16th notes at 293BPM

template<uint32_t TRIGGER_LIMIT>
class TapTempo 
{
private:
  uint32_t limit;
  uint32_t trig;
  uint16_t speed;
  bool bIsOn;
public:
  explicit TapTempo(const uint32_t tempo) : 
    limit(tempo), trig(TRIGGER_LIMIT), 
    speed(2048), bIsOn(false) {}

  void trigger(const bool on)
  {
    trigger(on, 0);
  }

  bool isOn() const
  {
    return bIsOn;
  }

  void trigger(const bool on, const int delay)
  {
    // if(trig < TAP_THRESHOLD)
    //   return;
    if(on && !bIsOn)
    {
      if(trig < TRIGGER_LIMIT)
      {
        // TODO: needs to be clamped I think, like in clock below.
        limit = trig + delay;
      }
      trig = 0;
//      debugMessage("limit/delay", (int)limit, (int)delay);
    }
    bIsOn = on;
  }

  void setLimit(const uint32_t value)
  {
    limit = value;
  }

  void setSpeed(const int16_t s)
  {
    if(abs(speed-s) > 16){
      const int64_t delta = static_cast<int64_t>(limit)*(speed-s)/2048;
      limit = max(static_cast<int64_t>(1), limit+delta);
      speed = s;
    }
  }

  float getPeriod() const
  {
    return static_cast<float>(limit)/TRIGGER_LIMIT;
  }

  float getFrequency() const
  {
    return TRIGGER_LIMIT/static_cast<float>(limit);
  }

  void clock()
  {
    if(trig < TRIGGER_LIMIT)
      trig++;
  }

  void clock(const uint32_t steps)
  {
    trig += steps;
    trig = min(trig, TRIGGER_LIMIT);
  }
};

#endif   // __TapTempo_hpp__
