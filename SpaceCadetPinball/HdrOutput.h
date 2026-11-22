#pragma once

// Presents the just-rendered SDL frame if the window is on an active HDR output.
// The caller presents normally through SDL when this returns false.
namespace HdrOutput
{
	// Diagnostic probe at normalized output coordinates. Returns Rec.709 luminance
	// from the actual FP16 pixel under the site when measurePeak is requested.
	struct LuminanceSite { float u, v, nits = -1.f; };
	bool Present(SDL_Window* window, SDL_Renderer* renderer, bool enabled, bool measurePeak=false,
	             bool testFloatBufferOnSDR=false, LuminanceSite* sites=nullptr, unsigned siteCount=0);
	const char* Status();
	float LastPeak();
	// Values from the FP16 swap chain before presentation, in nits (Rec.709 Y).
	struct MatrixMeasurement
	{
		float brightest, darkest;
		float litMean, redOverBlue, greenOverBlue;
		int litPixels;
	};
	MatrixMeasurement LastMatrix(unsigned index);
	bool Active();
	void Close();
}
