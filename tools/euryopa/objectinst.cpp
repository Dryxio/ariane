#include "euryopa.h"
#include "lod_associations.h"
#include "modloader.h"
#include "object_categories.h"
#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sys/stat.h>

// Blender bridge: a live-input file counts only if a running Blender wrote it
// within the last couple of seconds. Otherwise a stale file (Blender closed, or a
// leftover from a previous session) would re-hijack the camera/instances on launch.
static bool
bridgeFileIsFresh(const char *path, int maxAgeSec)
{
	struct stat st;
	if(stat(path, &st) != 0)
		return false;
	return (long)(time(nil) - st.st_mtime) <= maxAgeSec;
}

bool ReadCdImageEntryByLogicalPath(const char *logicalPath, std::vector<uint8> &data,
                                   char *outSourcePath, size_t outSourcePathSize);

CPtrList instances;
CPtrList selection;

enum
{
	OBJECT_ANIM_HAS_ROT = 1,
	OBJECT_ANIM_HAS_TRANS = 2,
	OBJECT_ANIM_HAS_SCALE = 4
};

struct ObjectAnimKeyFrame
{
	rw::Quat rotation;
	float time;
	rw::V3d translation;
};

struct ObjectAnimTrack
{
	char name[32];
	int nodeId;
	int type;
	std::vector<ObjectAnimKeyFrame> keyFrames;
};

struct ObjectAnimAsset
{
	bool attempted;
	bool loaded;
	float duration;
	char sourcePath[256];
	char animName[MODELNAMELEN];
	std::vector<ObjectAnimTrack> tracks;
};

struct ObjectAnimState
{
	rw::HAnimHierarchy *hier;
	std::vector<rw::Matrix> baseLocalMatrices;
	std::vector<int> trackIndexByNode;
};

struct IfpChunkHeader
{
	char ident[4];
	uint32 size;
};

static ObjectAnimAsset*
GetObjectAnimAsset(const ObjectDef *obj)
{
	return (ObjectAnimAsset*)obj->m_animAsset;
}

static ObjectAnimState*
GetObjectAnimState(const ObjectInst *inst)
{
	return (ObjectAnimState*)inst->m_animState;
}

static bool
readObjectAnimExact(const uint8 *data, size_t dataSize, size_t *offset, void *buf, size_t size)
{
	if(data == nil || offset == nil || *offset > dataSize || size > dataSize - *offset)
		return false;
	memcpy(buf, data + *offset, size);
	*offset += size;
	return true;
}

static rw::Quat
normalizeObjectAnimQuat(const rw::Quat &q)
{
	float len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
	if(len < 0.0001f)
		return rw::makeQuat(1.0f, 0.0f, 0.0f, 0.0f);
	return scale(q, 1.0f/len);
}

static bool
readObjectAnimChunkHeader(const std::vector<uint8> &data, size_t *offset, IfpChunkHeader *header)
{
	const uint8 *bytes = data.empty() ? nil : &data[0];
	return readObjectAnimExact(bytes, data.size(), offset, header, sizeof(*header));
}

static bool
readObjectAnimRoundedChunkData(const std::vector<uint8> &data, size_t *offset, uint32 size,
                               std::vector<uint8> &out)
{
	size_t rounded = (size + 3) & ~3;
	out.resize(rounded);
	if(rounded == 0)
		return true;
	const uint8 *bytes = data.empty() ? nil : &data[0];
	return readObjectAnimExact(bytes, data.size(), offset, &out[0], rounded);
}

static bool
loadObjectAnimLogicalPath(const char *logicalPath, std::vector<uint8> &data)
{
	char sourcePath[1024];
	sourcePath[0] = '\0';
	if(ReadCdImageEntryByLogicalPath(logicalPath, data, sourcePath, sizeof(sourcePath)))
		return true;

	const char *redirect = ModloaderGetSourcePath(logicalPath);
	int size = 0;
	uint8 *buf = nil;
	if(redirect)
		buf = ReadLooseFile(redirect, &size);
	else
		buf = ReadLooseFile(logicalPath, &size);
	if(buf && size > 0){
		data.assign(buf, buf + size);
		free(buf);
		return true;
	}
	if(buf)
		free(buf);

	char gameRoot[1024];
	char absPath[1024];
	if(GetGameRootDirectory(gameRoot, sizeof(gameRoot)) &&
	   BuildPath(absPath, sizeof(absPath), gameRoot, logicalPath)){
		buf = ReadLooseFile(absPath, &size);
		if(buf && size > 0){
			data.assign(buf, buf + size);
			free(buf);
			return true;
		}
		if(buf)
			free(buf);
	}
	return false;
}

static bool
loadObjectAnimationFromData(const std::vector<uint8> &data, const char *wantedName,
                            ObjectAnimAsset *asset)
{
	size_t offset = 0;
	IfpChunkHeader root;
	std::vector<uint8> chunkData;

	if(asset == nil)
		return false;
	asset->duration = 0.0f;
	asset->tracks.clear();

	if(!readObjectAnimChunkHeader(data, &offset, &root))
		return false;

	if(strncmp(root.ident, "ANP3", 4) == 0){
		char packageName[24];
		uint32 numAnimations = 0;
		if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, packageName, sizeof(packageName)) ||
		   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &numAnimations, sizeof(numAnimations)))
			return false;

		ObjectAnimAsset fallback = {};
		bool haveFallback = false;
		for(uint32 animIndex = 0; animIndex < numAnimations; animIndex++){
			char animName[24];
			uint32 numNodes = 0;
			uint32 frameDataSize = 0;
			uint32 flags = 0;
			if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, animName, sizeof(animName)) ||
			   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &numNodes, sizeof(numNodes)) ||
			   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &frameDataSize, sizeof(frameDataSize)) ||
			   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &flags, sizeof(flags)))
				return false;

			bool wanted = rw::strcmp_ci(animName, wantedName) == 0;
			ObjectAnimAsset candidate = {};
			candidate.duration = 0.0f;
			if(wanted || !haveFallback){
				strncpy(candidate.animName, animName, sizeof(candidate.animName)-1);
				candidate.tracks.reserve(numNodes);
			}

			for(uint32 nodeIndex = 0; nodeIndex < numNodes; nodeIndex++){
				char nodeName[24];
				uint32 frameType = 0;
				uint32 numKeyFrames = 0;
				uint32 boneId = 0;
				if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, nodeName, sizeof(nodeName)) ||
				   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &frameType, sizeof(frameType)) ||
				   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &numKeyFrames, sizeof(numKeyFrames)) ||
				   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &boneId, sizeof(boneId)))
					return false;

				bool hasTranslation = frameType == 2 || frameType == 4;
				bool compressed = frameType == 3 || frameType == 4;
				if(!compressed)
					return false;

				ObjectAnimTrack track = {};
				if(wanted || !haveFallback){
					strncpy(track.name, nodeName, sizeof(track.name)-1);
					track.nodeId = boneId;
					track.type = OBJECT_ANIM_HAS_ROT | (hasTranslation ? OBJECT_ANIM_HAS_TRANS : 0);
					track.keyFrames.reserve(numKeyFrames);
				}

				for(uint32 keyIndex = 0; keyIndex < numKeyFrames; keyIndex++){
					int16 qx, qy, qz, qw, dt;
					ObjectAnimKeyFrame keyFrame = {};
					if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &qx, sizeof(qx)) ||
					   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &qy, sizeof(qy)) ||
					   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &qz, sizeof(qz)) ||
					   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &qw, sizeof(qw)) ||
					   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &dt, sizeof(dt)))
						return false;

					keyFrame.rotation = normalizeObjectAnimQuat(rw::makeQuat(
						qw / 4096.0f,
						qx / 4096.0f,
						qy / 4096.0f,
						qz / 4096.0f
					));
					keyFrame.time = dt / 60.0f;

					if(hasTranslation){
						int16 tx, ty, tz;
						if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &tx, sizeof(tx)) ||
						   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &ty, sizeof(ty)) ||
						   !readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset, &tz, sizeof(tz)))
							return false;
						keyFrame.translation = { tx / 1024.0f, ty / 1024.0f, tz / 1024.0f };
					}

					if(wanted || !haveFallback)
						track.keyFrames.push_back(keyFrame);
					candidate.duration = max(candidate.duration, keyFrame.time);
				}

				if(wanted || !haveFallback)
					candidate.tracks.push_back(track);
			}

			if(wanted){
				*asset = candidate;
				return true;
			}
			if(!haveFallback){
				fallback = candidate;
				haveFallback = true;
			}
		}
		if(haveFallback){
			*asset = fallback;
			return true;
		}
		return false;
	}

	int numPackages = 0;
	if(strncmp(root.ident, "ANLF", 4) == 0){
		if(!readObjectAnimRoundedChunkData(data, &offset, root.size, chunkData) ||
		   chunkData.size() < sizeof(int32))
			return false;
		numPackages = *(int32*)&chunkData[0];
	}else if(strncmp(root.ident, "ANPK", 4) == 0){
		offset = 0;
		numPackages = 1;
	}else
		return false;

	ObjectAnimAsset fallback = {};
	bool haveFallback = false;
	for(int pkg = 0; pkg < numPackages; pkg++){
		IfpChunkHeader anpk, info;
		if(!readObjectAnimChunkHeader(data, &offset, &anpk) ||
		   !readObjectAnimChunkHeader(data, &offset, &info) ||
		   !readObjectAnimRoundedChunkData(data, &offset, info.size, chunkData) ||
		   chunkData.size() < sizeof(int32))
			return false;

		int numAnimations = *(int32*)&chunkData[0];
		for(int animIndex = 0; animIndex < numAnimations; animIndex++){
			IfpChunkHeader nameChunk, dgan, nodeInfoChunk;
			std::vector<uint8> nameData;
			if(!readObjectAnimChunkHeader(data, &offset, &nameChunk) ||
			   !readObjectAnimRoundedChunkData(data, &offset, nameChunk.size, nameData) ||
			   !readObjectAnimChunkHeader(data, &offset, &dgan) ||
			   !readObjectAnimChunkHeader(data, &offset, &nodeInfoChunk) ||
			   !readObjectAnimRoundedChunkData(data, &offset, nodeInfoChunk.size, chunkData) ||
			   chunkData.size() < 8)
				return false;

			bool wanted = rw::strcmp_ci((const char*)&nameData[0], wantedName) == 0;
			int numNodes = *(int32*)&chunkData[0];
			ObjectAnimAsset candidate = {};
			candidate.duration = 0.0f;
			if(wanted || !haveFallback){
				strncpy(candidate.animName, (const char*)&nameData[0], sizeof(candidate.animName)-1);
				candidate.tracks.reserve(numNodes);
			}

			for(int nodeIndex = 0; nodeIndex < numNodes; nodeIndex++){
				IfpChunkHeader cpan, animChunk;
				if(!readObjectAnimChunkHeader(data, &offset, &cpan) ||
				   !readObjectAnimChunkHeader(data, &offset, &animChunk) ||
				   !readObjectAnimRoundedChunkData(data, &offset, animChunk.size, chunkData) ||
				   chunkData.size() < 44)
					return false;

				int numKeyFrames = *(int32*)&chunkData[28];
				int nodeId = *(int32*)&chunkData[40];
				int type = 0;
				ObjectAnimTrack track = {};
				if(wanted || !haveFallback){
					strncpy(track.name, (const char*)&chunkData[0], sizeof(track.name)-1);
					track.nodeId = nodeId;
				}

				if(numKeyFrames <= 0){
					if(wanted || !haveFallback)
						candidate.tracks.push_back(track);
					continue;
				}

				IfpChunkHeader keyChunk;
				if(!readObjectAnimChunkHeader(data, &offset, &keyChunk))
					return false;

				if(strncmp(keyChunk.ident, "KR00", 4) == 0)
					type = OBJECT_ANIM_HAS_ROT;
				else if(strncmp(keyChunk.ident, "KRT0", 4) == 0)
					type = OBJECT_ANIM_HAS_ROT | OBJECT_ANIM_HAS_TRANS;
				else if(strncmp(keyChunk.ident, "KRTS", 4) == 0)
					type = OBJECT_ANIM_HAS_ROT | OBJECT_ANIM_HAS_TRANS | OBJECT_ANIM_HAS_SCALE;
				else
					return false;

				if(wanted || !haveFallback){
					track.type = type;
					track.keyFrames.reserve(numKeyFrames);
				}

				for(int keyIndex = 0; keyIndex < numKeyFrames; keyIndex++){
					ObjectAnimKeyFrame keyFrame = {};
					if(type & OBJECT_ANIM_HAS_ROT){
						if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset,
						                        &keyFrame.rotation, sizeof(keyFrame.rotation)))
							return false;
						keyFrame.rotation.x = -keyFrame.rotation.x;
						keyFrame.rotation.y = -keyFrame.rotation.y;
						keyFrame.rotation.z = -keyFrame.rotation.z;
						keyFrame.rotation = normalizeObjectAnimQuat(keyFrame.rotation);
					}
					if(type & OBJECT_ANIM_HAS_TRANS){
						if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset,
						                        &keyFrame.translation, sizeof(keyFrame.translation)))
							return false;
					}
					if(type & OBJECT_ANIM_HAS_SCALE){
						rw::V3d unusedScale;
						if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset,
						                        &unusedScale, sizeof(unusedScale)))
							return false;
					}
					if(!readObjectAnimExact(data.empty() ? nil : &data[0], data.size(), &offset,
					                        &keyFrame.time, sizeof(keyFrame.time)))
						return false;

					if(wanted || !haveFallback)
						track.keyFrames.push_back(keyFrame);
					candidate.duration = max(candidate.duration, keyFrame.time);
				}

				if(wanted || !haveFallback)
					candidate.tracks.push_back(track);
			}

			if(wanted){
				*asset = candidate;
				return true;
			}
			if(!haveFallback){
				fallback = candidate;
				haveFallback = true;
			}
		}
	}

	if(haveFallback){
		*asset = fallback;
		return true;
	}
	return false;
}

static bool
sampleObjectAnimTrack(const ObjectAnimTrack &track, float time,
                      rw::Quat *rotation, rw::V3d *translation)
{
	if(track.keyFrames.empty())
		return false;
	if(track.keyFrames.size() == 1){
		if(rotation) *rotation = track.keyFrames[0].rotation;
		if(translation) *translation = track.keyFrames[0].translation;
		return true;
	}

	const ObjectAnimKeyFrame *prev = &track.keyFrames[0];
	const ObjectAnimKeyFrame *next = &track.keyFrames[track.keyFrames.size()-1];
	for(size_t i = 1; i < track.keyFrames.size(); i++){
		if(time <= track.keyFrames[i].time){
			next = &track.keyFrames[i];
			prev = &track.keyFrames[i-1];
			break;
		}
	}

	float span = next->time - prev->time;
	float t = span > 0.0001f ? (time - prev->time) / span : 0.0f;
	t = clamp(t, 0.0f, 1.0f);
	if(rotation) *rotation = slerp(prev->rotation, next->rotation, t);
	if(translation) *translation = add(scale(prev->translation, 1.0f - t), scale(next->translation, t));
	return true;
}

static ObjectAnimAsset*
EnsureObjectAnimAsset(ObjectDef *obj)
{
	if(obj == nil || obj->m_animname[0] == '\0')
		return nil;

	ObjectAnimAsset *asset = GetObjectAnimAsset(obj);
	if(asset){
		if(asset->attempted && !asset->loaded)
			return nil;
		return asset->loaded ? asset : nil;
	}

	asset = new ObjectAnimAsset();
	asset->attempted = true;
	obj->m_animAsset = asset;

	std::vector<std::string> candidates;
	if(obj->m_imageIndex >= 0){
		const char *archive = GetCdImageLogicalName(obj->m_imageIndex);
		if(archive && archive[0]){
			char path[512];
			if(snprintf(path, sizeof(path), "%s/%s.ifp", archive, obj->m_animname) < (int)sizeof(path))
				candidates.push_back(path);
		}
	}
	char fallback[64];
	if(snprintf(fallback, sizeof(fallback), "anim/%s.ifp", obj->m_animname) < (int)sizeof(fallback))
		candidates.push_back(fallback);
	if(snprintf(fallback, sizeof(fallback), "models/gta3.img/%s.ifp", obj->m_animname) < (int)sizeof(fallback))
		candidates.push_back(fallback);
	if(snprintf(fallback, sizeof(fallback), "models/gta_int.img/%s.ifp", obj->m_animname) < (int)sizeof(fallback))
		candidates.push_back(fallback);

	std::vector<uint8> ifpData;
	for(size_t i = 0; i < candidates.size(); i++){
		if(loadObjectAnimLogicalPath(candidates[i].c_str(), ifpData)){
			strncpy(asset->sourcePath, candidates[i].c_str(), sizeof(asset->sourcePath)-1);
			break;
		}
	}
	if(ifpData.empty()){
		log("Object animation: IFP not found for %s (%s)\n", obj->m_name, obj->m_animname);
		return nil;
	}

	if(!loadObjectAnimationFromData(ifpData, obj->m_name, asset)){
		log("Object animation: no matching anim for %s in %s\n", obj->m_name, asset->sourcePath);
		return nil;
	}

	asset->loaded = true;
	log("Object animation: loaded %s from %s for %s\n",
	    asset->animName[0] ? asset->animName : obj->m_name,
	    asset->sourcePath[0] ? asset->sourcePath : obj->m_animname,
	    obj->m_name);
	return asset;
}

static bool
SetupAnimatedClump(ObjectInst *inst, rw::Clump *clump)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	ObjectAnimAsset *asset = EnsureObjectAnimAsset(obj);
	if(asset == nil || !asset->loaded || clump == nil)
		return false;

	rw::HAnimHierarchy *hier = rw::HAnimHierarchy::get(clump);
	if(hier == nil)
		return false;
	hier->attach();

	FORLIST(lnk, clump->atomics){
		rw::Atomic *atomic = rw::Atomic::fromClump(lnk);
		if(atomic && atomic->geometry && rw::Skin::get(atomic->geometry))
			rw::Skin::setHierarchy(atomic, hier);
	}

	ObjectAnimState *state = new ObjectAnimState();
	state->hier = hier;
	state->baseLocalMatrices.resize(hier->numNodes);
	state->trackIndexByNode.assign(hier->numNodes, -1);

	for(int i = 0; i < hier->numNodes; i++){
		if(hier->nodeInfo[i].frame)
			state->baseLocalMatrices[i] = hier->nodeInfo[i].frame->matrix;
		else
			state->baseLocalMatrices[i].setIdentity();
	}

	for(size_t i = 0; i < asset->tracks.size(); i++){
		const ObjectAnimTrack &track = asset->tracks[i];
		int index = hier->getIndex(track.nodeId);
		if(index < 0){
			for(int boneIndex = 0; boneIndex < hier->numNodes; boneIndex++){
				rw::Frame *frame = hier->nodeInfo[boneIndex].frame;
				if(frame == nil)
					continue;
				const char *boneName = gta::getNodeName(frame);
				if(boneName && rw::strcmp_ci(boneName, track.name) == 0){
					index = boneIndex;
					break;
				}
			}
		}
		if(index >= 0)
			state->trackIndexByNode[index] = (int)i;
	}

	bool haveMappedTrack = false;
	for(size_t i = 0; i < state->trackIndexByNode.size(); i++)
		if(state->trackIndexByNode[i] >= 0){
			haveMappedTrack = true;
			break;
		}
	if(!haveMappedTrack){
		delete state;
		return false;
	}

	inst->m_animState = state;
	inst->m_animTime = 0.0f;
	return true;
}

