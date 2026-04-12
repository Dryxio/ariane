#include "euryopa.h"

static ObjectInst **instArrays[NUMSCENES];
static int instArraySizes[NUMSCENES];
static int numInstArrays;

static IplDef ipllist[NUMIPLS];
static int numIpls;

int
AddInstArraySlot(int n)
{
	if(numInstArrays >= NUMSCENES){
		log("warning: too many scenes (max %d)\n", NUMSCENES);
		return -1;
	}
	ObjectInst **instArray = rwNewT(ObjectInst*, n, 0);
	instArrays[numInstArrays++] = instArray;
	instArraySizes[numInstArrays-1] = n;
	log("AddInstArraySlot: allocated slot %d/%d for %d text inst(s)\n",
	    numInstArrays-1, NUMSCENES, n);
	return numInstArrays-1;
}

ObjectInst**
GetInstArray(int i)
{
	return instArrays[i];
}

int
GetInstArraySize(int i)
{
	if(i < 0 || i >= numInstArrays)
		return 0;
	return instArraySizes[i];
}

// streaming

static int
FindIplSlot(const char *name)
{
	int i;
	for(i = 0; i < numIpls; i++){
		if(rw::strncmp_ci(ipllist[i].name, name, MODELNAMELEN) == 0)
			return i;
	}
	return -1;
}

IplDef*
GetIplDef(int i)
{
	if(i < 0 || i >= numIpls){
//		log("warning: invalid Ipl slot %d\n", i);
		return nil;
	}
	return &ipllist[i];
}

int
AddIplSlot(const char *name)
{
	int i;
	i = FindIplSlot(name);
	if(i >= 0)
		return i;
	i = numIpls++;
	strncpy(ipllist[i].name, name, MODELNAMELEN);
	ipllist[i].instArraySlot = -1;
	ipllist[i].imageIndex = -1;
	return i;
}

void
LoadIpl(int slot, const char *sceneName)
{
	int i;
	int size;
	uint8 *buffer;
	FileObjectInstance *insts;
	GameFile *file;

	IplDef *ipl = GetIplDef(slot);

	if(ipl->imageIndex < 0){
		log("warning: no streaming info for ipl %s\n", ipl->name);
		return;
	}

	ObjectInst *lodinst;
	ObjectInst **instArray = nil;
	int instArraySize = 0;
	if(ipl->instArraySlot >= 0){
		instArray = GetInstArray(ipl->instArraySlot);
		instArraySize = GetInstArraySize(ipl->instArraySlot);
	}

	buffer = ReadFileFromImage(ipl->imageIndex, &size);
	file = GetGameFileFromImage(ipl->imageIndex);
	if(*(uint32*)buffer == 0x79726E62){	// bnry
		int16 numInsts = *(int16*)(buffer+4);
		insts = (FileObjectInstance*)(buffer + *(int32*)(buffer+0x1C));
		for(i = 0; i < numInsts; i++){

			// Skip deleted instances (objectId zeroed out)
			if(insts->objectId == 0){
				insts++;
				continue;
			}

			ObjectDef *obj = GetObjectDef(insts->objectId);
			if(obj == nil){
				log("warning: object %d was never defined\n", insts->objectId);
				insts++;
				continue;
			}

			ObjectInst *inst = AddInstance();
			inst->Init(insts);
			inst->m_file = file;
			inst->m_imageIndex = ipl->imageIndex;
			inst->m_binInstIndex = i;
			SetInstIplFilterKey(inst, sceneName ? sceneName : (file ? file->name : nil));

			if(inst->m_lodId < 0)
				inst->m_lod = nil;
			else{
				if(instArray == nil || inst->m_lodId >= instArraySize){
					inst->m_lod = nil;
					insts++;
					continue;
				}
				lodinst = instArray[inst->m_lodId];
				if(lodinst == nil){
					inst->m_lod = nil;	// LOD was deleted
				}else{
					inst->m_lod = lodinst;
					lodinst->m_numChildren++;
					ObjectDef *lodobj = GetObjectDef(lodinst->m_objectId);
					if(lodobj && lodinst->m_numChildren == 1 && obj->m_colModel && lodobj->m_colModel != obj->m_colModel)
						lodobj->m_colModel = obj->m_colModel;
				}
			}

			insts++;
		}

		// numCars: 0x14
		// cars: 0x3C
	}
	// TODO: parse text file (but no cars section :/)
}
