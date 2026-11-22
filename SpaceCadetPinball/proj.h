#pragma once
#include "maths.h"

struct vector_type4
{
	float X;
	float Y;
	float Z;
	float W;
};

struct mat4_row_major
{
	vector_type4 Row0;
	vector_type4 Row1;
	vector_type4 Row2;
	vector_type4 Row3;
};


class proj
{
public:
	static void init(float* mat4x3, float d, float centerX, float centerY, float zMin, float zScaler);
	static vector3 matrix_vector_multiply(const mat4_row_major& mat, const vector3& vec);
	static float z_distance(const vector3& vec);
	static vector2i xform_to_2d(const vector3& vec);
	static vector2i xform_to_2d(const vector2& vec);
	static vector3 ReverseXForm(const vector2i& vec);
	static void recenter(float centerX, float centerY);
	static uint16_t NormalizeDepth(float depth);
	static float DepthUnitsPerPixel(uint16_t depth);
	static vector2 BallImage(const vector3& center, float radius, float& projectedRadius);
	static vector3 CameraPoint(const vector3& world);
	static vector3 CameraDirection(const vector3& world);
	static vector3 ViewRay(float x, float y);
	static vector2 ImagePoint(const vector3& camera);
	static vector3 WorldDirection(const vector3& direction);
	static float DepthDistance(uint16_t packed);
private:
	static mat4_row_major matrix;
	static float d_, centerx, centery;
	static float zscaler, zmin, zmax;
};
