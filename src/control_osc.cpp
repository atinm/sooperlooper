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

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>

#include "control_osc.hpp"
#include "event_nonrt.hpp"
#include "engine.hpp"
#include "ringbuffer.hpp"
#include "version.h"

#include <lo/lo.h>
#include <sigc++/sigc++.h>
using namespace SigC;

using namespace SooperLooper;
using namespace std;

static void error_callback(int num, const char *m, const char *path)
{
#ifdef DEBUG
	fprintf(stderr, "liblo server error %d in path %s: %s\n", num, path, m);
#endif
}



ControlOSC::ControlOSC(Engine * eng, unsigned int port)
	: _engine(eng), _port(port)
{
	char tmpstr[255];

	_ok = false;
	_shutdown = false;
	_osc_server = 0;
	_osc_unix_server = 0;
	_osc_thread = 0;
	
	for (int j=0; j < 20; ++j) {
		snprintf(tmpstr, sizeof(tmpstr), "%d", _port);

		if ((_osc_server = lo_server_new (tmpstr, error_callback))) {
			break;
		}
#ifdef DEBUG		
		cerr << "can't get osc at port: " << _port << endl;
#endif
		_port++;
		continue;
	}

	/*** APPEARS sluggish for now
	     
	// attempt to create unix socket server too
	//snprintf(tmpstr, sizeof(tmpstr), "/tmp/sooperlooper_%d", getpid());
	snprintf(tmpstr, sizeof(tmpstr), "/tmp/sooperlooper_XXXXXX");
	int fd = mkstemp(tmpstr);
	if (fd >=0) {
		unlink(tmpstr);
		close(fd);
		_osc_unix_server = lo_server_new (tmpstr, error_callback);
		if (_osc_unix_server) {
			_osc_unix_socket_path = tmpstr;
		}
	}

	*/

	_engine->LoopAdded.connect(slot (*this, &ControlOSC::on_loop_added));
	_engine->LoopRemoved.connect(slot (*this, &ControlOSC::on_loop_removed));

	register_callbacks();

	// for all loops
	on_loop_added(-1);
	
	// lo_server_thread_add_method(_sthread, NULL, NULL, ControlOSC::_dummy_handler, this);

	// initialize string maps
	_str_cmd_map["record"]  = Event::RECORD;
	_str_cmd_map["overdub"]  = Event::OVERDUB;
	_str_cmd_map["multiply"]  = Event::MULTIPLY;
	_str_cmd_map["insert"]  = Event::INSERT;
	_str_cmd_map["replace"]  = Event::REPLACE;
	_str_cmd_map["reverse"]  = Event::REVERSE;
	_str_cmd_map["mute"]  = Event::MUTE;
	_str_cmd_map["undo"]  = Event::UNDO;
	_str_cmd_map["redo"]  = Event::REDO;
	_str_cmd_map["scratch"]  = Event::SCRATCH;
	_str_cmd_map["trigger"]  = Event::TRIGGER;
	_str_cmd_map["oneshot"]  = Event::ONESHOT;
	
	for (map<string, Event::command_t>::iterator iter = _str_cmd_map.begin(); iter != _str_cmd_map.end(); ++iter) {
		_cmd_str_map[(*iter).second] = (*iter).first;
	}

	_str_ctrl_map["rec_thresh"]  = Event::TriggerThreshold;
	_str_ctrl_map["feedback"]  = Event::Feedback;
	_str_ctrl_map["use_feedback_play"]  = Event::UseFeedbackPlay;
	_str_ctrl_map["dry"]  = Event::DryLevel;
	_str_ctrl_map["wet"]  = Event::WetLevel;
	_str_ctrl_map["rate"]  = Event::Rate;
	_str_ctrl_map["scratch_pos"]  = Event::ScratchPosition;
	_str_ctrl_map["tap_trigger"]  = Event::TapDelayTrigger;
	_str_ctrl_map["quantize"]  = Event::Quantize;
	_str_ctrl_map["round"]  = Event::Round;
	_str_ctrl_map["redo_is_tap"]  = Event::RedoTap;
	_str_ctrl_map["sync"]  = Event::SyncMode;
	_str_ctrl_map["use_rate"]  = Event::UseRate;
	_str_ctrl_map["fade_samples"]  = Event::FadeSamples;
	_str_ctrl_map["waiting"]  = Event::Waiting;
	_str_ctrl_map["state"]  = Event::State;
	_str_ctrl_map["loop_len"]  = Event::LoopLength;
	_str_ctrl_map["loop_pos"]  = Event::LoopPosition;
	_str_ctrl_map["cycle_len"]  = Event::CycleLength;
	_str_ctrl_map["free_time"]  = Event::FreeTime;
	_str_ctrl_map["total_time"]  = Event::TotalTime;
	_str_ctrl_map["midi_start"] = Event::MidiStart;
	_str_ctrl_map["midi_stop"] = Event::MidiStop;
	_str_ctrl_map["midi_tick"] = Event::MidiTick;
	
	// global params
	_str_ctrl_map["tempo"] = Event::Tempo;
	_str_ctrl_map["eighth_per_cycle"] = Event::EighthPerCycle;
	_str_ctrl_map["sync_source"] = Event::SyncTo;
	_str_ctrl_map["tap_tempo"] = Event::TapTempo;

	for (map<string, Event::control_t>::iterator iter = _str_ctrl_map.begin(); iter != _str_ctrl_map.end(); ++iter) {
		_ctrl_str_map[(*iter).second] = (*iter).first;
	}

	if (!init_osc_thread()) {
		return;
	}
	

	_ok = true;
}

