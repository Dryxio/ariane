#include "III.h"

static int32 nodeNameOffset;

static void*
createNodeName(void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	name[0] = '\0';
	return object;
}

static void*
copyNodeName(void *dst, void *src, int32 offset, int32)
{
	char *dstname = PLUGINOFFSET(char, dst, offset);
	char *srcname = PLUGINOFFSET(char, src, offset);
	strncpy(dstname, srcname, 23);
	return dst;
}

static void*
destroyNodeName(void *object, int32, int32)
{
	return object;
}

static rw::Stream*
readNodeName(rw::Stream *stream, int32 len, void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	stream->read8(name, len);
	name[len] = '\0';
	//printf("%s\n", name);
	return stream;
}

static rw::Stream*
writeNodeName(rw::Stream *stream, int32 len, void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	stream->write8(name, len);
	return stream;
}

static int32
getSizeNodeName(void *object, int32 offset, int32)
{
	char *name = PLUGINOFFSET(char, object, offset);
	int32 len = strlen(name);
	return len > 0 ? len : 0;
}

enum
{
	ID_NODENAME       = MAKEPLUGINID(VEND_ROCKSTAR, 0xFE),
};

void
NodeNamePluginAttach(void)
{
	nodeNameOffset = rw::Frame::registerPlugin(24, ID_NODENAME,
	                                       createNodeName,
	                                       destroyNodeName,
	                                       copyNodeName);
	rw::Frame::registerPluginStream(ID_NODENAME,
	                            readNodeName,
	                            writeNodeName,
	                            getSizeNodeName);
}

char*
GetFrameNodeName(rw::Frame *f)
{
	return PLUGINOFFSET(char, f, nodeNameOffset);
}
