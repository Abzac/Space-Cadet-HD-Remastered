#pragma once
#include "maths.h"

// Camera-space triangles recovered from the DAT range surface. Dirty screen
// regions refit their bounding boxes when a target or flipper moves.
namespace TableRelief
{
struct Hit
{
	vector3 position{}, normal{};
	vector2 uv{};
	float distance{};
};
void Reset();
size_t FaceCount();
void Update(int boardWidth, int height, int x, int y, int width, int rows, const uint16_t* depth, int stride,
			int scale);
bool Trace(const vector3& origin, const vector3& direction, float limit, Hit& hit);
bool Surface(int x, int y, vector3& position, vector3& normal);
} // namespace TableRelief
