#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include <args.h>

#include <rwgta.h>

#include "../euryopa/minilzo/minilzo.h"


using namespace std;
using namespace rw;

static struct {
	const char *str;
	uint32 val;
} platforms[] = {
	{ "mobile", PLATFORM_WDGL },
	{ "ps2",    PLATFORM_PS2 },
	{ "xbox",   PLATFORM_XBOX },
	{ "d3d8",   PLATFORM_D3D8 },
	{ "d3d9",   PLATFORM_D3D9 },
	{ NULL, 0 }
};

char *argv0;
char *inputfilename;

/* case insensitive fopen */
FILE*
fmustopen_ci(const char *path, const char *mode)
{
	char cipath[1024];
	strncpy(cipath, path, 1024);
	rw::makePath(cipath);
	FILE *f = fopen(cipath, mode);
	if(f == nil){
		fprintf(stderr, "error: couldn't open '%s'\n", cipath);
		exit(1);
	}
	return f;
}


void
usage(void)
{
	fprintf(stderr, "usage: %s input.img [output.img]\n", argv0);
	exit(1);
}

static uint8 *compressionBuf;
static uint32 compressionBufSize;

static uint8*
DecompressFile(uint8 *src, uint32 *size)
{
	static uint8 blockbuf[128*1024];
	int32 total = *((int32*)src+2);
	total -= 12;
	src += 12;

	int sz = 0;
	while(total > 0){
		assert(*(uint32*)src == 4);
		uint32 blocksz = *((uint32*)src+2);
		src += 12;
		lzo_uint out_len = 128*1024;
		lzo_int r = lzo1x_decompress_safe(src, blocksz, blockbuf, &out_len, 0);
		if(r != LZO_E_OK){
			fprintf(stderr, "LZO decompress error");
			return nil;
		}
		while(sz + out_len > compressionBufSize){
			if(compressionBufSize == 0)
				compressionBufSize = 10*2048;
			compressionBufSize *= 2;
			compressionBuf = (uint8*)realloc(compressionBuf, compressionBufSize);
		}
		memcpy(compressionBuf+sz, blockbuf, out_len);
		sz += out_len;

		src += blocksz;
		total -= blocksz+12;
	}
	if(size)
		*size = sz;
	return compressionBuf;
}

struct DirEntry
{
	uint32 pos;
	uint32 size;
	char name[24];
};

FILE *inimg, *indir;
FILE *outimg, *outdir;
StreamFile stream;

uint8*
getFileContents(DirEntry *entry, uint32 *size)
{
	uint8 *data;
	data = rwNewT(uint8, entry->size*2048, 0);
	fseek(inimg, entry->pos*2048, SEEK_SET);
	fread(data, 1, entry->size*2048, inimg);
	*size = entry->size * 2048;

	if(*(uint32*)data == 0x67A3A1CE){
		uint8 *tmp = DecompressFile(data, size);
		rwFree(data);
		data = rwNewT(uint8, *size, 0);
		memcpy(data, tmp, *size);
	}

	return data;
}

void
finishFile(DirEntry *entry, uint32 start, uint32 end)
{
	static uint8 zero[2048];
	uint32 size = end - start;
	entry->pos = start / 2048;
	entry->size = (size + 2047)/2048;
	uint32 pad = entry->size*2048 - size;
	stream.write8(zero, pad);
	fwrite(entry, 1, sizeof(*entry), outdir);
}

void
copyfile(DirEntry *entry)
{
	uint32 size;
	uint8 *data = getFileContents(entry, &size);
	uint32 start = stream.tell();
	stream.write8(data, size);
	rwFree(data);
	uint32 end = stream.tell();
	finishFile(entry, start, end);
}

int outplatform = rw::PLATFORM_D3D8;

void
convertClump(Clump *clump)
{
	FORLIST(lnk, clump->atomics)
		gta::attachCustomPipelines(Atomic::fromClump(lnk));;
	int32 platform = findPlatform(clump);
	if(platform){
		rw::platform = platform;
		switchPipes(clump, rw::platform);
	}

	//if(uninstance)
		FORLIST(lnk, clump->atomics){
			Atomic *a = Atomic::fromClump(lnk);
			a->uninstance();
			if(outplatform != PLATFORM_PS2)
				ps2::unconvertADC(a->geometry);
		}

	rw::platform = outplatform;
	switchPipes(clump, rw::platform);
}