ControlOSC::~ControlOSC()
{
	if (!_osc_unix_socket_path.empty()) {
		// unlink it
		unlink(_osc_unix_socket_path.c_str());
	}

	// stop server thread
	terminate_osc_thread();
}

void
ControlOSC::register_callbacks()
{
	lo_server srvs[2];
	lo_server serv;

	srvs[0] = _osc_server;
	srvs[1] = _osc_unix_server;
	
	for (size_t i=0; i < 2; ++i) {
		if (!srvs[i]) continue;
		serv = srvs[i];


		/* add method that will match the path /quit with no args */
		lo_server_add_method(serv, "/quit", "", ControlOSC::_quit_handler, this);

		// add ping handler:  s:returl s:retpath
		lo_server_add_method(serv, "/ping", "ss", ControlOSC::_ping_handler, this);

		// add loop add handler:  i:channels  i:bytes_per_channel
		lo_server_add_method(serv, "/loop_add", "if", ControlOSC::_loop_add_handler, this);

		// add loop del handler:  i:index 
		lo_server_add_method(serv, "/loop_del", "i", ControlOSC::_loop_del_handler, this);

		// un/register config handler:  s:returl  s:retpath
		lo_server_add_method(serv, "/register", "ss", ControlOSC::_register_config_handler, this);
		lo_server_add_method(serv, "/unregister", "ss", ControlOSC::_unregister_config_handler, this);

		lo_server_add_method(serv, "/set", "sf", ControlOSC::_global_set_handler, this);
		lo_server_add_method(serv, "/get", "sss", ControlOSC::_global_get_handler, this);

		// un/register_update args= s:ctrl s:returl s:retpath
		lo_server_add_method(serv, "/register_update", "sss", ControlOSC::_global_register_update_handler, this);
		lo_server_add_method(serv, "/unregister_update", "sss", ControlOSC::_global_unregister_update_handler, this);

		// certain RT global ctrls
		lo_server_add_method(serv, "/sl/-2/set", "sf", ControlOSC::_set_handler, new CommandInfo(this, -2, Event::type_global_control_change));

		// get all midi bindings:  s:returl s:retpath
		lo_server_add_method(serv, "/get_all_midi_bindings", "ss", ControlOSC::_midi_binding_handler,
				     new MidiBindCommand(this, MidiBindCommand::GetAllBinding));

		// remove a specific midi binding:  s:binding_serialization
		lo_server_add_method(serv, "/remove_midi_binding", "s", ControlOSC::_midi_binding_handler,
				     new MidiBindCommand(this, MidiBindCommand::RemoveBinding));

		// add a specific midi binding:  s:binding_serialization
		lo_server_add_method(serv, "/add_midi_binding", "s", ControlOSC::_midi_binding_handler,
				     new MidiBindCommand(this, MidiBindCommand::AddBinding));
		
		// MIDI clock
		lo_server_add_method(serv, "/sl/midi_start", NULL, ControlOSC::_midi_start_handler, this);
		lo_server_add_method(serv, "/sl/midi_stop", NULL, ControlOSC::_midi_stop_handler, this);
		lo_server_add_method(serv, "/sl/midi_tick", NULL, ControlOSC::_midi_tick_handler, this);
	
	}
}

