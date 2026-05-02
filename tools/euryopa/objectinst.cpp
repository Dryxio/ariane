#include "euryopa.h"
#include "lod_associations.h"
#include "modloader.h"
#include "object_categories.h"
#include <cmath>
#include <string>
#include <vector>

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
		if(other->m_lod == lodInst)
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
	if(m_isDeleted) return;
	m_isDeleted = true;
	StampChangeSeq(this);
	Deselect();

	// If this HD building was the last live child of its LOD, delete the LOD too.
	if(m_lod && !m_lod->m_isDeleted &&
	   countLiveLodChildren(m_lod, this) == 0)
		m_lod->Delete();

	// If this IS a LOD, delete all HD buildings that reference it
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && !other->m_isDeleted)
			other->Delete();
	}
}

void
ObjectInst::Undelete(void)
{
	if(!m_isDeleted) return;
	m_isDeleted = false;
	StampChangeSeq(this);
	if(m_imageIndex >= 0 && m_wasSavedDeleted)
		m_isDirty = true;

	// Also undelete the LOD pair
	if(m_lod && m_lod->m_isDeleted)
		m_lod->Undelete();

	// If this IS a LOD, undelete HD children
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_lod == this && other->m_isDeleted)
			other->Undelete();
	}
}

void
DeleteSelected(void)
{
	// Collect all instances that will be deleted (including LOD cascades)
	// First, gather the selected ones
	ObjectInst *toDelete[MAX_BATCH_OBJECTS];
	int numToDelete = 0;
	CPtrNode *p, *next;
	for(p = selection.first; p; p = next){
		next = p->next;
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && numToDelete < MAX_BATCH_OBJECTS)
			toDelete[numToDelete++] = inst;
	}
	if(numToDelete == 0) return;

	// Now delete them and collect ALL that got deleted (including LOD cascade)
	ObjectInst *allDeleted[MAX_BATCH_OBJECTS];
	int numAllDeleted = 0;

	// Snapshot which are already deleted
	// Then delete, and find newly deleted ones
	for(int i = 0; i < numToDelete; i++)
		toDelete[i]->Delete();

	// Scan for all instances that are now deleted to record in undo
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_isDeleted && numAllDeleted < MAX_BATCH_OBJECTS){
			// Check if this was freshly deleted (it's in our set or cascaded)
			// Simple approach: just record all deleted. On undo we undelete them.
			// This is slightly broad but safe.
		}
	}

	// Simpler: just record what we explicitly asked to delete,
	// the cascade is handled by Undelete() automatically
	UndoRecordDelete(toDelete, numToDelete);
}

int
DeleteAllInstances(void)
{
	int numDeleted = 0;
	ClearSelection();

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst == nil || inst->m_isDeleted)
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
static int spawnObjectId = -1;
static GameFile *customIplFile = nil;
static const char *DEFAULT_CUSTOM_IPL_PATH = "data\\maps\\custom.ipl";
static const char *CUSTOM_IMPORT_IDE_PATH = "data/maps/ariane/custom.ide";
static const char *CUSTOM_IMPORT_IPL_PATH = "data/maps/ariane/custom.ipl";
static char currentCustomIplPath[256] = "data\\maps\\custom.ipl";
static char currentCustomIplSourcePath[1024];
static bool currentCustomIplAppendToDat = true;

// NEW: Option to save new objects to their original IPL instead of custom.ipl
bool gSaveToOriginalIpl = false;

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

