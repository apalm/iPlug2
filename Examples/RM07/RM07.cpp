#include "RM07.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR
#include "IControls.h"

class DrumPadControl : public IControl, public IVectorBase
{
public:
  DrumPadControl(const IRECT &bounds, const IVStyle &style, int index)
      : IControl(bounds), IVectorBase(style), mIndex(index)
  {
    mDblAsSingleClick = true;
    AttachIControl(this, "");
  }

  void Draw(IGraphics &g) override
  {
    //    g.FillRect(mFlash ? COLOR_WHITE : COLOR_BLACK, mRECT);
    DrawPressableShape(g, EVShape::AllRounded, mRECT, mFlash, mMouseIsOver, false);
    mFlash = false;
  }

  void OnMouseDown(float x, float y, const IMouseMod &mod) override
  {
    TriggerAnimation();
    // IMidiMsg msg;
    // msg.MakeNoteOnMsg(mMidiNoteNumber, 127, 0);
    // GetDelegate()->SendMidiMsgFromUI(msg);

    // Send an arbitrary message instead of a MIDI note-on message, because
    // if this class is instantiated with the MIDI note number, the note number
    // would be stale when the MIDI mapping is changed. It also allows this
    // class to not care about MIDI.
    GetDelegate()->SendArbitraryMsgFromUI(kMessageDrumPadMouseDown, kNoTag, sizeof(mIndex), &mIndex);
  }

  void OnMouseUp(float x, float y, const IMouseMod &mod) override
  {
    // IMidiMsg msg;
    // msg.MakeNoteOffMsg(mMidiNoteNumber, 0);
    // GetDelegate()->SendMidiMsgFromUI(msg);
    GetDelegate()->SendArbitraryMsgFromUI(kMessageDrumPadMouseUp, kNoTag, sizeof(mIndex), &mIndex);
  }

  void TriggerAnimation()
  {
    mFlash = true;
    SetAnimation(SplashAnimationFunc, DEFAULT_ANIMATION_DURATION);
  }

private:
  bool mFlash = false;
  int mIndex;
};
#endif

RM07::RM07(const InstanceInfo &info)
    : Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamGain)->InitDouble("Gain", 100., 0., 100.0, 0.01, "%");
  GetParam(kParamMultiOuts)->InitBool("Multi-outs", false);
  GetParam(kParamMIDIMappingType)->InitInt("MIDI Mapping Type", 0, 0, 1);
#if IPLUG_EDITOR // http://bit.ly/2S64BDd
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, 1.);
  };

  mLayoutFunc = [&](IGraphics *pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(COLOR_WHITE);
    pGraphics->EnableMouseOver(true);
    //    pGraphics->EnableMultiTouch(true);
    // pGraphics->ShowFPSDisplay(true);
    //    pGraphics->EnableLiveEdit(true);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    IRECT b = pGraphics->GetBounds().GetPadded(-5);
    const IRECT buttons = b.ReduceFromTop(50.f);
    const IRECT volumeContainer = b.ReduceFromTop(80.f);
    const IRECT pads = b.GetPadded(-5.f);

    pGraphics->AttachControl(new IVToggleControl(buttons.GetGridCell(0, 1, 4), kParamMultiOuts));
    pGraphics->AttachControl(new IVMeterControl<kNumDrums * 2>(buttons.GetGridCell(1, 1, 4, EDirection::Horizontal, 3), ""), kCtrlTagMeter);
    pGraphics->AttachControl(new IVKnobControl(volumeContainer.GetGridCell(0, 1, 4), kParamGain));
    pGraphics->AttachControl(new IVRadioButtonControl(volumeContainer.GetGridCell(1, 1, 4), kParamMIDIMappingType, {}));
    IVStyle style = DEFAULT_STYLE.WithRoundness(0.1f).WithFrameThickness(3.f);
    int numOfPadRows = 2;
    for (int i = 0; i < kNumDrums; i++)
    {
      IRECT cell = pads.GetGridCell(i, numOfPadRows, kNumDrums / numOfPadRows);
      pGraphics->AttachControl(new DrumPadControl(cell, style, i), i);
    }
  };
