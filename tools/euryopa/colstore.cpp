#include "euryopa.h"
#include "modloader.h"

static ColDef collist[NUMCOLS];
static int numCols;

static int
FindColSlot(const char *name)
{
	int i;
	for(i = 0; i < numCols; i++){
		if(rw::strncmp_ci(collist[i].name, name, MODELNAMELEN) == 0)
			return i;
	}
	return -1;
}

ColDef*
GetColDef(int i)
{
	if(i < 0 || i >= numCols){
//		log("warning: invalid col slot %d\n", i);
		return nil;
	}
	return &collist[i];
}

int
AddColSlot(const char *name)
{
	int i;
	i = FindColSlot(name);
	if(i >= 0)
		return i;
	i = numCols++;
	strncpy(collist[i].name, name, MODELNAMELEN);
	collist[i].imageIndex = -1;
	return i;
}


void
LoadAllCollisions(void)
{
	ColDef *col;
	int i;
	for(i = 0; i < numCols; i++){
		col = GetColDef(i);
		if(col->imageIndex >= 0 || ModloaderFindOverride(col->name, "col"))
			LoadCol(i);
	}
}

void
LoadCol(int slot)
{
	uint8 *buffer;
	int size;
	int offset;
	ColFileHeader *header;
	int version;
	ObjectDef *obj;
	GameFile *file;
	const char *loosePath;
	bool looseFile;
	ColDef *col = GetColDef(slot);

	loosePath = ModloaderFindOverride(col->name, "col");
	looseFile = false;
	file = nil;
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		if(buffer){
			looseFile = true;
			if(col->imageIndex >= 0)
				file = GetGameFileFromImage(col->imageIndex);
		}
	}
	if(!looseFile){
		if(col->imageIndex < 0){
			log("warning: no streaming info for col %s\n", col->name);
			return;
		}
		buffer = ReadFileFromImage(col->imageIndex, &size);
		file = GetGameFileFromImage(col->imageIndex);
	}

	if(buffer == nil){
		log("warning: no streaming info for col %s\n", col->name);
		return;
	}
	offset = 0;
	while(offset < size){
		header = (ColFileHeader*)(buffer+offset);
		version = 0;
		switch(header->fourcc){
		case 0x4C4C4F43:	// COLL
			version = 1;
			break;
		case 0x324C4F43:	// COL2
			version = 2;
			break;
		case 0x334C4F43:	// COL3
			version = 3;
			break;
		case 0x344C4F43:	// COL4
			version = 4;
			break;
		default:
			return;
		}
		offset += sizeof(ColFileHeader);

		obj = GetObjectDef(header->name, nil);
		if(obj){
			CColModel *col = new CColModel;
			col->file = file;
			obj->m_colModel = col;
			switch(version){
			case 1: ReadColModel(col, buffer+offset, header->modelsize-24); break;
			case 2: ReadColModelVer2(col, buffer+offset, header->modelsize-24); break;
			case 3: ReadColModelVer3(col, buffer+offset, header->modelsize-24); break;
			case 4: ReadColModelVer4(col, buffer+offset, header->modelsize-24); break;
			default:
				printf("unknown COL version %d\n", version);
				obj->m_colModel = nil;
			}
		}else
			printf("Couldn't find object %s for collision\n", header->name);
		offset += header->modelsize-24;
	}

	if(looseFile)
		free(buffer);
}

// Blender bridge: reload collision straight from a loose .col file (may hold
// several models). Mirrors LoadCol's inner loop but reads an explicit path and
// frees the previous CColModel, so a single-model .col from Blender live-updates
// just that object's collision without touching the rest of its library.
void
ForceColReloadFromFile(const char *path)
{
	int size;
	uint8 *buffer = ReadLooseFile(path, &size);
	if(buffer == nil){
		log("BlenderBridge: can't read col %s\n", path);
		return;
	}
	int offset = 0;
	while(offset + (int)sizeof(ColFileHeader) <= size){
		ColFileHeader *header = (ColFileHeader*)(buffer+offset);
		int version;
		switch(header->fourcc){
		case 0x4C4C4F43: version = 1; break;	// COLL
		case 0x324C4F43: version = 2; break;	// COL2
		case 0x334C4F43: version = 3; break;	// COL3
		case 0x344C4F43: version = 4; break;	// COL4
		default: goto done;
		}
		offset += sizeof(ColFileHeader);
		ObjectDef *obj = GetObjectDef(header->name, nil);
		if(obj){
			CColModel *col = new CColModel;
			col->file = nil;
			switch(version){
			case 1: ReadColModel(col, buffer+offset, header->modelsize-24); break;
			case 2: ReadColModelVer2(col, buffer+offset, header->modelsize-24); break;
			case 3: ReadColModelVer3(col, buffer+offset, header->modelsize-24); break;
			case 4: ReadColModelVer4(col, buffer+offset, header->modelsize-24); break;
			}
			CColModel *old = obj->m_colModel;
			obj->m_colModel = col;
			if(old) delete old;
			log("BlenderBridge: reloaded collision for %s\n", header->name);
		}else
			log("BlenderBridge: no object %s for collision\n", header->name);
		offset += header->modelsize-24;
	}
done:
	free(buffer);
}

// Blender bridge: rebuild a standalone .col from a model's live collision. The
// version header + rawdata were kept verbatim at load, so the body is byte-exact;
// only the 32-byte file header (fourcc + size + name) is synthesized. v1/uncaptured
// collisions can't be re-serialized → returns false.
bool
ExportColForModel(ObjectDef *obj, const char *path)
{
	if(obj == nil)
		return false;
	CColModel *c = obj->m_colModel;
	if(c == nil || c->colHeaderSize == 0 || c->rawdata == nil || c->rawdataSize <= 0){
		log("BlenderBridge: no exportable collision for %s\n", obj->m_name);
		return false;
	}
	uint32 fourcc = c->colVersion == 2 ? 0x324C4F43 :	// COL2
	                c->colVersion == 4 ? 0x344C4F43 :	// COL4
	                0x334C4F43;				// COL3
	int bodySize = c->colHeaderSize + c->rawdataSize;	// == original modelsize - 24
	uint32 modelsize = (uint32)(24 + bodySize);
	int total = 8 + (int)modelsize;
	uint8 *out = (uint8*)malloc(total);
	if(out == nil)
		return false;
	int p = 0;
	memcpy(out+p, &fourcc, 4); p += 4;
	memcpy(out+p, &modelsize, 4); p += 4;
	char nm[24];
	memset(nm, 0, sizeof(nm));
	strncpy(nm, obj->m_name, 21);
	*(uint16*)(nm+22) = (uint16)obj->m_id;			// model id in the trailing 2 bytes
	memcpy(out+p, nm, 24); p += 24;
	memcpy(out+p, c->colHeader, c->colHeaderSize); p += c->colHeaderSize;
	memcpy(out+p, c->rawdata, c->rawdataSize); p += c->rawdataSize;
	FILE *f = fopen(path, "wb");
	bool ok = false;
	if(f){
		ok = ((int)fwrite(out, 1, total, f) == total);
		fclose(f);
	}
	free(out);
	if(!ok)
		log("BlenderBridge: failed to write %s\n", path);
	return ok;
}