static bool
pathsEqualCiNormalized(const char *a, const char *b)
{
	if(a == nil || b == nil)
		return false;
	while(*a || *b){
		char ca = *a++;
		char cb = *b++;
		if(ca == '\\') ca = '/';
		if(cb == '\\') cb = '/';
		if(ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
		if(cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
		if(ca != cb)
			return false;
	}
	return true;
}

void
SetCustomPlacementIpl(const char *logicalPath, const char *sourcePath, bool addToDat)
{
	char normalizedLogical[256];
	char resolvedSource[1024];

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

	bool sameLogical = strcmp(currentCustomIplPath, normalizedLogical) == 0;
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
	ObjectDef *obj = GetObjectDef(id);
	if(obj && obj->m_file && pathsEqualCiNormalized(obj->m_file->name, CUSTOM_IMPORT_IDE_PATH))
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

static const char*
GetDatFilename(void)
{
	switch(gameversion){
	case GAME_III: return "data/gta3.dat";
	case GAME_VC:  return "data/gta_vc.dat";
	case GAME_SA:  return "data/gta.dat";
	case GAME_LCS: return "data/gta_lcs.dat";
	case GAME_VCS: return "data/gta_vcs.dat";
	default:       return nil;
	}
}

static bool
IplEntryExistsInDat(const char *datfile, const char *iplpath)
{
	FILE *f = fopen_ci(datfile, "r");
	if(f == nil) return false;
	char line[512];
	while(fgets(line, sizeof(line), f)){
		// strip newline
		char *p = line;
		while(*p && *p != '\r' && *p != '\n') p++;
		*p = '\0';
		if(strncmp(line, "IPL ", 4) == 0){
			if(strcmp(line+4, iplpath) == 0){
				fclose(f);
				return true;
			}
		}
	}
	fclose(f);
	return false;
}

static void
AppendIplToDat(const char *iplpath)
{
	const char *datfile = GetDatFilename();
	if(datfile == nil) return;
	if(IplEntryExistsInDat(datfile, iplpath)) return;

	const char *targetPath = ModloaderGetSourcePath(datfile);
	if(targetPath == nil)
		targetPath = getPath(datfile);
	FILE *f = fopen(targetPath, "a");
	if(f == nil){
		log("WARNING: could not append to %s\n", datfile);
		return;
	}
	fprintf(f, "\nIPL %s\n", iplpath);
	fclose(f);
	log("Added IPL %s to %s\n", iplpath, datfile);
}

static GameFile*
GetOrCreateCustomIplFile(void)
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
	if(currentCustomIplAppendToDat)
		AppendIplToDat(currentCustomIplPath);
	return customIplFile;
}

// NEW: Get the original IPL file for an object definition
static GameFile*
GetOriginalIplForObject(int objectId, int *outImageIndex)
{
	ObjectDef *obj = GetObjectDef(objectId);
	if(obj == nil || obj->m_file == nil)
		return nil;
	
	// Find an existing instance with the same object ID to get its IPL file
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(inst->m_objectId == objectId && inst->m_file){
			if(outImageIndex) *outImageIndex = inst->m_imageIndex;
			return inst->m_file;
		}
	}
	
	// If no existing instance, try to guess the IPL from the IDE file name
	char iplName[1024];
	strncpy(iplName, obj->m_file->name, sizeof(iplName)-1);
	iplName[sizeof(iplName)-1] = '\0';
	char *dot = strrchr(iplName, '.');
	if(dot && rw::strncmp_ci(dot, ".ide", 4) == 0){
		strcpy(dot, ".ipl");
		GameFile *file = nil;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst && inst->m_file && rw::strncmp_ci(inst->m_file->name, iplName, 1024) == 0){
				file = inst->m_file;
				break;
			}
		}
		if(file){
			if(outImageIndex) *outImageIndex = -1;
			return file;
		}
	}
	
	// No existing instances and no matching IDE-based IPL found, return nil (will fall back to custom IPL)
	return nil;
}

// NEW: Get the appropriate IPL file for spawning based on settings
static GameFile*
GetSpawnTargetIplFile(int objectId, int *outImageIndex)
{
	if(outImageIndex) *outImageIndex = -1;
	if(gSaveToOriginalIpl){
		GameFile *originalFile = GetOriginalIplForObject(objectId, outImageIndex);
		if(originalFile){
			log("Spawning object %d to original IPL: %s\n", objectId, originalFile->name);
			return originalFile;
		}
		// Fall back to custom IPL if no original found
		log("No original IPL found for object %d, using custom IPL\n", objectId);
	}
	return GetOrCreateCustomIplFile();
}

