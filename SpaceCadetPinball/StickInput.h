#pragma once
namespace StickInput
{
	void Axis(SDL_JoystickID device, int axis, int value, bool gameplay);
	void Cancel();
	void Remove(SDL_JoystickID device);
}
