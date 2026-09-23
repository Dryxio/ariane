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

// colVersion: 1 = COL1 ("COLL", float vertices — the only format III/VC read),
//             3 = COL3 (SA, int16/128 compressed vertices).
// boundsOnly: COL1 only — write the bounding sphere/box but no geometry, i.e. a model
//             the game treats as non-colliding (what vanilla III/VC LODs have).
bool GenerateColFromAtomic(rw::Atomic *atomic, const char *modelName, int colVersion,
                           std::vector<char> &outBytes, AutoColStats *stats,
                           char *err, size_t errSize, bool boundsOnly = false);
bool GenerateCol3FromAtomic(rw::Atomic *atomic, const char *modelName,
                            std::vector<char> &outBytes, AutoColStats *stats,
                            char *err, size_t errSize);