static int
GetMaxIplIndexForFile(GameFile *file)
{
	int maxIplIndex = -1;
	CPtrNode *p;
	for(p = instances.first; p; p = p->next){
		ObjectInst *other = (ObjectInst*)p->item;
		if(other->m_file == file && other->m_imageIndex < 0){
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

	// Use new logic to determine target IPL file
	int imageIndex = -1;
	GameFile *file = GetSpawnTargetIplFile(spawnObjectId, &imageIndex);
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
		lodInst->m_imageIndex = imageIndex;
		outInsts[n++] = lodInst;
	}

	if(n >= outCapacity) return n;
	ObjectInst *hdInst = createSpawnedInstance(spawnObjectId, position, file, ++maxIdx, orientation);
	hdInst->m_imageIndex = imageIndex;
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
	// quaternion distance: if |dot| < 0.9999 the rotation changed
	float dot = fabsf(inst->m_rotation.x * inst->m_savedRotation.x +
	                   inst->m_rotation.y * inst->m_savedRotation.y +
	                   inst->m_rotation.z * inst->m_savedRotation.z +
	                   inst->m_rotation.w * inst->m_savedRotation.w);
	if(dot < 0.9999f)
		flags |= DIFF_ROTATED;
	return flags;
}

// 3D Preview — uses Scene camera with swapped framebuffers (librw pattern)
rw::Texture *gPreviewTexture;
static rw::Raster *previewColorRaster;
static rw::Raster *previewDepthRaster;
static bool previewInited;
static bool previewInitFailed;

#define PREVIEW_SIZE 256

void
InitPreviewRenderer(void)
{
	if(previewInited || previewInitFailed) return;

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
	if(gPreviewTexture){
		gPreviewTexture->raster = nil;
		gPreviewTexture->destroy();
		gPreviewTexture = nil;
	}
	if(previewColorRaster){ previewColorRaster->destroy(); previewColorRaster = nil; }
	if(previewDepthRaster){ previewDepthRaster->destroy(); previewDepthRaster = nil; }
	previewInited = false;
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
		if(previewClump){ previewClump->getFrame()->destroy(); previewClump->destroy(); previewClump = nil; }
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

	// Use scene camera with swapped framebuffers (librw pattern)
	rw::Camera *cam = Scene.camera;

	// Save ALL camera state
	rw::Raster *savedFB = cam->frameBuffer;
	rw::Raster *savedZB = cam->zBuffer;
	float savedNear = cam->nearPlane;
	float savedFar = cam->farPlane;
	float savedFog = cam->fogPlane;
	rw::Frame *camFrame = cam->getFrame();
	rw::Matrix savedMatrix = camFrame->matrix;

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

	// Restore ALL camera state
	cam->frameBuffer = savedFB;
	cam->zBuffer = savedZB;
	cam->setNearPlane(savedNear);
	cam->setFarPlane(savedFar);
	cam->fogPlane = savedFog;
	cam->setFOV(TheCamera.m_fov, TheCamera.m_aspectRatio);
	camFrame->matrix = savedMatrix;
	camFrame->updateObjects();

	Timecycle::SetLights();
}

// Clipboard
#define MAX_CLIPBOARD MAX_BATCH_OBJECTS
static ObjectInst *clipboard[MAX_CLIPBOARD];
static int clipboardCount;

// Undo/Redo system
#define MAX_UNDO 128
static UndoAction undoStack[MAX_UNDO];
static int undoCount;	// number of actions in stack
static int undoPos;	// current position (next undo)

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
	undoCount = undoPos;
	if(undoCount >= MAX_UNDO){
		// Shift everything down
		memmove(&undoStack[0], &undoStack[1], (MAX_UNDO-1)*sizeof(UndoAction));
		undoCount--;
		undoPos--;
	}
	undoStack[undoCount] = *a;
	undoCount++;
	undoPos = undoCount;
}

void
UndoRecordMove(ObjectInst *inst, rw::V3d oldPos, ObjectInst *lodInst, rw::V3d lodOldPos)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
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
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_ROTATE;
	a.inst = inst;
	a.oldRot = oldRot;
	a.newRot = inst->m_rotation;
	pushUndo(&a);
}

void
UndoRecordDelete(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_DELETE;
	a.numDeleted = num > MAX_BATCH_OBJECTS ? MAX_BATCH_OBJECTS : num;
	for(int i = 0; i < a.numDeleted; i++)
		a.deletedInsts[i] = insts[i];
	pushUndo(&a);
}

