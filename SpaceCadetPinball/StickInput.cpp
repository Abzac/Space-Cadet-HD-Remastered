#include "pch.h"
#include "StickInput.h"
#include "pb.h"
#include "options.h"
#include "TPinballTable.h"
#include "TPlunger.h"

namespace
{
	struct Pad
	{
		int x=0, y=0;
		bool nudgeReady=true, pullReady=true;
		GameBindings nudgeAction=GameBindings::Max;
	};
	std::map<SDL_JoystickID,Pad> pads;
	SDL_JoystickID pulling=-1;
	const int rest=8500, nudgeThreshold=21000, pullThreshold=6900, releaseThreshold=4000;
	void releaseNudge(SDL_JoystickID device, Pad& pad)
	{
		if (pad.nudgeAction==GameBindings::Max) return;
		options::RouteInput({InputTypes::StickNudge, ~pad.nudgeAction},device,false,true);
		pad.nudgeAction=GameBindings::Max;
	}
}
void StickInput::Axis(SDL_JoystickID device, int axis, int value, bool gameplay)
{
	if (axis!=SDL_CONTROLLER_AXIS_LEFTX && axis!=SDL_CONTROLLER_AXIS_LEFTY && axis!=SDL_CONTROLLER_AXIS_RIGHTY) return;
	auto& pad=pads[device];
	auto table=pb::MainTable;
	const bool live=gameplay && table && pb::game_mode==GameModes::InGame && !table->TiltLockFlag;
	if (axis==SDL_CONTROLLER_AXIS_RIGHTY)
	{
		if (value<=releaseThreshold)
		{
			if (pulling==device)
			{
				if (live && options::Options.StickPlunger) table->Plunger->ReleaseAnalog();
				else if (table) table->Plunger->CancelAnalog();
				pulling=-1;
			}
			pad.pullReady=true;
		}
		else if (!live || !options::Options.StickPlunger)
			pad.pullReady=false;
		else if (pulling==device || (pulling==-1 && pad.pullReady && value>=pullThreshold))
		{
			float amount=float(value-releaseThreshold)/(32767-releaseThreshold);
			if (table->Plunger->SetAnalogPull(amount)) pulling=device;
		}
		return;
	}
	if (axis==SDL_CONTROLLER_AXIS_LEFTX) pad.x=value; else pad.y=value;
	if (std::max(std::abs(pad.x),std::abs(pad.y))<rest)
	{
		releaseNudge(device,pad);
		pad.nudgeReady=true; return;
	}
	if (!live || !options::Options.StickNudge) { pad.nudgeReady=false; return; }
	if (!pad.nudgeReady || std::max(std::abs(pad.x),std::abs(pad.y))<nudgeThreshold) return;
	pad.nudgeReady=false;
	if (std::abs(pad.x)>std::abs(pad.y))
		pad.nudgeAction=pad.x<0 ? GameBindings::LeftTableBump : GameBindings::RightTableBump;
	else if (pad.y<0) pad.nudgeAction=GameBindings::BottomTableBump;
	if (pad.nudgeAction!=GameBindings::Max)
		options::RouteInput({InputTypes::StickNudge, ~pad.nudgeAction},device,true,true);
}
void StickInput::Cancel()
{
	if (pulling!=-1 && pb::MainTable) pb::MainTable->Plunger->CancelAnalog();
	for (auto& item:pads)
	{
		releaseNudge(item.first,item.second);
		if (item.first==pulling) item.second.pullReady=false;
		if (std::max(std::abs(item.second.x),std::abs(item.second.y))>=rest) item.second.nudgeReady=false;
	}
	pulling=-1;
}
void StickInput::Remove(SDL_JoystickID device)
{
	if (device==pulling)
	{
		if (pb::MainTable) pb::MainTable->Plunger->CancelAnalog();
		pulling=-1;
	}
	auto it=pads.find(device);
	if (it!=pads.end()) { releaseNudge(device,it->second); pads.erase(it); }
}
