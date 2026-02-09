#ifndef __FREQUENCY_H__
#define __FREQUENCY_H__

#include "basicmaths.h"

#define HZA4       440.0f
#define MIDIA4     69.0f
#define MIDIOCTAVE 12.0f

struct Frequency
{
  static Frequency ofHertz(float hz) 
  { 
    return Frequency(hz);
  }

  static Frequency ofMidiNote(float midiNote)
  {
    float hz = HZA4 * powf(2.0f, (midiNote - MIDIA4) / MIDIOCTAVE);
    return ofHertz(hz);
  }

  float asMidiNote() const
  {
    float midiNote = MIDIA4 + MIDIOCTAVE * (float)logf(mHz / HZA4) / (float)logf(2.0);
    return midiNote;
  }

  float asHz() const { return mHz; }

private:
  float mHz;
  Frequency(float hz) : mHz(hz) {}
};

#endif // __FREQUENCY_H__
