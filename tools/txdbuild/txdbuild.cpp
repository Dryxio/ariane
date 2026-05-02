#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <ctype.h>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

char *argv0;

using namespace std;
using namespace rw;

struct {
	const char *str;
	uint32 val;
} platforms[] = {
	{ "ps2",    PLATFORM_PS2 },
	{ "xbox",   PLATFORM_XBOX },
	{ "d3d8",   PLATFORM_D3D8 },
	{ "d3d9",   PLATFORM_D3D9 },
	{ NULL, 0 }
};

char*
skipWhite(char *s)
{
	while(isspace(*s))
		s++;
	return s;
}

char*
LoadLine(FILE *f)
{
	static char linebuf[1024];
again:
	if(fgets(linebuf, 1024, f) == nil)
		return nil;
	// remove leading whitespace
	char *s = skipWhite(linebuf);
	// remove trailing whitespace
	int end = strlen(s);
	char c;
	while(c = s[--end], isspace(c))
		s[end] = '\0';
	// convert ',' -> ' '
	for(char *t = s; *t; t++)
		if(*t == ',') *t = ' ';
	// don't return empty lines
	if(*s == '\0')
		goto again;
	return s;
}

struct TxdDef
{
	char name[32];
	rw::TexDictionary *texDict;
	TxdDef *next;
};
TxdDef *txds;

void
MakeTxdCurrent(char *name)
{
	TxdDef *txd;
	for(txd = txds; txd; txd = txd->next){
		if(strcmp_ci(name, txd->name) == 0){
			rw::TexDictionary::setCurrent(txd->texDict);
			return;
		}
	}

	txd = new TxdDef;
	strcpy(txd->name, name);
	txd->texDict = rw::TexDictionary::create();
	txd->next = txds;
	txds = txd;
	rw::TexDictionary::setCurrent(txd->texDict);
	printf("created %s\n", txd->name);
}

void
KeepTextures(void)
{
	rw::TexDictionary *txd = rw::TexDictionary::getCurrent();
	FORLIST(lnk, txd->textures){
		Texture *tex = Texture::fromDict(lnk);
		tex->addRef();
	}
}

void
LoadModel(char *model)
{
	char path[256];
	// TODO: don't hardcode path
	sprintf(path, "Models/map_img/%s.dff", model);


	StreamFile in;
	if(!in.open(path, "rb")){
		fprintf(stderr, "Error: couldn't open %s\n", path);
		return;
	}

	currentUVAnimDictionary = nil;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(&in, &header);
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(&in);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(&in, &header);
	}
	if(header.type != ID_CLUMP){
		fprintf(stderr, "Error: couldn't file clump in %s\n", path);
		in.close();
		return;
	}
	Clump *c = Clump::streamRead(&in);
	in.close();
	if(c == nil){
		fprintf(stderr, "Error: couldn't read clump\n");
		return;
	}
	KeepTextures();
	c->destroy();
}

void
ReadIDE(FILE *f)
{
	char *line;
	int id;
	char model[32], txd[32];
	int sect = 0;

	while(line = LoadLine(f)){
		if(sect){
			if(strcmp(line, "end") == 0){
				sect = 0;
			}else{
				sscanf(line, "%d %s %s", &id, model, txd);
				MakeTxdCurrent(txd);
				LoadModel(model);
			//	printf("%s %s\n", model, txd);
			}
		}else if(strcmp(line, "objs") == 0 || strcmp(line, "tobj") == 0)
			sect = 1;
	}
}

void
ReadDat(FILE *f)
{
	char *line;
	FILE *ff;

	while(line = LoadLine(f)){
	//	printf("%s\n", line);
		if(strncmp(line, "IDE", 3) == 0){
			ff = fopen(line + 4, "r");
			if(ff == nil){
				fprintf(stderr, "couldn't open %s\n", line+4);
				continue;
			}
			ReadIDE(ff);
			fclose(ff);
		}else if(strncmp(line, "IMAGEPATH", 9) == 0){
			char path[256];
			strcpy(path, line+10);
			strcat(path, "/");
			rw::Image::setSearchPath(path);
		}
	}
}

void
DumpTxds(void)
{
	char path[256];
	StreamFile out;
	TxdDef *txd;
	for(txd = txds; txd; txd = txd->next){
		if(txd->texDict->count() == 0)
			continue;

		// TODO: don't hardcode path
		sprintf(path, "Models/txd/%s.txd", txd->name);

		if(out.open(path, "wb")){
			printf("writing %s\n", path);
			txd->texDict->streamWrite(&out);
			out.close();
		}else
			printf("can't wrtie %s\n", path);
	}		
}

void
usage(void)
{
	fprintf(stderr, "usage: %s [-v version] [-o platform] gta.dat\n", argv0);
	fprintf(stderr, "\t-v RW version, e.g. 33004 for 3.3.0.4\n");
	fprintf(stderr, "\t-o output platform. ps2, xbox, mobile, d3d8, d3d9\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
//	rw::version = 0x36003;
	rw::version = 0x34003;

//	rw::platform = rw::PLATFORM_PS2;
//	rw::platform = rw::PLATFORM_OGL;
//	rw::platform = rw::PLATFORM_XBOX;
	rw::platform = rw::PLATFORM_D3D8;
//	rw::platform = rw::PLATFORM_D3D9;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();
//	rw::d3d::isP8supported = 0;

	char *s;
	ARGBEGIN{
	case 'v':
		sscanf(EARGF(usage()), "%x", &rw::version);
		break;
	case 'o':
		s = EARGF(usage());
		for(int i = 0; platforms[i].str; i++){
			if(strcmp(platforms[i].str, s) == 0){
				rw::platform = platforms[i].val;
				goto found;
			}
		}
		fprintf(stderr, "unknown platform %s\n", s);
		rw::platform = PLATFORM_D3D8;
	found:
		break;
	default:
		usage();
	}ARGEND;

	if(argc < 1)
		usage();

	FILE *f;
	f = fopen(argv[0], "r");
	if(f == nil){
		fprintf(stderr, "couldn't open file %s\n", argv[0]);
		return 1;
	}

	ReadDat(f);

	fclose(f);

	DumpTxds();

	return 0;
}