#endif
}

#if IPLUG_EDITOR
void RM07::OnMidiMsgUI(const IMidiMsg &msg)
{
  if (GetUI() && msg.StatusMsg() == IMidiMsg::kNoteOn)
  {
    int midiNoteNumber = msg.NoteNumber();
    int padIndex = mDSP.GetPadIndex(midiNoteNumber);
    if (padIndex != -1)
    {
      DrumPadControl *pPad = dynamic_cast<DrumPadControl *>(GetUI()->GetControlWithTag(padIndex));
      pPad->SetSplashPoint(pPad->GetRECT().MW(), pPad->GetRECT().MH());
      pPad->TriggerAnimation();
    }
  }
}

bool RM07::OnMessage(int messageTag, int controlTag, int dataSize, const void *pData)
{
  if (GetUI())
  {
    if (messageTag == kMessageDrumPadMouseDown)
    {
      const int padIndex = *static_cast<const int *>(pData);
      std::vector<int> midiMapping = mDSP.GetMIDIMapping();
      int midiNoteNumber = midiMapping[padIndex];
      IMidiMsg msg;
      msg.MakeNoteOnMsg(midiNoteNumber, 127, 0);
      SendMidiMsgFromUI(msg);
    }
    else if (messageTag == kMessageDrumPadMouseUp)
    {
      const int padIndex = *static_cast<const int *>(pData);
      std::vector<int> midiMapping = mDSP.GetMIDIMapping();
      int midiNoteNumber = midiMapping[padIndex];
      IMidiMsg msg;
      msg.MakeNoteOffMsg(midiNoteNumber, 0);
      SendMidiMsgFromUI(msg);
    }
  }
}
#endif

#if IPLUG_DSP
void RM07::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String &str) const
{
  if (direction == ERoute::kOutput)
  {
    if (busIdx >= 0 && busIdx < kNumDrums)
    {
      str.Set(("Drum" + std::to_string(busIdx + 1)).c_str());
    }

    return;
  }

  str.Set("");
}

void RM07::ProcessBlock(sample **inputs, sample **outputs, int nFrames)
{
  const double gain = GetParam(kParamGain)->Value() / 100.;
  const int nChans = NOutChansConnected();

  mDSP.ProcessBlock(outputs, nFrames);

  for (auto s = 0; s < nFrames; s++)
  {
    for (auto c = 0; c < nChans; c++)
    {
      outputs[c][s] = outputs[c][s] * gain;
    }
  }

  mSender.ProcessBlock(outputs, nFrames, kCtrlTagMeter);
}

void RM07::OnIdle()
{
  mSender.TransmitData(*this);
}

void RM07::OnReset()
{
  mDSP.Reset(GetSampleRate(), GetBlockSize());
}

void RM07::ProcessMidiMsg(const IMidiMsg &msg)
{
  TRACE;

  int status = msg.StatusMsg();

  switch (status)
  {
  case IMidiMsg::kNoteOn:
  case IMidiMsg::kNoteOff:
  {
    goto handle;
  }
  default:
    return;
  }

handle:
  mDSP.ProcessMidiMsg(msg);
  SendMidiMsg(msg);
}

void RM07::OnParamChange(int paramIdx)
{
  switch (paramIdx)
  {
  case kParamMultiOuts:
    mDSP.SetMultiOut(GetParam(paramIdx)->Bool());
    break;
  case kParamMIDIMappingType:
    mDSP.SetMIDIMapping(GetParam(paramIdx)->Int());
  default:
    break;
  }
}

bool RM07::GetMidiNoteText(int noteNumber, char *name) const
{
  int pitch = noteNumber % 12;

  WDL_String str;
  str.SetFormatted(32, "Drum %i", pitch);

  strcpy(name, str.Get());

  //  switch (pitch)
  //  {
  //    case 0:
  //      strcpy(name, "drum 1");
  //      return true;
  //    default:
  //      *name = '\0';
  //      return false;
  //  }

  return true;
}
#endif
