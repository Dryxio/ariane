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

Raster*
xboxToD3d8(Raster *raster)
{
	using namespace xbox;

	Raster *newras;
	if(raster->platform != PLATFORM_XBOX)
		return raster;
	XboxRaster *ras = PLUGINOFFSET(XboxRaster, raster, nativeRasterOffset);

	int32 numLevels = raster->getNumLevels();

	int32 format = raster->format;
//	format &= ~Raster::MIPMAP;
	if(ras->format){
		newras = Raster::create(raster->width, raster->height, raster->depth,
		                        format | raster->type | Raster::DONTALLOCATE, PLATFORM_D3D8);
		int32 dxt = 0;
		switch(ras->format){
		case xbox::D3DFMT_DXT1:
			dxt = 1;
			break;
		case xbox::D3DFMT_DXT3:
			dxt = 3;
			break;
		case xbox::D3DFMT_DXT5:
			dxt = 5;
			break;
		}
		d3d::allocateDXT(newras, dxt, numLevels, ras->hasAlpha);
	}else{
	//	printf("swizzled!\n");
		newras = Raster::create(raster->width, raster->height, raster->depth,
		                        format | raster->type, PLATFORM_D3D8);
	}

	if(raster->format & Raster::PAL4)
		d3d::setPalette(newras, ras->palette, 32);
	else if(raster->format & Raster::PAL8)
		d3d::setPalette(newras, ras->palette, 256);

	uint8 *data;
	for(int32 i = 0; i < numLevels; i++){
		if(i >= newras->getNumLevels())
			break;
		data = raster->lock(i, Raster::LOCKREAD);
		d3d::setTexels(newras, data, i);
		raster->unlock(i);
	}

	raster->destroy();
	return newras;
}

float gGamma = 1.0f;
float changeGamma(float f)
{
	return pow(f, gGamma);
}
float gAdd = 0.0f;
float gMult = 1.0f;
float changeLinearMap(float f)
{
	f = f*gMult + gAdd;
	if(f < 0.0f) f = 0.0f;
	if(f >= 1.0f) f = 1.0f;
	return f;
}
float gammaAndMap(float f)
{
	return changeLinearMap(changeGamma(f));
}

void
colmod(uint8 *col, float (*f)(float))
{
	int i;
	for(i = 0; i < 3; i++)
		col[i] = f(col[i]/255.0f)*255;
}

void
colmod1555(uint8 *col, float (*f)(float))
{
	uint16 c = *(uint16*)col;
	int r, g, b;
	r = c>>10 & 0x1F;
	g = c>>5 & 0x1F;
	b = c & 0x1F;
	c &= 0x8000;
	r = f(r/31.0f)*31;
	g = f(g/31.0f)*31;
	b = f(b/31.0f)*31;
	c |= r<<10 | g<<5 | b;
	*(uint16*)col = c;
}

void
colmod565(uint8 *col, float (*f)(float))
{
	int r, g, b;
	uint16 c;

	c = *(uint16*)col;
	r = c>>11 & 0x1F;
	g = c>>5 & 0x3F;
	b = c & 0x1F;
	r = f(r/31.0f)*31;
	g = f(g/63.0f)*63;
	b = f(b/31.0f)*31;
	c = r<<11 | g<<5 | b;
	*(uint16*)col = c;
}

void
rastermodcustom(Raster *ras, float (*f)(float))
{
	using namespace d3d;
	D3dRaster *natras = PLUGINOFFSET(D3dRaster, ras, nativeRasterOffset);

	uint8 *cols;
	int w, h;

	int levels = ras->getNumLevels();
	int n;
	w = ras->width;
	h = ras->height;
	for(n = 0; n < levels; n++){
		cols = ras->lock(n, Raster::LOCKWRITE);

		switch(natras->format){
		case D3DFMT_DXT1:
			for(int32 j = 0; j < w*h/2; j += 8){
				colmod565(&cols[j+0], f);
				colmod565(&cols[j+2], f);
			}
			break;
		case D3DFMT_DXT3:
			for(int32 j = 0; j < w*h; j += 16){
				colmod565(&cols[j+8], f);
				colmod565(&cols[j+10], f);
			}
			break;
		case D3DFMT_DXT5:
			for(int32 j = 0; j < w*h; j += 16){
				colmod565(&cols[j+8], f);
				colmod565(&cols[j+10], f);
			}
			break;
		default:
			fprintf(stderr, "unknown custom format %x\n", natras->format);
			break;
		}

		w /= 2;
		h /= 2;
		ras->unlock(n);
	}
}

