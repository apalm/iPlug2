#pragma once

#include "Oscillator.h"
#include "ADSREnvelope.h"
#include <vector>

static constexpr int kNumDrums = 8;
static constexpr double kStartFreq = 300.;     //Hz
static constexpr double kFreqDiff = 100.;      //Hz
static constexpr double kPitchEnvRange = 100.; //Hz
static constexpr double kAmpDecayTime = 300;   //Ms
static constexpr double kPitchDecayTime = 50.; //Ms

// Track/pad to MIDI note number mapping
std::vector<int> whiteKeyMIDINoteMapping{36, 38, 40, 41, 43, 45, 47, 48};
std::vector<int> chromaticMIDINoteMapping{36, 37, 38, 39, 40, 41, 42, 43};

using namespace iplug;

class DrumSynthDSP
{
public:
  struct DrumVoice
  {
    ADSREnvelope<sample> mPitchEnv{"pitch", nullptr, false};
    ADSREnvelope<sample> mAmpEnv{"amp", nullptr, false};
    FastSinOscillator<sample> mOsc;
    double mBaseFreq;

    DrumVoice(double baseFreq) : mBaseFreq(baseFreq)
    {
      mAmpEnv.SetStageTime(ADSREnvelope<sample>::kAttack, 0.);
      mAmpEnv.SetStageTime(ADSREnvelope<sample>::kDecay, kAmpDecayTime);

      mPitchEnv.SetStageTime(ADSREnvelope<sample>::kAttack, 0.);
      mPitchEnv.SetStageTime(ADSREnvelope<sample>::kDecay, kPitchDecayTime);
    }

    inline sample Process()
    {
      return mOsc.Process(mBaseFreq + mPitchEnv.Process()) * mAmpEnv.Process();
    }

    void Trigger(double amp)
    {
      mOsc.Reset();
      mPitchEnv.Start(amp * kPitchEnvRange);
      mAmpEnv.Start(amp);
    }

    inline bool IsActive() const
    {
      return mAmpEnv.GetBusy();
    }
  };

  DrumSynthDSP()
  //  : mMidiQueue(IMidiMsg::QueueSize(DEFAULT_BLOCK_SIZE, DEFAULT_SAMPLE_RATE))
  {
    for (int d = 0; d < kNumDrums; d++)
    {
      mDrums.push_back(DrumVoice(kStartFreq + (d * kFreqDiff)));
    }
  }

  void Reset(double sampleRate, int blockSize)
  {
    mMidiQueue.Resize(blockSize);
    //    mMidiQueue.Resize(IMidiMsg::QueueSize(blockSize, sampleRate));
  }

  void ProcessBlock(sample **outputs, int nFrames)
  {
    for (int s = 0; s < nFrames; s++)
    {
      while (!mMidiQueue.Empty())
      {
        IMidiMsg &msg = mMidiQueue.Peek();
        if (msg.mOffset > s)
        {
          break;
        }

        if (msg.StatusMsg() == IMidiMsg::kNoteOn && msg.Velocity())
        {
          int padIndex = GetPadIndex(msg.NoteNumber());
          if (padIndex != -1)
          {
            mDrums[padIndex].Trigger(msg.Velocity() / 127.f);
          }
        }

        mMidiQueue.Remove();
      }

      if (mMultiOut)
      {
        int channel = 0;
        for (int d = 0; d < kNumDrums; d++)
        {
          outputs[channel][s] = 0.;

          if (mDrums[d].IsActive())
          {
            outputs[channel][s] = mDrums[d].Process();
          }

          outputs[channel + 1][s] = outputs[channel][s];

          channel += 2;
        }
      }
      else
      {
        outputs[0][s] = 0.;

        for (int d = 0; d < kNumDrums; d++)
        {
          if (mDrums[d].IsActive())
          {
            outputs[0][s] += mDrums[d].Process();
          }
        }

        outputs[1][s] = outputs[0][s];
      }
    }
    mMidiQueue.Flush(nFrames);
  }

  void ProcessMidiMsg(const IMidiMsg &msg)
  {
    mMidiQueue.Add(msg);
  }

  void SetMultiOut(bool multiOut)
  {
    mMultiOut = multiOut;
  }

  std::vector<int> GetMIDIMapping()
  {
    return mMIDIMapping;
  }

  int GetPadIndex(int midiNoteNumber)
  {
    std::vector<int>::iterator it;
    it = std::find(mMIDIMapping.begin(), mMIDIMapping.end(), midiNoteNumber);
    // Found MIDI note number
    if (it != mMIDIMapping.end())
    {
      int index = it - mMIDIMapping.begin();
      return index;
    }
    else
    {
      return -1;
    }
  }

  void SetMIDIMapping(int midiMappingType)
  {
    mMIDIMapping = midiMappingType == 0 ? whiteKeyMIDINoteMapping : chromaticMIDINoteMapping;
  }

private:
  bool mMultiOut = false;
  std::vector<int> mMIDIMapping;
  std::vector<DrumVoice> mDrums;
  IMidiQueue mMidiQueue;
};
