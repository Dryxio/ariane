#include "autocol.h"
#include "euryopa.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

static const int kAutoColSoftTriangleThreshold = 2048;
static const int kAutoColHardVertexCap = 60000;
static const int kAutoColHardTriangleCap = 32767;
static const float kAutoColCompressionScale = 128.0f;
static const float kAutoColCompressionPrecision = 1.0f / kAutoColCompressionScale;
static const float kAutoColMinTriangleArea = 0.0001f;
static const float kAutoColCollinearThreshold = 1.0e-10f;

struct AutoColVertex
{
	float x, y, z;
};

struct AutoColTriangle
{
	uint16_t a, b, c;
	uint8_t surface;
	uint8_t light;
};

struct AutoColBounds
{
	rw::V3d min;
	rw::V3d max;
	rw::V3d center;
	float radius;
};

struct QuantizedVertexKey
{
	int x, y, z;

	bool
	operator<(const QuantizedVertexKey &other) const
	{
		if(x != other.x) return x < other.x;
		if(y != other.y) return y < other.y;
		return z < other.z;
	}
};

static bool
setAutoColError(char *err, size_t errSize, const char *fmt, ...)
{
	if(err && errSize > 0){
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(err, errSize, fmt, ap);
		va_end(ap);
	}
	return false;
}

static void
writeU16(std::vector<char> &buffer, size_t offset, uint16_t value)
{
	buffer[offset + 0] = (char)(value & 0xFF);
	buffer[offset + 1] = (char)((value >> 8) & 0xFF);
}

static void
writeU32(std::vector<char> &buffer, size_t offset, uint32_t value)
{
	buffer[offset + 0] = (char)(value & 0xFF);
	buffer[offset + 1] = (char)((value >> 8) & 0xFF);
	buffer[offset + 2] = (char)((value >> 16) & 0xFF);
	buffer[offset + 3] = (char)((value >> 24) & 0xFF);
}

static void
writeI16(std::vector<char> &buffer, size_t offset, int16_t value)
{
	writeU16(buffer, offset, (uint16_t)value);
}

static void
writeF32(std::vector<char> &buffer, size_t offset, float value)
{
	uint32_t raw;
	memcpy(&raw, &value, sizeof(raw));
	writeU32(buffer, offset, raw);
}

static bool
extractAtomicGeometry(rw::Atomic *atomic, std::vector<AutoColVertex> &vertices,
                      std::vector<AutoColTriangle> &triangles,
                      AutoColStats *stats, char *err, size_t errSize)
{
	if(atomic == nil)
		return setAutoColError(err, errSize, "Couldn't generate collision: object atomic is missing.");
	if(atomic->geometry == nil)
		return setAutoColError(err, errSize, "Couldn't generate collision: object geometry is missing.");

	rw::Geometry *geo = atomic->geometry;
	if(geo->numVertices <= 0 || geo->numTriangles <= 0)
		return setAutoColError(err, errSize, "Couldn't generate collision: geometry has no vertices or triangles.");
	if(geo->morphTargets == nil || geo->morphTargets[0].vertices == nil)
		return setAutoColError(err, errSize, "Couldn't generate collision: geometry has no readable vertex data.");

	vertices.reserve(geo->numVertices);
	for(int i = 0; i < geo->numVertices; i++){
		rw::V3d v = geo->morphTargets[0].vertices[i];
		AutoColVertex out = { v.x, v.y, v.z };
		vertices.push_back(out);
	}

	triangles.reserve(geo->numTriangles);
	for(int i = 0; i < geo->numTriangles; i++){
		rw::Triangle tri = geo->triangles[i];
		AutoColTriangle out = {
			(uint16_t)tri.v[0],
			(uint16_t)tri.v[1],
			(uint16_t)tri.v[2],
			0,
			0
		};
		triangles.push_back(out);
	}

	if(stats){
		stats->originalVertices = (int)vertices.size();
		stats->originalTriangles = (int)triangles.size();
	}
	return true;
}