void
rastermod(Raster *ras, float (*f)(float))
{
	using namespace d3d;
	if(ras->platform != PLATFORM_D3D8)
		return;
	D3dRaster *natras = PLUGINOFFSET(D3dRaster, ras, nativeRasterOffset);

	if(natras->customFormat){
		rastermodcustom(ras, f);
		return;
	}

	uint8 *cols;
	int len;
	if(ras->format & (Raster::PAL4 | Raster::PAL8)){
		cols = (uint8*)natras->palette;
		if(ras->format & Raster::PAL4)
			len = 16;
		else
			len = 256;
		while(len--){
			colmod(cols, f);
			cols += 4;
		}
	}else if((ras->format & 0xF00) == Raster::C888 ||
		(ras->format & 0xF00) == Raster::C8888){
		int levels = ras->getNumLevels();
		int n;
		len = ras->width * ras->height;
		for(n = 0; n < levels; n++){
			cols = ras->lock(n, Raster::LOCKWRITE);
			int origlen = len;
			while(len--){
				colmod(cols, f);
				cols += 4;
			}
			len = origlen/4;
			ras->unlock(n);
		}
	}else if((ras->format & 0xF00) == Raster::C1555){
		int levels = ras->getNumLevels();
		int n;
		len = ras->width * ras->height;
		for(n = 0; n < levels; n++){
			cols = ras->lock(n, Raster::LOCKWRITE);
			int origlen = len;
			while(len--){
				colmod1555(cols, f);
				cols += 2;
			}
			len = origlen/4;
			ras->unlock(n);
		}
	}else
		fprintf(stderr, "cannot convert\n");
}

void
changeDepth(Texture *tex, int depth)
{
	Image *img = tex->raster->toImage();
	tex->raster->destroy();

	if(depth == 4 || depth == 8)
		img->palettize(depth);
	// TODO: more

	tex->raster = Raster::createFromImage(img);
	img->destroy();
}

struct StrAssoc {
	const char *key;
	int val;

	static int get(StrAssoc *desc, char *key);
};

int
StrAssoc::get(StrAssoc *desc, char *key)
{
	for(; desc->key[0] != '\0'; desc++)
		if(strcmp(desc->key, key) == 0)
			return desc->val;
	return desc->val;
}

enum {
	NEW = 1,
	SEARCHPATH,
	ADD,
	DEPTH,
	LINEAR,
	WRITE
};

StrAssoc cmds[] = {
	{ "new", NEW },
	{ "searchpath", SEARCHPATH },
	{ "add", ADD },
	{ "depth", DEPTH },
	{ "linear", LINEAR },
	{ "write", WRITE },
	{ "", 0 }
};

char*
skipwhite(char *s)
{
	while(isspace(*s))
		s++;
	return s;
}

