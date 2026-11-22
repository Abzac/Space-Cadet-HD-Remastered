#include "pch.h"
#include "TableRelief.h"
#include "proj.h"
#include <limits>

namespace
{
struct Box
{
	vector3 lo{1.e20f, 1.e20f, 1.e20f}, hi{-1.e20f, -1.e20f, -1.e20f};
	void include(const vector3& p)
	{
		lo.X = std::min(lo.X, p.X);
		lo.Y = std::min(lo.Y, p.Y);
		lo.Z = std::min(lo.Z, p.Z);
		hi.X = std::max(hi.X, p.X);
		hi.Y = std::max(hi.Y, p.Y);
		hi.Z = std::max(hi.Z, p.Z);
	}
};
struct Node
{
	Box box;
	int x{}, y{}, w{}, h{}, left{-1}, right{-1};
};
struct Vertex
{
	vector3 position{}, ray{};
	uint16_t range{65535};
};
struct Face
{
	vector3 normal{};
	float plane{};
};
struct Cell
{
	Face upper, lower;
};
std::vector<Cell> cells;
std::vector<Node> nodes;
std::vector<Vertex> points;
int pitch{}, height{};

int partition(int x, int y, int w, int h)
{
	const int index = int(nodes.size());
	nodes.emplace_back();
	nodes[index].x = x;
	nodes[index].y = y;
	nodes[index].w = w;
	nodes[index].h = h;
	if (w > 4 || h > 4)
	{
		int left, right;
		if (w >= h)
		{
			const int a = w / 2;
			left = partition(x, y, a, h);
			right = partition(x + a, y, w - a, h);
		}
		else
		{
			const int a = h / 2;
			left = partition(x, y, w, a);
			right = partition(x, y + a, w, h - a);
		}
		nodes[index].left = left;
		nodes[index].right = right;
	}
	return index;
}

void refit(int index, int x, int y, int right, int bottom)
{
	auto& n = nodes[index];
	if (x > n.x + n.w || right <= n.x || y > n.y + n.h || bottom <= n.y) return;
	Box box;
	if (n.left >= 0)
	{
		for (int child : {n.left, n.right})
		{
			refit(child, x, y, right, bottom);
			const auto& c = nodes[child].box;
			if (c.lo.X <= c.hi.X)
			{
				box.include(c.lo);
				box.include(c.hi);
			}
		}
	}
	else
	{
		for (int yy = n.y; yy <= n.y + n.h; ++yy)
			for (int xx = n.x; xx <= n.x + n.w; ++xx)
			{
				const auto& v = points[yy * pitch + xx];
				if (v.range != 65535) box.include(v.position);
			}
	}
	n.box = box;
}

float dot(const vector3& a, const vector3& b)
{
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}
struct Ray
{
	vector3 origin, direction, inverse;
};
float entry(const Ray& ray, const Box& box, float limit)
{
	if (box.lo.X > box.hi.X) return limit;
	const float ax = (box.lo.X - ray.origin.X) * ray.inverse.X, bx = (box.hi.X - ray.origin.X) * ray.inverse.X;
	const float ay = (box.lo.Y - ray.origin.Y) * ray.inverse.Y, by = (box.hi.Y - ray.origin.Y) * ray.inverse.Y;
	const float az = (box.lo.Z - ray.origin.Z) * ray.inverse.Z, bz = (box.hi.Z - ray.origin.Z) * ray.inverse.Z;
	const float lo = std::max(.003f, std::max(std::min(ax, bx), std::max(std::min(ay, by), std::min(az, bz))));
	const float hi = std::min(limit, std::min(std::max(ax, bx), std::min(std::max(ay, by), std::max(az, bz))));
	return hi >= lo ? lo : limit;
}

Face makeFace(const Vertex& a, const Vertex& b, const Vertex& c)
{
	Face face;
	const int nearest = std::min(a.range, std::min(b.range, c.range));
	const int farthest = std::max(a.range, std::max(b.range, c.range));
	if (farthest == 65535 || farthest - nearest > 2100) return face;
	const auto ab = maths::vector_sub(b.position, a.position), ac = maths::vector_sub(c.position, a.position);
	maths::cross(ab, ac, face.normal);
	face.plane = dot(face.normal, a.position);
	return face;
}

void triangle(const Ray& ray, const Face& face, int x, int y, bool upper, TableRelief::Hit& hit)
{
	const float denominator = dot(ray.direction, face.normal);
	if (fabsf(denominator) < 1.e-9f) return;
	const float t = (face.plane - dot(ray.origin, face.normal)) / denominator;
	if (t <= .003f || t >= hit.distance) return;
	const vector3 p{ray.origin.X + t * ray.direction.X, ray.origin.Y + t * ray.direction.Y,
					ray.origin.Z + t * ray.direction.Z};
	const auto uv = proj::ImagePoint(p);
	const float u = uv.X - x - .5f, v = uv.Y - y - .5f;
	if (u < 0.f || u > 1.f || v < 0.f || v > 1.f || (v <= u) != upper) return;
	hit.distance = t;
	hit.position = p;
	hit.uv = uv;
	hit.normal = face.normal;
}
void traceNode(int index, const Ray& ray, TableRelief::Hit& hit)
{
	const auto& n = nodes[index];
	if (n.left >= 0)
	{
		int a = n.left, b = n.right;
		float da = entry(ray, nodes[a].box, hit.distance), db = entry(ray, nodes[b].box, hit.distance);
		if (db < da)
		{
			std::swap(da, db);
			std::swap(a, b);
		}
		if (da < hit.distance) traceNode(a, ray, hit);
		if (db < hit.distance) traceNode(b, ray, hit);
		return;
	}
	for (int y = n.y; y < n.y + n.h; ++y)
		for (int x = n.x; x < n.x + n.w; ++x)
		{
			const auto& cell = cells[y * pitch + x];
			triangle(ray, cell.upper, x, y, true, hit);
			triangle(ray, cell.lower, x, y, false, hit);
		}
}
} // namespace