static void
ApplyAnimatedClump(ObjectInst *inst)
{
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	ObjectAnimAsset *asset = obj ? GetObjectAnimAsset(obj) : nil;
	ObjectAnimState *state = GetObjectAnimState(inst);
	if(asset == nil || !asset->loaded || state == nil || state->hier == nil)
		return;

	float animTime = inst->m_animTime;
	if(asset->duration > 0.001f)
		animTime = std::fmod(animTime, asset->duration);

	rw::HAnimHierarchy *hier = state->hier;
	rw::Matrix rootMat;
	rw::Frame *hierRoot = hier->parentFrame;
	rw::Frame *rootParent = hierRoot ? hierRoot->getParent() : nil;
	if(hierRoot && rootParent && !(hier->flags & rw::HAnimHierarchy::LOCALSPACEMATRICES))
		rootMat = *rootParent->getLTM();
	else
		rootMat.setIdentity();

	rw::Matrix *parentMat = &rootMat;
	rw::Matrix *stack[64];
	rw::Matrix **sp = stack;
	*sp++ = parentMat;

	for(int i = 0; i < hier->numNodes; i++){
		rw::Matrix local = state->baseLocalMatrices[i];
		int trackIndex = i < (int)state->trackIndexByNode.size() ? state->trackIndexByNode[i] : -1;
		if(trackIndex >= 0 && trackIndex < (int)asset->tracks.size()){
			const ObjectAnimTrack &track = asset->tracks[trackIndex];
			rw::Quat rotation;
			rw::V3d translation;
			if(sampleObjectAnimTrack(track, animTime, &rotation, &translation)){
				rw::V3d localPos = local.pos;
				local.rotate(rotation, rw::COMBINEREPLACE);
				local.pos = localPos;
				if(track.type & OBJECT_ANIM_HAS_TRANS){
					if(i == 0 && rootParent == nil && hierRoot == hier->nodeInfo[i].frame)
						local.pos = add(localPos, translation);
					else
						local.pos = translation;
				}
			}
		}

		if(hier->nodeInfo[i].frame)
			hier->nodeInfo[i].frame->matrix = local;
		if(hier->matrices)
			rw::Matrix::mult(&hier->matrices[i], &local, parentMat);

		if(hier->nodeInfo[i].flags & rw::HAnimHierarchy::PUSH){
			if(sp < stack + 64)
				*sp++ = parentMat;
		}
		parentMat = hier->matrices ? &hier->matrices[i] : parentMat;
		if(hier->nodeInfo[i].flags & rw::HAnimHierarchy::POP){
			if(sp > stack)
				parentMat = *--sp;
			else
				parentMat = &rootMat;
		}
	}

	if(hier->parentFrame)
		hier->parentFrame->updateObjects();
}

static bool
BuildAssetExportPath(const char *dir, const char *name, const char *ext, char *dst, size_t size)
{
	char filename[128];

	if(dir == nil || name == nil || ext == nil)
		return false;
	if(snprintf(filename, sizeof(filename), "%s.%s", name, ext) >= (int)sizeof(filename))
		return false;
	return BuildPath(dst, size, dir, filename);
}

static bool
WriteBufferToPath(const char *path, const uint8 *data, int size)
{
	FILE *f;

	if(path == nil || data == nil || size <= 0)
		return false;
	if(!EnsureParentDirectoriesForPath(path))
		return false;

	f = fopen(path, "wb");
	if(f == nil)
		return false;

	size_t written = fwrite(data, 1, size, f);
	fclose(f);
	return written == (size_t)size;
}

static bool
ExportEffectiveAsset(const char *name, const char *ext, int32 imageIndex, const char *dstPath)
{
	uint8 *buffer;
	int size = 0;
	bool freeBuffer = false;

	const char *loosePath = ModloaderFindOverride(name, ext);
	if(loosePath){
		buffer = ReadLooseFile(loosePath, &size);
		freeBuffer = true;
	}else{
		if(imageIndex < 0)
			return false;
		buffer = ReadFileFromImage(imageIndex, &size);
	}

	if(buffer == nil || size <= 0){
		if(freeBuffer)
			free(buffer);
		return false;
	}

	bool ok = WriteBufferToPath(dstPath, buffer, size);
	if(freeBuffer)
		free(buffer);
	return ok;
}

static bool
HasEarlierSelectedModel(CPtrNode *upto, const char *name)
{
	for(CPtrNode *p = selection.first; p && p != upto; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj;
		if(inst == nil || inst->m_isDeleted)
			continue;
		obj = GetObjectDef(inst->m_objectId);
		if(obj && rw::strncmp_ci(obj->m_name, name, MODELNAMELEN) == 0)
			return true;
	}
	return false;
}

static bool
HasEarlierSelectedTxd(CPtrNode *upto, const char *name)
{
	for(CPtrNode *p = selection.first; p && p != upto; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj;
		TxdDef *txd;
		if(inst == nil || inst->m_isDeleted)
			continue;
		obj = GetObjectDef(inst->m_objectId);
		if(obj == nil)
			continue;
		txd = GetTxdDef(obj->m_txdSlot);
		if(txd && rw::strncmp_ci(txd->name, name, MODELNAMELEN) == 0)
			return true;
	}
	return false;
}

static bool
sameIplFamily(ObjectInst *a, ObjectInst *b)
{
	if(a == nil || b == nil)
		return false;
	if(a->m_iplFilterKey[0] != '\0' && b->m_iplFilterKey[0] != '\0' &&
	   rw::strcmp_ci(a->m_iplFilterKey, b->m_iplFilterKey) == 0)
		return true;
	if(a->m_file && b->m_file && LogicalPathEquals(a->m_file->name, b->m_file->name))
		return true;
	return false;
}

static ObjectInst*
resolveInstanceLod(ObjectInst *inst)
{
	if(inst == nil)
		return nil;
	if(inst->m_lodId < 0){
		inst->m_lod = nil;
		return nil;
	}
	if(inst->m_lod && inst->m_lod != inst &&
	   inst->m_lod->m_iplIndex == inst->m_lodId &&
	   sameIplFamily(inst, inst->m_lod))
		return inst->m_lod;

	// A family save temporarily rewrites LOD pointers. If a previous operation
	// left only the local IPL index behind, recover the pointer before Delete or
	// Undelete decides whether the paired LOD must follow the HD instance.
	ObjectInst *resolved = nil;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *candidate = (ObjectInst*)p->item;
		if(candidate == nil || candidate == inst || candidate->m_imageIndex >= 0)
			continue;
		if(candidate->m_iplIndex != inst->m_lodId || !sameIplFamily(inst, candidate))
			continue;
		if(resolved != nil && resolved != candidate){
			log("Ambiguous LOD index %d in IPL family; refusing delete cascade\n", inst->m_lodId);
			inst->m_lod = nil;
			return nil;
		}
		resolved = candidate;
	}
	inst->m_lod = resolved;
	return resolved;
}

static bool
instanceReferencesLod(ObjectInst *inst, ObjectInst *lodInst)
{
	if(inst == nil || lodInst == nil || inst == lodInst)
		return false;
	return inst->m_lodId >= 0 &&
	       inst->m_lodId == lodInst->m_iplIndex &&
	       sameIplFamily(inst, lodInst);
}

static int
countLiveLodChildren(ObjectInst *lodInst, ObjectInst *ignoreInst)
{
	int count = 0;
	CPtrNode *p;

	if(lodInst == nil)
		return 0;

	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other == ignoreInst || other->m_isDeleted)
			continue;
		if(instanceReferencesLod(other, lodInst))
			count++;
	}
	return count;
}

void
ObjectInst::UpdateMatrix(void)
{
	if(isSA() && std::fabs(m_rotation.x) <= 0.05f && std::fabs(m_rotation.y) <= 0.05f){
		// Match SA's IPL loader: quaternions with tiny X/Y components are treated
		// as heading-only instead of full 3D rotations.
		float w = m_rotation.w;
		if(w < -1.0f) w = -1.0f;
		if(w > 1.0f) w = 1.0f;
		float heading = acosf(w) * (m_rotation.z < 0.0f ? 2.0f : -2.0f);
		float s = sinf(heading);
		float c = cosf(heading);

		m_matrix.setIdentity();
		m_matrix.right = { c, s, 0.0f };
		m_matrix.up = { -s, c, 0.0f };
		m_matrix.at = { 0.0f, 0.0f, 1.0f };
		m_matrix.flags = rw::Matrix::TYPEORTHONORMAL;
	}else{
		m_matrix.rotate(conj(m_rotation), rw::COMBINEREPLACE);
	}
	m_matrix.translate(&m_translation, rw::COMBINEPOSTCONCAT);
}

void*
ObjectInst::CreateRwObject(void)
{
	rw::Frame *f;
	rw::Atomic *atomic;
	rw::Clump *clump;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil) return nil;

	if(obj->m_type == ObjectDef::ATOMIC){
		if(obj->m_atomics[0] == nil)
			return nil;
		atomic = obj->m_atomics[0]->clone();
		f = rw::Frame::create();
		atomic->setFrame(f);
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		m_rwObject = atomic;
	}else if(obj->m_type == ObjectDef::CLUMP){
		if(obj->m_clump == nil)
			return nil;
		clump = obj->m_clump->clone();
		f = clump->getFrame();
		f->transform(&m_matrix, rw::COMBINEREPLACE);
		SetupAnimatedClump(this, clump);
		m_rwObject = clump;
	}
	return m_rwObject;
}

void
ObjectInst::Init(FileObjectInstance *fi)
{
	m_objectId = fi->objectId;
	if(fi->area & 0x100) m_isUnimportant = true;
	if(fi->area & 0x400) m_isUnderWater = true;
	if(fi->area & 0x800) m_isTunnel = true;
	if(fi->area & 0x1000) m_isTunnelTransition = true;
	m_area = fi->area & 0xFF;
	m_rotation = fi->rotation;
	m_translation = fi->position;
	m_origTranslation = fi->position;
	m_origRotation = fi->rotation;
	m_savedTranslation = fi->position;
	m_savedRotation = fi->rotation;
	m_savedStateValid = true;
	m_wasSavedDeleted = false;
	m_gameEntityExists = true;
	m_lodId = fi->lod;
	UpdateMatrix();
}

void
ObjectInst::SetupBigBuilding(void)
{
	m_isBigBuilding = true;
}

CRect
ObjectInst::GetBoundRect(void)
{
	CRect rect;
	rw::V3d v;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return rect;
	CColModel *col = obj->m_colModel;

	for(int ix = 0; ix < 2; ix++)
	for(int iy = 0; iy < 2; iy++)
	for(int iz = 0; iz < 2; iz++){
		v.x = ix ? col->boundingBox.max.x : col->boundingBox.min.x;
		v.y = iy ? col->boundingBox.max.y : col->boundingBox.min.y;
		v.z = iz ? col->boundingBox.max.z : col->boundingBox.min.z;
		rw::V3d::transformPoints(&v, &v, 1, &m_matrix);
		rect.ContainPoint(v);
	}

	return rect;
}

bool
ObjectInst::IsOnScreen(void)
{
	rw::Sphere sph;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil)
		return false;
	if(obj->m_colModel == nil)
		return true;
	CColModel *col = obj->m_colModel;
	sph.center = col->boundingSphere.center;
	sph.radius = col->boundingSphere.radius;
	return TheCamera.IsSphereVisible(&sph, &m_matrix);
}

static void
updateMatFXAnim(rw::Material *m)
{
	if(!rw::UVAnim::exists(m))
		return;
	rw::UVAnim::addTime(m, timeStep);
	rw::UVAnim::applyUpdate(m);
}

void
ObjectInst::PreRender(void)
{
	int i;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil)
		return;
	if(obj->m_type == ObjectDef::ATOMIC){
		if(obj->m_hasPreRendered)
			return;
		obj->m_hasPreRendered = true;

		if(gPlayAnimations){
			rw::Atomic *atm = (rw::Atomic*)m_rwObject;
			if(rw::MatFX::getEffects(atm))
				for(i = 0; i < atm->geometry->matList.numMaterials; i++)
					updateMatFXAnim(atm->geometry->matList.materials[i]);
		}
	}else if(obj->m_type == ObjectDef::CLUMP && m_rwObject){
		if(gPlayAnimations)
			m_animTime += timeStep;
		ApplyAnimatedClump(this);
	}
}

void
ObjectInst::JumpTo(void)
{
	rw::V3d center;
	ObjectDef *obj = GetObjectDef(m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return;
	CSphere *sph = &obj->m_colModel->boundingSphere;
	rw::V3d::transformPoints(&center, &sph->center, 1, &m_matrix);
	TheCamera.setTarget(center);
	TheCamera.setDistanceFromTarget(TheCamera.minDistToSphere(sph->radius));
}

void
ObjectInst::Select(void)
{
	if(!IsInstInIplMapDocument(this)){
		// Direct clicks and rectangle/list selection all converge here. Rate-limit
		// the explanation so a drag across many reference objects yields one toast.
		static double lastLockedReferenceToast = -100.0;
		double now = ImGui::GetTime();
		if(now - lastLockedReferenceToast >= 1.0){
			if(IsIplMapDocumentOpen())
				ToastFor(TOAST_SELECTION, 3.0f,
				         "Reference object — only the opened IPL is editable.");
			else if(m_file && IsExternalIplMapLogicalPath(m_file->name))
				ToastFor(TOAST_SELECTION, 4.0f,
				         "Locked external reference — reopen its source IPL to edit it.");
			lastLockedReferenceToast = now;
		}
		return;
	}
	if(m_selected) return;
	m_selected = true;
	selection.InsertItem(this);
}

void
ObjectInst::Deselect(void)
{
	CPtrNode *p;
	if(!m_selected) return;
	m_selected = false;
	for(p = selection.first; p; p = p->next)
		if(p->item == this){
			selection.RemoveNode(p);
			return;
		}
}

void
ClearSelection(void)
{
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		((ObjectInst*)p->item)->m_selected = false;
		selection.RemoveNode(p);
	}
}

void
ObjectInst::Delete(void)
{
	if(!IsInstInIplMapDocument(this)) return;
	if(m_isDeleted) return;
	m_isDeleted = true;
	StampChangeSeq(this);
	Deselect();

	// If this HD building was the last live child of its LOD, delete the LOD too.
	ObjectInst *lodInst = resolveInstanceLod(this);
	if(lodInst && !lodInst->m_isDeleted &&
	   countLiveLodChildren(lodInst, this) == 0)
		lodInst->Delete();

	// If this IS a LOD, delete all HD buildings that reference it
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(instanceReferencesLod(other, this) && !other->m_isDeleted)
			other->Delete();
	}
}

void
ObjectInst::Undelete(void)
{
	if(!IsInstInIplMapDocument(this)) return;
	if(!m_isDeleted) return;
	m_isDeleted = false;
	StampChangeSeq(this);
	if(m_imageIndex >= 0 && m_wasSavedDeleted)
		m_isDirty = true;

	// Also undelete the LOD pair
	ObjectInst *lodInst = resolveInstanceLod(this);
	if(lodInst && lodInst->m_isDeleted)
		lodInst->Undelete();

	// If this IS a LOD, undelete HD children
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(instanceReferencesLod(other, this) && other->m_isDeleted)
			other->Undelete();
	}
}

static void
setDeletedWithoutCascade(ObjectInst *inst, bool deleted)
{
	if(inst == nil || inst->m_isDeleted == deleted)
		return;
	inst->m_isDeleted = deleted;
	StampChangeSeq(inst);
	if(deleted)
		inst->Deselect();
	else if(inst->m_imageIndex >= 0 && inst->m_wasSavedDeleted)
		inst->m_isDirty = true;
}

void
DeleteSelected(void)
{
	// Collect all instances that will be deleted (including LOD cascades)
	// First, gather the selected ones
	std::vector<ObjectInst*> toDelete;
	bool capped = false;
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted){
			if((int)toDelete.size() >= MAX_BATCH_OBJECTS){
				capped = true;
				break;
			}
			toDelete.push_back(inst);
		}
	}
	if(toDelete.empty()) return;
	if(capped)
		Toast(TOAST_DELETE, "Delete limited to first %d selected instance(s)", MAX_BATCH_OBJECTS);

	// Record the exact transition set. Undo must not recursively undelete an HD
	// child that was already deleted before this action and shares the same LOD.
	std::vector<ObjectInst*> previouslyLive;
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst && !inst->m_isDeleted)
			previouslyLive.push_back(inst);
	}

	for(int i = 0; i < (int)toDelete.size(); i++)
		toDelete[i]->Delete();

	std::vector<ObjectInst*> newlyDeleted;
	for(size_t i = 0; i < previouslyLive.size(); i++)
		if(previouslyLive[i]->m_isDeleted)
			newlyDeleted.push_back(previouslyLive[i]);
	UndoRecordDelete(newlyDeleted.data(), (int)newlyDeleted.size());
}

int
DeleteAllInstances(void)
{
	int numDeleted = 0;
	ClearSelection();

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_isDeleted || !IsInstInIplMapDocument(inst))
			continue;
		// Whole-map delete intentionally skips per-instance LOD cascade work.
		// Every instance is being deleted in the same pass, so the recursive
		// ObjectInst::Delete path only adds O(n^2) scans on large SA maps.
		inst->m_isDeleted = true;
		StampChangeSeq(inst);
		inst->m_selected = false;
		numDeleted++;
	}
	return numDeleted;
}

// Object Spawner state
bool gPlaceMode;
bool gPrefabPlaceMode;
static int spawnObjectId = -1;
static char prefabPlacePath[1024];
static GameFile *customIplFile = nil;
static const char *DEFAULT_CUSTOM_IPL_PATH = "data\\maps\\custom.ipl";
static const char *CUSTOM_IMPORT_IDE_PATH = "data/maps/ariane/custom.ide";
static const char *CUSTOM_IMPORT_IPL_PATH = "data/maps/ariane/custom.ipl";
static char currentCustomIplPath[256] = "data\\maps\\custom.ipl";
static char currentCustomIplSourcePath[1024];
static bool currentCustomIplAppendToDat = true;

