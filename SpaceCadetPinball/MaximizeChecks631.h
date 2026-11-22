#pragma once

namespace maximizeChecks631
{
inline bool checkFrameSize(FILE* report, const std::string& prefix)
{
	auto renderer = winmain::Renderer;
	auto previous = SDL_GetRenderTarget(renderer);
	bool ok = true;
	for (SDL_Point size : {SDL_Point{2560,1440}, SDL_Point{900,624}})
	{
		auto target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, size.x, size.y);
		auto frame = SDL_CreateRGBSurfaceWithFormat(0, size.x, size.y, 32, SDL_PIXELFORMAT_ARGB8888);
		bool rendered = target && frame && SDL_SetRenderTarget(renderer, target) == 0;
		if (rendered)
		{
			fullscrn::window_size_changed();
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			render::PresentVScreen(pb::time_now);
			rendered = SDL_RenderReadPixels(renderer, nullptr, frame->format->format, frame->pixels, frame->pitch) == 0;
		}
		if (rendered)
		{
			const auto box = render::DestinationRect;
			int left = size.x, right = -1, top = size.y, bottom = -1;
			for (int y=0; y<size.y; ++y)
			{
				const auto row = reinterpret_cast<const Uint32*>(static_cast<const Uint8*>(frame->pixels) + y*frame->pitch);
				for (int x=0; x<size.x; ++x)
					if (row[x] & 0x00f0f0f0u)
					{
						left=std::min(left,x); right=std::max(right,x);
						top=std::min(top,y); bottom=std::max(bottom,y);
					}
			}
			const double idealWidth = size.y * (600. / 416.);
			ok &= box.h==size.y && std::abs(box.w-idealWidth)<1.1 &&
				std::abs(2*box.x+box.w-size.x)<=1 && top<box.h*.03 && bottom>box.h*.94 &&
				left>=box.x && left<box.x+box.w*.06 && right<box.x+box.w && right>box.x+box.w*.97;
			fprintf(report,"First frame at %dx%d: canvas %d,%d %dx%d, visible art x %d..%d, y %d..%d.\n",
				size.x,size.y,box.x,box.y,box.w,box.h,left,right,top,bottom);
			if (SDL_GetHintBoolean("SPACECADET_CAPTURE_STRIPS631",SDL_FALSE))
				SDL_SaveBMP(frame,(prefix+"first-size-"+std::to_string(size.x)+".bmp").c_str());
		}
		ok &= rendered;
		SDL_SetRenderTarget(renderer,previous);
		SDL_FreeSurface(frame);
		SDL_DestroyTexture(target);
	}
	fullscrn::window_size_changed();
	return ok;
}
}

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "SDL_syswm.h"

namespace maximizeChecks631
{
inline bool run(FILE* report)
{
	auto window = winmain::MainWindow;
	SDL_SysWMinfo wm{};
	SDL_VERSION(&wm.version);
	if (!SDL_GetWindowWMInfo(window, &wm) || wm.subsystem != SDL_SYSWM_WINDOWS) return false;
	const HWND hwnd = wm.info.win.window;
	const Uint32 id = SDL_GetWindowID(window);
	int initialWidth, initialHeight;
	SDL_GetWindowSize(window, &initialWidth, &initialHeight);
	winmain::SetWindowSize(900);
	RECT before{}, full{}, after{};
	GetWindowRect(hwnd, &before);
	int savedWidth, savedHeight;
	SDL_GetWindowSize(window, &savedWidth, &savedHeight);

	// Use the same system command as the maximize button in the Windows caption.
	SendMessage(hwnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
	bool received = false;
	SDL_Event event;
	const Uint64 until = SDL_GetTicks64() + 750;
	while (!received && SDL_GetTicks64() < until)
	{
		SDL_PumpEvents();
		while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_WINDOWEVENT, SDL_WINDOWEVENT) > 0)
			if (event.window.windowID == id && event.window.event == SDL_WINDOWEVENT_MAXIMIZED)
			{
				winmain::event_handler(&event);
				received = true;
			}
		if (!received) SDL_Delay(1);
	}
	GetWindowRect(hwnd, &full);
	MONITORINFO monitor{};
	monitor.cbSize = sizeof(monitor);
	const bool covers = GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) &&
		EqualRect(&full, &monitor.rcMonitor);
	const bool caption = (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION) != 0;
	const bool desktop = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
	const bool entered = received && covers && !caption && desktop && options::Options.FullScreen;
	if (options::Options.FullScreen) inputChecks::tap(SDLK_F4);
	// A queued notification from the maximized placement must not reopen fullscreen.
	inputChecks::windowEvent(SDL_WINDOWEVENT_MAXIMIZED);
	GetWindowRect(hwnd, &after);
	int width, height;
	SDL_GetWindowSize(window, &width, &height);
	const bool restored = width == savedWidth && height == savedHeight && EqualRect(&before, &after) &&
		(GetWindowLongPtr(hwnd, GWL_STYLE) & WS_CAPTION) && !options::Options.FullScreen &&
		!(SDL_GetWindowFlags(window) & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MAXIMIZED));
	fprintf(report, "Windows maximize command: event %d, desktop %d, covers monitor %d, caption %d; F4 restored %dx%d to %dx%d, placement %d.\n",
		received, desktop, covers, caption, savedWidth, savedHeight, width, height, EqualRect(&before, &after));
	winmain::SetWindowSize(initialWidth, initialHeight);
	return entered && restored;
}
}
#endif
