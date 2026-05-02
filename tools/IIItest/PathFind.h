#ifndef _PATHFIND_H_
#define _PATHFIND_H_

class CPathFind
{
public:
	static void AllocatePathFindInfoMem(int n);
	static void StoreNodeInfoCar(short id, short i, signed char type, signed char next,
	                             short x, short y, short z,
	                             short width, signed char left, signed char right);
	static void StoreNodeInfoPed(short id, short i, signed char type, signed char next,
	                             short x, short y, short z,
	                             short width, bool crossing);

	static bool DoesObjectHavePath(uint id);
};

#endif