char*
getline(void)
{
	static char line[1024];
again:
	if(fgets(line, 1024, stdin) == NULL)
		return NULL;
	// remove leading whitespace
	char *s = skipwhite(line);
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

char*
removequotes(char *s)
{
	char *t;
	if(*s == '"'){
		s++;
		for(t = s; *t != '\0'; t++)
			if(*t == '"'){
				*t = '\0';
				break;
			}
	}
	return s;
}

int
runscript(void)
{
	char *s, *arg;
	int cmd;
	int depth;
	TexDictionary::setCurrent(TexDictionary::create());
	rw::Texture::setLoadTextures(true);
	Texture *tex = NULL;
	StreamFile out;
	while(s = getline()){
		if(s[0] == '#')
			continue;
		s = strtok(s, "\t");
		cmd = StrAssoc::get(cmds, s);
		arg = strtok(NULL, "\t");
#define NEEDARG(arg) \
			if(arg == NULL){\
				fprintf(stderr, "missing argument to '%s'\n", s);\
				break;\
			}
		switch(cmd){
		case NEW:
			TexDictionary::getCurrent()->destroy();
			TexDictionary::setCurrent(TexDictionary::create());
			break;
		case SEARCHPATH:
			NEEDARG(arg);
			Image::setSearchPath(arg);
			break;
		case ADD:
			NEEDARG(arg);
			arg = removequotes(arg);
			tex = Texture::read(arg, strtok(NULL, "\t"));
			break;
		case DEPTH:
			NEEDARG(arg);
			depth = atoi(arg);
			if(tex)
				changeDepth(tex, depth);
			break;
		case LINEAR:
			if(tex)
				tex->filterAddressing = tex->filterAddressing&~0xFF | Texture::LINEAR;
			break;
		case WRITE:
			NEEDARG(arg);
			if(out.open(arg, "wb")){
				TexDictionary::getCurrent()->streamWrite(&out);
				out.close();
			}else
				fprintf(stderr, "couldn't write txd %s\n", arg);
			break;
		default:
			fprintf(stderr, "unknown command '%s'\n", s);
		}
#undef NEEDARG
	}
	return 0;
}

bool
fileExists(const char *path)
{
	FILE *f;
	f = fopen(path, "r");
	if(f == nil)
		return false;
	fclose(f);
	return true;
}

void
makeUniqueFilename(char *filename, const char *file, const char *extension)
{
	int i;
	sprintf(filename, "%s%s", file, extension);
	if(!fileExists(filename))
		return;
	for(i = 1; ; i++){
		sprintf(filename, "%s_!%d%s", file, i, extension);
		if(!fileExists(filename))
			return;
	}
}

// Only overwrite a TGA if it's larger than the one already existing
void
writeLargerTGA(Image *image, const char *filename)
{
	FILE *f;
	f = fopen(filename, "rb");
	if(f == nil){
		writeTGA(image, filename);
		printf("writing %s\n", filename);
	}else{
		uint16 w, h;
		fseek(f, 12, SEEK_SET);
		fread(&w, 1, 2, f);
		fread(&h, 1, 2, f);
		fclose(f);
		if(image->width > w){
			if(image->height < h)
				printf("warning: %s is smaller in height\n", filename);
			writeTGA(image, filename);
			printf("writing %s\n", filename);
		}
	}
}


void
usage(void)
{
	fprintf(stderr, "usage: %s [-v version] [-o platform] in.txd [out.txd]\n", argv0);
	fprintf(stderr, "\t-v RW version, e.g. 33004 for 3.3.0.4\n");
	fprintf(stderr, "\t-o output platform. ps2, xbox, mobile, d3d8, d3d9\n");
	fprintf(stderr, "\t-x extract textures\n");
	fprintf(stderr, "\t-l list textures\n");
	fprintf(stderr, "\t-m (with -x) extracts masks separately\n");
	fprintf(stderr, "\t-t convert palettized to 32 bit\n");
	fprintf(stderr, "\t-s run script from stdin, see source code\n");
	fprintf(stderr, "\t-g change gamma by 1.5 to make LCS/VCS style textures (only d3d8 textures)\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	rw::version = 0x36003;
//	rw::platform = rw::PLATFORM_PS2;
//	rw::platform = rw::PLATFORM_OGL;
//	rw::platform = rw::PLATFORM_XBOX;
	rw::platform = rw::PLATFORM_D3D8;
//	rw::platform = rw::PLATFORM_D3D9;
//	int outplatform = rw::PLATFORM_XBOX;
	int outplatform = rw::PLATFORM_D3D9;
	int script = 0;
	int extract = 0;
	int separatemask = 0;
	int unpalettize = 0;
	int leedsgamma = 0;
	int listtex = 0;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();
	rw::Texture::setLoadTextures(false);
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
				outplatform = platforms[i].val;
				goto found;
			}
		}
		fprintf(stderr, "unknown platform %s\n", s);
		outplatform = PLATFORM_D3D8;
	found:
		break;
	case 's':
		script++;
		break;
	case 'x':
		extract++;
		break;
	case 'm':
		separatemask++;
		break;
	case 't':
		unpalettize++;
		break;
	case 'g':
		leedsgamma++;
		break;
	case 'l':
		listtex++;
		break;
	default:
		usage();
	}ARGEND;

	if(script)
		return runscript();

	if(argc < 1)
		usage();

	rw::StreamFile in;
	if(in.open(argv[0], "rb") == NULL){
		fprintf(stderr, "couldn't open file %s\n", argv[0]);
		return 1;
	}
	ChunkHeaderInfo header;
	readChunkHeaderInfo(&in, &header);
	assert(header.type == ID_TEXDICTIONARY);
	rw::TexDictionary *txd;
	txd = rw::TexDictionary::streamRead(&in);
	assert(txd);
	in.close();
	rw::TexDictionary::setCurrent(txd);

	if(rw::version == 0){
		rw::version = header.version;
		rw::build = header.build;
	}

	if(listtex){
		FORLIST(lnk, txd->textures){
			Texture *tex = Texture::fromDict(lnk);
			printf("%d %d %d %s %s\n", tex->raster->width, tex->raster->height, tex->raster->depth, tex->name, tex->mask);
		}
	}

	if(leedsgamma){
		FORLIST(lnk, txd->textures){
			Texture *tex = Texture::fromDict(lnk);
			Raster *ras = tex->raster;

			gGamma = 1.5f;
			rastermod(ras, changeGamma);

			// brighten vehicle body
//			gGamma = 1/2.0f;
//			gAdd = 0.1f;
//			gMult = 1.0f - gAdd;
//			if(strstr(tex->name, "body"))
//				rastermod(ras, gammaAndMap);

//			if((ras->format & 0xF00) == Raster::C1555)
//				printf("%s %s\n", argv[0], tex->name);
		}
	}