void
UndoRecordPaste(ObjectInst **insts, int num)
{
	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_PASTE;
	a.numPasted = num > MAX_BATCH_OBJECTS ? MAX_BATCH_OBJECTS : num;
	for(int i = 0; i < a.numPasted; i++)
		a.pastedInsts[i] = insts[i];
	pushUndo(&a);
}

void
UndoRecordTransformBatch(UndoTransform *transforms, int num)
{
	if(num <= 0)
		return;

	UndoAction a;
	memset(&a, 0, sizeof(a));
	a.type = UNDO_TRANSFORM_BATCH;
	a.numTransforms = num > MAX_BATCH_OBJECTS ? MAX_BATCH_OBJECTS : num;
	for(int i = 0; i < a.numTransforms; i++)
		a.transforms[i] = transforms[i];
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
			a->deletedInsts[i]->Undelete();
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
			a->deletedInsts[i]->Delete();
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
	clipboardCount = 0;
	CPtrNode *p;
	for(p = selection.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!inst->m_isDeleted && clipboardCount < MAX_CLIPBOARD)
			clipboard[clipboardCount++] = inst;
	}
	if(clipboardCount > 0)
		log("Copied %d instance(s)\n", clipboardCount);
}

static ObjectInst*
cloneInstance(ObjectInst *src, GameFile *dstFile, int iplIndex, rw::V3d offset)
{
	ObjectInst *inst = AddInstance();
	inst->m_objectId = src->m_objectId;
	inst->m_area = src->m_area;
	inst->m_rotation = src->m_rotation;
	inst->m_translation.x = src->m_translation.x + offset.x;
	inst->m_translation.y = src->m_translation.y + offset.y;
	inst->m_translation.z = src->m_translation.z + offset.z;
	inst->m_isUnimportant = src->m_isUnimportant;
	inst->m_isUnderWater = src->m_isUnderWater;
	inst->m_isTunnel = src->m_isTunnel;
	inst->m_isTunnelTransition = src->m_isTunnelTransition;
	inst->m_isBigBuilding = src->m_isBigBuilding;
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
	inst->UpdateMatrix();
	inst->CreateRwObject();
	InsertInstIntoSectors(inst);
	return inst;
}

static GameFile*
findPasteDestinationFile(ObjectInst *src)
{
	if(src == nil)
		return nil;

	if(src->m_imageIndex < 0)
		return src->m_file;

	// Prefer the linked text LOD file when it exists.
	if(src->m_lod && src->m_lod->m_imageIndex < 0 && src->m_lod->m_file)
		return src->m_lod->m_file;

	// Streaming instances loaded alongside a text IPL share the same
	// visibility/filter key. Reuse that scene so pasted copies land in a
	// writable text IPL instead of an IMG entry name like foo_stream0.
	if(src->m_iplFilterKey[0] != '\0'){
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *other = (ObjectInst*)p->item;
			if(other == nil || other->m_imageIndex >= 0 || other->m_file == nil)
				continue;
			if(strcmp(other->m_iplFilterKey, src->m_iplFilterKey) == 0)
				return other->m_file;
		}
	}

	// Some streamed models have no loaded text anchor at all.
	// Put pasted copies into the custom placement IPL instead of writing a
	// bogus text file with the binary scene name.
	return GetOrCreateCustomIplFile();
}

