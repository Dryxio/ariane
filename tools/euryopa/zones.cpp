#include "euryopa.h"

namespace Zones
{

/*
 * The types of zones we have in the games:
 *
 * TheZones:
 *             III   VC	  SA        type
 *      Zones    X                           = Navig + Info Zones
 *   MapZones    X    X    X         3
 * NavigZones         X    X         0,1     += Info in SA
 *  InfoZones         X	             2
 *
 * Audio zones:
 *         III: from Zones
 *          VC: from NavigZones
 *          SA: from IPL auzo (boxes and spheres)
 *
 * CullZones:
 *                III  VC   SA
 *          cull    X
 *        attrib    X   X    X
 * mirror attrib             X
 * tunnel attrib             X
*/

struct Zone
{
	char name[8];
	CBox box;
	int type;
	int level;
	char text[8];	// GXT entry in SA
};

enum eZoneType
{
	ZONE_NAVIG0,
	ZONE_NAVIG1,
	ZONE_INFO,
	ZONE_MAPZONE,
};

// Rotated zone as used in SA, but we use floats not shorts
struct ZoneDef
{
	// position of one corner
	float posx;
	float posy;
	// base vectors
	float side1x;
	float side1y;
	float side2x;
	float side2y;
	// z bounds
	float zmin;
	float zmax;

	void set(CBox box){
		posx = box.min.x;
		posy = box.min.y;
		zmin = box.min.z;
		zmax = box.max.z;
		side1x = 0.0f;
		side1y = box.max.y - box.min.y;
		side2x = box.max.x - box.min.x;
		side2y = 0.0f;
	}