#define LOD_LOOKUP_SIZE 20000
static int lodLookup[LOD_LOOKUP_SIZE];

void
InitLodLookup(void)
{
	memset(lodLookup, -1, sizeof(lodLookup));
	if(!isSA()) return;
	for(int i = 0; saLodAssociations[i][0] >= 0; i++){
		int hd = saLodAssociations[i][0];
		int lod = saLodAssociations[i][1];
		if(hd < LOD_LOOKUP_SIZE)
			lodLookup[hd] = lod;
	}
}

int
GetSpawnObjectId(void)
{
	return spawnObjectId;
}

void
SetCustomPlacementIpl(const char *logicalPath, const char *sourcePath, bool addToDat)
{
	char normalizedLogical[256];
	char resolvedSource[1024];
	if(IsIplMapDocumentOpen() &&
	   (logicalPath == nil || !LogicalPathEquals(logicalPath, GetIplMapDocumentLogicalPath())))
		return;

	if(logicalPath == nil || logicalPath[0] == '\0')
		logicalPath = DEFAULT_CUSTOM_IPL_PATH;

	strncpy(normalizedLogical, logicalPath, sizeof(normalizedLogical)-1);
	normalizedLogical[sizeof(normalizedLogical)-1] = '\0';
	for(char *p = normalizedLogical; *p; p++)
		if(*p == '/')
			*p = '\\';

	resolvedSource[0] = '\0';
	if(sourcePath && sourcePath[0]){
		strncpy(resolvedSource, sourcePath, sizeof(resolvedSource)-1);
		resolvedSource[sizeof(resolvedSource)-1] = '\0';
	}else if(!addToDat){
		BuildModloaderLogicalExportPath(logicalPath, resolvedSource, sizeof(resolvedSource));
	}

	bool sameLogical = LogicalPathEquals(currentCustomIplPath, normalizedLogical);
	bool sameSource = strcmp(currentCustomIplSourcePath, resolvedSource) == 0;
	if(!sameLogical || !sameSource || currentCustomIplAppendToDat != addToDat)
		customIplFile = nil;

	strncpy(currentCustomIplPath, normalizedLogical, sizeof(currentCustomIplPath)-1);
	currentCustomIplPath[sizeof(currentCustomIplPath)-1] = '\0';
	strncpy(currentCustomIplSourcePath, resolvedSource, sizeof(currentCustomIplSourcePath)-1);
	currentCustomIplSourcePath[sizeof(currentCustomIplSourcePath)-1] = '\0';
	currentCustomIplAppendToDat = addToDat;
}

void
SetSpawnObjectId(int id)
{
	spawnObjectId = id;
	// Open Map owns the placement target for the duration of the document.
	// Choosing a model in the Browser must not silently send new objects back
	// to the legacy custom.ipl file.
	if(IsIplMapDocumentOpen())
		return;
	ObjectDef *obj = GetObjectDef(id);
	if(obj && obj->m_file && LogicalPathEquals(obj->m_file->name, CUSTOM_IMPORT_IDE_PATH))
		SetCustomPlacementIpl(CUSTOM_IMPORT_IPL_PATH, nil, false);
	else
		SetCustomPlacementIpl(DEFAULT_CUSTOM_IPL_PATH, nil, true);
}

int
GetLodForObject(int id)
{
	if(isSA()){
		if(id >= 0 && id < LOD_LOOKUP_SIZE)
			return lodLookup[id];
		return -1;
	}else{
		ObjectDef *obj = GetObjectDef(id);
		if(obj && obj->m_relatedModel && !obj->m_isBigBuilding)
			return obj->m_relatedModel->m_id;
		return -1;
	}
}

GameFile*
GetOrCreateCurrentPlacementIplFile(void)
{
	if(customIplFile)
		return customIplFile;
	char path[256];
	strncpy(path, currentCustomIplPath, sizeof(path));
	path[sizeof(path)-1] = '\0';
	customIplFile = NewGameFile(path);
	if(currentCustomIplSourcePath[0]){
		free(customIplFile->sourcePath);
		customIplFile->sourcePath = strdup(currentCustomIplSourcePath);
	}
	return customIplFile;
}

static int
GetMaxIplIndexForFile(GameFile *file)
{
	int maxIplIndex = -1;
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_file && file &&
		   LogicalPathEquals(other->m_file->name, file->name) &&
		   other->m_imageIndex < 0){
			if(other->m_iplIndex > maxIplIndex)
				maxIplIndex = other->m_iplIndex;
		}
	}
	return maxIplIndex;
}

static ObjectInst*
createSpawnedInstance(int objectId, rw::V3d position, GameFile *file, int iplIndex,
	const rw::Quat *orientation = nil)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = objectId;
	inst->m_area = currentArea;
	if(orientation){
		inst->m_rotation = *orientation;
	}else{
		inst->m_rotation.x = 0;
		inst->m_rotation.y = 0;
		inst->m_rotation.z = 0;
		inst->m_rotation.w = 1;
	}
	inst->m_translation = position;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = file;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = iplIndex;
	SetInstIplFilterKey(inst, file ? file->name : nil);
	inst->m_isAdded = true;
	inst->m_isDirty = true;
	inst->m_savedStateValid = false;
	inst->m_wasSavedDeleted = false;
	inst->m_gameEntityExists = false;
	StampChangeSeq(inst);

	ObjectDef *obj = GetObjectDef(objectId);

	if(obj && obj->m_isBigBuilding)
		inst->SetupBigBuilding();

	inst->UpdateMatrix();

	if(obj && !obj->IsLoaded()){
		RequestObject(objectId);
		LoadAllRequestedObjects();
	}

	inst->CreateRwObject();

	if(obj && obj->m_colModel)
		InsertInstIntoSectors(inst);
	else{
		CPtrList *list = inst->m_isBigBuilding
			? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
	}

	return inst;
}

static void
finalizeLinkedLod(ObjectInst *hdInst, ObjectInst *lodInst)
{
	ObjectDef *hdObj, *lodObj;

	hdInst->m_lodId = lodInst->m_iplIndex;
	hdInst->m_lod = lodInst;
	lodInst->m_numChildren = 1;

	if(!lodInst->m_isBigBuilding)
		lodInst->SetupBigBuilding();

	hdObj = GetObjectDef(hdInst->m_objectId);
	lodObj = GetObjectDef(lodInst->m_objectId);
	if(hdObj && lodObj && lodInst->m_numChildren == 1 && hdObj->m_colModel){
		lodObj->m_colModel = hdObj->m_colModel;
		lodObj->m_gotChildCol = true;
	}

	RemoveInstFromSectors(lodInst);
	InsertInstIntoSectors(lodInst);
}

// Core spawn helper: creates HD (+ optional LOD), applies orientation,
// selects the HD, and fills outInsts. Does NOT clear selection, does NOT
// record undo, does NOT emit a toast — caller handles those so scatter/brush
// can batch an entire stroke into one undo entry.
// Returns the number of insts written to outInsts (1 or 2).
int
SpawnPlaceObjectNoUndo(rw::V3d position, const rw::Quat *orientation,
	ObjectInst **outInsts, int outCapacity)
{
	if(spawnObjectId < 0) return 0;
	ObjectDef *obj = GetObjectDef(spawnObjectId);
	if(obj == nil) return 0;
	if(outInsts == nil || outCapacity <= 0) return 0;

	GameFile *file = GetOrCreateCurrentPlacementIplFile();
	int maxIdx = GetMaxIplIndexForFile(file);

	int lodObjId = -1;
	if(isSA()){
		if(spawnObjectId < LOD_LOOKUP_SIZE)
			lodObjId = lodLookup[spawnObjectId];
	}else{
		if(obj->m_relatedModel && !obj->m_isBigBuilding)
			lodObjId = obj->m_relatedModel->m_id;
	}

	int n = 0;
	ObjectInst *lodInst = nil;
	if(lodObjId >= 0 && GetObjectDef(lodObjId) && n < outCapacity){
		lodInst = createSpawnedInstance(lodObjId, position, file, ++maxIdx, orientation);
		outInsts[n++] = lodInst;
	}

	if(n >= outCapacity) return n;
	ObjectInst *hdInst = createSpawnedInstance(spawnObjectId, position, file, ++maxIdx, orientation);
	if(lodInst)
		finalizeLinkedLod(hdInst, lodInst);
	hdInst->Select();
	outInsts[n++] = hdInst;

	return n;
}

void
SpawnPlaceObject(rw::V3d position, const rw::Quat *orientation)
{
	if(spawnObjectId < 0) return;
	ObjectDef *obj = GetObjectDef(spawnObjectId);
	if(obj == nil) return;

	ObjectInst *pasted[4];
	ClearSelection();
	int numPasted = SpawnPlaceObjectNoUndo(position, orientation, pasted, 4);
	if(numPasted == 0) return;

	UndoRecordPaste(pasted, numPasted);
	Toast(TOAST_SPAWN, "Placed %s", obj->m_name);
}

void
SpawnExitPlaceMode(void)
{
	gPlaceMode = false;
	spawnObjectId = -1;
}

void
EnterPrefabPlaceMode(const char *path)
{
	if(path == nil || path[0] == '\0')
		return;
	strncpy(prefabPlacePath, path, sizeof(prefabPlacePath));
	prefabPlacePath[sizeof(prefabPlacePath)-1] = '\0';
	gPrefabPlaceMode = true;
	gPlaceMode = false;
}

void
ExitPrefabPlaceMode(void)
{
	gPrefabPlaceMode = false;
	prefabPlacePath[0] = '\0';
}

const char*
GetPrefabPlacePath(void)
{
	return prefabPlacePath;
}

// Object Browser categories
static int categoryLookup[NUMOBJECTDEFS];

void
InitObjectCategories(void)
{
	memset(categoryLookup, -1, sizeof(categoryLookup));
	for(int i = 0; objCategoryMap[i][0] >= 0; i++){
		int id = objCategoryMap[i][0];
		int cat = objCategoryMap[i][1];
		if(id < NUMOBJECTDEFS && categoryLookup[id] < 0)
			categoryLookup[id] = cat;
	}
}

int
GetObjectCategory(int modelId)
{
	if(modelId < 0 || modelId >= NUMOBJECTDEFS) return -1;
	return categoryLookup[modelId];
}

// Favourites
static bool favourites[NUMOBJECTDEFS];

void
LoadFavourites(void)
{
	memset(favourites, 0, sizeof(favourites));
	FILE *f = fopenArianeDataRead("favourites.txt", "favourites.txt");
	if(f == nil) return;
	char line[64];
	while(fgets(line, sizeof(line), f)){
		int id = atoi(line);
		if(id >= 0 && id < NUMOBJECTDEFS)
			favourites[id] = true;
	}
	fclose(f);
}

void
SaveFavourites(void)
{
	FILE *f = fopenArianeDataWrite("favourites.txt");
	if(f == nil) return;
	for(int i = 0; i < NUMOBJECTDEFS; i++)
		if(favourites[i])
			fprintf(f, "%d\n", i);
	fclose(f);
}

bool
IsFavourite(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return false;
	return favourites[id];
}

void
ToggleFavourite(int id)
{
	if(id < 0 || id >= NUMOBJECTDEFS) return;
	favourites[id] = !favourites[id];
	SaveFavourites();
}

// Diff viewer — monotonic change counter for chronological ordering
static uint32 gChangeSeqCounter = 0;

uint32
BumpChangeSeq(void)
{
	return ++gChangeSeqCounter;
}

void
StampChangeSeq(ObjectInst *inst)
{
	inst->m_changeSeq = BumpChangeSeq();
}

uint32
GetLatestChangeSeq(void)
{
	return gChangeSeqCounter;
}

float
GetQuaternionSimilarity(const rw::Quat &a, const rw::Quat &b)
{
	float dot = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
	float lenSqA = a.x*a.x + a.y*a.y + a.z*a.z + a.w*a.w;
	float lenSqB = b.x*b.x + b.y*b.y + b.z*b.z + b.w*b.w;
	if(lenSqA <= 1.0e-12f || lenSqB <= 1.0e-12f){
		float direct = (a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) +
		               (a.z-b.z)*(a.z-b.z) + (a.w-b.w)*(a.w-b.w);
		float negated = (a.x+b.x)*(a.x+b.x) + (a.y+b.y)*(a.y+b.y) +
		                (a.z+b.z)*(a.z+b.z) + (a.w+b.w)*(a.w+b.w);
		return direct <= 1.0e-12f || negated <= 1.0e-12f ? 1.0f : 0.0f;
	}
	float similarity = fabsf(dot) / sqrtf(lenSqA*lenSqB);
	return similarity < 1.0f ? similarity : 1.0f;
}

// Diff viewer — compute bitmask of changes since last save
int
GetInstanceDiffFlags(ObjectInst *inst)
{
	if(!inst->m_savedStateValid){
		// Never saved — it's new (if not deleted)
		if(!inst->m_isDeleted)
			return DIFF_ADDED;
		return 0;	// created then deleted before save — ignore
	}
	if(inst->m_isDeleted && !inst->m_wasSavedDeleted)
		return DIFF_DELETED;
	if(inst->m_isDeleted)
		return 0;	// was deleted at save, still deleted

	int flags = 0;
	if(!inst->m_isDeleted && inst->m_wasSavedDeleted)
		flags |= DIFF_RESTORED;
	float dist = length(sub(inst->m_translation, inst->m_savedTranslation));
	if(dist >= 0.001f)
		flags |= DIFF_MOVED;
	if(GetQuaternionSimilarity(inst->m_rotation, inst->m_savedRotation) < 0.9999f)
		flags |= DIFF_ROTATED;
	return flags;
}

struct PrefabEntry {
	int objectId;
	char modelName[MODELNAMELEN];
	float relX, relY, relZ;
	float rotX, rotY, rotZ, rotW;
	int area;
	int lodRef;
};

static bool
ReadPrefabEntries(const char *path, PrefabEntry *entries, int maxEntries, int *numEntriesOut, bool showToasts)
{
	FILE *f = fopen(path, "r");
	if(f == nil){
		log("ReadPrefabEntries: failed to open %s\n", path);
		return false;
	}

	char line[512];
	int version = 0, game = -1, count = 0;
	while(fgets(line, sizeof(line), f)){
		if(strncmp(line, "ARIANE_PREFAB", 13) == 0)
			sscanf(line, "ARIANE_PREFAB %d", &version);
		else if(strncmp(line, "game", 4) == 0)
			sscanf(line, "game %d", &game);
		else if(strncmp(line, "count", 5) == 0){
			sscanf(line, "count %d", &count);
			break;
		}
	}

	if(version < 1 || count <= 0){
		fclose(f);
		log("ReadPrefabEntries: invalid prefab header in %s\n", path);
		return false;
	}

	if(game >= 0 && game != (int)params.map){
		fclose(f);
		if(showToasts)
			Toast(TOAST_SPAWN, "Prefab is for a different game");
		return false;
	}

	if(count > maxEntries){
		if(showToasts)
			Toast(TOAST_SPAWN, "Prefab too large (%d instances, max %d)", count, maxEntries);
		count = maxEntries;
	}

	int numEntries = 0;
	while(numEntries < count && fgets(line, sizeof(line), f)){
		PrefabEntry *e = &entries[numEntries];
		memset(e, 0, sizeof(*e));
		e->lodRef = -1;
		int n = sscanf(line, "%d , %29s %f , %f , %f , %f , %f , %f , %f , %d , %d",
			&e->objectId, e->modelName, &e->relX, &e->relY, &e->relZ,
			&e->rotX, &e->rotY, &e->rotZ, &e->rotW,
			&e->area, &e->lodRef);
		if(n >= 9){
			char *comma = strchr(e->modelName, ',');
			if(comma) *comma = '\0';
			if(n < 11) e->lodRef = -1;
			numEntries++;
		}
	}
	fclose(f);

	if(numEntries == 0)
		return false;
	if(numEntriesOut)
		*numEntriesOut = numEntries;
	return true;
}

// Keep preview passes isolated from the main scene camera. On GL3, endUpdate()
// does not restore the framebuffer, viewport, matrices, or other device state.
rw::Texture *gPreviewTexture;
static rw::Raster *previewColorRaster;
static rw::Raster *previewDepthRaster;
static rw::Camera *previewCamera;
static bool previewInited;
static bool previewInitFailed;

#define PREVIEW_SIZE 256
#define OBJECT_THUMBNAIL_SIZE 96
#define OBJECT_THUMBNAIL_SLOTS 64
#define OBJECT_THUMBNAILS_PER_FRAME 3
#define PREFAB_THUMBNAIL_SIZE 128
#define PREFAB_THUMBNAIL_SLOTS 32
#define PREFAB_THUMBNAILS_PER_FRAME 1

struct ObjectThumbnailSlot
{
	int objectId;
	bool ready;
	bool failed;
	uint32 lastUsed;
	int lastRequestedFrame;
	rw::Raster *colorRaster;
	rw::Raster *depthRaster;
	rw::Texture *texture;
};

static ObjectThumbnailSlot objectThumbnailSlots[OBJECT_THUMBNAIL_SLOTS];
static uint32 objectThumbnailUseSeq;
static bool objectThumbnailSlotsInited;
static bool objectThumbnailInitFailed;

struct PrefabThumbnailSlot
{
	char path[512];
	bool ready;
	bool failed;
	uint32 lastUsed;
	int lastRequestedFrame;
	rw::Raster *colorRaster;
	rw::Raster *depthRaster;
	rw::Texture *texture;
};

static PrefabThumbnailSlot prefabThumbnailSlots[PREFAB_THUMBNAIL_SLOTS];
static uint32 prefabThumbnailUseSeq;
static bool prefabThumbnailInitFailed;

static rw::Camera*
getPreviewCamera(void)
{
	if(previewCamera)
		return previewCamera;

	previewCamera = rw::Camera::create();
	if(previewCamera == nil)
		return nil;
	rw::Frame *frame = rw::Frame::create();
	if(frame == nil){
		previewCamera->destroy();
		previewCamera = nil;
		return nil;
	}
	previewCamera->setFrame(frame);
	if(Scene.world)
		Scene.world->addCamera(previewCamera);
	return previewCamera;
}

static void
initObjectThumbnailSlots(void)
{
	if(objectThumbnailSlotsInited)
		return;
	for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS; i++)
		objectThumbnailSlots[i].objectId = -1;
	objectThumbnailSlotsInited = true;
}