bool
ControlOSC::init_osc_thread ()
{
	// create new thread to run server
	if (pipe (_request_pipe)) {
		cerr << "Cannot create osc request signal pipe" <<  strerror (errno) << endl;
		return false;
	}

	if (fcntl (_request_pipe[0], F_SETFL, O_NONBLOCK)) {
		cerr << "osc: cannot set O_NONBLOCK on signal read pipe " << strerror (errno) << endl;
		return false;
	}

	if (fcntl (_request_pipe[1], F_SETFL, O_NONBLOCK)) {
		cerr << "osc: cannot set O_NONBLOCK on signal write pipe " << strerror (errno) << endl;
		return false;
	}
	
	pthread_create (&_osc_thread, NULL, &ControlOSC::_osc_receiver, this);
	if (!_osc_thread) {
		return false;
	}

	//pthread_detach (_osc_thread);
	return true;
}

void
ControlOSC::terminate_osc_thread ()
{
	void* status;

	_shutdown = true;

	poke_osc_thread ();

	pthread_join (_osc_thread, &status);
}

void
ControlOSC::poke_osc_thread ()
{
	char c;

	if (write (_request_pipe[1], &c, 1) != 1) {
		cerr << "cannot send signal to osc thread! " <<  strerror (errno) << endl;
	}
}


void
ControlOSC::on_loop_added (int instance)
{
	// will be called from main event loop

	char tmpstr[255];
#ifdef DEBUG
	cerr << "loop added: " << instance << endl;
#endif
	lo_server srvs[2];
	lo_server serv;

	srvs[0] = _osc_server;
	srvs[1] = _osc_unix_server;
	
	for (size_t i=0; i < 2; ++i) {
		if (!srvs[i]) continue;
		serv = srvs[i];
		
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/down", instance);
		lo_server_add_method(serv, tmpstr, "s", ControlOSC::_updown_handler, new CommandInfo(this, instance, Event::type_cmd_down));
	
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/up", instance);
		lo_server_add_method(serv, tmpstr, "s", ControlOSC::_updown_handler, new CommandInfo(this, instance, Event::type_cmd_up));
	
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/set", instance);
		lo_server_add_method(serv, tmpstr, "sf", ControlOSC::_set_handler, new CommandInfo(this, instance, Event::type_control_change));
	
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/get", instance);
		lo_server_add_method(serv, tmpstr, "sss", ControlOSC::_get_handler, new CommandInfo(this, instance, Event::type_control_request));

		// load loop:  s:filename  s:returl  s:retpath
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/load_loop", instance);
		lo_server_add_method(serv, tmpstr, "sss", ControlOSC::_loadloop_handler, new CommandInfo(this, instance, Event::type_control_request));

		// save loop:  s:filename  s:format s:endian s:returl  s:retpath
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/save_loop", instance);
		lo_server_add_method(serv, tmpstr, "sssss", ControlOSC::_saveloop_handler, new CommandInfo(this, instance, Event::type_control_request));
	
		// register_update args= s:ctrl s:returl s:retpath
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/register_update", instance);
		lo_server_add_method(serv, tmpstr, "sss", ControlOSC::_register_update_handler, new CommandInfo(this, instance, Event::type_control_request));

		// unregister_update args= s:ctrl s:returl s:retpath
		snprintf(tmpstr, sizeof(tmpstr), "/sl/%d/unregister_update", instance);
		lo_server_add_method(serv, tmpstr, "sss", ControlOSC::_unregister_update_handler, new CommandInfo(this, instance, Event::type_control_request));
	}
	
	send_all_config();
}

