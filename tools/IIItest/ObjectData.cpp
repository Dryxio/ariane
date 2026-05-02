#include "III.h"

ObjectInfo CObjectData::ms_aObjectInfo[NUMOBJECTINFO];

void
CObjectData::Initialise(const char *file)
{
	FileHandle f;
	ObjectInfo *p;
	int id;
	char *line;
	char name[256];
	float percentSubmerged;
	int damageEffect;
	int colResponse;
	int camAvoid;

	f = CFileMgr::OpenFile(file, "r");
	if(f == nil)
		return;

	p = &ms_aObjectInfo[0];
	id = 0;
	while(line = CFileLoader::LoadLine(f)){
		if(line[0] == '*')
			return;
		if(line[0] == ';')
			continue;
		sscanf(line, "%s %f %f %f %f %f %f %f %d %d %d",
		       name, &p->mass, &p->turnMass, &p->airResistance,
		       &p->elasticity, &percentSubmerged, &p->uprootLimit,
		       &p->colDamageMult, &damageEffect, &colResponse,
		       &camAvoid);
		p->submerged = 100.0f/percentSubmerged * p->mass/125.0f;
		p->damageEffect = damageEffect;
		p->colResponse = colResponse;
		p->camAvoid = !!camAvoid;
		CBaseModelInfo *mi = CModelInfo::GetModelInfo(name, nil);
		if(mi)
			mi->SetObjectID(id++);
		else
			debug("CObjectData: Cannot find object %s\n", name);
	}
	CFileMgr::CloseFile(f);
}
