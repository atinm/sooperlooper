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

#ifndef __sooperlooper_gui_loop_control__
#define __sooperlooper_gui_loop_control__

#include <wx/string.h>
#include <sigc++/sigc++.h>
#include <map>
#include <vector>

#include <lo/lo.h>

namespace SooperLooperGui {


class LoopControl
	: public SigC::Object
{
  public:
	
	// ctor(s)
	LoopControl (wxString host, int port);
	virtual ~LoopControl();

	bool post_down_event (int index, wxString cmd);
	bool post_up_event (int index, wxString cmd);

	bool post_ctrl_change (int index, wxString ctrl, float val);
	
	void request_values (int index);
	void request_all_values (int index);
	void update_values();

	void register_input_controls(int index, bool unreg=false);

	
	bool is_updated (int index, wxString ctrl);
	
	bool get_value (int index, wxString ctrl, float &retval);
	bool get_state (int index, wxString & state);
	
  protected:

	static int _control_handler(const char *path, const char *types, lo_arg **argv, int argc,
				    void *data, void *user_data);

	int control_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data);
		

	void setup_param_map();
	
	wxString   _osc_url;
	lo_address _osc_addr;

	lo_server  _osc_server;
	wxString  _our_url;

	typedef std::map<wxString, float> ControlValMap;
	typedef std::vector<ControlValMap> ControlValMapList;
        ControlValMapList _params_val_map;

	std::map<int, wxString> state_map;

	typedef std::map<wxString, bool> UpdatedCtrlMap;
	typedef std::vector<ControlValMap> UpdatedCtrlMapList;
	UpdatedCtrlMapList _updated;
	
};

};

#endif