static bool
weldVerticesForCompression(const std::vector<AutoColVertex> &input,
                          std::vector<AutoColVertex> &welded,
                          std::vector<uint16_t> &indexMap,
                          char *err, size_t errSize)
{
	std::map<QuantizedVertexKey, uint16_t> lookup;
	indexMap.resize(input.size());

	for(size_t i = 0; i < input.size(); i++){
		int qx = (int)lroundf(input[i].x * kAutoColCompressionScale);
		int qy = (int)lroundf(input[i].y * kAutoColCompressionScale);
		int qz = (int)lroundf(input[i].z * kAutoColCompressionScale);
		if(qx < -32768 || qx > 32767 ||
		   qy < -32768 || qy > 32767 ||
		   qz < -32768 || qz > 32767)
			return setAutoColError(err, errSize,
			                       "Couldn't generate collision: DFF geometry exceeds COL3 compressed coordinate range.");

		QuantizedVertexKey key = { qx, qy, qz };
		std::map<QuantizedVertexKey, uint16_t>::const_iterator it = lookup.find(key);
		if(it != lookup.end()){
			indexMap[i] = it->second;
			continue;
		}

		if(welded.size() >= 65535)
			return setAutoColError(err, errSize,
			                       "Couldn't generate collision: generated mesh exceeds COL vertex index limit.");

		uint16_t newIndex = (uint16_t)welded.size();
		lookup[key] = newIndex;
		indexMap[i] = newIndex;

		AutoColVertex quantized = {
			qx * kAutoColCompressionPrecision,
			qy * kAutoColCompressionPrecision,
			qz * kAutoColCompressionPrecision
		};
		welded.push_back(quantized);
	}

	return true;
}

static rw::V3d
subVertex(const AutoColVertex &a, const AutoColVertex &b)
{
	rw::V3d out;
	out.x = a.x - b.x;
	out.y = a.y - b.y;
	out.z = a.z - b.z;
	return out;
}

static rw::V3d
crossVertex(const rw::V3d &a, const rw::V3d &b)
{
	rw::V3d out;
	out.x = a.y*b.z - a.z*b.y;
	out.y = a.z*b.x - a.x*b.z;
	out.z = a.x*b.y - a.y*b.x;
	return out;
}

static bool
filterAndCompactMesh(const std::vector<AutoColVertex> &welded,
                    const std::vector<uint16_t> &indexMap,
                    const std::vector<AutoColTriangle> &inputTriangles,
                    std::vector<AutoColVertex> &finalVertices,
                    std::vector<AutoColTriangle> &finalTriangles,
                    AutoColStats *stats,
                    char *err, size_t errSize)
{
	std::vector<AutoColTriangle> filtered;
	filtered.reserve(inputTriangles.size());

	for(size_t i = 0; i < inputTriangles.size(); i++){
		const AutoColTriangle &tri = inputTriangles[i];
		if(tri.a >= indexMap.size() || tri.b >= indexMap.size() || tri.c >= indexMap.size())
			continue;

		uint16_t a = indexMap[tri.a];
		uint16_t b = indexMap[tri.b];
		uint16_t c = indexMap[tri.c];
		if(a == b || b == c || a == c){
			if(stats) stats->removedDuplicateIndexTriangles++;
			continue;
		}
		if(a >= welded.size() || b >= welded.size() || c >= welded.size())
			continue;

		rw::V3d edge1 = subVertex(welded[b], welded[a]);
		rw::V3d edge2 = subVertex(welded[c], welded[a]);
		rw::V3d cross = crossVertex(edge1, edge2);
		double crossMagnitude = sqrt((double)cross.x*cross.x + (double)cross.y*cross.y + (double)cross.z*cross.z);
		if(crossMagnitude < kAutoColCollinearThreshold){
			if(stats) stats->removedCollinearTriangles++;
			continue;
		}

		double area = crossMagnitude * 0.5;
		if(area < kAutoColMinTriangleArea){
			if(stats) stats->removedZeroAreaTriangles++;
			continue;
		}

		AutoColTriangle out = { a, b, c, tri.surface, tri.light };
		filtered.push_back(out);
	}

	if(filtered.empty())
		return setAutoColError(err, errSize,
		                       "Couldn't generate collision: DFF geometry became empty after cleanup.");

	std::vector<int> remap(welded.size(), -1);
	finalVertices.reserve(welded.size());
	finalTriangles.reserve(filtered.size());

	for(size_t i = 0; i < filtered.size(); i++){
		const AutoColTriangle &tri = filtered[i];
		uint16_t oldIndices[3] = { tri.a, tri.b, tri.c };
		uint16_t newIndices[3];
		for(int j = 0; j < 3; j++){
			uint16_t oldIndex = oldIndices[j];
			if(remap[oldIndex] < 0){
				if(finalVertices.size() >= 65535)
					return setAutoColError(err, errSize,
					                       "Couldn't generate collision: generated mesh exceeds COL vertex index limit.");
				remap[oldIndex] = (int)finalVertices.size();
				finalVertices.push_back(welded[oldIndex]);
			}
			newIndices[j] = (uint16_t)remap[oldIndex];
		}
		AutoColTriangle out = { newIndices[0], newIndices[1], newIndices[2], tri.surface, tri.light };
		finalTriangles.push_back(out);
	}

	if(finalVertices.empty() || finalTriangles.empty())
		return setAutoColError(err, errSize,
		                       "Couldn't generate collision: compacted geometry is empty.");
	if((int)finalVertices.size() > kAutoColHardVertexCap)
		return setAutoColError(err, errSize,
		                       "Couldn't generate collision: generated COL has %d vertices (v1 cap is %d).",
		                       (int)finalVertices.size(), kAutoColHardVertexCap);
	if((int)finalTriangles.size() > kAutoColHardTriangleCap)
		return setAutoColError(err, errSize,
		                       "Couldn't generate collision: generated COL has %d triangles (v1 cap is %d).",
		                       (int)finalTriangles.size(), kAutoColHardTriangleCap);

	if(stats){
		stats->weldedVertices = (int)welded.size();
		stats->finalVertices = (int)finalVertices.size();
		stats->finalTriangles = (int)finalTriangles.size();
		stats->exceededSoftTriangleThreshold = stats->finalTriangles > kAutoColSoftTriangleThreshold;
	}
	return true;
}