void
ControlOSC::on_loop_removed ()
{
	// will be called from main event loop
	send_all_config();
}


std::string
ControlOSC::get_server_url()
{
	string url;
	char * urlstr;

	if (_osc_server) {
		urlstr = lo_server_get_url (_osc_server);
		url = urlstr;
		free (urlstr);
	}
	
	return url;
}

std::string
ControlOSC::get_unix_server_url()
{
	string url;
	char * urlstr;

	if (_osc_unix_server) {
		urlstr = lo_server_get_url (_osc_unix_server);
		url = urlstr;
		free (urlstr);
	}
	
	return url;
}


/* server thread */

void *
ControlOSC::_osc_receiver(void * arg)
{
	static_cast<ControlOSC*> (arg)->osc_receiver();
	return 0;
}

void
ControlOSC::osc_receiver()
{
	struct pollfd pfd[3];
	int fds[3];
	lo_server srvs[3];
	int nfds = 0;
	int timeout = -1;

	fds[0] = _request_pipe[0];
	nfds++;
	
	if (_osc_server && lo_server_get_socket_fd(_osc_server) >= 0) {
		fds[nfds] = lo_server_get_socket_fd(_osc_server);
		srvs[nfds] = _osc_server;
		nfds++;
	}

	if (_osc_unix_server && lo_server_get_socket_fd(_osc_unix_server) >= 0) {
		fds[nfds] = lo_server_get_socket_fd(_osc_unix_server);
		srvs[nfds] = _osc_unix_server;
		nfds++;
	}
	
	
	while (!_shutdown) {

		for (int i=0; i < nfds; ++i) {
			pfd[i].fd = fds[i];
			pfd[i].events = POLLIN|POLLHUP|POLLERR;
		}
		
	again:
		// cerr << "poll on " << nfds << " for " << timeout << endl;
		if (poll (pfd, nfds, timeout) < 0) {
			if (errno == EINTR) {
				/* gdb at work, perhaps */
				goto again;
			}
			
			cerr << "OSC thread poll failed: " <<  strerror (errno) << endl;
			
			break;
		}

		if (_shutdown) {
			break;
		}
		
		if ((pfd[0].revents & ~POLLIN)) {
			cerr << "OSC: error polling extra port" << endl;
			break;
		}

		for (int i=1; i < nfds; ++i) {
			if (pfd[i].revents & POLLIN)
			{
				// this invokes callbacks
				lo_server_recv (srvs[i]);
			}
		}

	}

	cerr << "got shutdown" << endl;
	
	if (_osc_server) {
		lo_server_free (_osc_server);
		_osc_server = 0;
	}

	if (_osc_unix_server) {
		cerr << "freeing unix server" << endl;
		lo_server_free (_osc_unix_server);
		_osc_unix_server = 0;
	}
	
}



/* STATIC callbacks */


int ControlOSC::_dummy_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
#ifdef DEBUG
	cerr << "got path: " << path << endl;
#endif
	return 0;
}


int ControlOSC::_quit_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->quit_handler (path, types, argv, argc, data);

}

int ControlOSC::_ping_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->ping_handler (path, types, argv, argc, data);

}

int ControlOSC::_global_set_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->global_set_handler (path, types, argv, argc, data);

}
int ControlOSC::_global_get_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->global_get_handler (path, types, argv, argc, data);

}


