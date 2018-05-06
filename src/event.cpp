/*
** Copyright (C) 2004 Jesse Chappell <jesse@essej.net>
**              and Benno Senoner and Christian Schoenebeck
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
**
*/

#include "event.hpp"
#include <iostream>
#include <stdint.h>
#include <sys/time.h>

using namespace std;

namespace SooperLooper {

    /**
     * Create an EventGenerator.
     *
     * @param SampleRate - sample rate of the sampler engine's audio output
     *                     signal (in Hz)
     */
    EventGenerator::EventGenerator(uint32_t sampleRate) {
        uiSampleRate       = sampleRate;
        uiSamplesProcessed = 0;
        fragmentTime.end   = createTimeStamp();
    }

    /**
     * Updates the time stamps for the beginning and end of the current audio
     * fragment. This is needed to be able to calculate the respective sample
     * point later to which an event belongs to.
     *
     * @param SamplesToProcess - number of sample points to process in this
     *                           audio fragment cycle
     */
    void EventGenerator::updateFragmentTime(uint32_t samplesToProcess)
    {
        // update time stamp for this audio fragment cycle
        fragmentTime.begin = fragmentTime.end;
        fragmentTime.end   = createTimeStamp();
        // recalculate sample ratio for this audio fragment
        time_stamp_t fragmentDuration = fragmentTime.end - fragmentTime.begin;
        fragmentTime.sample_ratio = (double) uiSamplesProcessed / (double) fragmentDuration;

	//cerr << "begin: " << fragmentTime.begin << " end: " << fragmentTime.end << "  ratio: " << fragmentTime.sample_ratio << endl;
	// store amount of samples to process for the next cycle
        uiSamplesProcessed = samplesToProcess;
    }

    /**
     * Create a new event with the current time as time stamp.
     */
	Event EventGenerator::createEvent(long fragTime)
	{
		if (fragTime < 0) {
			return Event(this, createTimeStamp());
		}
		else {
			return Event(this, (int) fragTime);
		}
	}

	Event EventGenerator::createTimestampedEvent(time_stamp_t timeStamp)
	{
		return Event(this, timeStamp);
	}


// 	Event EventGenerator::createEvent()
//     {
// 	    time_stamp_t ts = createTimeStamp();
// 	    cerr << "ts is : " << ts << endl;
// 	    uint32_t pos = toFragmentPos(ts);
// 	    cerr << "event pos: " << pos << endl;
// 	    return Event(pos);
//     }

    /**
     * Creates a real time stamp for the current moment.
     */
    EventGenerator::time_stamp_t EventGenerator::createTimeStamp()
    {
	    struct timeval tv;
	    gettimeofday(&tv, NULL);

	    return (time_stamp_t)(tv.tv_usec * 1e-6) + (time_stamp_t)(tv.tv_sec);
    }

    /**
     * Will be called by an EventGenerator to create a new Event.
     */
    Event::Event(EventGenerator* pGenerator, time_stamp_t Time)
      : Type(type_cmd_down),Command(UNKNOWN),Control(Unknown),Instance(0), Value(0)
      {
        pEventGenerator = pGenerator;
        TimeStamp       = Time;
        iFragmentPos    = -1;
    }

    Event::Event(EventGenerator* pGenerator, int fragmentpos)
      : Type(type_cmd_down),Command(UNKNOWN),Control(Unknown),Instance(0), Value(0)
    {
        pEventGenerator = pGenerator;
        iFragmentPos    = fragmentpos;
    }

#ifdef DEBUG
    std::ostream& operator<<(std::ostream& os, const Event::type_t& t) {
      switch(t) {
        case Event::type_cmd_down:
          os << "type_cmd_down";
          break;
        case Event::type_cmd_up:
          os << "type_cmd_up";
          break;
        case Event::type_cmd_upforce:
          os << "type_cmd_upforce";
          break;
        case Event::type_cmd_hit:
          os << "type_cmd_hit";
          break;
        case Event::type_control_change:
          os << "type_control_change";
          break;
        case Event::type_control_request:
          os << "type_control_request";
          break;
        case Event::type_global_control_change:
          os << "type_global_control_change";
          break;
        case Event::type_sync:
          os << "type_sync";
          break;
      }
      return os;
    };