static AutoColBounds
calculateBounds(const std::vector<AutoColVertex> &vertices)
{
	AutoColBounds bounds;
	bounds.min.x = bounds.max.x = vertices[0].x;
	bounds.min.y = bounds.max.y = vertices[0].y;
	bounds.min.z = bounds.max.z = vertices[0].z;

	for(size_t i = 1; i < vertices.size(); i++){
		bounds.min.x = std::min(bounds.min.x, vertices[i].x);
		bounds.min.y = std::min(bounds.min.y, vertices[i].y);
		bounds.min.z = std::min(bounds.min.z, vertices[i].z);
		bounds.max.x = std::max(bounds.max.x, vertices[i].x);
		bounds.max.y = std::max(bounds.max.y, vertices[i].y);
		bounds.max.z = std::max(bounds.max.z, vertices[i].z);
	}

	bounds.center.x = (bounds.min.x + bounds.max.x) * 0.5f;
	bounds.center.y = (bounds.min.y + bounds.max.y) * 0.5f;
	bounds.center.z = (bounds.min.z + bounds.max.z) * 0.5f;
	bounds.radius = 0.0f;

	for(size_t i = 0; i < vertices.size(); i++){
		float dx = vertices[i].x - bounds.center.x;
		float dy = vertices[i].y - bounds.center.y;
		float dz = vertices[i].z - bounds.center.z;
		float dist = sqrtf(dx*dx + dy*dy + dz*dz);
		if(dist > bounds.radius)
			bounds.radius = dist;
	}
	return bounds;
}

static void
writeNameField(std::vector<char> &buffer, size_t offset, const char *modelName)
{
	memset(&buffer[offset], 0, 24);
	size_t len = strlen(modelName);
	if(len > 23)
		len = 23;
	memcpy(&buffer[offset], modelName, len);
}