int ControlOSC::_updown_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->updown_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_set_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->set_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_get_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->get_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_register_update_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->register_update_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_unregister_update_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->unregister_update_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_loop_add_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->loop_add_handler (path, types, argv, argc, data);
}

int ControlOSC::_loop_del_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->loop_del_handler (path, types, argv, argc, data);
}

int ControlOSC::_register_config_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->register_config_handler (path, types, argv, argc, data);
}

int ControlOSC::_unregister_config_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->unregister_config_handler (path, types, argv, argc, data);
}

int ControlOSC::_loadloop_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->loadloop_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_saveloop_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	CommandInfo * cp = static_cast<CommandInfo*> (user_data);
	return cp->osc->saveloop_handler (path, types, argv, argc, data, cp);
}

int ControlOSC::_global_register_update_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->global_register_update_handler (path, types, argv, argc, data);
}

int ControlOSC::_global_unregister_update_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->global_unregister_update_handler (path, types, argv, argc, data);
}

int ControlOSC::_midi_start_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->midi_start_handler (path, types, argv, argc, data);
}

int ControlOSC::_midi_stop_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->midi_stop_handler (path, types, argv, argc, data);
}

int ControlOSC::_midi_tick_handler(const char *path, const char *types, lo_arg **argv, int argc,
			 void *data, void *user_data)
{
	ControlOSC * osc = static_cast<ControlOSC*> (user_data);
	return osc->midi_tick_handler (path, types, argv, argc, data);
}

int ControlOSC::_midi_binding_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
	MidiBindCommand * cp = static_cast<MidiBindCommand*> (user_data);
	return cp->osc->midi_binding_handler (path, types, argv, argc, data, cp);
}


/* real callbacks */

int ControlOSC::quit_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	_engine->quit();
	return 0;
}


int ControlOSC::ping_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	string returl (&argv[0]->s);
	string retpath (&argv[1]->s);

	_engine->push_nonrt_event ( new PingEvent (returl, retpath));
	
	return 0;
}


int ControlOSC::global_get_handler(const char *path, const char *types, lo_arg **argv, int argc,void *data)
{
	// s: param  s: returl  s: retpath
	string param (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	_engine->push_nonrt_event ( new GlobalGetEvent (param, returl, retpath));
	return 0;
}

int ControlOSC::global_set_handler(const char *path, const char *types, lo_arg **argv, int argc,void *data)
{
	// s: param  f: val
	string param(&argv[0]->s);
	float val  = argv[1]->f;

	_engine->push_nonrt_event ( new GlobalSetEvent (param, val));

	// send out updates to registered in main event loop
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Send, -2, to_control_t(param), "", "", val));
	
	return 0;
}

int
ControlOSC::global_register_update_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// un/register_update args= s:ctrl s:returl s:retpath
	string ctrl (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	// -2 means global
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Register, -2, to_control_t(ctrl), returl, retpath));

	return 0;
}

int
ControlOSC::global_unregister_update_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// 1st is return URL string 2nd is retpath
	string ctrl (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	// -2 means global
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Unregister, -2, to_control_t(ctrl), returl, retpath));

	return 0;
}


int
ControlOSC::midi_binding_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, MidiBindCommand * info)
{

	return 0;
}


int
ControlOSC::midi_start_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	_engine->push_sync_event (Event::MidiStart);
	return 0;
}


int
ControlOSC::midi_stop_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	_engine->push_sync_event (Event::MidiStop);
	return 0;
}


int
ControlOSC::midi_tick_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	_engine->push_sync_event (Event::MidiTick);
	return 0;
}


int ControlOSC::updown_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{
	// first arg is a string
	
	string cmd(&argv[0]->s);

	_engine->push_command_event(info->type, to_command_t(cmd), info->instance);
	
	return 0;
}


int ControlOSC::set_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{

	// first arg is a control string, 2nd is float val

	string ctrl(&argv[0]->s);
	float val  = argv[1]->f;

	_engine->push_control_event(info->type, to_control_t(ctrl), val, info->instance);


	// send out updates to registered in main event loop
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Send, info->instance, to_control_t(ctrl), "", "", val));
	
	return 0;

}

