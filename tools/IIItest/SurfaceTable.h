#ifndef _SURFACETABLE_H_
#define _SURFACETABLE_H_

enum
{
	SURFACE_0,
	SURFACE_1,
	SURFACE_2,
	SURFACE_3,
	SURFACE_4,
	SURFACE_5,
	SURFACE_6,
	SURFACE_7,
	SURFACE_8,
	SURFACE_9,
	SURFACE_10,
	SURFACE_11,
	SURFACE_12,
	SURFACE_13,
	SURFACE_14,
	SURFACE_15,
	SURFACE_16,
	SURFACE_17,
	SURFACE_18,
	SURFACE_19,
	SURFACE_20,
	SURFACE_21,
	SURFACE_22,
	SURFACE_23,
	SURFACE_24,
	SURFACE_25,
	SURFACE_26,
	SURFACE_27,
	SURFACE_28,
	SURFACE_29,
	SURFACE_30,
	SURFACE_31,
	SURFACE_32,

	NUMSURFACETYPES
};

enum
{
	ADHESIVE_RUBBER,
	ADHESIVE_HARD,
	ADHESIVE_ROAD,
	ADHESIVE_LOOSE,
	ADHESIVE_WET,

	NUMADHESIVEGROUPS
};

class CSurfaceTable
{
	static float ms_aAdhesiveLimitTable[NUMADHESIVEGROUPS][NUMADHESIVEGROUPS];
public:
	static void Initialise(const char *filename);
	static void Shutdown(void) {}
	static int GetAdhesionGroup(uint8 surfaceType);
	static float GetWetMultiplier(uint8 surfaceType);
	static float GetAdhesiveLimit(CColPoint &colpoint);
};

#endif
