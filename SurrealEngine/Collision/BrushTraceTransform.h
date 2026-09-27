#pragma once

#include "Math/coords.h"

// Match RenderBrush's object transform. Keep trace t in world-distance units:
// the transformed direction must not be normalized after applying scale.
struct BrushTraceTransform
{
	BrushTraceTransform(vec3 location, Rotator rotation, vec3 pivot, float scale)
		: location(location), pivot(pivot), scale(scale), rotation(Coords::Rotation(rotation).ToMatrix()), inverse(mat4::transpose(this->rotation)) { }

	bool Valid() const { return std::isfinite(scale) && std::abs(scale) > 0.00001f; }
	dvec3 Point(const dvec3& point) const { return to_dvec3(((inverse * vec4(to_vec3(point) - location, 0.0f)).xyz() + pivot) / scale); }
	dvec3 Direction(const dvec3& direction) const { return to_dvec3((inverse * vec4(to_vec3(direction), 0.0f)).xyz() / scale); }
	dvec3 Extents(double height, double radius) const
	{
		// Enclose the transformed world AABB, including rotated platforms.
		vec3 x = (inverse * vec4((float)radius, 0, 0, 0)).xyz();
		vec3 y = (inverse * vec4(0, (float)radius, 0, 0)).xyz();
		vec3 z = (inverse * vec4(0, 0, (float)height, 0)).xyz();
		return dvec3(std::abs(x.x) + std::abs(y.x) + std::abs(z.x), std::abs(x.y) + std::abs(y.y) + std::abs(z.y), std::abs(x.z) + std::abs(y.z) + std::abs(z.z)) / (double)std::abs(scale);
	}
	vec3 Normal(vec3 normal) const { return normalize((rotation * vec4(normal / scale, 0.0f)).xyz()); }

	vec3 location, pivot;
	float scale;
	mat4 rotation, inverse;
};