int ControlOSC::loop_add_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// 1st is an int #channels
	// 2nd is a float #bytes per channel (if 0, use engine default) 
	
	int channels = argv[0]->i;
	float secs = argv[1]->f;

	_engine->push_nonrt_event ( new ConfigLoopEvent (ConfigLoopEvent::Add, channels, secs, 0));
	
	return 0;
}

int ControlOSC::loop_del_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// 1st is index of loop to delete
	
	int index = argv[0]->i;

	_engine->push_nonrt_event ( new ConfigLoopEvent (ConfigLoopEvent::Remove, 0, 0.0f, index));

	return 0;
}

int ControlOSC::loadloop_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{

	// first arg is fname, 2nd is return URL string 3rd is retpath
	string fname (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	_engine->push_nonrt_event ( new LoopFileEvent (LoopFileEvent::Load, info->instance, fname, returl, retpath));
	
	return 0;
}

int ControlOSC::saveloop_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{
	// save loop:  s:filename  s:format s:endian s:returl  s:retpath
	string fname (&argv[0]->s);
	string format (&argv[1]->s);
	string endian (&argv[2]->s);
	string returl (&argv[3]->s);
	string retpath (&argv[4]->s);

	LoopFileEvent::FileFormat fmt = LoopFileEvent::FormatFloat;
	LoopFileEvent::Endian end = LoopFileEvent::LittleEndian;

	if (format == "float") {
		fmt = LoopFileEvent::FormatFloat;
	}
	else if (format == "pcm16") {
		fmt = LoopFileEvent::FormatPCM16;
	}
	else if (format == "pcm24") {
		fmt = LoopFileEvent::FormatPCM24;
	}
	else if (format == "pcm32") {
		fmt = LoopFileEvent::FormatPCM32;
	}

	if (endian == "big") {
		end = LoopFileEvent::BigEndian;
	}
	
	// push this onto a queue for the main event loop to process
	_engine->push_nonrt_event ( new LoopFileEvent (LoopFileEvent::Save, info->instance, fname, returl, retpath, fmt, end));
	
	return 0;
}


lo_address
ControlOSC::find_or_cache_addr(string returl)
{
	lo_address addr = 0;
	
	if (_retaddr_map.find(returl) == _retaddr_map.end()) {
		addr = lo_address_new_from_url (returl.c_str());
		if (lo_address_errno (addr) < 0) {
			fprintf(stderr, "addr error %d: %s\n", lo_address_errno(addr), lo_address_errstr(addr));
		}
		_retaddr_map[returl] = addr;
	}
	else {
		addr = _retaddr_map[returl];
	}
	
	return addr;
}

int ControlOSC::get_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{
	// cerr << "get " << path << endl;

	// first arg is control string, 2nd is return URL string 3rd is retpath
	string ctrl (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	_engine->push_nonrt_event ( new GetParamEvent (info->instance, to_control_t(ctrl), returl, retpath));
	
	return 0;
}

int ControlOSC::register_update_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{
	// first arg is control string, 2nd is return URL string 3rd is retpath
	string ctrl (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Register, info->instance, to_control_t(ctrl), returl, retpath));
	
	return 0;
}

int ControlOSC::unregister_update_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, CommandInfo *info)
{

	// first arg is control string, 2nd is return URL string 3rd is retpath
	string ctrl (&argv[0]->s);
	string returl (&argv[1]->s);
	string retpath (&argv[2]->s);

	// push this onto a queue for the main event loop to process
	_engine->push_nonrt_event ( new ConfigUpdateEvent (ConfigUpdateEvent::Unregister, info->instance, to_control_t(ctrl), returl, retpath));
	
	return 0;
}

int
ControlOSC::register_config_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// 1st is return URL string 2nd is retpath
	string returl (&argv[0]->s);
	string retpath (&argv[1]->s);

	_engine->push_nonrt_event ( new RegisterConfigEvent (RegisterConfigEvent::Register, returl, retpath));
	
	return 0;
}

