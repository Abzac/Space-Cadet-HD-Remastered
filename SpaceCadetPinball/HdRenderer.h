#pragma once

struct gdrv_bitmap8;
struct render_sprite;
struct zmap_header_type;
struct ColorRgba;
struct vector3;
struct BallOrientation;
struct rectangle_type;

// HD artwork uses its own coverage and a registered, enlarged DAT depth surface.
namespace HdRenderer
{
constexpr int Scale = 4;
void Init(gdrv_bitmap8* screen);
void Shutdown();
void ClearDepth(int width, int height, int x, int y);
void AnimateDischarges(const std::vector<render_sprite*>& sprites);
void TrackBall(render_sprite* sprite, const vector3& position, float radius, const BallOrientation& orientation);
void WallContact(const gdrv_bitmap8* bmp);
void SetReceiverDiagnosticBypass(bool enabled);
bool HasSpinnerFrames(const gdrv_bitmap8* carrier);
bool HasPlungerMotion();
void UpdatePlunger(render_sprite& sprite);
void PaintPlunger(const render_sprite& sprite, const rectangle_type& dirty);
void Fill(gdrv_bitmap8* dest, int width, int height, int x, int y, ColorRgba color);
void Copy(gdrv_bitmap8* dest, int width, int height, int x, int y, gdrv_bitmap8* src, int sourceX, int sourceY,
		  bool transparent);
void Paint(gdrv_bitmap8* dest, int width, int height, int x, int y, gdrv_bitmap8* src, int sourceX, int sourceY,
		   const uint16_t* sourceDepth, int sourceStride, int spinnerFrame = -1);
SDL_Texture* Frame();
void Effects(const std::vector<render_sprite*>& sprites, const std::vector<render_sprite*>& balls,
			 const zmap_header_type& depth, int boardWidth, const SDL_FRect& dest, float decorTime);
} // namespace HdRenderer