void TableRelief::Reset()
{
	nodes.clear();
	points.clear();
	cells.clear();
	pitch = height = 0;
}

void TableRelief::Update(int boardWidth, int rows, int x, int y, int w, int h, const uint16_t* depth, int stride,
						 int scale)
{
	if (x >= boardWidth || y >= rows || boardWidth < 2 || rows < 2) return;
	if (boardWidth != pitch || height != rows)
	{
		Reset();
		pitch = boardWidth;
		height = rows;
		points.resize(pitch * height);
		cells.resize(pitch * height);
		for (int yy = 0; yy < height; ++yy)
			for (int xx = 0; xx < pitch; ++xx)
				points[yy * pitch + xx].ray = proj::ViewRay(xx + .5f, yy + .5f);
		nodes.reserve(pitch * height / 4);
		partition(0, 0, pitch - 1, height - 1);
		x = y = 0;
		w = pitch;
		h = height;
	}
	const int right = std::min(x + w, pitch), bottom = std::min(y + h, height);
	int changedX = right, changedY = bottom, changedRight = x, changedBottom = y;
	for (int yy = y; yy < bottom; ++yy)
		for (int xx = x; xx < right; ++xx)
		{
			auto& v = points[yy * pitch + xx];
			const auto packed = depth[(yy * scale + scale / 2) * stride + xx * scale + scale / 2];
			if (v.range == packed) continue;
			v.range = packed;
			changedX = std::min(changedX, xx);
			changedY = std::min(changedY, yy);
			changedRight = std::max(changedRight, xx + 1);
			changedBottom = std::max(changedBottom, yy + 1);
			const float range = proj::DepthDistance(v.range);
			v.position = {v.ray.X * range, v.ray.Y * range, v.ray.Z * range};
		}
	if (changedX >= changedRight) return;
	for (int yy = std::max(0, changedY - 1); yy < std::min(changedBottom, height - 1); ++yy)
		for (int xx = std::max(0, changedX - 1); xx < std::min(changedRight, pitch - 1); ++xx)
		{
			auto& cell = cells[yy * pitch + xx];
			const auto* v = &points[yy * pitch + xx];
			cell.upper = makeFace(v[0], v[1], v[pitch + 1]);
			cell.lower = makeFace(v[0], v[pitch + 1], v[pitch]);
		}
	refit(0, changedX, changedY, changedRight, changedBottom);
}

size_t TableRelief::FaceCount()
{
	size_t count = 0;
	for (const auto& c : cells)
	{
		count += dot(c.upper.normal, c.upper.normal) > 0.f;
		count += dot(c.lower.normal, c.lower.normal) > 0.f;
	}
	return count;
}

bool TableRelief::Trace(const vector3& origin, const vector3& direction, float limit, Hit& hit)
{
	if (nodes.empty()) return false;
	auto inverse = [](float v) { return fabsf(v) < 1.e-9f ? copysignf(1.e9f, v) : 1.f / v; };
	const Ray ray{origin, direction, {inverse(direction.X), inverse(direction.Y), inverse(direction.Z)}};
	hit.distance = limit;
	if (entry(ray, nodes[0].box, limit) >= limit) return false;
	traceNode(0, ray, hit);
	if (hit.distance == limit) return false;

	return true;
}

bool TableRelief::Surface(int x, int y, vector3& position, vector3& normal)
{
	if (x < 0 || x >= pitch - 1 || y < 0 || y >= height - 1) return false;
	const auto i = y * pitch + x;
	if (points[i].range == 65535) return false;
	const auto& cell = cells[i];
	position = points[i].position;
	normal = {cell.upper.normal.X + cell.lower.normal.X, cell.upper.normal.Y + cell.lower.normal.Y,
			  cell.upper.normal.Z + cell.lower.normal.Z};
	const float length = maths::magnitude(normal);
	if (length < 1.e-7f) return false;
	const float scale = (dot(normal, position) > 0.f ? -1.f : 1.f) / length;
	normal.X *= scale;
	normal.Y *= scale;
	normal.Z *= scale;
	return true;
}
