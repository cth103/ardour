#include <iostream>
#include <ctime>

#include "ardour/mixer_snapshot.h"
#include "ardour/audioengine.h"
#include "ardour/route_group.h"
#include "ardour/vca_manager.h"
#include "ardour/vca.h"

#include "pbd/stateful.h"
#include "pbd/id.h"
#include "pbd/i18n.h"

using namespace std;
using namespace ARDOUR;

MixerSnapshot::MixerSnapshot(Session* s)
    : id(0)
    , label("")
    , timestamp(time(0))
{   
    if(s)
        _session = s;
}

MixerSnapshot::~MixerSnapshot()
{
}

void MixerSnapshot::clear()
{
    timestamp = time(0);
    route_states.clear();
    group_states.clear();
    vca_states.clear();
}

void MixerSnapshot::snap(boost::shared_ptr<Route> route) 
{
    if(route) {
        cout << route->name() << endl;

        string name = route->name();
        XMLNode node (route->get_state());
        vector<string> slaves;

        State state {route->id(), (string) route->name(), node, slaves};
        route_states.push_back(state);

        //is it in a group?
        string group_name;
        node.get_property(X_("route-group"), group_name);

        RouteGroup* group = _session->route_group_by_name(group_name);

        if(group) {
            XMLNode node (group->get_state());
            State state {group->id(), group->name(), node, slaves};
            group_states.push_back(state);
        }

        //push back VCA's connected to this route
        VCAList vl = _session->vca_manager().vcas();
        for(VCAList::const_iterator i = vl.begin(); i != vl.end(); i++) {
            if(route->slaved_to((*i))) {
                XMLNode node ((*i)->get_state());
                vector<string> slaves;
                slaves.push_back(route->name());
                State state {(*i)->id(), (*i)->name(), node, slaves};
                vca_states.push_back(state);
            }
        }
    }
}

void MixerSnapshot::snap() 
{
    if(!_session)
        return;

    clear();
    
    RouteList rl = _session->get_routelist();

    if(rl.empty())
        return;

    for(RouteList::const_iterator i = rl.begin(); i != rl.end(); i++) {
        //copy current state
        XMLNode node ((*i)->get_state());
        
        //placeholder
        vector<string> slaves;

        State state {(*i)->id(), (string) (*i)->name(), node, slaves};
        route_states.push_back(state);
    }

    //push back groups
    list<RouteGroup*> gl = _session->route_groups();
    for(list<RouteGroup*>::const_iterator i = gl.begin(); i != gl.end(); i++) {
        //copy current state
        XMLNode node ((*i)->get_state());
        
        //placeholder
        vector<string> slaves;

        State state {(*i)->id(), (string) (*i)->name(), node, slaves};
        group_states.push_back(state);
    }
    
    //push back VCA's
    VCAList vl = _session->vca_manager().vcas();
    for(VCAList::const_iterator i = vl.begin(); i != vl.end(); i++) {
        //copy current state
        XMLNode node ((*i)->get_state());

        boost::shared_ptr<RouteList> rl = _session->get_tracks();

        vector<string> slaves;
        for(RouteList::const_iterator t = rl->begin(); t != rl->end(); t++) {
            if((*t)->slaved_to((*i))) {
                slaves.push_back((*t)->name());
            }
        }

        State state {(*i)->id(), (string) (*i)->name(), node, slaves};
        vca_states.push_back(state);
    }
}

void MixerSnapshot::recall() {
    
    //routes
    for(vector<State>::const_iterator i = route_states.begin(); i != route_states.end(); i++) {
        State state = (*i);
        
        boost::shared_ptr<Route> route = _session->route_by_id(state.id);
        
        if(!route)
            route = _session->route_by_name(state.name);

        if(route)
            route->set_state(state.node, PBD::Stateful::loading_state_version);
    }

    //groups
    for(vector<State>::const_iterator i = group_states.begin(); i != group_states.end(); i++) {
        State state = (*i);

        RouteGroup* group = _session->route_group_by_name(state.name);

        if(!group) {
            group = new RouteGroup(*_session, state.name);
            //notify session
            _session->add_route_group(group);
        }

        if(group) {
            group->set_state(state.node, PBD::Stateful::loading_state_version);
            group->changed();
        }
    }

    //vcas
    for(vector<State>::const_iterator i = vca_states.begin(); i != vca_states.end(); i++) {
        State state = (*i);

        boost::shared_ptr<VCA> vca = _session->vca_manager().vca_by_name(state.name);

        if(!vca) {
           VCAList vl = _session->vca_manager().create_vca(1, state.name);
           boost::shared_ptr<VCA> vca = vl.front();

           if(vca) {
               vca->set_state(state.node, PBD::Stateful::loading_state_version);
               for(vector<string>::const_iterator s = state.slaves.begin(); s != state.slaves.end(); s++) {
                   boost::shared_ptr<Route> route = _session->route_by_name((*s));
                   if(route) {route->assign(vca);}
                   continue;
               }
           }
        } else {
            vca->set_state(state.node, PBD::Stateful::loading_state_version);
            for(vector<string>::const_iterator s = state.slaves.begin(); s != state.slaves.end(); s++) {
                boost::shared_ptr<Route> route = _session->route_by_name((*s));
                if(route) {route->assign(vca);}
            }
        }
    }
}