	void makeVertices(rw::V3d *verts){
		verts[0].x = posx;
		verts[0].y = posy;
		verts[0].z = zmin;
		verts[1].x = posx + side1x;
		verts[1].y = posy + side1y;
		verts[1].z = zmin;
		verts[2].x = posx + side2x;
		verts[2].y = posy + side2y;
		verts[2].z = zmin;
		verts[3].x = posx + side1x + side2x;
		verts[3].y = posy + side1y + side2y;
		verts[3].z = zmin;
		verts[4] = verts[0];
		verts[5] = verts[1];
		verts[6] = verts[2];
		verts[7] = verts[3];
		verts[4].z = zmax;
		verts[5].z = zmax;
		verts[6].z = zmax;
		verts[7].z = zmax;
	}
};

enum eZoneAttribs
{
	ATTRZONE_CAMCLOSEIN		= 1,
	ATTRZONE_STAIRS			= 2,
	ATTRZONE_1STPERSON		= 4,
	ATTRZONE_NORAIN			= 8,
	ATTRZONE_NOPOLICE		= 0x10,
	ATTRZONE_NOTCULLZONE		= 0x20,
	ATTRZONE_DOINEEDCOLLISION	= 0x40,
	ATTRZONE_SUBWAYVISIBLE		= 0x80,
// VC
	ATTRZONE_POLICEABANDONCARS	= 0x100,
	ATTRZONE_INROOMFORAUDIO		= 0x200,
	ATTRZONE_WATERFUDGE		= 0x400,
// SA
	ATTRZONE_FEWERPEDS		= 0x400,
	ATTRZONE_TUNNEL			= 0x800,
	ATTRZONE_MILITARYZONE		= 0x1000,
	// 0x2000?
	ATTRZONE_EXTRAAIRRESISTANCE	= 0x4000,
	ATTRZONE_FEWERCARS		= 0x8000
};

// only in III
struct CullZone
{
//	rw::V3d position;	// unused by the game it seems
	CBox box;
	// the game has more but we don't
};

struct AttribZone
{
	ZoneDef zone;
//	CBox box;	// used in III and VC but we want to support everything
	int attribs;
	int wantedLevelDrop;
};

struct MirrorAttribZone
{
	AttribZone z;
	rw::Plane mirror;
};

static Zone mapZones[NUMZONES];
static int numMapZones;
static Zone navigZones[NUMZONES];	// also used for III zones
static int numNavigZones;
static Zone infoZones[NUMZONES];
static int numInfoZones;

void
CreateZone(const char *name, int type, CBox box, int level, const char *text)
{
	Zone *z;
	box.FindMinMax();
	switch(type){
	case ZONE_NAVIG0:
	case ZONE_NAVIG1:
		if(numNavigZones >= NUMZONES){
			log("warning: Too many zones\n");
			return;
		}
		z = &navigZones[numNavigZones++];
		break;
	case ZONE_INFO:
		if(numInfoZones >= NUMZONES){
			log("warning: Too many zones\n");
			return;
		}
		z = &infoZones[numInfoZones++];
		break;
	case ZONE_MAPZONE:
		if(numMapZones >= NUMZONES){
			log("warning: Too many zones\n");
			return;
		}
		z = &mapZones[numMapZones++];
		break;
	default:
		log("warning: unknown zone type %d\n", type);
		return;
	}

	strncpy(z->name, name, 7);
	z->name[7] = '\0';
	if(text){
		strncpy(z->text, text, 7);
		z->text[7] = '\0';
	}else
		z->text[0] = '\0';
	z->box = box;
	z->type = type;
	z->level = level;
}

void
RenderZones(Zone *zones, int n)
{
	rw::Matrix ident;
	ident.setIdentity();
	rw::RGBA cols[] = {
		{ 255, 255, 255, 255 },
		{ 255, 0, 0, 255 },
		{ 0, 255, 0, 255 },
		{ 0, 0, 255, 255 },
	};
	int i;
	for(i = 0; i < n; i++)
		RenderWireBox(&zones[i].box, cols[zones[i].level], &ident);
}

void
Render(void)
{
	if(gRenderMapZones) RenderZones(mapZones, numMapZones);
	if(gRenderNavigZones) RenderZones(navigZones, numNavigZones);
	if(gRenderInfoZones) RenderZones(infoZones, numInfoZones);
}

// Cull and Attrib zones

enum
{
//	NUMCULLZONES = 800,
	NUMCULLZONES = 1000,
	NUMATTRIBZONES = 2000,
	NUMMIRRORATTRIBZONES = 200
};

static CullZone cullZones[NUMCULLZONES];
static int numCullZones;
static int numCullZonesNeeded;
static AttribZone attribZones[NUMATTRIBZONES];
static int numAttribZones;
static MirrorAttribZone mirrorAttribZones[NUMMIRRORATTRIBZONES];
static int numMirrorAttribZones;

void
AddAttribZone(CBox box, int flags, int wantedLevelDrop)
{
	box.FindMinMax();

	if(isIII() && (flags & ATTRZONE_NOTCULLZONE) == 0){
		if(numCullZones >= NUMCULLZONES){
			numCullZonesNeeded++;
			log("warning: too many cull zones. need %d\n", numCullZonesNeeded);
		}else{
			cullZones[numCullZones++].box = box;
			numCullZonesNeeded = numCullZones;
		}
	}

	if(!isIII() || flags & ~ATTRZONE_NOTCULLZONE){
		if(numAttribZones >= NUMATTRIBZONES){
			log("warning: too many attrib zones\n");
			return;
		}
		AttribZone *az = &attribZones[numAttribZones++];
		az->zone.set(box);
		az->attribs = flags;
		az->wantedLevelDrop = wantedLevelDrop;
	}
}

void
AddAttribZone(rw::V3d pos,
	float s1x, float s1y,
	float s2x, float s2y,
	float zmin, float zmax, int flags)
{
	if(numAttribZones >= NUMATTRIBZONES){
		log("warning: too many attrib zones\n");
		return;
	}
	AttribZone *az = &attribZones[numAttribZones++];
	az->zone.posx = pos.x - s1x - s2x;
	az->zone.posy = pos.y - s1y - s2y;
	az->zone.side1x = s1x*2.0f;
	az->zone.side1y = s1y*2.0f;
	az->zone.side2x = s2x*2.0f;
	az->zone.side2y = s2y*2.0f;
	az->zone.zmin = zmin;
	az->zone.zmax = zmax;
	az->attribs = flags;
	az->wantedLevelDrop = 0;
}

void
AddMirrorAttribZone(rw::V3d pos,
	float s1x, float s1y,
	float s2x, float s2y,
	float zmin, float zmax,
	int flags, rw::Plane mirror)
{
	if(numMirrorAttribZones >= NUMMIRRORATTRIBZONES){
		log("warning: too many mirror attrib zones\n");
		return;
	}
	MirrorAttribZone *az = &mirrorAttribZones[numMirrorAttribZones++];
	az->z.zone.posx = pos.x - s1x - s2x;
	az->z.zone.posy = pos.y - s1y - s2y;
	az->z.zone.side1x = s1x*2.0f;
	az->z.zone.side1y = s1y*2.0f;
	az->z.zone.side2x = s2x*2.0f;
	az->z.zone.side2y = s2y*2.0f;
	az->z.zone.zmin = zmin;
	az->z.zone.zmax = zmax;
	az->z.attribs = flags;
	az->z.wantedLevelDrop = 0;
	az->mirror = mirror;
}

void
RenderAttribZones(void)
{
	rw::V3d verts[8];
	rw::RGBA col = { 255, 255, 255, 255 };
	int i;
	for(i = 0; i < numAttribZones; i++){
		AttribZone *az = &attribZones[i];
		az->zone.makeVertices(verts);
		RenderWireBoxVerts(verts, col);
	}
	col.green = 0;
	col.blue = 128;
	for(i = 0; i < numMirrorAttribZones; i++){
		MirrorAttribZone *az = &mirrorAttribZones[i];
		az->z.zone.makeVertices(verts);
		RenderWireBoxVerts(verts, col);
	}
}

void
RenderCullZones(void)
{
	rw::Matrix ident;
	ident.setIdentity();
	rw::RGBA col = { 255, 128, 0, 255 };
	int i;
	for(i = 0; i < numCullZones; i++)
		RenderWireBox(&cullZones[i].box, col, &ident);
}

}
