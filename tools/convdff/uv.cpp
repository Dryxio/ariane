#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdarg>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

using namespace std;
using namespace rw;

static int32
replaceID(int32 *ids, int32 num, int32 from, int32 to)
{
	int32 n = 0;
	for(int32 i = 0; i < num; i++)
		if(ids[i] == from){
			ids[i] = to;
			n++;
		}
	return n;
}

// find connected groups of triangles and assign vertices to groups
static int32*
findGroupIDs(Geometry *g)
{
	int32 i;
	int32 *ids = rwNewT(int32, g->numVertices, 0);
	for(i = 0; i < g->numVertices; i++)
		ids[i] = -1;

	// assign initial IDs
	int32 nextID = 0;
	for(i = 0; i < g->numTriangles; i++){
		int v1 = g->triangles[i].v[0];
		int v2 = g->triangles[i].v[1];
		int v3 = g->triangles[i].v[2];

		int32 id =
			ids[v1] != -1 ? ids[v1] :
			ids[v2] != -1 ? ids[v2] :
			ids[v3] != -1 ? ids[v3] : nextID++;
		if(ids[v1] != id && ids[v1] != -1)
			replaceID(ids, g->numVertices, ids[v1], id);
		ids[v1] = id;
		if(ids[v2] != id && ids[v2] != -1)
			replaceID(ids, g->numVertices, ids[v2], id);
		ids[v2] = id;
		if(ids[v3] != id && ids[v3] != -1)
			replaceID(ids, g->numVertices, ids[v3], id);
		ids[v3] = id;
	}

	// ID range can have gaps now, compress
	int32 numIDs = nextID;
	nextID = 0;
	for(i = 0; i < numIDs; i++)
		if(replaceID(ids, g->numVertices, i, nextID) > 0)
			nextID++;

	return ids;
}

// TODO: be more careful about materials and texture wrapping modes
void
centerTexCoords(Geometry *g)
{
	if(g->flags & Geometry::NATIVE)
		return;

	int32 *groupIDs = findGroupIDs(g);

	for(int32 k = 0; k < g->numTexCoordSets; k++)
	for(int32 id = 0;; id++){
		float minU = 100000.0f;
		float minV = 100000.0f;
		float maxU = -100000.0f;
		float maxV = -100000.0f;
		int n = 0;

		for(int32 i = 0; i < g->numVertices; i++)
			if(groupIDs[i] == id){
				float u = g->texCoords[k][i].u;
				float v = g->texCoords[k][i].v;
				minU = min(minU, u);
				minV = min(minV, v);
				maxU = max(maxU, u);
				maxV = max(maxV, v);
				n++;
			}
		if(n == 0)
			break;

		float midU = (int)((maxU+minU)/2.0f);
		float midV = (int)((maxV+minV)/2.0f);
		for(int32 i = 0; i < g->numVertices; i++)
			if(groupIDs[i] == id){
				g->texCoords[k][i].u -= midU;
				g->texCoords[k][i].v -= midV;
			}
	}

	rwFree(groupIDs);
}