void
convertTexDict(TexDictionary *texDict)
{
	FORLIST(lnk, texDict->textures){
		Texture *tex = Texture::fromDict(lnk);
		tex->raster = Raster::convertTexToCurrentPlatform(tex->raster);
	}
}

void
convertDff(DirEntry *entry)
{
	uint32 size;
	uint8 *data = getFileContents(entry, &size);
	StreamMemory instream;
	instream.open(data, size);

	ChunkHeaderInfo header;
	uint32 start = stream.tell();
	while(readChunkHeaderInfo(&instream, &header) && header.type == ID_CLUMP){
		Clump *clump = Clump::streamRead(&instream);
		if(clump == nil){
			fprintf(stderr, "couldn't read clump '%s'\n", entry->name);
			continue;
		}
		convertClump(clump);
		clump->streamWrite(&stream);
		clump->destroy();
	}
	instream.close();
	rwFree(data);
	uint32 end = stream.tell();
	finishFile(entry, start, end);
}

void
convertTxd(DirEntry *entry)
{
	uint32 size;
	uint8 *data = getFileContents(entry, &size);
	StreamMemory instream;
	instream.open(data, size);

	ChunkHeaderInfo header;
	uint32 start = stream.tell();
	if(readChunkHeaderInfo(&instream, &header) && header.type == ID_TEXDICTIONARY){
		TexDictionary *txd = rw::TexDictionary::streamRead(&instream);
		if(txd == nil)
			fprintf(stderr, "couldn't read tex dictionary '%s'\n", entry->name);
		else{
			convertTexDict(txd);
			txd->streamWrite(&stream);
			txd->destroy();
		}
	}
	instream.close();
	rwFree(data);
	uint32 end = stream.tell();
	finishFile(entry, start, end);
}

int
main(int argc, char *argv[])
{
	if(argc < 2)
		usage();

//	rw::version = 0x34003;
	rw::version = 0x33002;
//	rw::platform = rw::PLATFORM_PS2;
//	rw::platform = rw::PLATFORM_OGL;
//	rw::platform = rw::PLATFORM_XBOX;
	rw::platform = rw::PLATFORM_D3D8;
//	rw::platform = rw::PLATFORM_D3D9;

	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();

	rw::Texture::setCreateDummies(1);



	char *basename = argv[1];
	char *ext = strrchr(basename, '.');
	if(ext == nil)
		usage();
	*ext = '\0';

	char *outname;
	if(argc > 2){
		outname = argv[2];
		char *ext = strrchr(outname, '.');
		if(ext == nil)
			usage();
		*ext = '\0';
	}else
		outname = (char*)"output";

	char tmpname[1024];
	snprintf(tmpname, sizeof(tmpname), "%s.img", basename);
	inimg = fmustopen_ci(tmpname, "rb");
	snprintf(tmpname, sizeof(tmpname), "%s.dir", basename);
	indir = fmustopen_ci(tmpname, "rb");
	snprintf(tmpname, sizeof(tmpname), "%s.img", outname);
	outimg = fmustopen_ci(tmpname, "wb");
	snprintf(tmpname, sizeof(tmpname), "%s.dir", outname);
	outdir = fmustopen_ci(tmpname, "wb");
	stream.file = outimg;

	DirEntry entry;
	while(fread(&entry, 1, sizeof(DirEntry), indir) == sizeof(DirEntry)){
		printf("%s\n", entry.name); fflush(stdout);
		ext = strrchr(entry.name, '.');
		if(ext == nil)
			copyfile(&entry);
		else if(rw::strncmp_ci(ext, ".dff", 3) == 0)
			convertDff(&entry);
		else if(rw::strncmp_ci(ext, ".txd", 3) == 0)
			convertTxd(&entry);
		else
			copyfile(&entry);
	}
	

	return 0;
}