static void
destroyObjectThumbnailSlot(ObjectThumbnailSlot *slot)
{
	if(slot->texture){
		slot->texture->raster = nil;
		slot->texture->destroy();
		slot->texture = nil;
	}
	if(slot->colorRaster){ slot->colorRaster->destroy(); slot->colorRaster = nil; }
	if(slot->depthRaster){ slot->depthRaster->destroy(); slot->depthRaster = nil; }
	slot->objectId = -1;
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = 0;
	slot->lastRequestedFrame = 0;
}

static void
destroyPrefabThumbnailSlot(PrefabThumbnailSlot *slot)
{
	if(slot->texture){
		slot->texture->raster = nil;
		slot->texture->destroy();
		slot->texture = nil;
	}
	if(slot->colorRaster){ slot->colorRaster->destroy(); slot->colorRaster = nil; }
	if(slot->depthRaster){ slot->depthRaster->destroy(); slot->depthRaster = nil; }
	slot->path[0] = '\0';
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = 0;
	slot->lastRequestedFrame = 0;
}

static bool
initObjectThumbnailSlot(ObjectThumbnailSlot *slot)
{
	if(slot->texture && slot->colorRaster && slot->depthRaster)
		return true;
	if(objectThumbnailInitFailed)
		return false;

	destroyObjectThumbnailSlot(slot);
	slot->colorRaster = rw::Raster::create(OBJECT_THUMBNAIL_SIZE, OBJECT_THUMBNAIL_SIZE, 0, rw::Raster::CAMERATEXTURE);
	if(slot->colorRaster == nil){
		objectThumbnailInitFailed = true;
		return false;
	}
	slot->depthRaster = rw::Raster::create(OBJECT_THUMBNAIL_SIZE, OBJECT_THUMBNAIL_SIZE, 0, rw::Raster::ZBUFFER);
	if(slot->depthRaster == nil){
		objectThumbnailInitFailed = true;
		return false;
	}
	slot->texture = rw::Texture::create(slot->colorRaster);
	if(slot->texture == nil){
		objectThumbnailInitFailed = true;
		return false;
	}
	slot->texture->setFilter(rw::Texture::LINEAR);
	slot->objectId = -1;
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = 0;
	slot->lastRequestedFrame = 0;
	return true;
}

static bool
initPrefabThumbnailSlot(PrefabThumbnailSlot *slot)
{
	if(slot->texture && slot->colorRaster && slot->depthRaster)
		return true;
	if(prefabThumbnailInitFailed)
		return false;

	char path[512];
	strncpy(path, slot->path, sizeof(path));
	path[sizeof(path)-1] = '\0';
	destroyPrefabThumbnailSlot(slot);
	strncpy(slot->path, path, sizeof(slot->path));
	slot->path[sizeof(slot->path)-1] = '\0';

	slot->colorRaster = rw::Raster::create(PREFAB_THUMBNAIL_SIZE, PREFAB_THUMBNAIL_SIZE, 0, rw::Raster::CAMERATEXTURE);
	if(slot->colorRaster == nil){
		prefabThumbnailInitFailed = true;
		return false;
	}
	slot->depthRaster = rw::Raster::create(PREFAB_THUMBNAIL_SIZE, PREFAB_THUMBNAIL_SIZE, 0, rw::Raster::ZBUFFER);
	if(slot->depthRaster == nil){
		prefabThumbnailInitFailed = true;
		return false;
	}
	slot->texture = rw::Texture::create(slot->colorRaster);
	if(slot->texture == nil){
		prefabThumbnailInitFailed = true;
		return false;
	}
	slot->texture->setFilter(rw::Texture::LINEAR);
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = 0;
	slot->lastRequestedFrame = 0;
	return true;
}

static void
BuildPreviewMatrix(rw::Matrix *matrix, const rw::Quat &rotation, const rw::V3d &translation)
{
	if(isSA() && std::fabs(rotation.x) <= 0.05f && std::fabs(rotation.y) <= 0.05f){
		float w = rotation.w;
		if(w < -1.0f) w = -1.0f;
		if(w > 1.0f) w = 1.0f;
		float heading = acosf(w) * (rotation.z < 0.0f ? 2.0f : -2.0f);
		float s = sinf(heading);
		float c = cosf(heading);

		matrix->setIdentity();
		matrix->right = { c, s, 0.0f };
		matrix->up = { -s, c, 0.0f };
		matrix->at = { 0.0f, 0.0f, 1.0f };
		matrix->flags = rw::Matrix::TYPEORTHONORMAL;
	}else
		matrix->rotate(conj(rotation), rw::COMBINEREPLACE);
	matrix->translate(&translation, rw::COMBINEPOSTCONCAT);
}

static bool
renderObjectToRaster(int objectId, rw::Raster *colorRaster, rw::Raster *depthRaster, int size, float angle)
{
	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil)
		return false;

	rw::Atomic *atm = nil;
	rw::Clump *clump = nil;
	if(!CreateObjectPreviewRwObject(objectId, &atm, &clump))
		return false;

	float radius = 5.0f;
	if(obj->m_colModel)
		radius = obj->m_colModel->boundingSphere.radius;
	if(radius < 1.0f) radius = 1.0f;
	float dist = radius * 2.5f;

	rw::V3d target = { 0.0f, 0.0f, 0.0f };
	if(obj->m_colModel)
		target = obj->m_colModel->boundingSphere.center;
	rw::V3d eye = { target.x + dist*cosf(angle), target.y + dist*sinf(angle), target.z + dist*0.4f };
	rw::V3d up = { 0.0f, 0.0f, -1.0f };
	rw::V3d dir = normalize(sub(target, eye));

	rw::Camera *cam = getPreviewCamera();
	if(cam == nil){
		if(atm){ atm->getFrame()->destroy(); atm->destroy(); }
		if(clump){ clump->destroy(); }
		return false;
	}
	rw::Frame *camFrame = cam->getFrame();

	cam->frameBuffer = colorRaster;
	cam->zBuffer = depthRaster;
	cam->setNearPlane(0.1f);
	cam->setFarPlane(1000.0f);
	cam->fogPlane = 900.0f;
	cam->setFOV(55.0f, 1.0f);
	camFrame->matrix.pos = eye;
	camFrame->matrix.lookAt(dir, up);
	camFrame->updateObjects();

	rw::RGBA clearCol = { 34, 34, 34, 255 };
	cam->clear(&clearCol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	cam->beginUpdate();

	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	pAmbient->setColor(0.7f, 0.7f, 0.7f);
	pDirect->setColor(0.5f, 0.5f, 0.5f);

	rw::Matrix ident;
	ident.setIdentity();
	if(atm){
		atm->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		atm->render();
	}else if(clump){
		clump->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		clump->render();
	}

	cam->endUpdate();

	if(atm){ atm->getFrame()->destroy(); atm->destroy(); }
	if(clump){ clump->destroy(); }
	Timecycle::SetLights();
	return true;
}

static bool
renderPrefabToRaster(const char *path, rw::Raster *colorRaster, rw::Raster *depthRaster, int size)
{
	PrefabEntry entries[256];
	int numEntries = 0;
	if(!ReadPrefabEntries(path, entries, 256, &numEntries, false))
		return false;

	rw::V3d minPt = { 1.0e30f, 1.0e30f, 1.0e30f };
	rw::V3d maxPt = { -1.0e30f, -1.0e30f, -1.0e30f };
	int numBounds = 0;
	for(int i = 0; i < numEntries; i++){
		ObjectDef *obj = GetObjectDef(entries[i].objectId);
		if(obj == nil)
			continue;
		float radius = obj->m_colModel ? obj->m_colModel->boundingSphere.radius : 1.0f;
		if(radius < 1.0f) radius = 1.0f;
		rw::V3d center = { entries[i].relX, entries[i].relY, entries[i].relZ };
		if(obj->m_colModel)
			center = add(center, obj->m_colModel->boundingSphere.center);
		minPt.x = min(minPt.x, center.x - radius);
		minPt.y = min(minPt.y, center.y - radius);
		minPt.z = min(minPt.z, center.z - radius);
		maxPt.x = max(maxPt.x, center.x + radius);
		maxPt.y = max(maxPt.y, center.y + radius);
		maxPt.z = max(maxPt.z, center.z + radius);
		numBounds++;
	}
	if(numBounds == 0)
		return false;

	rw::V3d target = scale(add(minPt, maxPt), 0.5f);
	float radius = 1.0f;
	for(int i = 0; i < numEntries; i++){
		ObjectDef *obj = GetObjectDef(entries[i].objectId);
		if(obj == nil)
			continue;
		float objRadius = obj->m_colModel ? obj->m_colModel->boundingSphere.radius : 1.0f;
		rw::V3d center = { entries[i].relX, entries[i].relY, entries[i].relZ };
		if(obj->m_colModel)
			center = add(center, obj->m_colModel->boundingSphere.center);
		float dist = length(sub(center, target)) + objRadius;
		if(dist > radius)
			radius = dist;
	}
	if(radius < 2.0f) radius = 2.0f;

	struct PrefabPreviewModel
	{
		int objectId;
		rw::Atomic *atomic;
		rw::Clump *clump;
	};
	PrefabPreviewModel models[256];
	int numModels = 0;
	for(int i = 0; i < numEntries; i++){
		int modelIndex = -1;
		for(int j = 0; j < numModels; j++)
			if(models[j].objectId == entries[i].objectId){
				modelIndex = j;
				break;
			}
		if(modelIndex >= 0)
			continue;

		rw::Atomic *atomic = nil;
		rw::Clump *clump = nil;
		if(!CreateObjectPreviewRwObject(entries[i].objectId, &atomic, &clump))
			continue;
		models[numModels].objectId = entries[i].objectId;
		models[numModels].atomic = atomic;
		models[numModels].clump = clump;
		numModels++;
	}
	if(numModels == 0)
		return false;

	uint32 hash = 2166136261u;
	for(const char *s = path; *s; s++)
		hash = (hash ^ (uint8)*s) * 16777619u;
	float angle = 0.65f + (hash % 19) * 0.045f;
	float dist = radius * 2.8f;
	rw::V3d eye = { target.x + dist*cosf(angle), target.y + dist*sinf(angle), target.z + dist*0.45f };
	rw::V3d up = { 0.0f, 0.0f, -1.0f };
	rw::V3d dir = normalize(sub(target, eye));

	rw::Camera *cam = getPreviewCamera();
	if(cam == nil){
		for(int i = 0; i < numModels; i++){
			if(models[i].atomic){ models[i].atomic->getFrame()->destroy(); models[i].atomic->destroy(); }
			if(models[i].clump){ models[i].clump->destroy(); }
		}
		return false;
	}
	rw::Frame *camFrame = cam->getFrame();

	cam->frameBuffer = colorRaster;
	cam->zBuffer = depthRaster;
	cam->setNearPlane(0.1f);
	cam->setFarPlane(max(1000.0f, radius * 8.0f));
	cam->fogPlane = max(900.0f, radius * 7.0f);
	cam->setFOV(58.0f, 1.0f);
	camFrame->matrix.pos = eye;
	camFrame->matrix.lookAt(dir, up);
	camFrame->updateObjects();

	rw::RGBA clearCol = { 34, 34, 34, 255 };
	cam->clear(&clearCol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	cam->beginUpdate();

	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	pAmbient->setColor(0.72f, 0.72f, 0.72f);
	pDirect->setColor(0.52f, 0.52f, 0.52f);

	for(int i = 0; i < numEntries; i++){
		PrefabPreviewModel *model = nil;
		for(int j = 0; j < numModels; j++)
			if(models[j].objectId == entries[i].objectId){
				model = &models[j];
				break;
			}
		if(model == nil)
			continue;

		rw::Quat rotation = { entries[i].rotX, entries[i].rotY, entries[i].rotZ, entries[i].rotW };
		rw::V3d translation = { entries[i].relX, entries[i].relY, entries[i].relZ };
		rw::Matrix matrix;
		BuildPreviewMatrix(&matrix, rotation, translation);
		if(model->atomic){
			model->atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
			model->atomic->render();
		}else{
			model->clump->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
			model->clump->render();
		}
	}

	cam->endUpdate();

	for(int i = 0; i < numModels; i++){
		if(models[i].atomic){ models[i].atomic->getFrame()->destroy(); models[i].atomic->destroy(); }
		if(models[i].clump){ models[i].clump->destroy(); }
	}

	Timecycle::SetLights();
	return true;
}

void
InitPreviewRenderer(void)
{
	if(previewInited || previewInitFailed) return;
	if(getPreviewCamera() == nil){ previewInitFailed = true; return; }

	previewColorRaster = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 0, rw::Raster::CAMERATEXTURE);
	if(previewColorRaster == nil){ previewInitFailed = true; return; }
	previewDepthRaster = rw::Raster::create(PREVIEW_SIZE, PREVIEW_SIZE, 0, rw::Raster::ZBUFFER);
	if(previewDepthRaster == nil){ previewInitFailed = true; return; }
	gPreviewTexture = rw::Texture::create(previewColorRaster);
	gPreviewTexture->setFilter(rw::Texture::LINEAR);
	previewInited = true;
}

void
ShutdownPreviewRenderer(void)
{
	initObjectThumbnailSlots();
	for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS; i++)
		destroyObjectThumbnailSlot(&objectThumbnailSlots[i]);
	objectThumbnailUseSeq = 0;
	objectThumbnailInitFailed = false;
	for(int i = 0; i < PREFAB_THUMBNAIL_SLOTS; i++)
		destroyPrefabThumbnailSlot(&prefabThumbnailSlots[i]);
	prefabThumbnailUseSeq = 0;
	prefabThumbnailInitFailed = false;

	if(gPreviewTexture){
		gPreviewTexture->raster = nil;
		gPreviewTexture->destroy();
		gPreviewTexture = nil;
	}
	if(previewColorRaster){ previewColorRaster->destroy(); previewColorRaster = nil; }
	if(previewDepthRaster){ previewDepthRaster->destroy(); previewDepthRaster = nil; }
	if(previewCamera){
		rw::Frame *frame = previewCamera->getFrame();
		if(previewCamera->world)
			previewCamera->world->removeCamera(previewCamera);
		previewCamera->destroy();
		previewCamera = nil;
		if(frame) frame->destroy();
	}
	previewInited = false;
}

rw::Texture*
GetObjectThumbnailTexture(int objectId)
{
	initObjectThumbnailSlots();
	if(objectId < 0 || GetObjectDef(objectId) == nil)
		return nil;

	int requestFrame = ImGui::GetFrameCount();
	for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS; i++){
		ObjectThumbnailSlot *slot = &objectThumbnailSlots[i];
		if(slot->objectId == objectId){
			slot->lastUsed = ++objectThumbnailUseSeq;
			slot->lastRequestedFrame = requestFrame;
			return slot->ready ? slot->texture : nil;
		}
	}

	ObjectThumbnailSlot *slot = nil;
	for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS; i++)
		if(objectThumbnailSlots[i].objectId < 0){
			slot = &objectThumbnailSlots[i];
			break;
		}
	if(slot == nil){
		for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS; i++){
			if(objectThumbnailSlots[i].lastRequestedFrame == requestFrame)
				continue;
			if(slot == nil || objectThumbnailSlots[i].lastUsed < slot->lastUsed)
				slot = &objectThumbnailSlots[i];
		}
		if(slot == nil)
			return nil;
	}

	slot->objectId = objectId;
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = ++objectThumbnailUseSeq;
	slot->lastRequestedFrame = requestFrame;
	return nil;
}

void
RenderRequestedObjectThumbnails(void)
{
	initObjectThumbnailSlots();
	if(objectThumbnailInitFailed)
		return;

	int attempted = 0;
	for(int i = 0; i < OBJECT_THUMBNAIL_SLOTS && attempted < OBJECT_THUMBNAILS_PER_FRAME; i++){
		ObjectThumbnailSlot *slot = &objectThumbnailSlots[i];
		if(slot->objectId < 0 || slot->ready || slot->failed)
			continue;
		attempted++;
		int objectId = slot->objectId;
		if(!initObjectThumbnailSlot(slot))
			return;
		slot->objectId = objectId;
		float angle = 0.8f + (objectId % 11) * 0.08f;
		if(renderObjectToRaster(objectId, slot->colorRaster, slot->depthRaster, OBJECT_THUMBNAIL_SIZE, angle)){
			slot->ready = true;
		}else{
			ObjectDef *obj = GetObjectDef(objectId);
			if(obj == nil)
				slot->objectId = -1;
			else
				slot->failed = true;
		}
	}
}

rw::Texture*
GetPrefabThumbnailTexture(const char *path)
{
	if(path == nil || path[0] == '\0')
		return nil;

	int requestFrame = ImGui::GetFrameCount();
	for(int i = 0; i < PREFAB_THUMBNAIL_SLOTS; i++){
		PrefabThumbnailSlot *slot = &prefabThumbnailSlots[i];
		if(slot->path[0] != '\0' && strcmp(slot->path, path) == 0){
			slot->lastUsed = ++prefabThumbnailUseSeq;
			slot->lastRequestedFrame = requestFrame;
			return slot->ready ? slot->texture : nil;
		}
	}

	PrefabThumbnailSlot *slot = nil;
	for(int i = 0; i < PREFAB_THUMBNAIL_SLOTS; i++)
		if(prefabThumbnailSlots[i].path[0] == '\0'){
			slot = &prefabThumbnailSlots[i];
			break;
		}
	if(slot == nil){
		for(int i = 0; i < PREFAB_THUMBNAIL_SLOTS; i++){
			if(prefabThumbnailSlots[i].lastRequestedFrame == requestFrame)
				continue;
			if(slot == nil || prefabThumbnailSlots[i].lastUsed < slot->lastUsed)
				slot = &prefabThumbnailSlots[i];
		}
		if(slot == nil)
			return nil;
	}

	strncpy(slot->path, path, sizeof(slot->path));
	slot->path[sizeof(slot->path)-1] = '\0';
	slot->ready = false;
	slot->failed = false;
	slot->lastUsed = ++prefabThumbnailUseSeq;
	slot->lastRequestedFrame = requestFrame;
	return nil;
}

void
RenderRequestedPrefabThumbnails(void)
{
	if(prefabThumbnailInitFailed)
		return;

	int attempted = 0;
	for(int i = 0; i < PREFAB_THUMBNAIL_SLOTS && attempted < PREFAB_THUMBNAILS_PER_FRAME; i++){
		PrefabThumbnailSlot *slot = &prefabThumbnailSlots[i];
		if(slot->path[0] == '\0' || slot->ready || slot->failed)
			continue;
		attempted++;
		char path[512];
		strncpy(path, slot->path, sizeof(path));
		path[sizeof(path)-1] = '\0';
		if(!initPrefabThumbnailSlot(slot))
			return;
		strncpy(slot->path, path, sizeof(slot->path));
		slot->path[sizeof(slot->path)-1] = '\0';
		if(renderPrefabToRaster(path, slot->colorRaster, slot->depthRaster, PREFAB_THUMBNAIL_SIZE))
			slot->ready = true;
		else if(!doesFileExist(path))
			slot->path[0] = '\0';
		else
			slot->failed = true;
	}
}