    std::ostream& operator<<(std::ostream& os, const Event::command_t& t) {
      switch(t) {
      case Event::UNKNOWN:
	os << "UNKNOWN";
	break;
      case Event::UNDO:
	os << "UNDO";
	break;
      case Event::REDO:
	os << "REDO";
	break;
      case Event::REPLACE:
	os << "REPLACE";
	break;
      case Event::REVERSE:
	os << "REVERSE";
	break;
      case Event::SCRATCH:
	os << "SCRATCH";
	break;
      case Event::RECORD:
	os << "RECORD";
	break;
      case Event::OVERDUB:
	os << "OVERDUB";
	break;
      case Event::MULTIPLY:
	os << "MULTIPLY";
	break;
      case Event::INSERT:
	os << "INSERT";
	break;
      case Event::MUTE:
	os << "MUTE";
	break;
        // extra features
      case Event::DELAY:
	os << "DELAY";
	break;
      case Event::REDO_TOG:
	os << "REDO_TOG";
	break;
      case Event::QUANT_TOG:
	os << "QUANT_TOG";
	break;
      case Event::ROUND_TOG:
	os << "ROUND_TOG";
	break;
      case Event::ONESHOT:
	os << "ONESHOT";
	break;
      case Event::TRIGGER:
	os << "TRIGGER";
	break;
      case Event::SUBSTITUTE:
	os << "SUBSTITUTE";
	break;
      case Event::UNDO_ALL:
	os << "UNDO_ALL";
	break;
      case Event::REDO_ALL:
	os << "REDO_ALL";
	break;
      case Event::MUTE_ON:
	os << "MUTE_ON";
	break;
      case Event::MUTE_OFF:
	os << "MUTE_OFF";
	break;
      case Event::PAUSE:
	os << "PAUSE";
	break;
      case Event::PAUSE_ON:
	os << "PAUSE_ON";
	break;
      case Event::PAUSE_OFF:
	os << "PAUSE_OFF";
	break;
      case Event::SOLO:
	os << "SOLO";
	break;
      case Event::SOLO_NEXT:
	os << "SOLO_NEXT";
	break;
      case Event::SOLO_PREV:
	os << "SOLO_PREV";
	break;
      case Event::RECORD_SOLO:
	os << "RECORD_SOLO";
	break;
      case Event::RECORD_SOLO_NEXT:
	os << "RECORD_SOLO_NEXT";
	break;
      case Event::RECORD_SOLO_PREV:
	os << "RECORD_SOLO_PREV";
	break;
      case Event::SET_SYNC_POS:
	os << "SET_SYNC_POS";
	break;
      case Event::RESET_SYNC_POS:
	os << "RESET_SYNC_POS";
	break;
      case Event::MUTE_TRIGGER:
	os << "MUTE_TRIGGER";
	break;
      case Event::RECORD_OR_OVERDUB:
	os << "RECORD_OR_OVERDUB";
	break;
      case Event::RECORD_EXCLUSIVE:
	os << "RECORD_EXCLUSIVE";
	break;
      case Event::RECORD_EXCLUSIVE_NEXT:
	os << "RECORD_EXCLUSIVE_NEXT";
	break;
      case Event::RECORD_EXCLUSIVE_PREV:
	os << "RECORD_EXCLUSIVE_PREV";
	break;
      case Event::RECORD_OR_OVERDUB_EXCL:
	os << "RECORD_OR_OVERDUB_EXCL";
	break;
      case Event::RECORD_OR_OVERDUB_EXCL_NEXT:
	os << "RECORD_OR_OVERDUB_EXCL_NEXT";
	break;
      case Event::RECORD_OR_OVERDUB_EXCL_PREV:
	os << "RECORD_OR_OVERDUB_EXCL_PREV";
	break;
      case Event::UNDO_TWICE:
	os << "UNDO_TWICE";
	break;
      case Event::RECORD_OR_OVERDUB_SOLO:
	os << "RECORD_OR_OVERDUB_SOLO";
	break;
      case Event::RECORD_OR_OVERDUB_SOLO_TRIG:
	os << "RECORD_OR_OVERDUB_SOLO_TRIG";
	break;
      case Event::RECORD_OVERDUB_END_SOLO:
	os << "RECORD_OVERDUB_END_SOLO";
	break;
      case Event::RECORD_OVERDUB_END_SOLO_TRIG:
	os << "RECORD_OVERDUB_END_SOLO_TRIG";
	break;
      case Event::RECORD_OR_OVERDUB_SOLO_NEXT:
	os << "RECORD_OR_OVERDUB_SOLO_NEXT";
	break;
      case Event::RECORD_OR_OVERDUB_SOLO_PREV:
	os << "RECORD_OR_OVERDUB_SOLO_PREV";
	break;
      case Event::LAST_COMMAND:
	os << "LAST_COMMAND";
	break;
      }
      return os;
    }