void
PasteClipboard(void)
{
	if(clipboardCount == 0) return;

	rw::V3d offset = { 10.0f, 0.0f, 0.0f };

	ObjectInst *pasted[MAX_CLIPBOARD * 2];
	int numPasted = 0;

	// Deduplicate: skip LODs that are already the m_lod of another
	// clipboard entry (they'll be auto-copied with their HD parent)
	ObjectInst *toPaste[MAX_CLIPBOARD];
	int numToPaste = 0;

	for(int i = 0; i < clipboardCount; i++){
		bool isLodOfAnother = false;
		for(int j = 0; j < clipboardCount; j++){
			if(i != j && clipboard[j]->m_lod == clipboard[i]){
				isLodOfAnother = true;
				break;
			}
		}
		if(!isLodOfAnother)
			toPaste[numToPaste++] = clipboard[i];
	}

	ClearSelection();

	for(int i = 0; i < numToPaste; i++){
		ObjectInst *src = toPaste[i];
		ObjectInst *srcLod = src->m_lod;

		GameFile *dstFile = findPasteDestinationFile(src);

		// Find the max m_iplIndex for this file (text IPL instances only)
		int maxIplIndex = -1;
		CPtrNode *p;
		for(p = instances.first; p; p = p->next){
			ObjectInst *other = (ObjectInst*)p->item;
			if(other->m_file == dstFile && other->m_imageIndex < 0){
				if(other->m_iplIndex > maxIplIndex)
					maxIplIndex = other->m_iplIndex;
			}
		}

		// Paste LOD first (if any) so we have its iplIndex for the HD's lodId
		ObjectInst *newLod = nil;
		if(isSA() && srcLod && !srcLod->m_isDeleted){
			newLod = cloneInstance(srcLod, dstFile, ++maxIplIndex, offset);
			pasted[numPasted++] = newLod;
		}

		// Paste HD
		ObjectInst *newHd = cloneInstance(src, dstFile, ++maxIplIndex, offset);

		// Link LOD
		if(newLod)
			finalizeLinkedLod(newHd, newLod);

		newHd->Select();
		pasted[numPasted++] = newHd;
	}

	if(numPasted > 0){
		UndoRecordPaste(pasted, numPasted);
		log("Pasted %d instance(s)\n", numPasted);
	}
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

struct PrefabEntry {
	int objectId;
	char modelName[MODELNAMELEN];
	float relX, relY, relZ;
	float rotX, rotY, rotZ, rotW;
	int area;
	int lodRef;
};

int
ImportPrefab(const char *path)
{
	FILE *f = fopen(path, "r");
	if(f == nil){
		log("ImportPrefab: failed to open %s\n", path);
		return 0;
	}

	char line[512];
	int version = 0, game = -1, count = 0;

	// Parse header
	while(fgets(line, sizeof(line), f)){
		if(strncmp(line, "ARIANE_PREFAB", 13) == 0){
			sscanf(line, "ARIANE_PREFAB %d", &version);
		}else if(strncmp(line, "game", 4) == 0){
			sscanf(line, "game %d", &game);
		}else if(strncmp(line, "count", 5) == 0){
			sscanf(line, "count %d", &count);
			break;
		}
	}

	if(version < 1 || count <= 0){
		fclose(f);
		log("ImportPrefab: invalid prefab header in %s\n", path);
		return 0;
	}

	if(game >= 0 && game != (int)params.map){
		fclose(f);
		Toast(TOAST_SPAWN, "Prefab is for a different game");
		return 0;
	}

	if(count > 256){
		Toast(TOAST_SPAWN, "Prefab too large (%d instances, max 256)", count);
		count = 256;
	}

	// Parse entries
	PrefabEntry *entries = new PrefabEntry[count];
	int numEntries = 0;

	while(numEntries < count && fgets(line, sizeof(line), f)){
		PrefabEntry *e = &entries[numEntries];
		int n = sscanf(line, "%d , %29s %f , %f , %f , %f , %f , %f , %f , %d , %d",
			&e->objectId, e->modelName, &e->relX, &e->relY, &e->relZ,
			&e->rotX, &e->rotY, &e->rotZ, &e->rotW,
			&e->area, &e->lodRef);
		if(n >= 9){
			// Strip trailing comma from modelName if present
			char *comma = strchr(e->modelName, ',');
			if(comma) *comma = '\0';
			if(n < 11) e->lodRef = -1;
			numEntries++;
		}
	}
	fclose(f);

	if(numEntries == 0){
		delete[] entries;
		return 0;
	}

	// Spawn position: in front of camera
	rw::V3d spawnPos = add(TheCamera.m_position, scale(TheCamera.m_at, 50.0f));

	GameFile *file = GetOrCreateCustomIplFile();
	int maxIdx = GetMaxIplIndexForFile(file);

	// Pass 1: create all instances
	ObjectInst *created[256];
	ObjectInst *pasted[256];
	int numCreated = 0;
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

	delete[] entries;
	log("ImportPrefab: placed %d instance(s) from %s\n", numPasted, path);
	return numPasted;
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
