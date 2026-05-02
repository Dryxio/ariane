#pragma once

#include <stddef.h>
#include <vector>

namespace rw {
struct Atomic;
}

struct AutoColStats
{
	int originalVertices;
	int originalTriangles;
	int weldedVertices;
	int finalVertices;
	int finalTriangles;
	int removedDuplicateIndexTriangles;
	int removedZeroAreaTriangles;
	int removedCollinearTriangles;
	bool exceededSoftTriangleThreshold;
};

bool GenerateCol3FromAtomic(rw::Atomic *atomic, const char *modelName,
                            std::vector<char> &outBytes, AutoColStats *stats,
                            char *err, size_t errSize);