    std::ostream& operator<<(std::ostream& os, const Event::control_t& t) {
      switch(t) {
      case Event::Unknown:
	os << "Unknown";
	break;
      case Event::TriggerThreshold:
	os << "TriggerThreshold";
	break;
      case Event::DryLevel:
	os << "DryLevel";
	break;
      case Event::WetLevel:
	os << "WetLevel";
	break;
      case Event::Feedback:
	os << "Feedback";
	break;
      case Event::Rate:
	os << "Rate";
	break;
      case Event::ScratchPosition:
	os << "ScratchPosition";
	break;
      case Event::MultiUnused:
	os << "MultiUnused";
	break;
      case Event::TapDelayTrigger:
	os << "TapDelayTrigger";
	break;
      case Event::UseFeedbackPlay:
	os << "UseFeedbackPlay";
	break;
      case Event::Quantize:
	os << "Quantize";
	break;
      case Event::Round:
	os << "Round";
	break;
      case Event::RedoTap:
	os << "RedoTap";
	break;
      case Event::SyncMode:
	os << "SyncMode";
	break;
      case Event::UseRate:
	os << "UseRate";
	break;
      case Event::FadeSamples:
	os << "FadeSamples";
	break;
      case Event::TempoInput:
	os << "TempoInput";
	break;
      case Event::PlaybackSync:
	os << "PlaybackSync";
	break;
      case Event::EighthPerCycleLoop:
	os << "EighthPerCycleLoop";
	break;
      case Event::UseSafetyFeedback:
	os << "UseSafetyFeedback";
	break;
      case Event::InputLatency:
	os << "InputLatency";
	break;
      case Event::OutputLatency:
	os << "OutputLatency";
	break;
      case Event::TriggerLatency:
	os << "TriggerLatency";
	break;
      case Event::MuteQuantized:
	os << "MuteQuantized";
	break;
      case Event::OverdubQuantized:
	os << "OverdubQuantized";
	break;
      case Event::SyncOffsetSamples:
	os << "SyncOffsetSamples";
	break;
      case Event::RoundIntegerTempo:
	os << "RoundIntegerTempo";
	break;
      // read only
      case Event::State:
	os << "State";
	break;
      case Event::LoopLength:
	os << "LoopLength";
	break;
      case Event::LoopPosition:
	os << "LoopPosition";
	break;
      case Event::CycleLength:
	os << "CycleLength";
	break;
      case Event::FreeTime:
	os << "FreeTime";
	break;
      case Event::TotalTime:
	os << "TotalTime";
	break;
      case Event::Waiting:
	os << "Waiting";
	break;
      case Event::TrueRate:
	os << "TrueRate";
	break;
      case Event::NextState:
	os << "NextState";
	break;
      // this is end of loop enum.. the following are global
      case Event::Tempo:
	os << "Tempo";
	break;
      case Event::SyncTo:
	os << "SyncTo";
	break;
      case Event::EighthPerCycle:
	os << "EighthPerCycle";
	break;
      case Event::TapTempo:
	os << "TapTempo";
	break;
      case Event::MidiStart:
	os << "MidiStart";
	break;
      case Event::MidiStop:
	os << "MidiStop";
	break;
      case Event::MidiTick:
	os << "MidiTick";
	break;
      case Event::AutoDisableLatency:
	os << "AutoDisableLatency";
	break;
      case Event::SmartEighths:
	os << "SmartEighths";
	break;
      case Event::OutputMidiClock:
	os << "OutputMidiClock";
	break;
      case Event::UseMidiStart:
	os << "UseMidiStart";
	break;
      case Event::UseMidiStop:
	os << "UseMidiStop";
	break;
      case Event::SelectNextLoop:
	os << "SelectNextLoop";
	break;
      case Event::SelectPrevLoop:
	os << "SelectPrevLoop";
	break;
      case Event::SelectAllLoops:
	os << "SelectAllLoops";
	break;
      case Event::SelectedLoopNum:
	os << "SelectedLoopNum";
	break;
      case Event::JackTimebaseMaster:
	os << "JackTimebaseMaster";
	break;
      // these are per-loop, but not used in the old plugin part
      case Event::SaveLoop:
	os << "SaveLoop";
	break;
      case Event::UseCommonIns:
	os << "UseCommonIns";
	break;
      case Event::UseCommonOuts:
	os << "UseCommonOuts";
	break;
      case Event::HasDiscreteIO:
	os << "HasDiscreteIO";
	break;
      case Event::ChannelCount:
	os << "ChannelCount";
	break;
      case Event::InPeakMeter:
	os << "InPeakMeter";
	break;
      case Event::OutPeakMeter:
	os << "OutPeakMeter";
	break;
      case Event::RelativeSync:
	os << "RelativeSync";
	break;
      case Event::InputGain:
	os << "InputGain";
	break;
      case Event::AutosetLatency:
	os << "AutosetLatency";
	break;
      case Event::IsSoloed:
	os << "IsSoloed";
	break;
      case Event::StretchRatio:
	os << "StretchRatio";
	break;
      case Event::PitchShift:
	os << "PitchShift";
	break;
      case Event::TempoStretch:
	os << "TempoStretch";
	break;
      // this is ugly, because i want them midi bindable
      case Event::PanChannel1:
	os << "PanChannel1";
	break;
      case Event::PanChannel2:
	os << "PanChannel2";
	break;
      case Event::PanChannel3:
	os << "PanChannel3";
	break;
      case Event::PanChannel4:
	os << "PanChannel4";
	break;
      // Put all new controls at the end to avoid screwing up the order of existing AU sessions (who store these numbers)
      case Event::ReplaceQuantized:
	os << "ReplaceQuantized";
	break;
      case Event::SendMidiStartOnTrigger:
	os << "SendMidiStartOnTrigger";
	break;
      case Event::MidiSelectAlternateBindings:
	os << "MidiSelectAlternateBindings";
	break;
      }
      return os;
    }
#endif
} // namespace SooperLooper
