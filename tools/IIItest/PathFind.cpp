#include "III.h"

struct PathNode
{
	short x;
	short y;
	short z;
	signed char type;
	signed char next;
	signed char left;
	signed char right;
	bool crossing;
};

PathNode *InfoForTilePeds;
PathNode *InfoForTileCars;
int gMaxEntries;

bool
CPathFind::DoesObjectHavePath(uint id)
{
	return InfoForTilePeds[12*id].type != 0 ||
	       InfoForTileCars[12*id].type != 0;
}

void
CPathFind::StoreNodeInfoCar(short id, short i, signed char type, signed char next,
	                    short x, short y, short z,
	                    short width, signed char left, signed char right)
{
	int n = id*12+i;
	assert(id < gMaxEntries);
	InfoForTileCars[n].type = type;
	InfoForTileCars[n].next = next;
	InfoForTileCars[n].x = x;
	InfoForTileCars[n].y = y;
	InfoForTileCars[n].z = z;
	InfoForTileCars[n].left = left;
	InfoForTileCars[n].right = right;
}

void
CPathFind::StoreNodeInfoPed(short id, short i, signed char type, signed char next,
	                    short x, short y, short z,
	                    short width, bool crossing)
{
	int n = id*12+i;
	assert(id < gMaxEntries);
	InfoForTilePeds[n].type = type;
	InfoForTilePeds[n].next = next;
	InfoForTilePeds[n].x = x;
	InfoForTilePeds[n].y = y;
	InfoForTilePeds[n].z = z;
	InfoForTilePeds[n].left = 0;
	InfoForTilePeds[n].right = 0;
	InfoForTilePeds[n].crossing = crossing;
}

void
CPathFind::AllocatePathFindInfoMem(int n)
{
	gMaxEntries = n;
	if(InfoForTilePeds)
		delete[] InfoForTilePeds;
	if(InfoForTileCars)
		delete[] InfoForTileCars;
	InfoForTilePeds = new PathNode[12*n];
	memset(InfoForTilePeds, 0, 12*n*sizeof(*InfoForTilePeds));
	InfoForTileCars = new PathNode[12*n];
	memset(InfoForTileCars, 0, 12*n*sizeof(*InfoForTileCars));
}