void
RenderPreviewObject(int objectId)
{
	if(previewInitFailed) return;
	if(objectId < 0) return;
	if(!previewInited) InitPreviewRenderer();
	if(!previewInited) return;

	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil) return;
	if(!obj->IsLoaded()){
		RequestObject(objectId);
		return;
	}

	// Clone model for preview (persistent, rebuilt when selection changes)
	static rw::Atomic *previewAtm = nil;
	static rw::Clump *previewClump = nil;
	static int previewCloneId = -1;

	if(previewCloneId != objectId){
		if(previewAtm){ previewAtm->getFrame()->destroy(); previewAtm->destroy(); previewAtm = nil; }
		if(previewClump){ previewClump->destroy(); previewClump = nil; }
		previewCloneId = objectId;

		if(obj->m_type == ObjectDef::ATOMIC && obj->m_atomics[0]){
			previewAtm = obj->m_atomics[0]->clone();
			rw::Frame *f = rw::Frame::create();
			previewAtm->setFrame(f);
		}else if(obj->m_type == ObjectDef::CLUMP && obj->m_clump){
			previewClump = obj->m_clump->clone();
		}
	}
	if(previewAtm == nil && previewClump == nil) return;

	// Camera setup
	float radius = 5.0f;
	if(obj->m_colModel)
		radius = obj->m_colModel->boundingSphere.radius;
	if(radius < 1.0f) radius = 1.0f;
	float dist = radius * 2.5f;

	static float angle = 0.0f;
	angle += 0.01f;
	if(angle > 2.0f * 3.14159f) angle -= 2.0f * 3.14159f;

	rw::V3d target = { 0.0f, 0.0f, 0.0f };
	if(obj->m_colModel)
		target = obj->m_colModel->boundingSphere.center;
	rw::V3d eye = { target.x + dist*cosf(angle), target.y + dist*sinf(angle), target.z + dist*0.4f };
	rw::V3d up = { 0.0f, 0.0f, -1.0f };  // negative Z because FBO is Y-flipped
	rw::V3d dir = normalize(sub(target, eye));

	rw::Camera *cam = getPreviewCamera();
	if(cam == nil)
		return;
	rw::Frame *camFrame = cam->getFrame();

	// Set preview camera state
	cam->frameBuffer = previewColorRaster;
	cam->zBuffer = previewDepthRaster;
	cam->setNearPlane(0.1f);
	cam->setFarPlane(1000.0f);
	cam->fogPlane = 900.0f;
	cam->setFOV(60.0f, 1.0f);
	camFrame->matrix.pos = eye;
	camFrame->matrix.lookAt(dir, up);
	camFrame->updateObjects();

	static rw::RGBA clearCol = { 40, 40, 40, 255 };
	cam->clear(&clearCol, rw::Camera::CLEARIMAGE|rw::Camera::CLEARZ);
	cam->beginUpdate();

	rw::SetRenderState(rw::FOGENABLE, 0);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	pAmbient->setColor(0.7f, 0.7f, 0.7f);
	pDirect->setColor(0.5f, 0.5f, 0.5f);

	rw::Matrix ident;
	ident.setIdentity();
	if(previewAtm){
		previewAtm->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		previewAtm->render();
	}else if(previewClump){
		previewClump->getFrame()->transform(&ident, rw::COMBINEREPLACE);
		previewClump->render();
	}

	cam->endUpdate();

	Timecycle::SetLights();
}

// Clipboard
static std::vector<ObjectInst*> clipboard;
static bool clipboardIsCut;

// Undo/Redo system
#define MAX_UNDO 128
static UndoAction undoStack[MAX_UNDO];
static int undoCount;	// number of actions in stack
static int undoPos;	// current position (next undo)

void
ResetUndoHistory(void)
{
	for(int i = 0; i < undoCount; i++)
		undoStack[i] = UndoAction();
	undoCount = 0;
	undoPos = 0;
}

static void
updateRwFrameForInst(ObjectInst *inst)
{
	if(inst->m_rwObject == nil) return;
	ObjectDef *obj = GetObjectDef(inst->m_objectId);
	if(obj == nil) return;
	rw::Frame *f;
	if(obj->m_type == ObjectDef::ATOMIC)
		f = ((rw::Atomic*)inst->m_rwObject)->getFrame();
	else
		f = ((rw::Clump*)inst->m_rwObject)->getFrame();
	f->transform(&inst->m_matrix, rw::COMBINEREPLACE);
}

static void
applyUndoTransform(const UndoTransform &t, bool useNewState)
{
	ObjectInst *inst = t.inst;
	if(inst == nil)
		return;

	bool refreshSectors = (t.flags & (UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT)) != 0;
	if(refreshSectors)
		RemoveInstFromSectors(inst);
	if(t.flags & UNDO_TRANSFORM_POS)
		inst->m_translation = useNewState ? t.newPos : t.oldPos;
	if(t.flags & UNDO_TRANSFORM_ROT)
		inst->m_rotation = useNewState ? t.newRot : t.oldRot;

	StampChangeSeq(inst);
	inst->m_isDirty = true;
	inst->UpdateMatrix();
	updateRwFrameForInst(inst);

	if(refreshSectors)
		InsertInstIntoSectors(inst);
}

static void
pushUndo(UndoAction *a)
{
	// If we're not at the top, discard redo history
	for(int i = undoPos; i < undoCount; i++)
		undoStack[i] = UndoAction();
	undoCount = undoPos;
	if(undoCount >= MAX_UNDO){
		// Shift everything down
		for(int i = 1; i < MAX_UNDO; i++)
			undoStack[i-1] = std::move(undoStack[i]);
		undoStack[MAX_UNDO-1] = UndoAction();
		undoCount--;
		undoPos--;
	}
	undoStack[undoCount] = std::move(*a);
	undoCount++;
	undoPos = undoCount;
}

void
UndoRecordMove(ObjectInst *inst, rw::V3d oldPos, ObjectInst *lodInst, rw::V3d lodOldPos)
{
	UndoAction a = {};
	a.type = UNDO_MOVE;
	a.inst = inst;
	a.oldPos = oldPos;
	a.newPos = inst->m_translation;
	a.lodInst = lodInst;
	a.lodOldPos = lodOldPos;
	if(lodInst)
		a.lodNewPos = lodInst->m_translation;
	pushUndo(&a);
}

void
UndoRecordRotate(ObjectInst *inst, rw::Quat oldRot)
{
	UndoAction a = {};
	a.type = UNDO_ROTATE;
	a.inst = inst;
	a.oldRot = oldRot;
	a.newRot = inst->m_rotation;
	pushUndo(&a);
}

void
UndoRecordDelete(ObjectInst **insts, int num)
{
	UndoAction a = {};
	a.type = UNDO_DELETE;
	a.numDeleted = num;
	a.deletedInsts.assign(insts, insts + num);
	pushUndo(&a);
}

void
UndoRecordPaste(ObjectInst **insts, int num)
{
	UndoAction a = {};
	a.type = UNDO_PASTE;
	a.numPasted = num;
	a.pastedInsts.assign(insts, insts + num);
	pushUndo(&a);
}

void
UndoRecordTransformBatch(UndoTransform *transforms, int num)
{
	if(num <= 0)
		return;

	UndoAction a = {};
	a.type = UNDO_TRANSFORM_BATCH;
	a.numTransforms = num;
	a.transforms.assign(transforms, transforms + num);
	pushUndo(&a);
}

void
Undo(void)
{
	if(undoPos <= 0) return;
	undoPos--;
	UndoAction *a = &undoStack[undoPos];

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->oldPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodOldPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->oldRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			setDeletedWithoutCascade(a->deletedInsts[i], false);
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Deselect();
			a->pastedInsts[i]->Delete();
		}
		break;
	case UNDO_TRANSFORM_BATCH:
		for(int i = 0; i < a->numTransforms; i++)
			applyUndoTransform(a->transforms[i], false);
		break;
	}
}

void
Redo(void)
{
	if(undoPos >= undoCount) return;
	UndoAction *a = &undoStack[undoPos];
	undoPos++;

	switch(a->type){
	case UNDO_MOVE:
		a->inst->m_translation = a->newPos;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		if(a->lodInst){
			a->lodInst->m_translation = a->lodNewPos;
			a->lodInst->UpdateMatrix();
			updateRwFrameForInst(a->lodInst);
		}
		break;
	case UNDO_ROTATE:
		a->inst->m_rotation = a->newRot;
		a->inst->UpdateMatrix();
		updateRwFrameForInst(a->inst);
		break;
	case UNDO_DELETE:
		for(int i = 0; i < a->numDeleted; i++)
			setDeletedWithoutCascade(a->deletedInsts[i], true);
		break;
	case UNDO_PASTE:
		for(int i = 0; i < a->numPasted; i++){
			a->pastedInsts[i]->Undelete();
			a->pastedInsts[i]->Select();
		}
		break;
	case UNDO_TRANSFORM_BATCH:
		for(int i = 0; i < a->numTransforms; i++)
			applyUndoTransform(a->transforms[i], true);
		break;
	}
}

ObjectInst*
GetInstanceByID(int32 id)
{
	CPtrNode *p;
	for(p = instances.first; p; p = p->next)
		if(((ObjectInst*)p->item)->m_id == id)
			return (ObjectInst*)p->item;
	return nil;
}

ObjectInst*
AddInstance(void)
{
	static int32 id = 1;

	ObjectInst *inst = new ObjectInst;
	memset(inst, 0, sizeof(ObjectInst));
	inst->m_id = id++;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	instances.InsertItem(inst);
	return inst;
}

void
CopySelected(void)
{
	clipboard.clear();
	clipboardIsCut = false;
	CPtrNode *p;
	bool capped = false;
	for(p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted){
			if((int)clipboard.size() >= MAX_BATCH_OBJECTS){
				capped = true;
				break;
			}
			clipboard.push_back(inst);
		}
	}
	if(capped)
		Toast(TOAST_COPY_PASTE, "Copy limited to first %d selected instance(s)", MAX_BATCH_OBJECTS);
	if(!clipboard.empty())
		log("Copied %d instance(s)\n", (int)clipboard.size());
}

void
CutSelected(void)
{
	clipboard.clear();
	clipboardIsCut = false;
	CPtrNode *p;
	bool capped = false;
	for(p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted){
			if((int)clipboard.size() >= MAX_BATCH_OBJECTS){
				capped = true;
				break;
			}
			clipboard.push_back(inst);
		}
	}
	if(capped)
		Toast(TOAST_COPY_PASTE, "Cut limited to first %d selected instance(s)", MAX_BATCH_OBJECTS);
	if(clipboard.empty())
		return;

	clipboardIsCut = true;
	DeleteSelected();
	log("Cut %d instance(s)\n", (int)clipboard.size());
}

static ObjectInst*
cloneInstanceWithObjectId(ObjectInst *src, GameFile *dstFile, int iplIndex, rw::V3d offset,
	int objectId, bool preserveBigBuildingState)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = objectId;
	inst->m_area = src->m_area;
	inst->m_rotation = src->m_rotation;
	inst->m_translation.x = src->m_translation.x + offset.x;
	inst->m_translation.y = src->m_translation.y + offset.y;
	inst->m_translation.z = src->m_translation.z + offset.z;
	inst->m_isUnimportant = src->m_isUnimportant;
	inst->m_isUnderWater = src->m_isUnderWater;
	inst->m_isTunnel = src->m_isTunnel;
	inst->m_isTunnelTransition = src->m_isTunnelTransition;
	inst->m_isBigBuilding = preserveBigBuildingState ? src->m_isBigBuilding : false;
	inst->m_lodId = -1;
	inst->m_lod = nil;
	inst->m_numChildren = 0;
	inst->m_file = dstFile;
	inst->m_imageIndex = -1;
	inst->m_binInstIndex = -1;
	inst->m_iplIndex = iplIndex;
	SetInstIplFilterKey(inst, dstFile ? dstFile->name : nil);
	inst->m_isAdded = true;
	inst->m_isDirty = true;
	inst->m_savedStateValid = false;
	inst->m_wasSavedDeleted = false;
	inst->m_gameEntityExists = false;
	StampChangeSeq(inst);
	ObjectDef *obj = GetObjectDef(objectId);
	if(!preserveBigBuildingState && obj && obj->m_isBigBuilding)
		inst->SetupBigBuilding();
	inst->UpdateMatrix();
	if(obj && !obj->IsLoaded()){
		RequestObject(objectId);
		LoadAllRequestedObjects();
	}
	inst->CreateRwObject();
	InsertInstIntoSectors(inst);
	return inst;
}

static ObjectInst*
cloneInstance(ObjectInst *src, GameFile *dstFile, int iplIndex, rw::V3d offset)
{
	return cloneInstanceWithObjectId(src, dstFile, iplIndex, offset, src->m_objectId, true);
}

static GameFile*
findPasteDestinationFile(ObjectInst *src)
{
	if(src == nil)
		return nil;

	// A paste creates a new placement. Keep it out of the source map IPL so
	// copying one original-map object does not force a full replacement export
	// of that area's IPL.
	return GetOrCreateCurrentPlacementIplFile();
}

static float
instanceDistanceSq(ObjectInst *a, ObjectInst *b)
{
	rw::V3d d = sub(a->m_translation, b->m_translation);
	return dot(d, d);
}

static int
getAssociatedLodObjectId(ObjectInst *src)
{
	if(src == nil || !isSA())
		return -1;
	int lodObjId = GetLodForObject(src->m_objectId);
	if(lodObjId < 0 || lodObjId == src->m_objectId || GetObjectDef(lodObjId) == nil)
		return -1;
	return lodObjId;
}

static ObjectInst*
findNearbyAssociatedLod(ObjectInst *src, int lodObjId)
{
	if(src == nil || lodObjId < 0)
		return nil;

	ObjectInst *bestSameFamily = nil;
	ObjectInst *bestAnyFamily = nil;
	float bestSameFamilyDistSq = 1.0e30f;
	float bestAnyFamilyDistSq = 1.0e30f;

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other == nil || other == src || other->m_isDeleted)
			continue;
		if(other->m_objectId != lodObjId)
			continue;

		float distSq = instanceDistanceSq(src, other);
		if(sameIplFamily(src, other) && distSq < bestSameFamilyDistSq){
			bestSameFamily = other;
			bestSameFamilyDistSq = distSq;
		}
		if(distSq < bestAnyFamilyDistSq){
			bestAnyFamily = other;
			bestAnyFamilyDistSq = distSq;
		}
	}

	// LOD pairs normally sit at the same placement. Keep the fallback close
	// enough to avoid borrowing a repeated LOD model from another zone.
	if(bestSameFamily && bestSameFamilyDistSq <= sq(250.0f))
		return bestSameFamily;
	if(bestAnyFamily && bestAnyFamilyDistSq <= sq(25.0f))
		return bestAnyFamily;
	return nil;
}

static ObjectInst*
findPasteSourceLod(ObjectInst *src, int *lodObjIdOut, bool *usedAssociationOut)
{
	if(lodObjIdOut)
		*lodObjIdOut = -1;
	if(usedAssociationOut)
		*usedAssociationOut = false;
	if(src == nil || !isSA())
		return nil;
	if(src->m_lod && !src->m_lod->m_isDeleted)
		return src->m_lod;

	int lodObjId = getAssociatedLodObjectId(src);
	if(lodObjIdOut)
		*lodObjIdOut = lodObjId;
	if(lodObjId < 0)
		return nil;

	ObjectInst *lod = findNearbyAssociatedLod(src, lodObjId);
	if(lod && usedAssociationOut)
		*usedAssociationOut = true;
	return lod;
}

static rw::V3d
getClipboardPasteOffset(ObjectInst **toPaste, int numToPaste, bool pasteInPlace)
{
	if(pasteInPlace)
		return { 0.0f, 0.0f, 0.0f };

	rw::V3d anchor = { 0.0f, 0.0f, 0.0f };
	int numAnchors = 0;

	for(int i = 0; i < numToPaste; i++){
		ObjectInst *inst = toPaste[i];
		if(inst == nil || inst->m_isDeleted)
			continue;
		anchor.x += inst->m_translation.x;
		anchor.y += inst->m_translation.y;
		anchor.z += inst->m_translation.z;
		numAnchors++;
	}

	if(numAnchors == 0)
		return { 10.0f, 0.0f, 0.0f };

	float invCount = 1.0f / (float)numAnchors;
	anchor.x *= invCount;
	anchor.y *= invCount;
	anchor.z *= invCount;

	rw::V3d pastePos = TheCamera.m_target;
	rw::V3d groundHit;
	if(gPlaceSnapToGround && GetGroundPlacementSurface(pastePos, &groundHit, nil, true))
		pastePos = groundHit;

	return sub(pastePos, anchor);
}

static int
PasteCutClipboard(void)
{
	std::vector<ObjectInst*> restored;

	ClearSelection();
	for(int i = 0; i < (int)clipboard.size(); i++){
		ObjectInst *inst = clipboard[i];
		if(inst == nil)
			continue;
		if(inst->m_isDeleted)
			inst->Undelete();
		inst->Select();
		restored.push_back(inst);
	}

	if(!restored.empty()){
		UndoRecordPaste(restored.data(), (int)restored.size());
		log("Restored %d cut instance(s)\n", (int)restored.size());
	}
	clipboardIsCut = false;
	return (int)restored.size();
}

