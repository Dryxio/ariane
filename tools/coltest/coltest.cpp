#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include <args.h>

#include <rwgta.h>
#include <collisions.h>

char *argv0;

#define COLL 0x4C4C4F43

struct ColHeader {
	rw::uint32 ident;
	rw::uint32 size;
};

void
writeSingleCol(CColModel *colmodel, char *name)
{
	char colname[24];
	ColHeader header;
	rw::uint8 *buf;
	rw::StreamFile stream;
	stream.open("out.col", "wb");
	header.ident = COLL;
	header.size = writeColModel(colmodel, &buf)+24;
	stream.write8(&header, 8);
	memset(colname, 0, 24);
	strncpy(colname, name, 24);
	stream.write8(colname, 24);
	stream.write8(buf, header.size-24);
	delete[] buf;
	stream.close();
}

void
readColFile(rw::Stream *stream)
{
	ColHeader header;
	char name[24];

	printf("reading bundle\n");
	while(1){
		if(stream->read8(&header, 8) == 0 ||
		   header.ident != COLL)
			return;
		rw::uint8 *buf = new rw::uint8[header.size];
		stream->read8(buf, header.size);
		memcpy(name, buf, 24);
		CColModel *colmodel = new CColModel;
		printf("%s %x\n", name, header.size);
		// SA only: *(uint16)(buf+22) == object id
		readColModel(colmodel, buf+24);
		delete[] buf;

		writeSingleCol(colmodel, name);
		delete colmodel;
	}
}

void
usage(void)
{
	fprintf(stderr, "usage: %s in.col\n", argv0);
	exit(1);
}

int
main(int argc, char *argv[])
{
	rw::Engine::init();
	gta::attachPlugins();
	rw::Engine::open(nil);
	rw::Engine::start();
	ARGBEGIN{
	}ARGEND;
	if(argc < 1)
		usage();
    
	rw::StreamFile in;
	if(in.open(argv[0], "rb") == NULL)
		return 1;

	readColFile(&in);

	in.close();

	return 0;
}
