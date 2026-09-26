#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace evolve {
struct Vec3 { float x{}, y{}, z{}; };
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 v, float s);
Vec3 normalized(Vec3 v);
Vec3 cross(Vec3 a, Vec3 b);
struct Vertex { Vec3 position, normal, color; };
struct PickPoint { Vec3 position; std::size_t index; };
struct SceneMesh { std::vector<Vertex> vertices; std::vector<PickPoint> pickPoints; };
Vec3 baseColor(char base);
// Schematic geometry in arbitrary display units, not atomic coordinates.
SceneMesh buildHelix(const std::string& sequence, const std::string& baseline,
    std::size_t selected, bool compare, bool showGrid);
}
