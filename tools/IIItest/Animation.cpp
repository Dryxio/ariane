#include "III.h"

AnimAssocDefinition *CAnimManager::ms_aAnimAssocDefinitions;
int CAnimManager::ms_numAnimAssocDefinitions;

// TODO: remove or ifdef this. GTA3 doesn't have it
void
CAnimManager::LoadAnimGroups(void)
{
	FileHandle file;
	char *line;
	int state = 0;
	int i = 0, j = 0;
	char *names, *blockNames, *animNames;
	if(file = CFileMgr::OpenFile("animgrp.dat", "rb"), file == nil)
		return;
	AnimAssocDefinition *assoc = nil;
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == ';' || line[0] == '#')
			continue;
		if(state == 0){
			// number of assoc definitions
			sscanf(line, "%d", &ms_numAnimAssocDefinitions);
			//printf("%d\n", ms_numAnimAssocDefinitions);
			ms_aAnimAssocDefinitions = new AnimAssocDefinition[ms_numAnimAssocDefinitions];
			names = new char[ms_numAnimAssocDefinitions*24];
			blockNames = new char[ms_numAnimAssocDefinitions*24];
			for(i = 0; i < ms_numAnimAssocDefinitions; i++){
				ms_aAnimAssocDefinitions[i].name = &names[i*24];
				ms_aAnimAssocDefinitions[i].blockName = &blockNames[i*24];
			}
			assoc = ms_aAnimAssocDefinitions;
			i = 0;
			state = 1;
		}else if(state == 1){
			// header of one definition
			sscanf(line, "%s %s %d %d", assoc->name, assoc->blockName,
			                            &assoc->modelIndex, &assoc->animCount);
			//printf("%s %s %d %d\n", assoc->name, assoc->blockName, assoc->modelIndex, assoc->animCount);
			assoc->animNames = new char*[assoc->animCount];
			assoc->animInfoList = new AnimAssocDefinition::Info[assoc->animCount];
			animNames = new char[24*assoc->animCount];
			for(j = 0; j < assoc->animCount; j++)
				assoc->animNames[j] = &animNames[j*24];
			j = 0;
			state = 2;
		}else if(state == 2){
			// individual animations
			sscanf(line, "%d %s %x", &assoc->animInfoList[j].animId,
			       assoc->animNames[j], &assoc->animInfoList[j].flags);
			//printf("\t%d %s %x\n", assoc->animInfoList[j].animId,
			//       assoc->animNames[j], assoc->animInfoList[j].flags);
			j++;
			if(j >= assoc->animCount){
				assoc++;
				state = 1;
				i++;
				if(i >= ms_numAnimAssocDefinitions)
					break;
			}
		}
	}
	CFileMgr::CloseFile(file);
}

char*
CAnimManager::GetAnimGroupName(int i)
{
	return ms_aAnimAssocDefinitions[i].name;
}
