/*
** Copyright (C) 2004 Jesse Chappell <jesse@essej.net>
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

#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/utils.h>
#include <wx/dir.h>
#include <wx/spinctrl.h>
#include <wx/splash.h>

#include <iostream>
#include <cstdio>
#include <cmath>

#include "version.h"

#include "gui_frame.hpp"
#include "gui_app.hpp"
#include "looper_panel.hpp"
#include "loop_control.hpp"
#include "slider_bar.hpp"
#include "choice_box.hpp"
#include "check_box.hpp"
#include "spin_box.hpp"
#include "pix_button.hpp"
#include "keyboard_target.hpp"
#include "help_window.hpp"
#include "prefs_dialog.hpp"

#include "pixmaps/sl_logo.xpm"
#include "pixmaps/tap_tempo_active.xpm"
#include "pixmaps/tap_tempo_disabled.xpm"
#include "pixmaps/tap_tempo_focus.xpm"
#include "pixmaps/tap_tempo_normal.xpm"
#include "pixmaps/tap_tempo_selected.xpm"
#include "pixmaps/sl_splash.xpm"

#include <midi_bind.hpp>

#include <pbd/xml++.h>

using namespace SooperLooper;
using namespace SooperLooperGui;
using namespace std;

enum {
	ID_UpdateTimer = 9000,
	ID_AboutMenu,
	ID_HelpTipsMenu,
	ID_PreferencesMenu,
	ID_ConnectionMenu,
	ID_KeybindingsMenu,
	ID_MidiBindingsMenu,
	ID_LoadSession,
	ID_SaveSession,
	ID_Quit,
	ID_QuitStop,
	ID_AddLoop,
	ID_AddMonoLoop,
	ID_AddStereoLoop,
	ID_RemoveLoop,
	ID_TempoSlider,
	ID_SyncChoice,
	ID_EighthSlider,
	ID_QuantizeChoice,
	ID_RoundCheck,
	ID_RelSyncCheck,
	ID_TapTempoButton,
	ID_TapTempoTimer,
	ID_AddCustomLoop,
	ID_XfadeSlider,
	ID_DryControl,
	ID_WetControl,
	ID_InGainControl
};


BEGIN_EVENT_TABLE(GuiFrame, wxFrame)

	EVT_IDLE(GuiFrame::OnIdle)
	EVT_CLOSE(GuiFrame::OnClose)
	EVT_SIZE(GuiFrame::OnSize)
	EVT_PAINT(GuiFrame::OnPaint)
	EVT_TIMER(ID_UpdateTimer, GuiFrame::OnUpdateTimer)
	EVT_TIMER(ID_TapTempoTimer, GuiFrame::on_taptempo_timer)

	EVT_ACTIVATE (GuiFrame::OnActivate)
	EVT_ACTIVATE_APP (GuiFrame::OnActivate)
	
	EVT_MENU(ID_Quit, GuiFrame::OnQuit)
	EVT_MENU(ID_QuitStop, GuiFrame::OnQuit)

	EVT_MENU(ID_AboutMenu, GuiFrame::on_about)
	EVT_MENU(ID_HelpTipsMenu, GuiFrame::on_help)

	EVT_MENU(ID_AddLoop, GuiFrame::on_add_loop)
	EVT_MENU(ID_AddMonoLoop, GuiFrame::on_add_loop)
	EVT_MENU(ID_AddStereoLoop, GuiFrame::on_add_loop)
	EVT_MENU(ID_AddCustomLoop, GuiFrame::on_add_custom_loop)
	EVT_MENU(ID_RemoveLoop, GuiFrame::on_remove_loop)

	EVT_MENU(ID_PreferencesMenu, GuiFrame::on_view_menu)
	EVT_MENU(ID_KeybindingsMenu, GuiFrame::on_view_menu)
	EVT_MENU(ID_MidiBindingsMenu, GuiFrame::on_view_menu)
	EVT_MENU(ID_ConnectionMenu, GuiFrame::on_view_menu)

	EVT_MENU(ID_LoadSession, GuiFrame::on_load_session)
	EVT_MENU(ID_SaveSession, GuiFrame::on_save_session)

	
END_EVENT_TABLE()

GuiFrame::GuiFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
	: wxFrame((wxFrame *)NULL, -1, title, pos, size, wxDEFAULT_FRAME_STYLE, wxT("sooperlooper"))

{
	_keyboard = new KeyboardTarget (this, "gui_frame");
	_curr_loop = -1;
	_tapdelay_val = 1.0f;
	_prefs_dialog = 0;
	_got_new_data = 0;
	_help_window = 0;
	_engine_alive = true;
	
	_rcdir = wxGetHomeDir() + wxFileName::GetPathSeparator() + wxT(".sooperlooper");

	_loop_control = new LoopControl(_rcdir);

	
	intialize_keybindings ();
		
	load_rc();
	
	init();

#ifdef __WXMAC__
	wxApp::s_macAboutMenuItemId = ID_AboutMenu;
	wxApp::s_macPreferencesMenuItemId = ID_PreferencesMenu;
	wxApp::s_macExitMenuItemId = ID_QuitStop;
//	wxApp::s_macHelpMenuTitleName = "Help";
#endif

	_update_timer = new wxTimer(this, ID_UpdateTimer);
	_update_timer->Start(9000, true);

	_taptempo_button_timer = new wxTimer(this, ID_TapTempoTimer);

	_loop_control->ConnectFailed.connect (slot (*this,  &GuiFrame::on_connect_failed));
	_loop_control->LostConnection.connect (slot (*this,  &GuiFrame::on_connection_lost));
	_loop_control->IsAlive.connect (slot (*this,  &GuiFrame::on_engine_alive));
	_loop_control->ErrorReceived.connect (slot (*this,  &GuiFrame::on_error_received));
	
}

GuiFrame::~GuiFrame()
{
	save_rc();
	
	for (unsigned int i=0; i < _looper_panels.size(); ++i) {
		// unregister
		_loop_control->register_auto_updates((int) i, true);
		_loop_control->register_input_controls((int) i, true);
	}

	delete _loop_control;

	delete _keyboard;
}

void
GuiFrame::init()
{
	_main_sizer = new wxBoxSizer(wxVERTICAL);
	_topsizer = new wxBoxSizer(wxVERTICAL);

	//wxBoxSizer * rowsizer = new wxBoxSizer(wxHORIZONTAL);
	wxInitAllImageHandlers();
	
	SetBackgroundColour(*wxBLACK);
	SetThemeEnabled(false);
	
	wxFont sliderFont = *wxSMALL_FONT;

	wxBoxSizer * rowsizer = new wxBoxSizer(wxHORIZONTAL);
	_top_panel = new wxPanel(this);
	_top_panel->SetThemeEnabled(false);
	_top_panel->SetBackgroundColour(*wxBLACK);

	wxBoxSizer * topcolsizer = new wxBoxSizer(wxVERTICAL);
	
	rowsizer->Add (1, 1, 1);

	_sync_choice = new ChoiceBox (_top_panel, ID_SyncChoice, true, wxDefaultPosition, wxSize (130, 20));
	_sync_choice->set_label (wxT("sync to"));
	_sync_choice->SetFont (sliderFont);
	_sync_choice->value_changed.connect (slot (*this,  &GuiFrame::on_syncto_change));
	_sync_choice->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("sync")));
	
	rowsizer->Add (_sync_choice, 0, wxALL|wxEXPAND, 2);
	
	_tempo_bar = new SpinBox(_top_panel, ID_TempoSlider, 0.0f, 10000.0f, 110.0f, true, wxDefaultPosition, wxSize(120, 20));
	_tempo_bar->set_units(wxT("bpm"));
	_tempo_bar->set_label(wxT("tempo"));
	_tempo_bar->set_snap_mode (SpinBox::IntegerSnap);
	_tempo_bar->set_allow_outside_bounds(true);
	_tempo_bar->SetFont (sliderFont);
	_tempo_bar->value_changed.connect (slot (*this,  &GuiFrame::on_tempo_change));
	_tempo_bar->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("tempo")));
	rowsizer->Add (_tempo_bar, 0, wxALL|wxEXPAND, 2);

 	_taptempo_button = new PixButton(_top_panel, ID_TapTempoButton, true);
	_taptempo_button->set_normal_bitmap (wxBitmap(tap_tempo_normal));
	_taptempo_button->set_selected_bitmap (wxBitmap(tap_tempo_selected));
	_taptempo_button->set_focus_bitmap (wxBitmap(tap_tempo_focus));
	_taptempo_button->set_disabled_bitmap (wxBitmap(tap_tempo_disabled));
	_taptempo_button->set_active_bitmap (wxBitmap(tap_tempo_active));
	_taptempo_button->pressed.connect (slot (*this, &GuiFrame::on_taptempo_press));
	_taptempo_button->released.connect (slot (*this, &GuiFrame::on_taptempo_release));
	_taptempo_button->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("taptempo")));
 	rowsizer->Add (_taptempo_button, 0, wxALL|wxEXPAND, 2);
	

	_eighth_cycle_bar = new SpinBox(_top_panel, ID_EighthSlider, 1.0f, 1024.0f, 16.0f, true, wxDefaultPosition, wxSize(110, 20));
	_eighth_cycle_bar->set_units(wxT(""));
	_eighth_cycle_bar->set_label(wxT("8th/cycle"));
	_eighth_cycle_bar->set_snap_mode (SpinBox::IntegerSnap);
	_eighth_cycle_bar->set_allow_outside_bounds(true);
	_eighth_cycle_bar->SetFont (sliderFont);
	_eighth_cycle_bar->value_changed.connect (slot (*this,  &GuiFrame::on_eighth_change));
	_eighth_cycle_bar->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("eighth")));
	rowsizer->Add (_eighth_cycle_bar, 0, wxALL|wxEXPAND, 2);
	

	_quantize_choice = new ChoiceBox (_top_panel, ID_QuantizeChoice, true, wxDefaultPosition, wxSize (110, 20));
	_quantize_choice->SetFont (sliderFont);
	_quantize_choice->set_label (wxT("quantize"));
	_quantize_choice->value_changed.connect (slot (*this,  &GuiFrame::on_quantize_change));
	_quantize_choice->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("quantize")));
	_quantize_choice->append_choice (wxT("off"), 0);
	_quantize_choice->append_choice (wxT("cycle"), 1);
	_quantize_choice->append_choice (wxT("8th"), 2);
	_quantize_choice->append_choice (wxT("loop"), 3);
	rowsizer->Add (_quantize_choice, 0, wxALL|wxEXPAND, 2);

	rowsizer->Add (1, 1, 1);

	wxStaticBitmap * logobit = new wxStaticBitmap(_top_panel, -1, wxBitmap(sl_logo_xpm));
	rowsizer->Add (logobit, 0, wxALIGN_BOTTOM);

	topcolsizer->Add (rowsizer, 0, wxEXPAND|wxTOP, 3);

	rowsizer = new wxBoxSizer(wxHORIZONTAL);
	rowsizer->Add (1, 1, 1);

	_xfade_bar = new SpinBox(_top_panel, ID_XfadeSlider, 0.0f, 128000.0f, 64.0f, true, wxDefaultPosition, wxSize(100, 20));
	_xfade_bar->set_units(wxT(""));
	_xfade_bar->set_label(wxT("xfade"));
	_xfade_bar->SetToolTip(wxT("operation crossfade length in samples"));
	_xfade_bar->set_snap_mode (SpinBox::IntegerSnap);
	_xfade_bar->set_decimal_digits (0);
	_xfade_bar->SetFont (sliderFont);
	_xfade_bar->value_changed.connect (slot (*this,  &GuiFrame::on_xfade_change));
	_xfade_bar->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("fade_samples")));
	rowsizer->Add (_xfade_bar, 0, wxALL|wxEXPAND, 2);

	_common_ingain_bar = new SliderBar(_top_panel, ID_InGainControl, 0.0f, 1.0f, 1.0f, true, wxDefaultPosition, wxSize(132,20));
	_common_ingain_bar->set_units(wxT("dB"));
	_common_ingain_bar->set_label(wxT("input gain"));
	_common_ingain_bar->set_scale_mode(SliderBar::ZeroGainMode);
	_common_ingain_bar->set_show_indicator_bar(true);
	_common_ingain_bar->SetFont(sliderFont);
	_common_ingain_bar->value_changed.connect (slot (*this, &GuiFrame::on_ingain_change));
	_common_ingain_bar->bind_request.connect (bind (slot (*this, &GuiFrame::on_bind_request), wxT("input_gain")));
	rowsizer->Add (_common_ingain_bar, 0, wxALL|wxEXPAND, 2);
	
	_common_dry_bar = new SliderBar(_top_panel, ID_DryControl, 0.0f, 1.0f, 1.0f, true, wxDefaultPosition, wxSize(132,20));
	_common_dry_bar->set_units(wxT("dB"));
	_common_dry_bar->set_label(wxT("main dry"));
	_common_dry_bar->set_scale_mode(SliderBar::ZeroGainMode);
	_common_dry_bar->set_show_indicator_bar(true);
	_common_dry_bar->SetFont(sliderFont);
	_common_dry_bar->value_changed.connect (slot (*this, &GuiFrame::on_dry_change));
	_common_dry_bar->bind_request.connect (bind (slot (*this, &GuiFrame::on_bind_request), wxT("dry")));
	rowsizer->Add (_common_dry_bar, 0, wxALL|wxEXPAND, 2);

	_common_wet_bar = new SliderBar(_top_panel, ID_WetControl, 0.0f, 1.0f, 1.0f, true, wxDefaultPosition, wxSize(132,20));
	_common_wet_bar->set_units(wxT("dB"));
	_common_wet_bar->set_label(wxT("main out"));
	_common_wet_bar->set_scale_mode(SliderBar::ZeroGainMode);
	_common_wet_bar->set_show_indicator_bar(true);
	_common_wet_bar->SetFont(sliderFont);
	_common_wet_bar->value_changed.connect (slot (*this, &GuiFrame::on_wet_change));
	_common_wet_bar->bind_request.connect (bind (slot (*this, &GuiFrame::on_bind_request), wxT("wet")));
	rowsizer->Add (_common_wet_bar, 0, wxALL|wxEXPAND, 2);
	

	
	_round_check = new CheckBox (_top_panel, ID_RoundCheck, wxT("round"), true, wxDefaultPosition, wxSize(60, 20));
	_round_check->SetFont (sliderFont);
	_round_check->value_changed.connect (slot (*this, &GuiFrame::on_round_check));
	_round_check->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("round")));
	rowsizer->Add (_round_check, 0, wxALL|wxEXPAND, 2);

	_relsync_check = new CheckBox (_top_panel, ID_RelSyncCheck, wxT("rel sync"), true, wxDefaultPosition, wxSize(75, 20));
	_relsync_check->SetFont (sliderFont);
	_relsync_check->value_changed.connect (slot (*this, &GuiFrame::on_relsync_check));
	_relsync_check->bind_request.connect (bind (slot (*this,  &GuiFrame::on_bind_request), wxT("relative_sync")));
	rowsizer->Add (_relsync_check, 0, wxALL|wxEXPAND, 2);
	
	rowsizer->Add (1, 1, 1);

	topcolsizer->Add (rowsizer, 0, wxEXPAND|wxBOTTOM, 3);

	
	_top_panel->SetSizer( topcolsizer );      // actually set the sizer
	topcolsizer->Fit( _top_panel );            // set size to minimum size as calculated by the sizer
	topcolsizer->SetSizeHints( _top_panel );   // set size hints to honour mininum size

	_topsizer->Add (_top_panel, 0, wxEXPAND);

	
	_scroller = new wxScrolledWindow(this, -1, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
	_scroller->SetBackgroundColour(*wxBLACK);
	

	// todo request how many loopers to construct based on connection
	_loop_control->LooperConnected.connect (slot (*this, &GuiFrame::init_loopers));
	_loop_control->Disconnected.connect (bind (slot (*this, &GuiFrame::init_loopers), 0));
	_loop_update_connection = _loop_control->NewDataReady.connect (slot (*this, &GuiFrame::osc_data_ready));


	wxMenuBar *menuBar = new wxMenuBar();

	wxMenu *menuFile = new wxMenu(wxT(""));

	menuFile->Append(ID_LoadSession, wxT("Load Session\tCtrl-L"), wxT("Load session"));
	menuFile->Append(ID_SaveSession, wxT("Save Session\tCtrl-P"), wxT("Save session"));

	menuFile->AppendSeparator();
	
	//menuFile->Append(ID_AddLoop, wxT("Add Default Loop"), wxT("Add one default loop"));
	menuFile->Append(ID_AddMonoLoop, wxT("Add Mono Loop\tCtrl-1"), wxT("Add one default mono loop"));
	menuFile->Append(ID_AddStereoLoop, wxT("Add Stereo Loop\tCtrl-2"), wxT("Add one default stereo loop"));
	menuFile->Append(ID_AddCustomLoop, wxT("Add Custom Loop(s)...\tCtrl-A"), wxT("Add one or more custom loops, where loop memory can be specified"));
	menuFile->Append(ID_RemoveLoop, wxT("Remove Last Loop\tCtrl-D"), wxT("Remove last loop"));

	menuFile->AppendSeparator();
	menuFile->Append(ID_PreferencesMenu, wxT("&Preferences...\tCtrl-P"), wxT("Preferences..."));

#ifndef __WXMAC__
	menuFile->AppendSeparator();
#endif
	
	menuFile->Append(ID_Quit, wxT("Quit but Leave Engine Running\tCtrl-Shift-Q"), wxT("Exit from GUI and leave engine running"));
	menuFile->Append(ID_QuitStop, wxT("Quit and Stop Engine\tCtrl-Q"), wxT("Exit from GUI and stop engine"));
	
	menuBar->Append(menuFile, wxT("&Session"));
	

	wxMenu *menuHelp = new wxMenu(wxT(""));
	menuHelp->Append(ID_HelpTipsMenu, wxT("&Usage Tips...\tCtrl-H"), wxT("Show Usage Tips window"));
	menuHelp->Append(ID_AboutMenu, wxT("&About..."), wxT("Show about dialog"));
	menuBar->Append(menuHelp, wxT("&Help"));
	
	// ... and attach this menu bar to the frame
	SetMenuBar(menuBar);


	_topsizer->Add (_scroller, 1, wxEXPAND);
	
	_scroller->SetSizer( _main_sizer );      // actually set the sizer
	_scroller->SetAutoLayout( true );     // tell dialog to use sizer

	_scroller->SetScrollRate (0, 30);
	_scroller->EnableScrolling (true, true);

	//_main_sizer->Fit( _scroller );            // set size to minimum size as calculated by the sizer
	_main_sizer->SetSizeHints( _scroller );   // set size hints to honour mininum size

	
	this->SetAutoLayout( true );     // tell dialog to use sizer
	this->SetSizer( _topsizer );      // actually set the sizer
	_topsizer->Fit( this );            // set size to minimum size as calculated by the sizer
	_topsizer->SetSizeHints( this );   // set size hints to honour mininum size

	_scroller->SetFocus();
}

void
GuiFrame::init_syncto_choice()
{
	// 		BrotherSync = -4,
	// 		InternalTempoSync = -3,
	// 		MidiClockSync = -2,
	// 		JackSync = -1,
	// 		NoSync = 0
		
	_sync_choice->clear_choices ();
	_sync_choice->append_choice (wxT("None"), 0);
	_sync_choice->append_choice (wxT("Internal"), -3);
	_sync_choice->append_choice (wxT("MidiClock"), -2);
	_sync_choice->append_choice (wxT("Jack"), -1);
//	_sync_choice->append_choice (wxT("BrotherSync"), -4);

	// the remaining choices are loops
	for (unsigned int i=0; i < _looper_panels.size(); ++i) {
		_sync_choice->append_choice (wxString::Format(wxT("Loop %d"), i+1), i+1);
	}

	update_syncto_choice ();
}

    


void
GuiFrame::init_loopers (int count)
{
	LooperPanel * looperpan;	

	if (count > (int) _looper_panels.size()) {
		while (count > (int) _looper_panels.size()) {
			looperpan = new LooperPanel(_loop_control, _scroller, -1);
			looperpan->set_index(_looper_panels.size());
			_main_sizer->Add (looperpan, 0, wxEXPAND|wxALL, 0);
			_looper_panels.push_back (looperpan);
		}
	}
	else if (count < (int)_looper_panels.size()) {
		while (count < (int)_looper_panels.size()) {
			looperpan = _looper_panels.back();
			_looper_panels.pop_back();
			_main_sizer->Remove(looperpan);
			looperpan->Destroy();
		}
	}

	_scroller->SetClientSize(_scroller->GetClientSize());
	_scroller->Layout();
	_scroller->SetScrollRate(0,30);

 	if (!_looper_panels.empty()) {
 		wxSize bestsz = _looper_panels[0]->GetBestSize();
		//cerr << "best w: " << bestsz.GetWidth() << endl;
 		_scroller->SetVirtualSizeHints (bestsz.GetWidth(), -1);
		_topsizer->Layout();
// 		_topsizer->Fit(this);
// 		_topsizer->SetSizeHints(this);

		// maybe resize
		if (_looper_panels.size() <= 4) {
			int topheight = _top_panel->GetSize().GetHeight();
#ifndef __WXMAC__
			if (GetMenuBar()) {
				topheight += GetMenuBar()->GetSize().GetHeight();
			}
#else
			topheight += 32;
#endif
			SetSize(GetSize().GetWidth(), bestsz.GetHeight() * _looper_panels.size()  + topheight); 
		}

 	}
	

	//_main_sizer->Layout();
	//_main_sizer->Fit(_scroller);
	//_main_sizer->SetSizeHints( _scroller );   // set size hints to honour mininum size
	
	// request all values for initial state
	_loop_control->request_global_values ();
	
	for (unsigned int i=0; i < _looper_panels.size(); ++i) {
		_looper_panels[i]->set_index(i);
		_loop_control->register_auto_updates((int) i);
		_loop_control->register_input_controls((int) i);
		_loop_control->request_all_values ((int)i);
	}

	init_syncto_choice ();

	set_curr_loop (_curr_loop);
	_engine_alive = true;
}

void
GuiFrame::osc_data_ready()
{
	// cerr << "osc ready" << endl;
	// this is called from another thread
	_got_new_data++;

	::wxWakeUpIdle();
}

void 
GuiFrame::on_connect_failed (const std::string & msg)
{
	wxMessageDialog dial(this, wxString::FromAscii(msg.c_str()), wxT("Connection Error"), wxOK);
	dial.SetTitle(wxT("Connection Error"));
	dial.ShowModal();
}

void 
GuiFrame::on_connection_lost (const std::string & msg)
{
	wxMessageDialog dial(this, wxString::FromAscii(msg.c_str()), wxT("Lost Connection"), wxOK);
	dial.SetTitle(wxT("Lost Connection"));

	dial.ShowModal();
}

void
GuiFrame::on_error_received (const std::string & msg)
{
	wxMessageDialog dial(this, wxString::FromAscii(msg.c_str()), wxT("Error Received"), wxOK);
	dial.SetTitle(wxT("Error Received"));

	dial.ShowModal();
}


void
GuiFrame::on_engine_alive ()
{
	//cerr << "got alive ping" << endl;
	_engine_alive = true;
}


void
GuiFrame::OnUpdateTimer(wxTimerEvent &ev)
{
	// check to see if our connected server is still alive
	
	if (_loop_control->connected()) {
 		_loop_control->update_values();

		if (!_engine_alive) {		
			_loop_control->disconnect();
			on_connection_lost ("Lost connection to SooperLooper engine.\nSee the Preferences->Connections tab to start a new one");
		}

		_engine_alive = false;
		_loop_control->send_alive_ping();
	}

	_update_timer->Start(9000, true);
}

void
GuiFrame::OnActivate(wxActivateEvent &ev)
{
	if (ev.GetActive()) {
		_keyboard->set_enabled (true);
	}
	else {
		_keyboard->set_enabled (false);
	}

	ev.Skip();
}


void
GuiFrame::on_taptempo_timer(wxTimerEvent &ev)
{
	_taptempo_button->set_active(false);
}

void
GuiFrame::update_controls()
{
	// get recent controls from loop control
	float val;

	if (_loop_control->is_global_updated(wxT("tempo"))) {
		_loop_control->get_global_value(wxT("tempo"), val);
		_tempo_bar->set_value (val);
	}

	if (_loop_control->is_global_updated(wxT("tap_tempo"))) {	
		_loop_control->get_global_value(wxT("tap_tempo"), val);

		float tempo;
		_loop_control->get_global_value(wxT("tempo"), tempo);
		// turn on tap active, then timeout to flip it back
		_taptempo_button->set_active(true);

		if (tempo > 200) {
			// half the tempo in ms
			int ms = (int) (1.0f/tempo * 30000.0f);
			_taptempo_button_timer->Start(ms, true);
		}
		else {
			_taptempo_button_timer->Start(150, true);
		}
	}
	
	
	if (_loop_control->is_global_updated(wxT("eighth_per_cycle"))) {
		_loop_control->get_global_value(wxT("eighth_per_cycle"), val);
		_eighth_cycle_bar->set_value (val);
	}
	
	if (_loop_control->is_global_updated(wxT("sync_source"))) {
		update_syncto_choice ();
	}

	// quantize from first loop
 	if (_loop_control->is_updated(0, wxT("quantize"))) {
		_loop_control->get_value(0, wxT("quantize"), val);
		val = roundf(val);
 		_quantize_choice->set_index_value ((int)val);
	}

 	if (_loop_control->is_updated(0, wxT("round"))) {
		_loop_control->get_value(0, wxT("round"), val);
 		_round_check->set_value (val > 0.0);
	}

 	if (_loop_control->is_updated(0, wxT("relative_sync"))) {
		_loop_control->get_value(0, wxT("relative_sync"), val);
 		_relsync_check->set_value (val > 0.0);
	}
	
	if (_loop_control->is_updated(0, wxT("fade_samples"))) {
		_loop_control->get_value(0, wxT("fade_samples"), val);
		_xfade_bar->set_value (val);
	}

	if (_loop_control->is_global_updated(wxT("dry"))) {
		_loop_control->get_global_value(wxT("dry"), val);
		_common_dry_bar->set_value (val);
	}

	if (_loop_control->is_global_updated(wxT("input_gain"))) {
		_loop_control->get_global_value(wxT("input_gain"), val);
		_common_ingain_bar->set_value (val);
	}
	
	// don't check, others might be using it
	_loop_control->get_global_value(wxT("in_peak_meter"), val);
	_common_dry_bar->set_indicator_value (val);

	
	if (_loop_control->is_global_updated(wxT("wet"))) {
		_loop_control->get_global_value(wxT("wet"), val);
		_common_wet_bar->set_value (val);
	}
	
	if (_loop_control->is_global_updated (wxT("out_peak_meter"))) {
		_loop_control->get_global_value (wxT("out_peak_meter"), val);
		_common_wet_bar->set_indicator_value (val);
	}

	
}

void
GuiFrame::update_syncto_choice()
{
	float val = 0.0f;
	_loop_control->get_global_value(wxT("sync_source"), val);
	
	long data = (long) roundf(val);
	int index = -1;
// 		BrotherSync = -4,
// 		InternalTempoSync = -3,
// 		MidiClockSync = -2,
// 		JackSync = -1,
// 		NoSync = 0
		
	wxString sval;
	ChoiceBox::ChoiceList chlist;
	_sync_choice->get_choices(chlist);
	int i=0;
	
	for (ChoiceBox::ChoiceList::iterator iter = chlist.begin(); iter != chlist.end(); ++iter, ++i)
	{
		if ((*iter).second == data) {
			index = i;
			break;
		}
	}
	
	_sync_choice->set_index_value (index);
}

void
GuiFrame::OnClose(wxCloseEvent &event)
{
	// send quit command to looper by default
	save_default_midibindings();
	_loop_update_connection.disconnect();

	// sleep for a short period before stopping engine
#if wxCHECK_VERSION(2,5,3)
	::wxMilliSleep(500);
#else
	::wxUsleep(500);
#endif

	_loop_control->send_quit();
	
	Destroy();
}

void
GuiFrame::OnQuit(wxCommandEvent& event)
{
	int id = event.GetId();

	save_default_midibindings();

	_loop_update_connection.disconnect();

	// sleep for a short period before stopping engine
#if wxCHECK_VERSION(2,5,3)
	::wxMilliSleep(500);
#else
	::wxUsleep(500);
#endif
	
	if (id == ID_Quit) {
		Destroy();
	}
	else if (id == ID_QuitStop) {
		// send quit command to looper
		_loop_control->send_quit();
		Destroy();
	}
}

void
GuiFrame::OnHide(wxCommandEvent &event)
{

}


void
GuiFrame::OnSize(wxSizeEvent & event)
{
	event.Skip();
}

void GuiFrame::OnPaint(wxPaintEvent & event)
{
	event.Skip();
}

void
GuiFrame::OnIdle(wxIdleEvent& event)
{
	if (_got_new_data) {
		//cerr << "idle update" << endl;

 		_loop_control->update_values();
		
    		for (unsigned int i=0; i < _looper_panels.size(); ++i) {
    			_looper_panels[i]->update_controls();
    		}
		
    		update_controls();

 		_got_new_data = 0;
	}
	
	//event.Skip();
}

void
GuiFrame::save_default_midibindings ()
{
	wxString dirname = _rcdir;

	if ( ! wxDirExists(dirname) ) {
		if (!wxMkdir ( dirname, 0755 )) {
			printf ("Error creating %s\n", static_cast<const char *> (dirname.mb_str())); 
			return;
		}
	}

	_loop_control->save_midi_bindings ( (dirname + wxFileName::GetPathSeparator() + wxT("default_midi.slb")));
}

void
GuiFrame::on_add_loop (wxCommandEvent &ev)
{
	int id = ev.GetId();

	LoopControl::SpawnConfig & sconf = _loop_control->get_spawn_config();
	
	if (id == ID_AddLoop) {
		_loop_control->post_add_loop();
	}
	else if (id == ID_AddMonoLoop) {
		_loop_control->post_add_loop (1, sconf.mem_secs, sconf.discrete_io);
	}
	else if (id == ID_AddStereoLoop) {
		_loop_control->post_add_loop (2, sconf.mem_secs, sconf.discrete_io);
	}
}

void
GuiFrame::on_add_custom_loop (wxCommandEvent &ev)
{
	AddCustomLoopDialog * dial = new AddCustomLoopDialog(this);
	dial->CentreOnParent();
	// it takes care of itself
	if (dial->ShowModal() == wxID_OK) {
		for (int i=0; i < dial->num_loops; ++i) {
			//cerr << "adding loop with " << dial->num_channels << "  secs: " << dial->secs_channel << endl;
			_loop_control->post_add_loop (dial->num_channels, dial->secs_channel, dial->discrete);
		}
	}

	delete dial;
}

void
GuiFrame::on_remove_loop (wxCommandEvent &ev)
{
	_loop_control->post_remove_loop();
}

void
GuiFrame::on_bind_request (wxString val)
{
	MidiBindInfo info;
	bool donothing = false;

	info.channel = 0;
	info.type = "cc";
	info.command = "set";
	info.instance = -2;
	info.lbound = 0.0;
	info.ubound = 1.0;
	info.style = MidiBindInfo::NormalStyle;

	
	if (val == wxT("tempo")) {
		info.lbound = 20.0f;
		info.ubound = 274.0f;
		info.control = "tempo";
	}
	else if (val == wxT("taptempo")) {
		info.control = "tap_tempo";
	}
	else if (val == wxT("eighth")) {
		info.control = "eighth_per_cycle";
		info.lbound = 1.0f;
		info.ubound = 128.0f;
	}
	else if (val == wxT("fade_samples")) {
		info.control = "fade_samples";
		info.lbound = 0.0f;
		info.ubound = 16384.0f;
	}
	else if (val == wxT("dry")) {
		info.control = "dry";
		info.style = MidiBindInfo::GainStyle;
	}
	else if (val == wxT("wet")) {
		info.control = "wet";
		info.style = MidiBindInfo::GainStyle;
	}
	else if (val == wxT("input_gain")) {
		info.control = "input_gain";
		info.style = MidiBindInfo::GainStyle;
	}
	else if (val == wxT("round")) {
		info.instance = -1;
		info.control = "round";
	}
	else if (val == wxT("sync")) {
		info.control = "sync_source";
		info.lbound = -3.0f;
		info.ubound = 16.0f;
	}
	else if (val == wxT("quantize")) {
		info.instance = -1;
		info.control = "quantize";
		info.lbound = 0.0f;
		info.ubound = 3.0f;
	}
	else {
		donothing = true;
	}

	if (!donothing) {
		_loop_control->learn_midi_binding(info, true);
	}
	
}


void
GuiFrame::on_tempo_change (float value)
{
	_loop_control->post_global_ctrl_change (wxT("tempo"), value);
}

void
GuiFrame::on_eighth_change (float value)
{
	_loop_control->post_global_ctrl_change (wxT("eighth_per_cycle"), value);
}

void
GuiFrame::on_xfade_change (float value)
{
	_loop_control->post_ctrl_change (-1, wxT("fade_samples"), value);
}

void
GuiFrame::on_dry_change (float value)
{
	_loop_control->post_global_ctrl_change (wxT("dry"), value);
}

void
GuiFrame::on_wet_change (float value)
{
	_loop_control->post_global_ctrl_change (wxT("wet"), value);
}

void
GuiFrame::on_ingain_change (float value)
{
	_loop_control->post_global_ctrl_change (wxT("input_gain"), value);
}

void
GuiFrame::on_syncto_change (int index, wxString val)
{
// 		BrotherSync = -4,
// 		InternalTempoSync = -3,
// 		MidiClockSync = -2,
// 		JackSync = -1,
// 		NoSync = 0
//            >0 is loop number
	
	float value = 0.0f;

	value = (float) _sync_choice->get_data_value();
	
	_loop_control->post_global_ctrl_change (wxT("sync_source"), value);
}


void
GuiFrame::on_quantize_change (int index, wxString val)
{
	// 0 is none, 1 is cycle, 2 is eighth, 3 is loop
	// send for all loops
	_loop_control->post_ctrl_change (-1, wxT("quantize"), (float) index);
}

void
GuiFrame::on_round_check (bool val)
{
	// send for all loops
	_loop_control->post_ctrl_change (-1, wxT("round"), val ? 1.0f: 0.0f);
}

void
GuiFrame::on_relsync_check (bool val)
{
	// send for all loops
	_loop_control->post_ctrl_change (-1, wxT("relative_sync"), val ? 1.0f: 0.0f);
}

void
GuiFrame::on_taptempo_press (int button)
{
	// needs to be a normal ctrl change for RTness
	_loop_control->post_ctrl_change (-2, wxT("tap_tempo"), 1.0f);
}

void
GuiFrame::on_taptempo_release (int button)
{
	if (button == PixButton::MiddleButton) {
		_loop_control->post_ctrl_change (-2, wxT("tap_tempo"), 1.0f);
	}
}


void
GuiFrame::process_key_event (wxKeyEvent &ev)
{
	// this is a pretty extreme hack
	// to let textfields, etc named with the right name
	// get their key events
	static wxString textname = wxT("KeyAware");

	//cerr << "got " << ev.GetKeyCode() << endl;
	
	wxWindow * focwin = wxWindow::FindFocus();
	if (focwin && (focwin->GetName() == textname
		       || (focwin->GetParent() && ((focwin->GetParent()->GetName() == textname)
						   || (focwin->GetParent()->GetParent()
						       && focwin->GetParent()->GetParent()->GetName() == textname)))))
	{
		ev.Skip();
	}
	else {
		_keyboard->process_key_event (ev);
	}
}


void GuiFrame::intialize_keybindings ()
{
	
	KeyboardTarget::add_action ("record", bind (slot (*this, &GuiFrame::command_action), wxT("record")));
	KeyboardTarget::add_action ("overdub", bind (slot (*this, &GuiFrame::command_action), wxT("overdub")));
	KeyboardTarget::add_action ("multiply", bind (slot (*this, &GuiFrame::command_action), wxT("multiply")));
	KeyboardTarget::add_action ("insert", bind (slot (*this, &GuiFrame::command_action), wxT("insert")));
	KeyboardTarget::add_action ("replace", bind (slot (*this, &GuiFrame::command_action), wxT("replace")));
	KeyboardTarget::add_action ("reverse", bind (slot (*this, &GuiFrame::command_action), wxT("reverse")));
	KeyboardTarget::add_action ("scratch", bind (slot (*this, &GuiFrame::command_action), wxT("scratch")));
	KeyboardTarget::add_action ("substitute", bind (slot (*this, &GuiFrame::command_action), wxT("substitute")));
	KeyboardTarget::add_action ("mute", bind (slot (*this, &GuiFrame::command_action), wxT("mute")));
	KeyboardTarget::add_action ("undo", bind (slot (*this, &GuiFrame::command_action), wxT("undo")));
	KeyboardTarget::add_action ("redo", bind (slot (*this, &GuiFrame::command_action), wxT("redo")));	
	KeyboardTarget::add_action ("oneshot", bind (slot (*this, &GuiFrame::command_action), wxT("oneshot")));
	KeyboardTarget::add_action ("trigger", bind (slot (*this, &GuiFrame::command_action), wxT("trigger")));

	KeyboardTarget::add_action ("delay", bind (slot (*this, &GuiFrame::misc_action), wxT("delay")));
	KeyboardTarget::add_action ("taptempo", bind (slot (*this, &GuiFrame::misc_action), wxT("taptempo")));
	KeyboardTarget::add_action ("load", bind (slot (*this, &GuiFrame::misc_action), wxT("load")));
	KeyboardTarget::add_action ("save", bind (slot (*this, &GuiFrame::misc_action), wxT("save")));
	KeyboardTarget::add_action ("cancel_midi_learn", bind (slot (*this, &GuiFrame::misc_action), wxT("cancel_learn")));

	KeyboardTarget::add_action ("select_loop_1", bind (slot (*this, &GuiFrame::select_loop_action), 1));
	KeyboardTarget::add_action ("select_loop_2", bind (slot (*this, &GuiFrame::select_loop_action), 2));
	KeyboardTarget::add_action ("select_loop_3", bind (slot (*this, &GuiFrame::select_loop_action), 3));
	KeyboardTarget::add_action ("select_loop_4", bind (slot (*this, &GuiFrame::select_loop_action), 4));
	KeyboardTarget::add_action ("select_loop_5", bind (slot (*this, &GuiFrame::select_loop_action), 5));
	KeyboardTarget::add_action ("select_loop_6", bind (slot (*this, &GuiFrame::select_loop_action), 6));
	KeyboardTarget::add_action ("select_loop_7", bind (slot (*this, &GuiFrame::select_loop_action), 7));
	KeyboardTarget::add_action ("select_loop_8", bind (slot (*this, &GuiFrame::select_loop_action), 8));
	KeyboardTarget::add_action ("select_loop_9", bind (slot (*this, &GuiFrame::select_loop_action), 9));
	KeyboardTarget::add_action ("select_loop_all", bind (slot (*this, &GuiFrame::select_loop_action), -1));

	
	// these are the defaults... they get overridden by rc file

	_keyboard->add_binding ("r", "record");
	_keyboard->add_binding ("o", "overdub");
	_keyboard->add_binding ("x", "multiply");
	_keyboard->add_binding ("i", "insert");
	_keyboard->add_binding ("p", "replace");
	_keyboard->add_binding ("v", "reverse");
	_keyboard->add_binding ("m", "mute");
	_keyboard->add_binding ("u", "undo");
	_keyboard->add_binding ("d", "redo");
	_keyboard->add_binding ("s", "scratch");
	_keyboard->add_binding ("b", "substitute");
	_keyboard->add_binding ("l", "delay");
	_keyboard->add_binding ("h", "oneshot");
	_keyboard->add_binding (" ", "trigger");
	_keyboard->add_binding ("t", "taptempo");
	_keyboard->add_binding ("Control-s", "save");
	_keyboard->add_binding ("Control-o", "load");
	_keyboard->add_binding ("escape", "cancel_midi_learn");
	
	_keyboard->add_binding ("1", "select_loop_1");
	_keyboard->add_binding ("2", "select_loop_2");
	_keyboard->add_binding ("3", "select_loop_3");
	_keyboard->add_binding ("4", "select_loop_4");
	_keyboard->add_binding ("5", "select_loop_5");
	_keyboard->add_binding ("6", "select_loop_6");
	_keyboard->add_binding ("7", "select_loop_7");
	_keyboard->add_binding ("8", "select_loop_8");
	_keyboard->add_binding ("9", "select_loop_9");
	_keyboard->add_binding ("0", "select_loop_all");
	
}

void GuiFrame::on_view_menu (wxCommandEvent &ev)
{
	if (ev.GetId() == ID_PreferencesMenu) {
		if (!_prefs_dialog) {
			_prefs_dialog = new PrefsDialog(this, -1, wxT("SooperLooper Preferences"));
			_prefs_dialog->SetSize (230,410);
		}
		else if (!_prefs_dialog->IsShown()) {
			_prefs_dialog->refresh_state();
		}

		_prefs_dialog->Show(true);
		_prefs_dialog->Raise();
		
	}
}

void GuiFrame::on_about (wxCommandEvent &ev)
{
	// construct splash
	wxBitmap bitmap(sl_splash_xpm);
	// add version info
	wxMemoryDC mdc;
	mdc.SelectObject(bitmap);
	int w,h;
	wxString vstr = wxString::Format(wxT("v %s"), sooperlooper_version);
	mdc.SetFont(*wxSWISS_FONT);
	mdc.SetTextForeground(*wxWHITE);
	mdc.GetTextExtent(vstr, &w, &h);
	mdc.DrawText(vstr, bitmap.GetWidth() / 2 - (w/2), 148);
	
	
	wxSplashScreen* splash = new wxSplashScreen(bitmap,
						    wxSPLASH_CENTRE_ON_PARENT|wxSPLASH_NO_TIMEOUT,
						    6000, this, -1, wxDefaultPosition, wxDefaultSize,
						    wxSTAY_ON_TOP);
	splash->SetTitle(wxT("About SooperLooper"));
	
}


void GuiFrame::on_help (wxCommandEvent &ev)
{
	// for now just refer to website
	if (!_help_window) {
		_help_window = new HelpWindow(this, -1, wxT("Online Help"));
	}
	
	_help_window->Show(true);
	_help_window->Raise();

}

void GuiFrame::command_action (bool release, wxString cmd)
{
	if (release) {
		_loop_control->post_up_event (_curr_loop, cmd);
	}
	else {
		_loop_control->post_down_event (_curr_loop, cmd);
	}
}

void GuiFrame::select_loop_action (bool release, int index)
{
	if (release) return;

	index--;
	
	if (index < (int) _looper_panels.size()) {

		set_curr_loop (index);
	}
}


void GuiFrame::misc_action (bool release, wxString cmd)
{
	int index = _curr_loop;

	// only on press
	if (release) return;

	if (index < 0) index = -1;
	
	if (cmd == wxT("taptempo")) {

		on_taptempo_press(1);
	}
	else if (cmd == wxT("delay")) {
		_tapdelay_val *= -1.0f;
		_loop_control->post_ctrl_change (index, wxString(wxT("tap_trigger")), _tapdelay_val);
	}
	else if (cmd == wxT("cancel_learn")) {

		_loop_control->cancel_midi_learn();
	}
	else if (cmd == wxT("save"))
	{
		if (index < 0) {
			index = 0;
		}


		wxString filename = get_keyboard().do_file_selector (wxT("Choose file to save loop"), wxT("wav"), wxT("*.*"),  wxSAVE|wxCHANGE_DIR|wxOVERWRITE_PROMPT);
		
		if ( !filename.empty() )
		{
			// add .wav if there isn't one already
			if (filename.size() <= 4 || (filename.size() > 4 && filename.substr(filename.size() - 4, 4) != wxT(".wav"))) {
				filename += wxT(".wav");
			}
			// todo: specify format
			_loop_control->post_save_loop (index, filename);
		}
		
	}
	else if (cmd == wxT("load"))
	{
		if (index < 0) {
			index = 0;
		}

		wxString filename = get_keyboard().do_file_selector (wxT("Choose file to open"), wxT(""), wxT("*.*"), wxOPEN|wxCHANGE_DIR);
		
		if ( !filename.empty() )
		{
			_loop_control->post_load_loop (index, filename);
		}

	}
}


bool GuiFrame::load_rc()
{
	// open file
	string configfname(static_cast<const char *> ((_rcdir + wxFileName::GetPathSeparator() + wxT("gui_config.xml")).fn_str() ));
	XMLTree configdoc (configfname);

	if (!configdoc.initialized()) {
		fprintf (stderr, "Error loading config at %s!\n", configfname.c_str()); 
		return false;
	}

	XMLNode * rootNode = configdoc.root();
	if (!rootNode || rootNode->name() != "SLConfig") {
		fprintf (stderr, "Preset root node not found in %s!\n", configfname.c_str()); 
		return false;
	}

	XMLNode * bindingsNode = rootNode->find_named_node ("KeyBindings");
	if (!bindingsNode ) {
		fprintf(stderr, "Preset Channels node not found in %s!\n", configfname.c_str()); 
		//return false;
	}
	else {
		_keyboard->set_binding_state (*bindingsNode);
	}

	bindingsNode = rootNode->find_named_node ("SpawnConfig");
	if (!bindingsNode ) {
		fprintf(stderr, "SpawnConfig node not found in %s!\n", configfname.c_str()); 
		//return false;
	}
	else {
		_loop_control->get_spawn_config().set_state (*bindingsNode);
		_loop_control->get_default_spawn_config().set_state (*bindingsNode);
	}


	return true;
}

bool GuiFrame::save_rc()
{
	wxString dirname = _rcdir;
	
	if ( ! wxDirExists(dirname) ) {
		if (!wxMkdir ( dirname, 0755 )) {
			printf ("Error creating %s\n", static_cast<const char *> (dirname.mb_str())); 
			return false;
		}
	}

	// make xmltree
	XMLTree configdoc;
	XMLNode * rootNode = new XMLNode("SLConfig");
	rootNode->add_property("version", sooperlooper_version);
	configdoc.set_root (rootNode);
	
	XMLNode * bindingsNode = rootNode->add_child ("KeyBindings");
	bindingsNode->add_child_nocopy (_keyboard->get_binding_state());

	bindingsNode = rootNode->add_child ("SpawnConfig");
	bindingsNode->add_child_nocopy (_loop_control->get_default_spawn_config().get_state());

	
	// write doc to file
	
	if (configdoc.write (static_cast<const char *> ((dirname + wxFileName::GetPathSeparator() + wxT("gui_config.xml")).fn_str())))
	{	    
		fprintf (stderr, "Stored settings into %s\n", static_cast<const char *> (dirname.fn_str()));
		return true;
	}
	else {
		fprintf (stderr, "Failed to store settings into %s\n", static_cast<const char *> (dirname.fn_str()));
		return false;
	}

}

void GuiFrame::set_curr_loop (int index)
{
	if (index < 0) index = -1;
	
	_curr_loop = index;

	// cerr << "got loop index " << _curr_loop << endl;
	
	int i=0;
	for (vector<LooperPanel *>::iterator iter = _looper_panels.begin(); iter != _looper_panels.end(); ++iter, ++i) {

		if (_curr_loop == i) {

			(*iter)->set_selected (true);
		}
		else {
			(*iter)->set_selected (false);
		}
	}
}

void GuiFrame::on_load_session (wxCommandEvent &ev)
{
	wxString filename = get_keyboard().do_file_selector (wxT("Choose session to load"), wxT("*.slsess"), wxT("*.*"), wxOPEN|wxCHANGE_DIR);
	
	if ( !filename.empty() )
	{
		_loop_control->post_load_session (filename);
	}

}

void GuiFrame::on_save_session (wxCommandEvent &ev)
{
	wxString filename = get_keyboard().do_file_selector (wxT("Choose file to save session"), wxT("slsess"), wxT("*.*"), wxSAVE|wxCHANGE_DIR|wxOVERWRITE_PROMPT);
	
	if ( !filename.empty() )
	{
		// add .slsession if there isn't one already
		if (filename.size() <= 7 || (filename.size() > 7 && filename.substr(filename.size() - 7, 7) != wxT(".slsess"))) {
			filename += wxT(".slsess");
		}
		_loop_control->post_save_session (filename);
	}

}



// @@@@@@@@@@@@@@@@@@@2

int AddCustomLoopDialog::num_loops = 1;
int AddCustomLoopDialog::num_channels = 2;
float AddCustomLoopDialog::secs_channel = 40.0f;
bool AddCustomLoopDialog::discrete = true;

AddCustomLoopDialog::AddCustomLoopDialog (GuiFrame * parent, wxWindowID id, const wxString& title,
					  const wxPoint& pos, const wxSize& size)
	: wxDialog ((wxWindow *)parent, id, title, pos, size, wxCAPTION)
{
	_parent = parent;
       
	wxBoxSizer *mainsizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer * setsizer = new wxFlexGridSizer(-1, 2, 4, 4);


	wxStaticText * statText = new wxStaticText(this, -1, wxT("# Loops to add:"));
	setsizer->Add (statText, 0, wxALIGN_RIGHT|wxALIGN_CENTRE_VERTICAL);
	_num_loops_spin = new wxSpinCtrl(this, -1, wxT("1"), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 16, num_loops);
	_num_loops_spin->SetValue (num_loops);
	setsizer->Add (_num_loops_spin, 0);

	statText = new wxStaticText(this, -1, wxT("# Channels per loop:"));
	setsizer->Add (statText, 0, wxALIGN_RIGHT|wxALIGN_CENTRE_VERTICAL);
	_num_channels_spin = new wxSpinCtrl(this, -1, wxT("2"), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 1, 16, num_channels);
	_num_channels_spin->SetValue (num_channels);
	setsizer->Add (_num_channels_spin, 0);

	
	_discrete_check = new wxCheckBox(this, -1, wxT("Individual Loop Ins/Outs"));
	_discrete_check->SetValue(discrete);
	setsizer->Add (_discrete_check, 0);
	setsizer->Add (1,1,1);

	
	statText = new wxStaticText(this, -1, wxT("Loop time (secs minimum):"));
	setsizer->Add (statText, 0, wxALIGN_RIGHT|wxALIGN_CENTRE_VERTICAL);
	_secs_per_channel_spin = new wxSpinCtrl(this, -1, wxT("20"), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 4, 1000, (int)secs_channel);
	_secs_per_channel_spin->SetValue ((int)secs_channel);
	setsizer->Add (_secs_per_channel_spin, 0);

	mainsizer->Add (setsizer, 1, wxEXPAND|wxALL, 6);


	wxBoxSizer * buttsizer = new wxBoxSizer(wxHORIZONTAL);

	buttsizer->Add (1,1,1);
	
	wxButton * butt = new wxButton(this, wxID_CANCEL, wxT("Cancel"));
	buttsizer->Add (butt, 0, wxALL, 5);

	butt = new wxButton(this, wxID_OK, wxT("OK"));
	buttsizer->Add (butt, 0, wxALL, 5);
	
	
	mainsizer->Add (buttsizer, 0, wxEXPAND|wxALL, 8);
	

	this->SetAutoLayout( true );     // tell dialog to use sizer
	this->SetSizer( mainsizer );      // actually set the sizer
	mainsizer->Fit( this );            // set size to minimum size as calculated by the sizer
	mainsizer->SetSizeHints( this );   // set size hints to honour mininum size
	
}


// called by wxOK
bool AddCustomLoopDialog::TransferDataFromWindow ()
{
	// set them

	num_loops = _num_loops_spin->GetValue();
	num_channels = _num_channels_spin->GetValue();
	secs_channel = (float) _secs_per_channel_spin->GetValue();
	discrete = _discrete_check->GetValue();
	return true;
}