/*
	FORLIST(lnk, txd->textures){
		Texture *tex = Texture::fromDict(lnk);
		Raster *ras = tex->raster;
		d3d::D3dRaster *natras = PLUGINOFFSET(d3d::D3dRaster, ras, d3d::nativeRasterOffset);
		if(ras->platform == PLATFORM_D3D8 || ras->platform == PLATFORM_D3D9)
		if(natras->customFormat == 0 && (ras->format & 0xF00) == Raster::C1555)
			printf("%s %s\n", argv[0], tex->name);
	}
*/
	FORLIST(lnk, txd->textures){
		Texture *tex = Texture::fromDict(lnk);
		tex->raster = Raster::convertTexToCurrentPlatform(tex->raster);
/*
		if(tex->raster->platform == PLATFORM_XBOX && outplatform == PLATFORM_D3D8){
			tex->raster = xboxToD3d8(tex->raster);
		}else if(tex->raster->platform == PLATFORM_D3D8 && outplatform == PLATFORM_D3D9){
			if(tex->raster->platform == PLATFORM_D3D8)
				tex->raster->platform = PLATFORM_D3D9;
		}else{
			rw::platform = outplatform;
			Image *img = tex->raster->toImage();
			tex->raster = Raster::createFromImage(img);
		}
*/
	}

//	for(Texture *tex = txd->first; tex; tex = tex->next)
//		tex->filterAddressing = (tex->filterAddressing&~0xF) | 0x2;
	rw::platform = outplatform;

	if(unpalettize){
		FORLIST(lnk, txd->textures){
			Texture *tex = Texture::fromDict(lnk);
			Image *img = tex->raster->toImage();
			img->unpalettize();
			Raster *ras = tex->raster;
			tex->raster = Raster::createFromImage(img);
			ras->destroy();
			img->destroy();
		}
	}

//	const char *ext = ".tga";
	const char *ext = ".png";

	char filename[1024];
	if(extract){
		FORLIST(lnk, txd->textures){
			Texture *tex = Texture::fromDict(lnk);
			Image *img = tex->raster->toImage();
	//		img->unindex();
			if(separatemask == 2){
				if(img->hasAlpha())
					sprintf(filename, "%s##%s%s", tex->name, tex->mask, ext);
				else
					sprintf(filename, "%s%s", tex->name, ext);
				writePNG(img, filename);
			}else{
			if(separatemask && tex->mask[0]){
				Image *mask = img->extractMask();
				// write colors without alpha if we have a
				// separate file.
				img->removeMask();

				sprintf(filename, "%s%s", tex->mask, ext);
//makeUniqueFilename(filename, tex->mask, ".tga");
//				writeLargerTGA(mask, filename);
//				writeTGA(mask, filename);
				writePNG(mask, filename);
				mask->destroy();
//			}else if(img->hasAlpha()){
//				printf("%s has alpha but no mask (%d)\n", tex->name, img->depth);
			}

			sprintf(filename, "%s%s", tex->name, ext);
//makeUniqueFilename(filename, tex->name, ".tga");
//			writeLargerTGA(img, filename);
//			writeTGA(img, filename);
			writePNG(img, filename);
			}

			img->destroy();
		}
	}else{
		rw::StreamFile out;
		if(argc > 1)
			out.open(argv[1], "wb");
		else
			out.open("out.txd", "wb");
		txd->streamWrite(&out);
		out.close();
	}

	return 0;
}