int
ControlOSC::unregister_config_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data)
{
	// 1st is return URL string 2nd is retpath
	string returl (&argv[0]->s);
	string retpath (&argv[1]->s);

	_engine->push_nonrt_event ( new RegisterConfigEvent (RegisterConfigEvent::Unregister, returl, retpath));
	return 0;
}


void
ControlOSC::finish_get_event (GetParamEvent & event)
{
	// called from the main event loop (not osc thread)
	string ctrl (to_control_str(event.control));
	string returl (event.ret_url);
	string retpath (event.ret_path);
	lo_address addr;

	addr = find_or_cache_addr (returl);
	if (!addr) {
		return;
	}
	
//	 cerr << "sending to " << returl << "  path: " << retpath << "  ctrl: " << ctrl << "  val: " <<  event.ret_value << endl;

	if (lo_send(addr, retpath.c_str(), "isf", event.instance, ctrl.c_str(), event.ret_value) == -1) {
		fprintf(stderr, "OSC error %d: %s\n", lo_address_errno(addr), lo_address_errstr(addr));
	}
	
}

void
ControlOSC::finish_global_get_event (GlobalGetEvent & event)
{
	// called from the main event loop (not osc thread)
	string param (event.param);
	string returl (event.ret_url);
	string retpath (event.ret_path);
	lo_address addr;

	addr = find_or_cache_addr (returl);
	if (!addr) {
		return;
	}
	
	// cerr << "sending to " << returl << "  path: " << retpath << "  ctrl: " << param << "  val: " <<  event.ret_value << endl;

	if (lo_send(addr, retpath.c_str(), "isf", -2, param.c_str(), event.ret_value) == -1) {
		fprintf(stderr, "OSC error %d: %s\n", lo_address_errno(addr), lo_address_errstr(addr));
	}
	
}


void ControlOSC::finish_update_event (ConfigUpdateEvent & event)
{
	// called from the main event loop (not osc thread)

	lo_address addr;
	string retpath = event.ret_path;
	string returl  = event.ret_url;
	string ctrl    = to_control_str (event.control);
	
	if (event.type == ConfigUpdateEvent::Send)
	{
		if (event.instance == -1) {
			for (unsigned int i = 0; i < _engine->loop_count_unsafe(); ++i) {
				send_registered_updates (ctrl, event.value, (int) i);
			}
		} else {
			send_registered_updates (ctrl, event.value, event.instance);
		}

	}
	else if (event.type == ConfigUpdateEvent::Register)
	{
		if ((addr = find_or_cache_addr (returl)) == 0) {
			return;
		}
				
		// add this to register_ctrl map
		InstancePair ipair(event.instance, ctrl);
		ControlRegistrationMap::iterator iter = _registration_map.find (ipair);
		
		if (iter == _registration_map.end()) {
			_registration_map[ipair] = UrlList();
			iter = _registration_map.find (ipair);
		}
		
		UrlList & ulist = (*iter).second;
		UrlPair upair(addr, retpath);
		
		if (find(ulist.begin(), ulist.end(), upair) == ulist.end()) {
#ifdef DEBUG
			cerr << "registered " << ctrl << "  " << returl << endl;
#endif
			ulist.push_back (upair);
		}
		
		
	}
	else if (event.type == ConfigUpdateEvent::Unregister) {

		if ((addr = find_or_cache_addr (returl)) == 0) {
			return;
		}
		
		// add this to register_ctrl map
		InstancePair ipair(event.instance, ctrl);
		ControlRegistrationMap::iterator iter = _registration_map.find (ipair);
		
		if (iter != _registration_map.end()) {
			UrlList & ulist = (*iter).second;
			UrlPair upair(addr, retpath);
			UrlList::iterator uiter = find(ulist.begin(), ulist.end(), upair);
			
			if (uiter != ulist.end()) {
#ifdef DEBUG
				cerr << "unregistered " << ctrl << "  " << returl << endl;
#endif
				ulist.erase (uiter);
			}
		}
		

	}
}

