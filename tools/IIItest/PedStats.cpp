#include "III.h"

CPedStats::Stats *CPedStats::ms_apPedStats[NUMPEDSTATS];

CPedStats::Stats::Stats(void)
{
	this->id = 0;
	strcpy(this->name, "PLAYER");
	this->fleeDistance = 20.0f;
	this->headingChangeRate = 15.0f;
	this->fear = 50;
	this->temper = 50;
	this->lawfullness = 50;
	this->sexiness = 50;
	this->attackStrength = 1.0f;
	this->defendWeakness = 1.0f;
	this->flags = 0;
}

void
CPedStats::Initialise(void)
{
	for(int i = 0; i < NUMPEDSTATS; i++)
		CPedStats::ms_apPedStats[i] = new CPedStats::Stats;
	CPedStats::LoadPedStats();
}

void
CPedStats::LoadPedStats(void)
{
	FileHandle file;
	char *line;
	char name[24];
	float fleeDist, headingChangeRate;
	int fear, temper, lawfullness, sexiness;
	float attackStrength, defendWeakness;
	int flags;

	CFileMgr::SetDir("DATA");
	file = CFileMgr::OpenFile("PEDSTATS.DAT", "rb");
	CFileMgr::SetDir("");
	if(file == nil)
		return;
	int i = 0;
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == '#')
			continue;
		sscanf(line, "%s %f %f %d %d %d %d %f %f %d",
		       name, &fleeDist, &headingChangeRate,
		       &fear, &temper, &lawfullness, &sexiness,
		       &attackStrength, &defendWeakness, &flags);
		CPedStats::Stats *s = CPedStats::ms_apPedStats[i];
		s->id = i;
		strncpy(s->name, name, 24);
		s->fleeDistance = fleeDist;
		s->headingChangeRate = headingChangeRate;
		s->fear = fear;
		s->temper = temper;
		s->lawfullness = lawfullness;
		s->sexiness = sexiness;
		s->attackStrength = attackStrength;
		s->defendWeakness = defendWeakness;
		s->flags = flags;
		i++;
	}
	CFileMgr::CloseFile(file);
}

int
CPedStats::GetPedStatType(const char *name)
{
	int i;
	for(i = 0; i < NUMPEDSTATS; i++){
		CPedStats::Stats *s = CPedStats::ms_apPedStats[i];
		if(strcmp(s->name, name) == 0)
			return i;
	}
	return -1;
}