static int
pasteClipboard(bool pasteInPlace)
{
	if(clipboard.empty()) return 0;
	if(clipboardIsCut)
		return PasteCutClipboard();

	std::vector<ObjectInst*> pasted;

	// Deduplicate: skip LODs that are already the m_lod of another
	// clipboard entry (they'll be auto-copied with their HD parent)
	std::vector<ObjectInst*> toPaste;
	std::unordered_set<ObjectInst*> lodsCopiedWithParent;
	lodsCopiedWithParent.reserve(clipboard.size());

	for(int i = 0; i < (int)clipboard.size(); i++){
		ObjectInst *lod = findPasteSourceLod(clipboard[i], nil, nil);
		if(lod)
			lodsCopiedWithParent.insert(lod);
	}
	for(int i = 0; i < (int)clipboard.size(); i++)
		if(lodsCopiedWithParent.find(clipboard[i]) == lodsCopiedWithParent.end())
			toPaste.push_back(clipboard[i]);

	rw::V3d offset = getClipboardPasteOffset(toPaste.data(), (int)toPaste.size(), pasteInPlace);

	ClearSelection();

	int recoveredLods = 0;
	int generatedLods = 0;
	std::unordered_map<GameFile*, int> maxIplIndexByFile;

	for(int i = 0; i < (int)toPaste.size(); i++){
		ObjectInst *src = toPaste[i];
		int associatedLodObjId = -1;
		bool usedAssociatedSource = false;
		ObjectInst *srcLod = findPasteSourceLod(src, &associatedLodObjId, &usedAssociatedSource);

		GameFile *dstFile = findPasteDestinationFile(src);

		std::unordered_map<GameFile*, int>::iterator maxIt = maxIplIndexByFile.find(dstFile);
		if(maxIt == maxIplIndexByFile.end()){
			int maxIplIndex = -1;
			CPtrNode *p;
			for(p = instances.first; p; p = p->next){
				ObjectInst *other = (ObjectInst*)p->item;
				if(other->m_file == dstFile && other->m_imageIndex < 0){
					if(other->m_iplIndex > maxIplIndex)
						maxIplIndex = other->m_iplIndex;
				}
			}
			maxIt = maxIplIndexByFile.insert(std::make_pair(dstFile, maxIplIndex)).first;
		}

		// Paste LOD first (if any) so we have its iplIndex for the HD's lodId
		ObjectInst *newLod = nil;
		if(isSA() && srcLod && !srcLod->m_isDeleted){
			newLod = cloneInstance(srcLod, dstFile, ++maxIt->second, offset);
			pasted.push_back(newLod);
			if(usedAssociatedSource)
				recoveredLods++;
		}else if(isSA() && associatedLodObjId >= 0){
			newLod = cloneInstanceWithObjectId(src, dstFile, ++maxIt->second, offset,
				associatedLodObjId, false);
			pasted.push_back(newLod);
			generatedLods++;
		}

		// Paste HD
		ObjectInst *newHd = cloneInstance(src, dstFile, ++maxIt->second, offset);

		// Link LOD
		if(newLod)
			finalizeLinkedLod(newHd, newLod);

		newHd->Select();
		pasted.push_back(newHd);
	}

	if(!pasted.empty()){
		UndoRecordPaste(pasted.data(), (int)pasted.size());
		log("Pasted %d instance(s)\n", (int)pasted.size());
		if(recoveredLods > 0)
			log("Recovered %d LOD link(s) from nearby associated instances\n", recoveredLods);
			if(generatedLods > 0)
				log("Generated %d associated LOD instance(s) during paste\n", generatedLods);
	}
	return (int)pasted.size();
}

int
PasteClipboard(void)
{
	return pasteClipboard(false);
}

int
PasteClipboardInPlace(void)
{
	return pasteClipboard(true);
}

int
ExportPrefab(const char *path)
{
	// Collect selected non-deleted instances + their LODs
	ObjectInst *insts[256];
	int numInsts = 0;
	bool overflow = false;

	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted)
			continue;
		if(numInsts >= 256){ overflow = true; break; }
		insts[numInsts++] = inst;
	}

	// Auto-include linked LODs that weren't explicitly selected
	int numExplicit = numInsts;
	for(int i = 0; i < numExplicit; i++){
		ObjectInst *lod = insts[i]->m_lod;
		if(lod == nil || lod->m_isDeleted)
			continue;
		// Check if LOD is already in the list
		bool found = false;
		for(int j = 0; j < numInsts; j++){
			if(insts[j] == lod){ found = true; break; }
		}
		if(!found){
			if(numInsts >= 256){ overflow = true; break; }
			insts[numInsts++] = lod;
		}
	}

	if(numInsts == 0)
		return 0;
	if(overflow)
		Toast(TOAST_SAVE, "Prefab truncated at 256 instances (selection too large)");

	// Compute centroid
	rw::V3d centroid = {0.0f, 0.0f, 0.0f};
	for(int i = 0; i < numInsts; i++){
		centroid.x += insts[i]->m_translation.x;
		centroid.y += insts[i]->m_translation.y;
		centroid.z += insts[i]->m_translation.z;
	}
	float invCount = 1.0f / (float)numInsts;
	centroid.x *= invCount;
	centroid.y *= invCount;
	centroid.z *= invCount;

	// Build local index map for LOD references within the prefab
	// insts[i] -> local index i, so we can resolve m_lod pointers
	if(!EnsureParentDirectoriesForPath(path)){
		log("ExportPrefab: failed to create parent directories for %s\n", path);
		return 0;
	}

	FILE *f = fopen(path, "w");
	if(f == nil){
		log("ExportPrefab: failed to open %s for writing\n", path);
		return 0;
	}

	fprintf(f, "ARIANE_PREFAB 1\n");
	fprintf(f, "game %d\n", (int)params.map);
	fprintf(f, "count %d\n", numInsts);

	for(int i = 0; i < numInsts; i++){
		ObjectInst *inst = insts[i];
		ObjectDef *obj = GetObjectDef(inst->m_objectId);

		// Resolve LOD reference as local index within this prefab
		int lodRef = -1;
		if(inst->m_lod){
			for(int j = 0; j < numInsts; j++){
				if(insts[j] == inst->m_lod){
					lodRef = j;
					break;
				}
			}
		}

		int area = inst->m_area;
		if(isSA()){
			if(inst->m_isUnimportant) area |= 0x100;
			if(inst->m_isUnderWater) area |= 0x400;
			if(inst->m_isTunnel) area |= 0x800;
			if(inst->m_isTunnelTransition) area |= 0x1000;
		}

		rw::V3d rel = sub(inst->m_translation, centroid);

		// objectId, modelName, relX, relY, relZ, rotX, rotY, rotZ, rotW, area, lodRef
		fprintf(f, "%d, %s, %f, %f, %f, %f, %f, %f, %f, %d, %d\n",
			inst->m_objectId,
			obj ? obj->m_name : "unknown",
			rel.x, rel.y, rel.z,
			inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w,
			area, lodRef);
	}

	fclose(f);
	log("ExportPrefab: wrote %d instance(s) to %s\n", numInsts, path);
	return numInsts;
}

int
ExportSelectedDffs(const char *dir, int *numFailed)
{
	int exported = 0;
	int failed = 0;
	char path[1024];

	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj;

		if(inst == nil || inst->m_isDeleted)
			continue;
		obj = GetObjectDef(inst->m_objectId);
		if(obj == nil || obj->m_name[0] == '\0'){
			failed++;
			continue;
		}
		if(HasEarlierSelectedModel(p, obj->m_name))
			continue;
		if(!BuildAssetExportPath(dir, obj->m_name, "dff", path, sizeof(path)) ||
		   !ExportEffectiveAsset(obj->m_name, "dff", obj->m_imageIndex, path)){
			log("ExportSelectedDffs: failed to export %s\n", obj->m_name);
			failed++;
			continue;
		}
		exported++;
	}

	if(numFailed)
		*numFailed = failed;
	return exported;
}

// ── Blender bridge (INU_Tools) ────────────────────────────────────
#define BRIDGE_PROTO_VER 2	// bump when the job format changes; both sides check "#v N"

// set true when a reverse batch moved instance(s); gui() auto-saves the map
// Shared inbox %LOCALAPPDATA%\INU_ariane_bridge\inbox: each selected model's
// .dff (its .txd too when autoTxd, and its LOD when one exists) + a .job file:
// a "#flags" header + one TAB-separated line per DFF carrying IDE/IPL metadata
// (id, instance id, draw dist, txd name, world translation + rotation quat).
static void
AppendBlenderMetaLine(char *body, int *blen, int cap, ObjectDef *obj, TxdDef *txd, ObjectInst *inst, bool ownInstance)
{
	if(*blen >= cap-1) return;
	// Always send the transform (so Blender places the object at its world spot).
	// ownInstance: HD sends its real iid + stable guid → the reverse moves that
	// instance. The LOD sends iid=-1 / empty guid → placed in Blender, but the
	// reverse never moves an instance from it (so it can't revert the HD's move).
	char guid[300] = "";
	if(ownInstance) GetInstGuid(inst, guid, sizeof(guid));
	*blen += snprintf(body+*blen, cap-*blen,
		"%s.dff\tid=%d\tiid=%d\tguid=%s\tdd=%.3f\ttxd=%s\tx=%.4f\ty=%.4f\tz=%.4f\tqx=%.6f\tqy=%.6f\tqz=%.6f\tqw=%.6f\n",
		obj->m_name, obj->m_id, ownInstance ? inst->m_id : -1, guid, obj->GetLargestDrawDist(),
		(txd && txd->name[0]) ? txd->name : "",
		inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
		inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w);
}

int
ExportSelectedToBlender(bool autoTxd, bool vanilla, bool ide, bool ipl, bool lodToo, bool colToo, int *numFailed)
{
	int exported = 0, failed = 0;
	char inbox[1024];
	if(!GetArianeDataPath(inbox, sizeof(inbox), "bridge\\inbox")){	// <game>\ariane\bridge\inbox
		if(numFailed) *numFailed = 1;
		return 0;
	}

	static char body[262144];	// static: too big for the stack, single-threaded UI call
	int blen = 0;
	blen += snprintf(body+blen, sizeof(body)-blen, "#v %d\n", BRIDGE_PROTO_VER);
	blen += snprintf(body+blen, sizeof(body)-blen,
		"#flags auto_txd=%d vanilla=%d ide=%d ipl=%d\n",
		autoTxd?1:0, vanilla?1:0, ide?1:0, ipl?1:0);

	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_isDeleted)
			continue;
		ObjectDef *obj = GetObjectDef(inst->m_objectId);
		if(obj == nil || obj->m_name[0] == '\0'){ failed++; continue; }
		if(HasEarlierSelectedModel(p, obj->m_name))
			continue;

		char path[1024];
		if(!BuildAssetExportPath(inbox, obj->m_name, "dff", path, sizeof(path)) ||
		   !ExportEffectiveAsset(obj->m_name, "dff", obj->m_imageIndex, path)){
			log("ExportSelectedToBlender: failed to export %s\n", obj->m_name);
			failed++;
			continue;
		}
		exported++;

		// texture dictionary named after the model → INU auto-TXD (same-name) matches
		TxdDef *txd = GetTxdDef(obj->m_txdSlot);
		if(autoTxd && txd && txd->name[0] &&
		   BuildAssetExportPath(inbox, obj->m_name, "txd", path, sizeof(path)))
			ExportEffectiveAsset(txd->name, "txd", txd->imageIndex, path);

		// collision (rebuilt from the model's live CColModel) → INU imports the .col
		if(colToo && BuildAssetExportPath(inbox, obj->m_name, "col", path, sizeof(path)))
			ExportColForModel(obj, path);

		AppendBlenderMetaLine(body, &blen, (int)sizeof(body), obj, txd, inst, true);

		// LOD — only if requested and the model actually has one (LOD<name> defined)
		char lodName[MODELNAMELEN+8];
		snprintf(lodName, sizeof(lodName), "LOD%s", obj->m_name);
		int lodId = -1;
		ObjectDef *lod = nil;
		if(lodToo && GetObjectDef(lodName, &lodId))
			lod = GetObjectDef(lodId);
		if(lod && lod->m_name[0]){
			if(BuildAssetExportPath(inbox, lod->m_name, "dff", path, sizeof(path)) &&
			   ExportEffectiveAsset(lod->m_name, "dff", lod->m_imageIndex, path)){
				TxdDef *ltxd = GetTxdDef(lod->m_txdSlot);
				if(autoTxd && ltxd && ltxd->name[0] &&
				   BuildAssetExportPath(inbox, lod->m_name, "txd", path, sizeof(path)))
					ExportEffectiveAsset(ltxd->name, "txd", ltxd->imageIndex, path);
				AppendBlenderMetaLine(body, &blen, (int)sizeof(body), lod, ltxd, inst, false);
			}
		}
	}

	if(exported > 0){
		static int jobCounter = 0;
		char jobPath[1024];
		snprintf(jobPath, sizeof(jobPath), "%s\\job_%u_%d.job", inbox,
			(unsigned)time(nil), jobCounter++);
		if(!WriteBufferToPath(jobPath, (const uint8*)body, (int)strlen(body)))
			log("ExportSelectedToBlender: failed to write job file\n");
		// poke file: Blender's watcher stats this (cheap) and only scans the inbox
		// when its timestamp changes — no blind directory polling every tick.
		char pokePath[1024];
		snprintf(pokePath, sizeof(pokePath), "%s\\poke", inbox);
		char pokeBuf[32];
		int pn = snprintf(pokeBuf, sizeof(pokeBuf), "%u\n", (unsigned)time(nil));
		WriteBufferToPath(pokePath, (const uint8*)pokeBuf, pn);
	}

	if(numFailed)
		*numFailed = failed;
	return exported;
}

static ObjectInst*
FindInstByPickId(int id)
{
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *in = (ObjectInst*)p->item;
		if(in && in->m_id == id)
			return in;
	}
	return nil;
}

// Stable per-instance GUID for the Blender bridge: "<iplname>#<indexInIpl>". Unlike
// the session-local pick-id (m_id), this survives save→reload — text-IPL deletions
// only comment the line out (index preserved) and new instances append, so the IPL
// name + m_iplIndex re-map to the same instance next session.
void
GetInstGuid(ObjectInst *inst, char *buf, int sz)
{
	if(buf == nil || sz <= 0) return;
	if(inst == nil){ buf[0] = '\0'; return; }
	if(inst->m_iplFilterKey[0])
		snprintf(buf, sz, "%s#%d", inst->m_iplFilterKey, inst->m_iplIndex);
	else	// never saved / no IPL yet — fall back to the session id (not cross-session)
		snprintf(buf, sz, "@%d", inst->m_id);
}

ObjectInst*
FindInstByGuid(const char *guid)
{
	if(guid == nil || guid[0] == '\0') return nil;
	char buf[300];
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *in = (ObjectInst*)p->item;
		if(in == nil) continue;
		GetInstGuid(in, buf, sizeof(buf));
		if(buf[0] && strcmp(buf, guid) == 0)
			return in;
	}
	return nil;
}

// Move an instance and keep its render + spatial state in sync (mirrors the reverse
// bridge move / the editor's own move). Used by E-2 to place a freshly created model.
void
MoveInstanceTo(ObjectInst *inst, rw::V3d pos, rw::Quat rot)
{
	if(inst == nil) return;
	RemoveInstFromSectors(inst);
	inst->m_translation = pos;
	inst->m_rotation = rot;
	StampChangeSeq(inst);
	inst->m_isDirty = true;
	inst->UpdateMatrix();
	updateRwFrameForInst(inst);
	InsertInstIntoSectors(inst);
}

// Phase D live sync (ariane → Blender): stream the selected instances' transforms
// to a small live file the addon watches, so dragging in ariane moves the matching
// Blender objects live. Only writes when the content actually changed — an idle
// selection produces no disk churn and never clobbers a Blender-side edit. The
// reverse (Blender → ariane) stays button-triggered, so there's no feedback loop.
// Focus guard: each side only DRIVES (writes live files) while its own window is the
// OS foreground. The other side's freshness gate then stops applying (files go stale),
// so whoever you're working in is never disturbed by the background app — this fixes
// the camera getting knocked around and the "actions jump to ariane" lag.
static bool
bridgeArianeFocused(void)
{
	HWND fg = GetForegroundWindow();
	if(fg == nil) return false;
	DWORD pid = 0;
	GetWindowThreadProcessId(fg, &pid);
	return pid == GetCurrentProcessId();
}

void
WriteArianeLiveState(void)
{
	static int tick = 0;
	if(++tick < 2)		// ~30 Hz at 60 fps (smoother live sync)
		return;
	tick = 0;

	if(!bridgeArianeFocused())	// not the active window → don't drive Blender
		return;

	// camera → its own file (high-frequency, decoupled from the moves/selection
	// file so navigating in ariane doesn't churn the moves file)
	{
		char cbuf[256];
		int cn = snprintf(cbuf, sizeof(cbuf),
			"cam\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.4f\t%.4f\t%.4f\n",
			TheCamera.m_position.x, TheCamera.m_position.y, TheCamera.m_position.z,
			TheCamera.m_target.x, TheCamera.m_target.y, TheCamera.m_target.z,
			// up vector so Blender can rebuild the exact orientation (no roll guessing)
			TheCamera.m_up.x, TheCamera.m_up.y, TheCamera.m_up.z);
		static std::string lastCam;
		if(std::string(cbuf) != lastCam){
			lastCam = cbuf;
			char cpath[1024], ctmp[1024];
			if(GetArianeDataPath(cpath, sizeof(cpath), "bridge\\live\\cam_ariane.txt")){
				EnsureParentDirectoriesForPath(cpath);
				snprintf(ctmp, sizeof(ctmp), "%s.tmp", cpath);
				FILE *cf = fopen(ctmp, "wb");
				if(cf){ fwrite(cbuf, 1, cn, cf); fclose(cf); remove(cpath); rename(ctmp, cpath); }
			}
		}
	}

	std::string body = "#live 1\n";
	std::string sel = "sel";	// selected guids, one line, for selection sync
	char lb[512], guid[300];
	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_isDeleted)
			continue;
		GetInstGuid(inst, guid, sizeof(guid));
		if(guid[0] == '\0')
			continue;
		sel += '\t'; sel += guid;
		snprintf(lb, sizeof(lb),
			"mov\t%s\t%.4f\t%.4f\t%.4f\t%.6f\t%.6f\t%.6f\t%.6f\n",
			guid, inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
			inst->m_rotation.x, inst->m_rotation.y, inst->m_rotation.z, inst->m_rotation.w);
		body += lb;
	}
	body += sel; body += '\n';

	static std::string lastBody;
	if(body == lastBody)		// nothing moved/selected since last write
		return;
	lastBody = body;

	char path[1024], tmp[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\live\\from_ariane.txt"))
		return;
	EnsureParentDirectoriesForPath(path);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if(f == nil)
		return;
	fwrite(body.data(), 1, body.size(), f);
	fclose(f);
	remove(path);
	rename(tmp, path);		// atomic-ish replace
}