void ControlOSC::finish_loop_config_event (ConfigLoopEvent &event)
{
	if (event.type == ConfigLoopEvent::Remove) {
		// unregister everything for this instance
		ControlRegistrationMap::iterator citer = _registration_map.begin();
		ControlRegistrationMap::iterator tmp;
		
		while ( citer != _registration_map.end()) {
			if ((*citer).first.first == event.index) {
				tmp = citer;
				++tmp;
				_registration_map.erase(citer);
				citer = tmp;
			}
			else {
				++citer;
			}
		}
	}
}

void
ControlOSC::send_registered_updates(string ctrl, float val, int instance)
{
	InstancePair ipair(instance, ctrl);
	ControlRegistrationMap::iterator iter = _registration_map.find (ipair);

	if (iter != _registration_map.end()) {
		UrlList & ulist = (*iter).second;

		for (UrlList::iterator url = ulist.begin(); url != ulist.end(); ++url)
		{
		        lo_address addr = (*url).first;
			
			if (lo_send(addr, (*url).second.c_str(), "isf", instance, ctrl.c_str(), val) == -1) {
				fprintf(stderr, "OSC error %d: %s\n", lo_address_errno(addr), lo_address_errstr(addr));
			}
		}
	}
	else {
#ifdef DEBUG
		cerr << "not in map: " << instance << " ctrL: " << ctrl << endl;
#endif
	}
}


void
ControlOSC::finish_register_event (RegisterConfigEvent &event)
{
	AddrPathPair apair(event.ret_url, event.ret_path);
	AddressList::iterator iter = find (_config_registrations.begin(), _config_registrations.end(), apair);

	if (iter == _config_registrations.end()) {
		_config_registrations.push_back (apair);
	}
}

void ControlOSC::send_all_config ()
{
	// for now just send pingacks to all registered addresses
	for (AddressList::iterator iter = _config_registrations.begin(); iter != _config_registrations.end(); ++iter)
	{
		send_pingack (true, (*iter).first, (*iter).second);
	}
}


void ControlOSC::send_pingack (bool useudp, string returl, string retpath)
{
	lo_address addr;

	addr = find_or_cache_addr (returl);
	if (!addr) {
		return;
	}

	string oururl;

	if (!useudp) {
		oururl = get_unix_server_url();
	}

	// default to udp
	if (oururl.empty()) {
		oururl = get_server_url();
	}
	
	// cerr << "sending to " << returl << "  path: " << retpath  << endl;
	if (lo_send(addr, retpath.c_str(), "ssi", oururl.c_str(), sooperlooper_version, _engine->loop_count_unsafe()) == -1) {
		fprintf(stderr, "OSC error %d: %s\n", lo_address_errno(addr), lo_address_errstr(addr));
	}
}



Event::command_t  ControlOSC::to_command_t (std::string cmd)
{
	map<string,Event::command_t>::iterator result = _str_cmd_map.find(cmd);

	if (result == _str_cmd_map.end()) {
		return Event::UNKNOWN;
	}

	return (*result).second;
}

std::string  ControlOSC::to_command_str (Event::command_t cmd)
{
	map<Event::command_t, string>::iterator result = _cmd_str_map.find(cmd);

	if (result == _cmd_str_map.end()) {
		return "unknown";
	}

	return (*result).second;
}


Event::control_t
ControlOSC::to_control_t (std::string cmd)
{
	map<string, Event::control_t>::iterator result = _str_ctrl_map.find(cmd);

	if (result == _str_ctrl_map.end()) {
		return Event::Unknown;
	}

	return (*result).second;

	
}

std::string
ControlOSC::to_control_str (Event::control_t cmd)
{
	map<Event::control_t,string>::iterator result = _ctrl_str_map.find(cmd);

	if (result == _ctrl_str_map.end()) {
		return "unknown";
	}

	return (*result).second;
}