static bool
serializeCol3(const std::vector<AutoColVertex> &vertices,
              const std::vector<AutoColTriangle> &triangles,
              const char *modelName,
              std::vector<char> &outBytes,
              char *err, size_t errSize)
{
	if(vertices.empty() || triangles.empty())
		return setAutoColError(err, errSize, "Couldn't generate collision: no collision geometry to serialize.");

	const uint32_t headerSize = 112;
	const uint32_t vertexSize = 6;
	const uint32_t faceSize = 8;
	uint32_t dataOffset = headerSize;
	uint32_t vertexOffset = dataOffset;
	dataOffset += (uint32_t)vertices.size() * vertexSize;
	uint32_t faceOffset = dataOffset;
	dataOffset += (uint32_t)triangles.size() * faceSize;
	uint32_t modelSize = dataOffset;

	outBytes.assign(8 + modelSize, 0);

	memcpy(&outBytes[0], "COL3", 4);
	writeU32(outBytes, 4, modelSize);
	writeNameField(outBytes, 8, modelName);

	AutoColBounds bounds = calculateBounds(vertices);
	size_t offset = 32;
	writeF32(outBytes, offset, bounds.min.x); offset += 4;
	writeF32(outBytes, offset, bounds.min.y); offset += 4;
	writeF32(outBytes, offset, bounds.min.z); offset += 4;
	writeF32(outBytes, offset, bounds.max.x); offset += 4;
	writeF32(outBytes, offset, bounds.max.y); offset += 4;
	writeF32(outBytes, offset, bounds.max.z); offset += 4;
	writeF32(outBytes, offset, bounds.center.x); offset += 4;
	writeF32(outBytes, offset, bounds.center.y); offset += 4;
	writeF32(outBytes, offset, bounds.center.z); offset += 4;
	writeF32(outBytes, offset, bounds.radius); offset += 4;

	writeU16(outBytes, offset, 0); offset += 2;
	writeU16(outBytes, offset, 0); offset += 2;
	writeU16(outBytes, offset, (uint16_t)triangles.size()); offset += 2;
	outBytes[offset++] = 0;
	outBytes[offset++] = 0;

	writeU32(outBytes, offset, 0x02); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, vertexOffset + 4); offset += 4;
	writeU32(outBytes, offset, faceOffset + 4); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;
	writeU32(outBytes, offset, 0); offset += 4;

	offset = 8 + vertexOffset;
	for(size_t i = 0; i < vertices.size(); i++){
		int qx = (int)lroundf(vertices[i].x * kAutoColCompressionScale);
		int qy = (int)lroundf(vertices[i].y * kAutoColCompressionScale);
		int qz = (int)lroundf(vertices[i].z * kAutoColCompressionScale);
		if(qx < -32768 || qx > 32767 ||
		   qy < -32768 || qy > 32767 ||
		   qz < -32768 || qz > 32767)
			return setAutoColError(err, errSize,
			                       "Couldn't generate collision: serialized coordinates exceed COL3 compressed range.");
		writeI16(outBytes, offset, (int16_t)qx); offset += 2;
		writeI16(outBytes, offset, (int16_t)qy); offset += 2;
		writeI16(outBytes, offset, (int16_t)qz); offset += 2;
	}

	offset = 8 + faceOffset;
	for(size_t i = 0; i < triangles.size(); i++){
		writeU16(outBytes, offset, triangles[i].a); offset += 2;
		writeU16(outBytes, offset, triangles[i].b); offset += 2;
		writeU16(outBytes, offset, triangles[i].c); offset += 2;
		outBytes[offset++] = (char)triangles[i].surface;
		outBytes[offset++] = (char)triangles[i].light;
	}

	return true;
}

bool
GenerateCol3FromAtomic(rw::Atomic *atomic, const char *modelName,
                       std::vector<char> &outBytes, AutoColStats *stats,
                       char *err, size_t errSize)
{
	if(stats)
		memset(stats, 0, sizeof(*stats));
	if(modelName == nil || modelName[0] == '\0')
		return setAutoColError(err, errSize, "Couldn't generate collision: model name is empty.");
	if(strlen(modelName) >= 24)
		return setAutoColError(err, errSize, "Couldn't generate collision: model name is too long for COL header.");

	std::vector<AutoColVertex> sourceVertices;
	std::vector<AutoColTriangle> sourceTriangles;
	if(!extractAtomicGeometry(atomic, sourceVertices, sourceTriangles, stats, err, errSize))
		return false;

	std::vector<AutoColVertex> weldedVertices;
	std::vector<uint16_t> indexMap;
	if(!weldVerticesForCompression(sourceVertices, weldedVertices, indexMap, err, errSize))
		return false;

	std::vector<AutoColVertex> finalVertices;
	std::vector<AutoColTriangle> finalTriangles;
	if(!filterAndCompactMesh(weldedVertices, indexMap, sourceTriangles, finalVertices, finalTriangles,
	                         stats, err, errSize))
		return false;

	return serializeCol3(finalVertices, finalTriangles, modelName, outBytes, err, errSize);
}