// Phase D live sync (Blender → ariane): apply the addon's live moves to instances
// by guid — but ONLY while the user is NOT dragging a gizmo here (gGizmoUsing). So
// dragging in ariane makes ariane master; dragging in Blender makes Blender master.
void
PollBlenderLiveIn(void)
{
	if(bridgeArianeFocused())	// ariane is the active window → it drives, don't follow
		return;
	if(gGizmoUsing)			// user is transforming in ariane → ignore Blender's stream
		return;
	static int tick = 0;
	if(++tick < 2)			// ~30 Hz
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\live\\from_blender.txt"))
		return;
	if(!bridgeFileIsFresh(path, 2))		// stale / Blender not live → ignore
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > (1<<20)){ fclose(f); return; }
	std::string data;
	data.resize((size_t)sz);
	fread(&data[0], 1, (size_t)sz, f);
	fclose(f);

	static std::string lastData;
	if(data == lastData)		// unchanged since last apply
		return;
	lastData = data;

	size_t pos = 0;
	while(pos < data.size()){
		size_t eol = data.find('\n', pos);
		std::string ln = data.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
		pos = (eol == std::string::npos) ? data.size() : eol + 1;
		if(ln.empty() || ln[0] == '#')
			continue;
		char buf[600];
		strncpy(buf, ln.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = '\0';
		char *tok = strtok(buf, "\t");
		if(tok == nil || strcmp(tok, "mov") != 0) continue;
		char *g   = strtok(nil, "\t");
		char *sx  = strtok(nil, "\t"), *sy  = strtok(nil, "\t"), *sz2 = strtok(nil, "\t");
		char *sqx = strtok(nil, "\t"), *sqy = strtok(nil, "\t");
		char *sqz = strtok(nil, "\t"), *sqw = strtok(nil, "\t");
		if(!g || !sx || !sy || !sz2 || !sqx || !sqy || !sqz || !sqw) continue;
		ObjectInst *in = FindInstByGuid(g);
		if(in == nil) continue;
		RemoveInstFromSectors(in);
		in->m_translation.x = (float)atof(sx);
		in->m_translation.y = (float)atof(sy);
		in->m_translation.z = (float)atof(sz2);
		in->m_rotation.x = (float)atof(sqx);
		in->m_rotation.y = (float)atof(sqy);
		in->m_rotation.z = (float)atof(sqz);
		in->m_rotation.w = (float)atof(sqw);
		StampChangeSeq(in);
		in->m_isDirty = true;
		in->UpdateMatrix();
		updateRwFrameForInst(in);
		InsertInstIntoSectors(in);
	}
}

static bool
camClose(const rw::V3d &a, const rw::V3d &b)
{
	return std::fabs(a.x-b.x) < 0.01f && std::fabs(a.y-b.y) < 0.01f && std::fabs(a.z-b.z) < 0.01f;
}

// Phase D live camera (Blender → ariane): drive ariane's camera from Blender's
// viewport, with the same "who's moving wins" arbitration. We detect ariane-side
// navigation by the camera changing since we last left it (ariane's own input runs
// earlier in Draw), and while that's happening we ignore Blender's stream.
void
PollBlenderCamIn(void)
{
	if(bridgeArianeFocused())		// ariane is the active window → it drives, don't follow
		return;

	static int tick = 0;
	if(++tick < 3)				// ~20 Hz read
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\live\\cam_blender.txt") ||
	   !bridgeFileIsFresh(path, 2))		// stale / Blender not live → ignore
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	char buf[256];
	size_t n = fread(buf, 1, sizeof(buf)-1, f);
	fclose(f);
	buf[n] = '\0';

	static std::string lastData;
	if(lastData == buf)
		return;
	lastData = buf;

	char *tok = strtok(buf, "\t \n");
	if(tok == nil || strcmp(tok, "cam") != 0) return;
	char *sx = strtok(nil,"\t \n"), *sy = strtok(nil,"\t \n"), *sz = strtok(nil,"\t \n");
	char *tx = strtok(nil,"\t \n"), *ty = strtok(nil,"\t \n"), *tz = strtok(nil,"\t \n");
	if(!sx || !sy || !sz || !tx || !ty || !tz) return;
	// up vector (optional for back-compat; Blender always sends it now)
	char *ux = strtok(nil,"\t \n"), *uy = strtok(nil,"\t \n"), *uz = strtok(nil,"\t \n");

	// SNAP straight to Blender's camera. Interpolating eye/target flips near the
	// vertical poles ("looks like the camera is swinging to the other side") and
	// can't keep up with a fast rotation — so match Blender exactly instead.
	TheCamera.m_position.x = (float)atof(sx);
	TheCamera.m_position.y = (float)atof(sy);
	TheCamera.m_position.z = (float)atof(sz);
	TheCamera.m_target.x = (float)atof(tx);
	TheCamera.m_target.y = (float)atof(ty);
	TheCamera.m_target.z = (float)atof(tz);

	// ariane's camera is roll-free by design: m_up must stay (0,0,±1) — turn()/orbit()
	// only ever fix m_up.z, so any x/y we put here would tilt the horizon forever.
	// Take just the SIGN of Blender's up (upright vs. flipped past the pole) and rebuild
	// a clean, roll-free up in the vertical plane of the view direction. This matches
	// Blender's own turntable orbit (also roll-free) and can't corrupt ariane's state.
	float sgn = 1.0f;
	if(ux && uy && uz && (float)atof(uz) < 0.0f)
		sgn = -1.0f;
	TheCamera.m_up.set(0.0f, 0.0f, sgn);
	rw::V3d dir = rw::sub(TheCamera.m_target, TheCamera.m_position);
	if(rw::length(dir) > 1e-4f){
		rw::V3d worldz = { 0.0f, 0.0f, sgn };
		rw::V3d right = rw::cross(rw::normalize(dir), worldz);
		if(rw::length(right) > 1e-4f)			// not looking straight up/down
			TheCamera.m_localup = rw::normalize(rw::cross(rw::normalize(right), rw::normalize(dir)));
		else
			TheCamera.m_localup = worldz;
	}
	TheCamera.update();
}

static std::string
selectionSignature(void)
{
	std::string s;
	char guid[300];
	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *in = (ObjectInst*)p->item;
		if(in == nil) continue;
		GetInstGuid(in, guid, sizeof(guid));
		s += guid; s += '|';
	}
	return s;
}

// Phase D live selection (Blender → ariane): mirror Blender's selection onto ariane
// instances, with the same arbitration — skip while dragging (gGizmoUsing) or while
// ariane's own selection just changed (the user clicked here).
void
PollBlenderSelIn(void)
{
	if(bridgeArianeFocused())	// ariane is the active window → it drives, don't follow
		return;
	if(gGizmoUsing)
		return;
	static int own = 0;
	static std::string last;
	static bool have = false;

	std::string cur = selectionSignature();
	if(have && cur != last)
		own = 20;			// ariane's selection changed by the user → ariane wins
	if(own > 0){
		own--;
		last = cur; have = true;
		return;
	}

	static int tick = 0;
	if(++tick < 3)
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\live\\sel_blender.txt"))
		return;
	if(!bridgeFileIsFresh(path, 2))		// stale / Blender not live → ignore
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz < 0 || sz > (1<<20)){ fclose(f); return; }
	std::string data;
	data.resize((size_t)sz);
	if(sz > 0) fread(&data[0], 1, (size_t)sz, f);
	fclose(f);

	static std::string lastData;
	if(lastData == data)
		return;
	lastData = data;

	std::vector<char> mut(data.begin(), data.end());
	mut.push_back('\0');
	char *tok = strtok(mut.data(), "\t \r\n");
	if(tok && strcmp(tok, "sel") == 0){
		ClearSelection();
		for(char *g = strtok(nil, "\t \r\n"); g; g = strtok(nil, "\t \r\n")){
			ObjectInst *in = FindInstByGuid(g);
			if(in) in->Select();
		}
	}
	last = selectionSignature(); have = true;
}

// Phase E-1 (Blender → ariane): create new instances of existing models. Blender
// writes create.job (one line per object: key + model name + transform); we create
// each via CreateBridgeInstance and write create.done mapping key → new guid so the
// addon can tag the object. Persists to the IPL only on the user's next save.
void
PollBlenderCreate(void)
{
	static int tick = 0;
	if(++tick < 10)			// creates are rare — poll gently
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\outbox\\create.job"))
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > (1<<20)){ fclose(f); return; }
	std::string data;
	data.resize((size_t)sz);
	fread(&data[0], 1, (size_t)sz, f);
	fclose(f);
	remove(path);			// consume the request

	std::string done;
	size_t pos = 0;
	while(pos < data.size()){
		size_t eol = data.find('\n', pos);
		std::string ln = data.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
		pos = (eol == std::string::npos) ? data.size() : eol + 1;
		if(ln.empty() || ln[0] == '#')
			continue;
		char lb[512];
		strncpy(lb, ln.c_str(), sizeof(lb)-1); lb[sizeof(lb)-1] = '\0';

		char key[128] = "", name[64] = "";
		float x=0, y=0, z=0, qx=0, qy=0, qz=0, qw=1;
		for(char *t = strtok(lb, "\t"); t; t = strtok(nil, "\t")){
			char *eq = strchr(t, '=');
			if(!eq) continue;
			*eq = '\0';
			const char *k = t, *v = eq+1;
			if(strcmp(k,"key")==0) strncpy(key, v, sizeof(key)-1);
			else if(strcmp(k,"name")==0) strncpy(name, v, sizeof(name)-1);
			else if(strcmp(k,"x")==0) x=(float)atof(v);
			else if(strcmp(k,"y")==0) y=(float)atof(v);
			else if(strcmp(k,"z")==0) z=(float)atof(v);
			else if(strcmp(k,"qx")==0) qx=(float)atof(v);
			else if(strcmp(k,"qy")==0) qy=(float)atof(v);
			else if(strcmp(k,"qz")==0) qz=(float)atof(v);
			else if(strcmp(k,"qw")==0) qw=(float)atof(v);
		}
		if(name[0] == '\0')
			continue;
		rw::V3d pos3 = { x, y, z };
		rw::Quat rot; rot.x = qx; rot.y = qy; rot.z = qz; rot.w = qw;
		char guid[300] = "";
		bool ok = CreateBridgeInstance(name, pos3, rot, guid, sizeof(guid));
		done += key; done += '\t';
		done += (ok && guid[0]) ? guid : "ERR";
		done += '\n';
		log("BlenderBridge: create %s → %s\n", name, (ok && guid[0]) ? guid : "FAILED");
	}

	if(!done.empty()){
		char donePath[1024];
		if(GetArianeDataPath(donePath, sizeof(donePath), "bridge\\outbox\\create.done")){
			FILE *df = fopen(donePath, "wb");
			if(df){ fwrite(done.data(), 1, done.size(), df); fclose(df); }
		}
	}
}

// Phase E-2 (Blender → ariane): register BRAND-NEW models. Blender exports the DFF/
// TXD(/COL) into the outbox and writes createmodel.job (key + name + drawdist + col
// flag + transform). We drive ariane's custom-import via CreateBridgeModel and reply
// in createmodel.done with key → guid+name (or ERR:msg). Persists on the user's save.
void
PollBlenderCreateModel(void)
{
	static int tick = 0;
	if(++tick < 10)
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\outbox\\createmodel.job"))
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz <= 0 || sz > (1<<20)){ fclose(f); return; }
	std::string data;
	data.resize((size_t)sz);
	fread(&data[0], 1, (size_t)sz, f);
	fclose(f);
	remove(path);

	char outbox[1024];
	if(!GetArianeDataPath(outbox, sizeof(outbox), "bridge\\outbox"))
		return;

	std::string done;
	size_t pos = 0;
	while(pos < data.size()){
		size_t eol = data.find('\n', pos);
		std::string ln = data.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
		pos = (eol == std::string::npos) ? data.size() : eol + 1;
		if(ln.empty() || ln[0] == '#')
			continue;
		char lb[512];
		strncpy(lb, ln.c_str(), sizeof(lb)-1); lb[sizeof(lb)-1] = '\0';

		char key[128] = "", name[64] = "", lodName[64] = "";
		float dd = 300.0f, x=0, y=0, z=0, qx=0, qy=0, qz=0, qw=1;
		int hasCol = 0, hasLod = 0;
		for(char *t = strtok(lb, "\t"); t; t = strtok(nil, "\t")){
			char *eq = strchr(t, '=');
			if(!eq) continue;
			*eq = '\0';
			const char *k = t, *v = eq+1;
			if(strcmp(k,"key")==0) strncpy(key, v, sizeof(key)-1);
			else if(strcmp(k,"name")==0) strncpy(name, v, sizeof(name)-1);
			else if(strcmp(k,"dd")==0) dd=(float)atof(v);
			else if(strcmp(k,"col")==0) hasCol=atoi(v);
			else if(strcmp(k,"lod")==0) hasLod=atoi(v);
			else if(strcmp(k,"lodname")==0) strncpy(lodName, v, sizeof(lodName)-1);
			else if(strcmp(k,"x")==0) x=(float)atof(v);
			else if(strcmp(k,"y")==0) y=(float)atof(v);
			else if(strcmp(k,"z")==0) z=(float)atof(v);
			else if(strcmp(k,"qx")==0) qx=(float)atof(v);
			else if(strcmp(k,"qy")==0) qy=(float)atof(v);
			else if(strcmp(k,"qz")==0) qz=(float)atof(v);
			else if(strcmp(k,"qw")==0) qw=(float)atof(v);
		}
		if(name[0] == '\0')
			continue;
		char dffP[1200], txdP[1200], colP[1200], lodDffP[1200], lodTxdP[1200];
		snprintf(dffP, sizeof(dffP), "%s\\%s.dff", outbox, name);
		snprintf(txdP, sizeof(txdP), "%s\\%s.txd", outbox, name);
		snprintf(colP, sizeof(colP), "%s\\%s.col", outbox, name);
		bool lodOk = hasLod && lodName[0];
		if(lodOk){
			snprintf(lodDffP, sizeof(lodDffP), "%s\\%s.dff", outbox, lodName);
			snprintf(lodTxdP, sizeof(lodTxdP), "%s\\%s.txd", outbox, lodName);
		}
		rw::V3d p3 = { x, y, z };
		rw::Quat r; r.x = qx; r.y = qy; r.z = qz; r.w = qw;
		char guid[300] = "", err[256] = "";
		bool ok = CreateBridgeModel(name, dffP, txdP, hasCol ? colP : nil,
			lodOk ? lodName : nil, lodOk ? lodDffP : nil, lodOk ? lodTxdP : nil,
			dd, p3, r, guid, sizeof(guid), err, sizeof(err));
		done += key; done += '\t';
		if(ok && guid[0]){ done += guid; done += '\t'; done += name; }
		else { done += "ERR:"; done += (err[0] ? err : "failed"); }
		done += '\n';
		log("BlenderBridge: create model %s → %s\n", name, (ok && guid[0]) ? guid : err);
	}

	if(!done.empty()){
		char donePath[1024];
		if(GetArianeDataPath(donePath, sizeof(donePath), "bridge\\outbox\\createmodel.done")){
			FILE *df = fopen(donePath, "wb");
			if(df){ fwrite(done.data(), 1, done.size(), df); fclose(df); }
		}
	}
}

// Phase F (Blender → ariane): reconcile SOFT deletions. Blender writes del_blender.txt
// = the set of guids currently deleted in Blender (an object vanished). We diff it
// against what the bridge has deleted so far: newly-listed guids → inst->Delete()
// (soft, restorable, commented out on save); guids that dropped off the list (Ctrl+Z
// in Blender) → inst->Undelete(). Freshness-gated so a closed Blender never touches
// anything, and Blender's own mass-vanish guard keeps a file reload from wiping.
static std::unordered_set<std::string> gBridgeDeleted;

void
PollBlenderDeletes(void)
{
	static int tick = 0;
	if(++tick < 6)
		return;
	tick = 0;

	char path[1024];
	if(!GetArianeDataPath(path, sizeof(path), "bridge\\live\\del_blender.txt"))
		return;
	if(!bridgeFileIsFresh(path, 3))		// Blender not live → leave state as-is
		return;
	FILE *f = fopen(path, "rb");
	if(f == nil)
		return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if(sz < 0 || sz > (1<<20)){ fclose(f); return; }
	std::string data;
	if(sz > 0){ data.resize((size_t)sz); fread(&data[0], 1, (size_t)sz, f); }
	fclose(f);

	static std::string lastData;
	if(lastData == data)
		return;
	lastData = data;

	std::unordered_set<std::string> target;		// guids Blender says are deleted
	std::vector<char> mut(data.begin(), data.end());
	mut.push_back('\0');
	for(char *t = strtok(mut.data(), "\t \r\n"); t; t = strtok(nil, "\t \r\n")){
		if(strcmp(t, "del") == 0) continue;
		target.insert(t);
	}

	// newly deleted → soft-delete in ariane
	for(std::unordered_set<std::string>::iterator it = target.begin(); it != target.end(); ++it){
		if(gBridgeDeleted.count(*it)) continue;
		ObjectInst *in = FindInstByGuid(it->c_str());
		if(in){ in->Delete(); gBridgeDeleted.insert(*it); }
	}
	// dropped off the list (undo) → restore
	for(std::unordered_set<std::string>::iterator it = gBridgeDeleted.begin(); it != gBridgeDeleted.end(); ){
		if(target.count(*it)){ ++it; continue; }
		ObjectInst *in = FindInstByGuid(it->c_str());
		if(in) in->Undelete();
		it = gBridgeDeleted.erase(it);
	}
}

// Copy outbox\<name>.<ext> into ariane's persistent overrides store and delete
// the outbox transient, so the hot outbox folder never accumulates. `dst` gets
// the path to register the override against (the persistent copy, or the outbox
// file itself as a fallback if the copy failed).
static bool
StashBridgeOverride(const char *outbox, const char *ovDir, const char *name, const char *ext, char *dst, size_t dstsz)
{
	char src[1024];
	snprintf(src, sizeof(src), "%s\\%s.%s", outbox, name, ext);
	snprintf(dst, dstsz, "%s\\%s.%s", ovDir, name, ext);
	int sz = 0;
	uint8 *buf = ReadLooseFile(src, &sz);
	if(buf == nil || sz <= 0){
		if(buf) free(buf);
		strncpy(dst, src, dstsz-1); dst[dstsz-1] = '\0';	// fallback: keep the outbox path
		return false;
	}
	bool ok = WriteBufferToPath(dst, buf, sz);
	free(buf);
	if(ok)
		remove(src);					// outbox transient no longer needed
	else { strncpy(dst, src, dstsz-1); dst[dstsz-1] = '\0'; }
	return ok;
}

// Reverse bridge state — a whole reload.job is read once (dynamically, no size
// limit) into gReversePending, then drained a few models per frame so a big
// batch doesn't truncate or stall a single frame.
static std::vector<std::string> gReversePending;
static size_t gReverseIdx = 0;
static int gReverseOk = 0, gReverseErr = 0, gReverseMoved = 0;
static std::string gReverseFailed;
static char gReverseOutbox[1024];
static char gReverseOvDir[1024];

// process one reload.job line (a model to hot-reload, or a #header)
static void
ProcessReverseLine(const char *rawline)
{
	if(rawline[0] == '#'){		// header line (#v N, #flags ...)
		if(strncmp(rawline, "#v ", 3) == 0){
			int v = atoi(rawline+3);
			if(v != BRIDGE_PROTO_VER)
				log("BlenderBridge: reload.job protocol v%d (ariane expects %d) — update one side\n",
					v, BRIDGE_PROTO_VER);
		}
		return;
	}

	char line[1024];
	strncpy(line, rawline, sizeof(line)-1); line[sizeof(line)-1] = '\0';

	char name[64] = "";
	char guid[300] = "";
	int iid = -1, hasTxd = 0, hasX = 0, posOnly = 0, wantDff = 1, hasCol = 0;
	float x=0, y=0, z=0, qx=0, qy=0, qz=0, qw=1;

	char *tab = strchr(line, '\t');
	int nl = tab ? (int)(tab-line) : (int)strlen(line);
	if(nl > 63) nl = 63;
	strncpy(name, line, nl); name[nl] = '\0';

	for(char *p = tab; p; ){
		p++;
		char *nxt = strchr(p, '\t');
		char kv[320];
		int kl = nxt ? (int)(nxt-p) : (int)strlen(p);
		if(kl > 319) kl = 319;
		strncpy(kv, p, kl); kv[kl] = '\0';
		char *eq = strchr(kv, '=');
		if(eq){
			*eq = '\0';
			const char *k = kv, *v = eq+1;
			if(strcmp(k,"iid")==0) iid = atoi(v);
			else if(strcmp(k,"guid")==0){ strncpy(guid, v, sizeof(guid)-1); guid[sizeof(guid)-1]='\0'; }
			else if(strcmp(k,"pos")==0) posOnly = atoi(v);
			else if(strcmp(k,"dff")==0) wantDff = atoi(v);
			else if(strcmp(k,"col")==0) hasCol = atoi(v);
			else if(strcmp(k,"txd")==0) hasTxd = atoi(v);
			else if(strcmp(k,"x")==0){ x=(float)atof(v); hasX=1; }
			else if(strcmp(k,"y")==0) y=(float)atof(v);
			else if(strcmp(k,"z")==0) z=(float)atof(v);
			else if(strcmp(k,"qx")==0) qx=(float)atof(v);
			else if(strcmp(k,"qy")==0) qy=(float)atof(v);
			else if(strcmp(k,"qz")==0) qz=(float)atof(v);
			else if(strcmp(k,"qw")==0) qw=(float)atof(v);
		}
		p = nxt;
	}
	if(name[0] == '\0')
		return;

	int id = -1;
	if(!GetObjectDef(name, &id)){
		log("BlenderBridge: unknown model %s\n", name);
		gReverseErr++;
		if(gReverseFailed.size() < 400){
			if(!gReverseFailed.empty()) gReverseFailed += ",";
			gReverseFailed += name;
		}
		return;
	}
	ObjectDef *obj = GetObjectDef(id);
	if(obj == nil)
		return;

	// Flag-driven: Blender says per line which parts it sent (dff/txd/col), so we
	// only override + reload those. "Обновить позицию" sends dff=0/txd=0/col=0 (or
	// pos=1) → nothing here runs, just the move below.
	if(posOnly){ wantDff = 0; hasTxd = 0; hasCol = 0; }

	if(wantDff){
		char ov[1024];
		StashBridgeOverride(gReverseOutbox, gReverseOvDir, name, "dff", ov, sizeof(ov));
		RegisterRuntimeOverride(name, "dff", ov);
		obj->Reload();		// free old geometry + reload from the override
	}

	if(hasTxd){
		TxdDef *td = GetTxdDef(obj->m_txdSlot);
		if(td && td->name[0]){
			char tov[1024];
			StashBridgeOverride(gReverseOutbox, gReverseOvDir, name, "txd", tov, sizeof(tov));
			RegisterRuntimeOverride(td->name, "txd", tov);
			ForceTxdReload(obj->m_txdSlot);
		}
	}

	if(hasCol){
		char cov[1024];
		StashBridgeOverride(gReverseOutbox, gReverseOvDir, name, "col", cov, sizeof(cov));
		RegisterRuntimeOverride(name, "col", cov);
		ForceColReloadFromFile(cov);
	}

	bool moved = false;
	if(hasX && (guid[0] || iid >= 0)){
		// Prefer the stable GUID (survives ariane restart); fall back to the
		// session pick-id for older jobs that didn't carry one.
		ObjectInst *in = guid[0] ? FindInstByGuid(guid) : nil;
		if(in == nil && iid >= 0) in = FindInstByPickId(iid);
		if(in){
			// Mirror the editor's own move (applyUndoTransform): without the sector
			// refresh + rwObject-frame update the pivot moves but the mesh stays put
			// until the user nudges it. Do the full sequence so it snaps live.
			RemoveInstFromSectors(in);
			in->m_translation.x = x; in->m_translation.y = y; in->m_translation.z = z;
			in->m_rotation.x = qx; in->m_rotation.y = qy; in->m_rotation.z = qz; in->m_rotation.w = qw;
			StampChangeSeq(in);
			in->m_isDirty = true;
			in->UpdateMatrix();
			updateRwFrameForInst(in);
			InsertInstIntoSectors(in);
			gReverseMoved++;
			moved = true;
		}
	}
	gReverseOk++;
	log("BlenderBridge: %s (id %d)%s%s%s%s\n", name, id,
		wantDff ? " +dff" : "", hasTxd ? " +txd" : "", hasCol ? " +col" : "",
		moved ? " +pos" : "");
}

// Reverse bridge: Blender's "-> Ariane" writes edited DFF(+TXD) to the outbox and
// a reload.job. We register the edits as runtime overrides and hot-reload the
// models — a slice per frame so a large batch never truncates or hitches. When
// the batch finishes we ack back with reload.done.
void
PollBlenderOutbox(void)
{
	if(gReversePending.empty()){			// idle: probe for a new job
		static int tick = 0;
		if(++tick < 20)
			return;
		tick = 0;

		if(!GetArianeDataPath(gReverseOutbox, sizeof(gReverseOutbox), "bridge\\outbox"))
			return;
		char jobPath[1024];
		snprintf(jobPath, sizeof(jobPath), "%s\\reload.job", gReverseOutbox);
		FILE *f = fopen(jobPath, "rb");
		if(f == nil)
			return;
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		std::string content;
		if(sz > 0){
			content.resize((size_t)sz);
			size_t rd = fread(&content[0], 1, (size_t)sz, f);
			content.resize(rd);
		}
		fclose(f);
		remove(jobPath);
		if(content.empty())
			return;

		GetArianeDataPath(gReverseOvDir, sizeof(gReverseOvDir), "bridge\\overrides");
		gReversePending.clear(); gReverseIdx = 0;
		gReverseOk = 0; gReverseErr = 0; gReverseMoved = 0; gReverseFailed.clear();
		for(size_t start = 0; start < content.size(); ){
			size_t e = content.find_first_of("\r\n", start);
			size_t len = (e == std::string::npos ? content.size() : e) - start;
			if(len > 0)
				gReversePending.push_back(content.substr(start, len));
			if(e == std::string::npos)
				break;
			start = e + 1;
		}
		if(gReversePending.empty())
			return;
	}

	// drain a bounded slice this frame
	int budget = 8;
	while(gReverseIdx < gReversePending.size() && budget-- > 0)
		ProcessReverseLine(gReversePending[gReverseIdx++].c_str());

	if(gReverseIdx >= gReversePending.size()){	// batch done → ack + reset
		char donePath[1024];
		snprintf(donePath, sizeof(donePath), "%s\\reload.done", gReverseOutbox);
		char ack[700];
		int al = snprintf(ack, sizeof(ack), "v=%d ok=%d err=%d\n", BRIDGE_PROTO_VER, gReverseOk, gReverseErr);
		if(!gReverseFailed.empty() && al < (int)sizeof(ack)-1)
			al += snprintf(ack+al, sizeof(ack)-al, "failed=%s\n", gReverseFailed.c_str());
		WriteBufferToPath(donePath, (const uint8*)ack, (int)strlen(ack));
			// Reverse position is VISUAL ONLY: instances move and are marked
			// dirty (m_isDirty), but the IPL is NOT auto-saved — the user saves
			// in ariane when they choose to persist.
		gReversePending.clear();
		gReverseIdx = 0;
	}
}

int
ExportSelectedTxds(const char *dir, int *numFailed)
{
	int exported = 0;
	int failed = 0;
	char path[1024];

	for(CPtrNode *p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		ObjectDef *obj;
		TxdDef *txd;

		if(inst == nil || inst->m_isDeleted)
			continue;
		obj = GetObjectDef(inst->m_objectId);
		if(obj == nil){
			failed++;
			continue;
		}
		txd = GetTxdDef(obj->m_txdSlot);
		if(txd == nil || txd->name[0] == '\0'){
			failed++;
			continue;
		}
		if(HasEarlierSelectedTxd(p, txd->name))
			continue;
		if(!BuildAssetExportPath(dir, txd->name, "txd", path, sizeof(path)) ||
		   !ExportEffectiveAsset(txd->name, "txd", txd->imageIndex, path)){
			log("ExportSelectedTxds: failed to export %s\n", txd->name);
			failed++;
			continue;
		}
		exported++;
	}

	if(numFailed)
		*numFailed = failed;
	return exported;
}

int
ImportPrefabAt(const char *path, rw::V3d spawnPos)
{
	PrefabEntry entries[256];
	int numEntries = 0;
	if(!ReadPrefabEntries(path, entries, 256, &numEntries, true))
		return 0;

	GameFile *file = GetOrCreateCurrentPlacementIplFile();
	int maxIdx = GetMaxIplIndexForFile(file);

	// Pass 1: create all instances
	ObjectInst *created[256];
	ObjectInst *pasted[256];
	for(int i = 0; i < 256; i++)
		created[i] = nil;
	int numPasted = 0;

	for(int i = 0; i < numEntries; i++){
		PrefabEntry *e = &entries[i];

		ObjectDef *obj = GetObjectDef(e->objectId);
		if(obj == nil){
			log("ImportPrefab: skipping unknown objectId %d (%s)\n", e->objectId, e->modelName);
			created[i] = nil;
			continue;
		}

		ObjectInst *inst = AddInstance();
		inst->m_objectId = e->objectId;
		inst->m_area = e->area & 0xFF;
		if(isSA()){
			if(e->area & 0x100) inst->m_isUnimportant = true;
			if(e->area & 0x400) inst->m_isUnderWater = true;
			if(e->area & 0x800) inst->m_isTunnel = true;
			if(e->area & 0x1000) inst->m_isTunnelTransition = true;
		}
		inst->m_rotation.x = e->rotX;
		inst->m_rotation.y = e->rotY;
		inst->m_rotation.z = e->rotZ;
		inst->m_rotation.w = e->rotW;
		inst->m_translation.x = spawnPos.x + e->relX;
		inst->m_translation.y = spawnPos.y + e->relY;
		inst->m_translation.z = spawnPos.z + e->relZ;
		inst->m_lodId = -1;
		inst->m_lod = nil;
		inst->m_numChildren = 0;
		inst->m_file = file;
		inst->m_imageIndex = -1;
		inst->m_binInstIndex = -1;
		inst->m_iplIndex = ++maxIdx;
		SetInstIplFilterKey(inst, file ? file->name : nil);
		inst->m_isAdded = true;
		inst->m_isDirty = true;
		inst->m_savedStateValid = false;
		inst->m_wasSavedDeleted = false;
		inst->m_gameEntityExists = false;
		StampChangeSeq(inst);

		if(obj->m_isBigBuilding)
			inst->SetupBigBuilding();

		inst->UpdateMatrix();

		if(!obj->IsLoaded()){
			RequestObject(e->objectId);
			LoadAllRequestedObjects();
		}

		inst->CreateRwObject();

		if(obj->m_colModel)
			InsertInstIntoSectors(inst);
		else{
			CPtrList *list = inst->m_isBigBuilding
				? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
			list->InsertItem(inst);
		}

		created[i] = inst;
		if(numPasted < 256)
			pasted[numPasted++] = inst;
	}

	// Pass 2: link LODs
	for(int i = 0; i < numEntries; i++){
		if(created[i] == nil)
			continue;
		int lr = entries[i].lodRef;
		if(lr >= 0 && lr < numEntries && created[lr] != nil)
			finalizeLinkedLod(created[i], created[lr]);
	}

	// Select all spawned instances
	ClearSelection();
	for(int i = 0; i < numEntries; i++){
		if(created[i] != nil)
			created[i]->Select();
	}

	if(numPasted > 0){
		if(numPasted > 64)
			Toast(TOAST_SPAWN, "Undo limited to first 64 of %d instances", numPasted);
		UndoRecordPaste(pasted, numPasted > 64 ? 64 : numPasted);
	}

	log("ImportPrefab: placed %d instance(s) from %s\n", numPasted, path);
	return numPasted;
}

int
ImportPrefab(const char *path)
{
	rw::V3d spawnPos = add(TheCamera.m_position, scale(TheCamera.m_at, 50.0f));
	return ImportPrefabAt(path, spawnPos);
}

void
RenderPrefabPlacementGhost(const char *path, rw::V3d spawnPos)
{
	PrefabEntry entries[256];
	int numEntries = 0;
	if(path == nil || path[0] == '\0')
		return;
	if(!ReadPrefabEntries(path, entries, 256, &numEntries, false))
		return;

	rw::RGBA col = { 80, 180, 255, 95 };
	for(int i = 0; i < numEntries; i++){
		PrefabEntry *e = &entries[i];
		rw::V3d pos;
		pos.x = spawnPos.x + e->relX;
		pos.y = spawnPos.y + e->relY;
		pos.z = spawnPos.z + e->relZ;
		rw::Quat rot = { e->rotX, e->rotY, e->rotZ, e->rotW };
		RenderPlacementGhost(e->objectId, pos, rot, col);
	}
}

int numSectorsX, numSectorsY;
CRect worldBounds;
Sector *sectors;
Sector outOfBoundsSector;

void
InitSectors(void)
{
	switch(params.map){
	case GAME_III:
		numSectorsX = 100;
		numSectorsY = 100;
		worldBounds.left = -2000.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 2000.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_VC:
		numSectorsX = 80;
		numSectorsY = 80;
		worldBounds.left = -2400.0f;
		worldBounds.bottom = -2000.0f;
		worldBounds.right = 1600.0f;
		worldBounds.top = 2000.0f;
		break;
	case GAME_SA:
		numSectorsX = 120;
		numSectorsY = 120;
		worldBounds.left = -10000.0f;
		worldBounds.bottom = -10000.0f;
		worldBounds.right = 10000.0f;
		worldBounds.top = 10000.0f;
		break;
	}
	sectors = new Sector[numSectorsX*numSectorsY];
}

Sector*
GetSector(int ix, int iy)
{
	return &sectors[ix*numSectorsY + iy];
}

//Sector*
//GetSector(float x, float y)
//{
//	int i, j;
//	assert(x > worldBounds.left && x < worldBounds.right);
//	assert(y > worldBounds.bottom && y < worldBounds.top);
//	i = (x + worldBounds.right - worldBounds.left)/numSectorsX;
//	j = (y + worldBounds.top - worldBounds.bottom)/numSectorsY;
//	return GetSector(i, j);
//}

int
GetSectorIndexX(float x)
{
	if(x < worldBounds.left) x = worldBounds.left;
	if(x >= worldBounds.right) x = worldBounds.right - 1.0f;
	return (x - worldBounds.left) / ((worldBounds.right - worldBounds.left) / numSectorsX);
}

int
GetSectorIndexY(float y)
{
	if(y < worldBounds.bottom) y = worldBounds.bottom;
	if(y >= worldBounds.top) y = worldBounds.top - 1.0f;
	return (y - worldBounds.bottom) / ((worldBounds.top - worldBounds.bottom) / numSectorsY);
}

bool
IsInstInBounds(ObjectInst *inst)
{
	ObjectDef *obj;
	// Some objects don't have collision data
	// TODO: figure out what to do with them
	obj = GetObjectDef(inst->m_objectId);
	if(obj == nil || obj->m_colModel == nil)
		return false;
	CRect bounds = inst->GetBoundRect();
	return bounds.left >= worldBounds.left &&
		bounds.right < worldBounds.right &&
		bounds.bottom >= worldBounds.bottom &&
		bounds.top < worldBounds.top;
}

static void
removeFromList(CPtrList *list, ObjectInst *inst)
{
	CPtrNode *p, *next;
	for(p = list->first; p; p = next){
		next = p->next;
		if(p->item == inst)
			list->DeleteNode(p);
	}
}

void
RemoveInstFromSectors(ObjectInst *inst)
{
	int x, y;

	// Remove from out of bounds sector
	removeFromList(&outOfBoundsSector.buildings, inst);
	removeFromList(&outOfBoundsSector.buildings_overlap, inst);
	removeFromList(&outOfBoundsSector.bigbuildings, inst);
	removeFromList(&outOfBoundsSector.bigbuildings_overlap, inst);

	// Remove from all sectors
	for(y = 0; y < numSectorsY; y++)
		for(x = 0; x < numSectorsX; x++){
			Sector *s = GetSector(x, y);
			removeFromList(&s->buildings, inst);
			removeFromList(&s->buildings_overlap, inst);
			removeFromList(&s->bigbuildings, inst);
			removeFromList(&s->bigbuildings_overlap, inst);
		}
}

void
InsertInstIntoSectors(ObjectInst *inst)
{
	Sector *s;
	CPtrList *list;
	int x, xstart, xmid, xend;
	int y, ystart, ymid, yend;

	if(!IsInstInBounds(inst)){
		list = inst->m_isBigBuilding ? &outOfBoundsSector.bigbuildings : &outOfBoundsSector.buildings;
		list->InsertItem(inst);
		return;
	}

	CRect bounds = inst->GetBoundRect();

	xstart = GetSectorIndexX(bounds.left);
	xend   = GetSectorIndexX(bounds.right);
	xmid   = GetSectorIndexX((bounds.left + bounds.right)/2.0f);
	ystart = GetSectorIndexY(bounds.bottom);
	yend   = GetSectorIndexY(bounds.top);
	ymid   = GetSectorIndexY((bounds.bottom + bounds.top)/2.0f);

	for(y = ystart; y <= yend; y++)
		for(x = xstart; x <= xend; x++){
			s = GetSector(x, y);
			if(x == xmid && y == ymid)
				list = inst->m_isBigBuilding ? &s->bigbuildings : &s->buildings;
			else
				list = inst->m_isBigBuilding ? &s->bigbuildings_overlap : &s->buildings_overlap;
			list->InsertItem(inst);
		}
}
