#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "euryopa.h"
#include "agentbridge.h"

#include <algorithm>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

static const int AGENT_PROTOCOL_VERSION = 1;
// Framed streams support normal bulk receipts while retaining a hard bound.
static const size_t AGENT_MAX_RESPONSE_BYTES = 4 * 1024 * 1024;

static const char*
agentBuildId(void)
{
	// Hash the executable itself rather than one translation unit's timestamp.
	// Incremental builds can relink changes from objectinst.cpp without compiling
	// this file, so __DATE__/__TIME__ alone can falsely identify two binaries as
	// the same build — exactly the stale-deployment failure this field prevents.
	static char buildId[96];
	if(buildId[0])
		return buildId;
	const char *path = sk::args.argc > 0 ? sk::args.argv[0] : nil;
	FILE *file = path ? fopen(path, "rb") : nil;
	if(file == nil){
		snprintf(buildId, sizeof(buildId), "agentbridge-%s-%s", __DATE__, __TIME__);
		return buildId;
	}
	unsigned long long hash = 1469598103934665603ULL;
	unsigned char bytes[16384];
	size_t count;
	while((count = fread(bytes, 1, sizeof(bytes), file)) > 0)
		for(size_t i = 0; i < count; i++){
			hash ^= bytes[i];
			hash *= 1099511628211ULL;
		}
	fclose(file);
	snprintf(buildId, sizeof(buildId), "ariane-fnv1a64-%016llx", hash);
	return buildId;
}

static bool gAgentBridgeInitialized;
static bool gAgentBridgeEnabled;
static bool gAgentCapturePending;
static char gAgentBridgeDirectory[1024];
static char gAgentSocketPath[1024];
static char gAgentSceneLogicalPath[256];
static char gAgentScenePhysicalPath[1024];
static char gAgentCapturePath[1024];
static std::string gAgentPendingRequestId;
static uint32 gAgentSceneRevision;
static uint32 gAgentCameraRevision;

struct AgentCameraPose
{
	rw::V3d position;
	rw::V3d target;
	rw::V3d up;
	float fov;
};
static AgentCameraPose gAgentTrackedCamera;
static bool gAgentTrackedCameraValid;
static AgentCameraPose gAgentCapturePose;
static AgentCameraPose gAgentCaptureRestorePose;
static bool gAgentCaptureRestore;
static uint32 gAgentCaptureCameraRevision;
static std::string gAgentCaptureLabel;

#ifdef _WIN32
typedef SOCKET AgentSocket;
static const AgentSocket INVALID_AGENT_SOCKET = INVALID_SOCKET;
#else
typedef int AgentSocket;
static const AgentSocket INVALID_AGENT_SOCKET = -1;
#endif
static AgentSocket gAgentSocket = INVALID_AGENT_SOCKET;
static AgentSocket gAgentReplySocket = INVALID_AGENT_SOCKET;
static std::string gAgentToken;
static bool gAgentTcp;
static bool gAgentUnixBound;

static void closeAgentSocket(AgentSocket &socket)
{
	if(socket == INVALID_AGENT_SOCKET) return;
#ifdef _WIN32
	closesocket(socket);
#else
	close(socket);
#endif
	socket = INVALID_AGENT_SOCKET;
}

static bool setAgentBlocking(AgentSocket socket, bool blocking)
{
#ifdef _WIN32
	u_long mode = blocking ? 0 : 1;
	return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
	int flags = fcntl(socket, F_GETFL, 0);
	return flags >= 0 && fcntl(socket, F_SETFL,
		blocking ? flags & ~O_NONBLOCK : flags | O_NONBLOCK) == 0;
#endif
}

static bool receiveAgentBytes(char *data, size_t size)
{
	while(size){
		int count = (int)recv(gAgentReplySocket, data, (int)size, 0);
		if(count <= 0) return false;
		data += count;
		size -= count;
	}
	return true;
}

struct AgentSceneSnapshot
{
	int32 id;
	rw::V3d position;
	rw::Quat rotation;
	bool deleted;
};
static bool gAgentSessionActive;
static std::string gAgentSessionId;
static std::vector<AgentSceneSnapshot> gAgentSessionSnapshot;
// Native map instances temporarily suppressed inside a leased scratch session.
// They are tracked separately so rollback never treats unrelated world objects
// as agent-created scene content.
static std::vector<AgentSceneSnapshot> gAgentScopedWorldSnapshot;

static std::string
jsonEscape(const char *text)
{
	std::string escaped;
	if(text == nil)
		return escaped;
	for(const unsigned char *p = (const unsigned char*)text; *p; p++){
		switch(*p){
		case '\\': escaped += "\\\\"; break;
		case '"': escaped += "\\\""; break;
		case '\n': escaped += "\\n"; break;
		case '\r': escaped += "\\r"; break;
		case '\t': escaped += "\\t"; break;
		default:
			if(*p >= 0x20)
				escaped += (char)*p;
			break;
		}
	}
	return escaped;
}

static AgentCameraPose
currentCameraPose(void)
{
	AgentCameraPose pose = { TheCamera.m_position, TheCamera.m_target,
		TheCamera.m_up, TheCamera.m_fov };
	return pose;
}

static bool
cameraPoseDiffers(const AgentCameraPose &a, const AgentCameraPose &b)
{
	return rw::length(rw::sub(a.position, b.position)) > 0.0001f ||
		rw::length(rw::sub(a.target, b.target)) > 0.0001f ||
		rw::length(rw::sub(a.up, b.up)) > 0.0001f || fabsf(a.fov - b.fov) > 0.0001f;
}

static void
applyCameraPose(const AgentCameraPose &pose)
{
	TheCamera.m_position = pose.position;
	TheCamera.m_target = pose.target;
	TheCamera.m_at = rw::normalize(rw::sub(pose.target, pose.position));
	TheCamera.m_up = rw::normalize(pose.up);
	TheCamera.m_localup = TheCamera.m_up;
	TheCamera.m_fov = std::max(1.0f, std::min(pose.fov, 150.0f));
	gAgentTrackedCamera = currentCameraPose();
	gAgentTrackedCameraValid = true;
}

static void
trackLiveCamera(void)
{
	AgentCameraPose pose = currentCameraPose();
	if(!gAgentTrackedCameraValid){
		gAgentTrackedCamera = pose;
		gAgentTrackedCameraValid = true;
		return;
	}
	if(cameraPoseDiffers(pose, gAgentTrackedCamera)){
		gAgentCameraRevision++;
		gAgentTrackedCamera = pose;
	}
}

static std::string
cameraPoseJson(const AgentCameraPose &pose)
{
	rw::V3d forward = rw::normalize(rw::sub(pose.target, pose.position));
	rw::V3d right = rw::normalize(rw::cross(forward, pose.up));
	rw::V3d viewUp = rw::normalize(rw::cross(right, forward));
	char body[960];
	snprintf(body, sizeof(body),
		"{\"position\":[%.5f,%.5f,%.5f],\"target\":[%.5f,%.5f,%.5f],"
		"\"forward\":[%.6f,%.6f,%.6f],\"right\":[%.6f,%.6f,%.6f],"
		"\"up\":[%.6f,%.6f,%.6f],\"up_reference\":[%.6f,%.6f,%.6f],\"fov\":%.3f}",
		pose.position.x, pose.position.y, pose.position.z,
		pose.target.x, pose.target.y, pose.target.z,
		forward.x, forward.y, forward.z, right.x, right.y, right.z,
		viewUp.x, viewUp.y, viewUp.z, pose.up.x, pose.up.y, pose.up.z, pose.fov);
	return body;
}

static ObjectInst*
getAgentInstUnderSegment(const rw::V3d &start, const rw::V3d &end,
	rw::V3d *hitPosition, float *hitDistance, float targetTolerance)
{
	rw::V3d delta = rw::sub(end, start);
	float segmentLength = rw::length(delta);
	if(segmentLength < 0.001f)
		return nil;
	Ray ray = { start, rw::normalize(delta) };
	float bestDistance = std::max(0.0f, segmentLength - targetTolerance);
	ObjectInst *best = nil;
	rw::V3d bestHit = { 0.0f, 0.0f, 0.0f };
	for(CPtrNode *node = instances.first; node; node = node->next){
		ObjectInst *inst = (ObjectInst*)node->item;
		if(inst->m_isDeleted || inst->m_numChildren > 0)
			continue;
		if(!gNoAreaCull && inst->m_area != currentArea && inst->m_area != 13)
			continue;
		rw::V3d hit;
		if(!IntersectRayColModel(ray, inst, &hit))
			continue;
		float distance = rw::dot(rw::sub(hit, start), ray.dir);
		if(distance < 0.01f || distance >= bestDistance)
			continue;
		bestDistance = distance;
		bestHit = hit;
		best = inst;
	}
	if(best){
		if(hitPosition) *hitPosition = bestHit;
		if(hitDistance) *hitDistance = bestDistance;
	}
	return best;
}

static void
writeResponse(const std::string &requestId, bool ok, const std::string &body)
{
	std::string response = std::string("{\"protocol_version\":") +
		std::to_string(AGENT_PROTOCOL_VERSION) + ",\"scene_revision\":" +
		std::to_string(gAgentSceneRevision) + ",\"request_id\":\"" +
		jsonEscape(requestId.c_str()) + "\",\"ok\":" + (ok ? "true" : "false") +
		"," + body + "}\n";
	// The framed stream transport removes the tiny AF_UNIX datagram ceiling, but
	// a hard application limit still prevents accidental unbounded replies.
	if(response.size() > AGENT_MAX_RESPONSE_BYTES){
		response = std::string("{\"protocol_version\":") +
			std::to_string(AGENT_PROTOCOL_VERSION) + ",\"scene_revision\":" +
			std::to_string(gAgentSceneRevision) + ",\"request_id\":\"" +
			jsonEscape(requestId.c_str()) +
			"\",\"ok\":false,\"error\":\"response exceeds the engine limit; use a paginated command\"}\n";
	}
	if(gAgentReplySocket != INVALID_AGENT_SOCKET){
		uint32 framedSize = htonl((uint32)response.size());
		const char *parts[2] = { (const char*)&framedSize, response.data() };
		size_t sizes[2] = { sizeof(framedSize), response.size() };
		for(int part = 0; part < 2; part++){
			size_t sent = 0;
			while(sent < sizes[part]){
				int count = (int)send(gAgentReplySocket, parts[part] + sent,
					(int)(sizes[part] - sent),
#ifdef MSG_NOSIGNAL
					MSG_NOSIGNAL
#else
					0
#endif
				);
				if(count <= 0)
					break;
				sent += (size_t)count;
			}
		}
		closeAgentSocket(gAgentReplySocket);
		return;
	}
	char finalPath[1200], temporaryPath[1200];
	snprintf(finalPath, sizeof(finalPath), "%s/response.json", gAgentBridgeDirectory);
	snprintf(temporaryPath, sizeof(temporaryPath), "%s/response.tmp", gAgentBridgeDirectory);
	FILE *file = fopen(temporaryPath, "wb");
	if(file == nil)
		return;
	fwrite(response.data(), 1, response.size(), file);
	fclose(file);
	remove(finalPath);
	rename(temporaryPath, finalPath);
}

static void
shutdownAgentSocket(void)
{
	closeAgentSocket(gAgentSocket);
	closeAgentSocket(gAgentReplySocket);
#ifndef _WIN32
	if(gAgentUnixBound) unlink(gAgentSocketPath);
#endif
}

static bool initializeAgentTcp(const char *portText)
{
	const char *token = getenv("ARIANE_ENGINE_TOKEN");
	char *end = nil;
	long port = strtol(portText, &end, 10);
	if(!portText[0] || *end || port < 1 || port > 65535 || !token ||
	   strlen(token) < 32 || strlen(token) > 256 || strpbrk(token, "\r\n")){
		log("Agent TCP requires a valid port and ARIANE_ENGINE_TOKEN (32-256 characters, no newlines)\n");
		return false;
	}
#ifdef _WIN32
	WSADATA data;
	if(WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
#endif
	gAgentSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(gAgentSocket == INVALID_AGENT_SOCKET) return false;
#ifdef _WIN32
	BOOL exclusive = TRUE;
	if(setsockopt(gAgentSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
	   (const char*)&exclusive, sizeof(exclusive)) != 0){
		shutdownAgentSocket();
		return false;
	}
#endif
	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons((unsigned short)port);
	if(!setAgentBlocking(gAgentSocket, false) ||
	   bind(gAgentSocket, (sockaddr*)&address, sizeof(address)) != 0 ||
	   listen(gAgentSocket, 16) != 0){
		shutdownAgentSocket();
		return false;
	}
	gAgentToken = token;
	gAgentTcp = true;
	atexit(shutdownAgentSocket);
	return true;
}

#ifndef _WIN32
static bool
initializeAgentSocket(void)
{
	if(strlen(gAgentSocketPath) >= sizeof(sockaddr_un::sun_path)){
		log("Agent socket path is too long: %s\n", gAgentSocketPath);
		return false;
	}
	gAgentSocket = socket(AF_UNIX, SOCK_STREAM, 0);
	if(gAgentSocket < 0)
		return false;
	int flags = fcntl(gAgentSocket, F_GETFL, 0);
	if(flags < 0 || fcntl(gAgentSocket, F_SETFL, flags | O_NONBLOCK) < 0){
		shutdownAgentSocket();
		return false;
	}
	sockaddr_un address = {};
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, gAgentSocketPath, sizeof(address.sun_path) - 1);
	unlink(gAgentSocketPath);
	if(bind(gAgentSocket, (sockaddr*)&address, sizeof(address)) < 0){
		shutdownAgentSocket();
		return false;
	}
	if(listen(gAgentSocket, 16) < 0){
		shutdownAgentSocket();
		return false;
	}
	gAgentUnixBound = true;
	chmod(gAgentSocketPath, S_IRUSR | S_IWUSR);
	atexit(shutdownAgentSocket);
	return true;
}
#endif

static void
writeError(const std::string &requestId, const char *message)
{
	writeResponse(requestId, false,
		std::string("\"error\":\"") + jsonEscape(message) + "\"");
}

static void
initializeBridge(void)
{
	gAgentBridgeInitialized = true;
	for(int i = 1; i + 1 < sk::args.argc; i++){
		if(strcmp(sk::args.argv[i], "--agent-bridge") == 0){
			strncpy(gAgentBridgeDirectory, sk::args.argv[i + 1], sizeof(gAgentBridgeDirectory) - 1);
			gAgentBridgeDirectory[sizeof(gAgentBridgeDirectory) - 1] = '\0';
		}else if(strcmp(sk::args.argv[i], "--agent-socket") == 0){
			strncpy(gAgentSocketPath, sk::args.argv[i + 1], sizeof(gAgentSocketPath) - 1);
			gAgentSocketPath[sizeof(gAgentSocketPath) - 1] = '\0';
		}
	}
	const char *tcpPort = getenv("ARIANE_ENGINE_TCP_PORT");
	if(tcpPort){
		gAgentBridgeEnabled = initializeAgentTcp(tcpPort);
		log(gAgentBridgeEnabled ? "Agent authenticated loopback TCP enabled\n" :
			"Agent TCP initialization failed\n");
		return; // Explicit TCP configuration must never fall back to an unauthenticated bridge.
	}
#ifndef _WIN32
	if(gAgentSocketPath[0]){
		gAgentBridgeEnabled = initializeAgentSocket();
		if(gAgentBridgeEnabled)
			log("Agent IPC v%d listening on %s\n", AGENT_PROTOCOL_VERSION, gAgentSocketPath);
		else
			log("Agent IPC could not bind %s: %s\n", gAgentSocketPath, strerror(errno));
	}
#endif
	if(!gAgentBridgeEnabled && gAgentBridgeDirectory[0]){
		gAgentBridgeEnabled = true;
		log("Legacy agent file bridge enabled at %s\n", gAgentBridgeDirectory);
	}
}

static void
splitRequestContent(const std::string &content, std::vector<std::string> &lines)
{
	size_t start = 0;
	while(start <= content.size()){
		size_t end = content.find('\n', start);
		if(end == std::string::npos)
			end = content.size();
		std::string line = content.substr(start, end - start);
		if(!line.empty() && line[line.size() - 1] == '\r')
			line.resize(line.size() - 1);
		lines.push_back(line);
		if(end == content.size())
			break;
		start = end + 1;
	}
}

static bool
readRequest(std::vector<std::string> &lines)
{
	if(gAgentSocket != INVALID_AGENT_SOCKET){
		gAgentReplySocket = accept(gAgentSocket, nil, nil);
		if(gAgentReplySocket == INVALID_AGENT_SOCKET) return false;
		if(!setAgentBlocking(gAgentReplySocket, true)){
			closeAgentSocket(gAgentReplySocket);
			return false;
		}
#ifdef SO_NOSIGPIPE
		int noSignal = 1;
		setsockopt(gAgentReplySocket, SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal));
#endif
#ifdef _WIN32
		DWORD timeout = 250, sendTimeout = 2000;
#else
		struct timeval timeout = { 0, 250000 }, sendTimeout = { 2, 0 };
#endif
		setsockopt(gAgentReplySocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
		setsockopt(gAgentReplySocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&sendTimeout, sizeof(sendTimeout));
		uint32 framedSize = 0;
		if(!receiveAgentBytes((char*)&framedSize, sizeof(framedSize))){
			closeAgentSocket(gAgentReplySocket);
			return false;
		}
		framedSize = ntohl(framedSize);
		if(framedSize == 0 || framedSize > 1024 * 1024){
			closeAgentSocket(gAgentReplySocket);
			return false;
		}
		std::string content(framedSize, '\0');
		if(!receiveAgentBytes(&content[0], framedSize)){
			closeAgentSocket(gAgentReplySocket);
			return false;
		}
		if(gAgentTcp){
			std::string prefix = "ARIANE_AUTH/1 " + gAgentToken + "\n";
			unsigned int difference = content.size() < prefix.size();
			for(size_t i = 0; i < prefix.size(); i++)
				difference |= (unsigned char)prefix[i] ^
					(i < content.size() ? (unsigned char)content[i] : 0);
			if(difference){
				writeError("unknown", "authentication failed");
				return false;
			}
			content.erase(0, prefix.size());
		}
		splitRequestContent(content, lines);
		if(lines.empty() || lines[0] != "ARIANE_IPC/1"){
			std::string requestId = lines.size() > 1 ? lines[1] : "unknown";
			writeError(requestId, "unsupported or missing protocol version");
			lines.clear();
			return false;
		}
		lines.erase(lines.begin());
		return true;
	}
	char requestPath[1200];
	snprintf(requestPath, sizeof(requestPath), "%s/request.txt", gAgentBridgeDirectory);
	FILE *file = fopen(requestPath, "rb");
	if(file == nil)
		return false;

	std::string content;
	char buffer[4096];
	while(!feof(file)){
		size_t size = fread(buffer, 1, sizeof(buffer), file);
		content.append(buffer, size);
		if(content.size() > 4 * 1024 * 1024){
			fclose(file);
			remove(requestPath);
			return false;
		}
	}
	fclose(file);
	remove(requestPath);

	splitRequestContent(content, lines);
	return true;
}

static bool
parseInt(const std::string &text, int *value)
{
	char *end = nil;
	long parsed = strtol(text.c_str(), &end, 10);
	if(end == text.c_str() || *end != '\0')
		return false;
	*value = (int)parsed;
	return true;
}

static bool
parseFloat(const std::string &text, float *value)
{
	char *end = nil;
	float parsed = strtof(text.c_str(), &end);
	if(end == text.c_str() || *end != '\0')
		return false;
	*value = parsed;
	return true;
}

static ObjectDef*
resolveObject(const std::string &text, int *objectId)
{
	int parsedId;
	if(parseInt(text, &parsedId)){
		ObjectDef *object = GetObjectDef(parsedId);
		if(object){
			*objectId = parsedId;
			return object;
		}
	}
	return GetObjectDef(text.c_str(), objectId);
}

static std::string
lowercase(const char *text)
{
	std::string lowered = text ? text : "";
	for(size_t i = 0; i < lowered.size(); i++)
		lowered[i] = (char)tolower((unsigned char)lowered[i]);
	return lowered;
}

static bool
isAgentSceneInstance(const ObjectInst *inst)
{
	return inst && inst->m_file && inst->m_file->name &&
		gAgentSceneLogicalPath[0] && LogicalPathEquals(inst->m_file->name, gAgentSceneLogicalPath);
}

static bool
getAgentGroundPlacementSurface(rw::V3d position, rw::V3d *ground, rw::V3d *normal = nil)
{
	struct SelectionState
	{
		ObjectInst *inst;
		int selected;
	};
	std::vector<SelectionState> states;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted)
			continue;
		states.push_back({ inst, inst->m_selected });
		// Ground placement should use GTA's terrain and existing world as its
		// support, never a roof or prop produced by an earlier agent command.
		inst->m_selected = true;
	}
	bool found = GetGroundPlacementSurface(position, ground, normal, true);
	for(size_t i = 0; i < states.size(); i++)
		states[i].inst->m_selected = states[i].selected;
	return found;
}

struct AgentSupportSample
{
	rw::V3d support;
	rw::V3d ground;
	rw::V3d normal;
	float clearance;
};

struct AgentPlacementAnalysis
{
	std::vector<AgentSupportSample> samples;
	int missingSamples;
	float minClearance;
	float maxClearance;
	float minRequiredZ;
	float maxRequiredZ;
	float maxSlopeDegrees;
	rw::V3d averageNormal;
};

struct AgentSupportContract
{
	const char *profile;
	int sampleGridSize;
	float maxFloatingClearance;
	float maxPenetration;
	float maxSupportRelief;
};

struct AgentSupportEvaluation
{
	AgentSupportContract contract;
	bool analyzed;
	bool floating;
	bool embedded;
	bool unsupportedRelief;
	bool missingSupport;
	bool valid;
};

static AgentSupportContract
agentSupportContract(int objectId)
{
	ObjectDef *object = GetObjectDef(objectId);
	float footprint = 0.0f;
	if(object && object->m_colModel){
		CBox box = object->m_colModel->boundingBox;
		footprint = std::max(box.max.x - box.min.x, box.max.y - box.min.y);
	}
	if(footprint >= 6.0f)
		return { "building", 3, 0.15f, 0.55f, 0.60f };
	return { "prop", 3, 0.15f, 0.25f, 0.40f };
}

static AgentSupportEvaluation
evaluateAgentSupport(int objectId, bool analyzed, const AgentPlacementAnalysis &analysis)
{
	AgentSupportContract contract = agentSupportContract(objectId);
	float relief = analyzed ? analysis.maxRequiredZ - analysis.minRequiredZ : 0.0f;
	AgentSupportEvaluation result = {
		contract,
		analyzed,
		analyzed && analysis.maxClearance > contract.maxFloatingClearance,
		analyzed && analysis.minClearance < -contract.maxPenetration,
		analyzed && relief > contract.maxSupportRelief,
		!analyzed || analysis.missingSamples > 0,
		false,
	};
	result.valid = result.analyzed && !result.floating && !result.embedded &&
		!result.unsupportedRelief && !result.missingSupport;
	return result;
}

static std::string
agentSupportEvaluationJson(const AgentSupportEvaluation &evaluation,
	const AgentPlacementAnalysis *analysis)
{
	float minClearance = analysis ? analysis->minClearance : 0.0f;
	float maxClearance = analysis ? analysis->maxClearance : 0.0f;
	float relief = analysis ? analysis->maxRequiredZ - analysis->minRequiredZ : 0.0f;
	char body[768];
	snprintf(body, sizeof(body),
		"{\"contract\":\"terrain-support-v1\",\"profile\":\"%s\",\"sample_grid\":%d,"
		"\"max_floating_clearance\":%.3f,\"max_penetration\":%.3f,"
		"\"max_support_relief\":%.3f,\"analyzed\":%s,\"valid\":%s,"
		"\"floating\":%s,\"embedded\":%s,\"unsupported_relief\":%s,"
		"\"missing_support\":%s,\"min_clearance\":%.4f,\"max_clearance\":%.4f,"
		"\"support_relief\":%.4f}",
		evaluation.contract.profile, evaluation.contract.sampleGridSize,
		evaluation.contract.maxFloatingClearance, evaluation.contract.maxPenetration,
		evaluation.contract.maxSupportRelief, evaluation.analyzed ? "true" : "false",
		evaluation.valid ? "true" : "false", evaluation.floating ? "true" : "false",
		evaluation.embedded ? "true" : "false",
		evaluation.unsupportedRelief ? "true" : "false",
		evaluation.missingSupport ? "true" : "false", minClearance, maxClearance, relief);
	return body;
}

static bool
analyzeAgentPlacement(int objectId, const rw::V3d &position, const rw::Quat &rotation,
	int gridSize, AgentPlacementAnalysis *analysis)
{
	ObjectDef *object = GetObjectDef(objectId);
	if(object == nil || object->m_colModel == nil)
		return false;
	// Corners, edge midpoints, and center capture the rigid footprint while
	// avoiding pathological detailed COL queries on dense intermediate points.
	gridSize = std::max(2, std::min(gridSize, 3));
	analysis->samples.clear();
	analysis->missingSamples = 0;
	analysis->minClearance = 1.0e30f;
	analysis->maxClearance = -1.0e30f;
	analysis->minRequiredZ = 1.0e30f;
	analysis->maxRequiredZ = -1.0e30f;
	analysis->maxSlopeDegrees = 0.0f;
	analysis->averageNormal = { 0.0f, 0.0f, 0.0f };

	CBox box = object->m_colModel->boundingBox;
	rw::Matrix rotationMatrix;
	rotationMatrix.rotate(conj(rotation), rw::COMBINEREPLACE);
	rotationMatrix.pos = { 0.0f, 0.0f, 0.0f };
	const float radiansToDegrees = 57.2957795131f;
	for(int y = 0; y < gridSize; y++){
		float fy = (float)y / (float)(gridSize - 1);
		for(int x = 0; x < gridSize; x++){
			float fx = (float)x / (float)(gridSize - 1);
			rw::V3d local = {
				box.min.x + (box.max.x - box.min.x) * fx,
				box.min.y + (box.max.y - box.min.y) * fy,
				box.min.z
			};
			rw::V3d rotated;
			rw::V3d::transformPoints(&rotated, &local, 1, &rotationMatrix);
			rw::V3d support = add(position, rotated);
			rw::V3d ground, normal;
			if(!getAgentGroundPlacementSurface(support, &ground, &normal)){
				analysis->missingSamples++;
				continue;
			}
			float clearance = support.z - ground.z;
			float requiredZ = ground.z - rotated.z;
			analysis->samples.push_back({ support, ground, normal, clearance });
			analysis->minClearance = std::min(analysis->minClearance, clearance);
			analysis->maxClearance = std::max(analysis->maxClearance, clearance);
			analysis->minRequiredZ = std::min(analysis->minRequiredZ, requiredZ);
			analysis->maxRequiredZ = std::max(analysis->maxRequiredZ, requiredZ);
			analysis->averageNormal = add(analysis->averageNormal, normal);
			float normalZ = std::max(-1.0f, std::min(1.0f, normal.z));
			analysis->maxSlopeDegrees = std::max(analysis->maxSlopeDegrees,
				acosf(normalZ) * radiansToDegrees);
		}
	}
	if(analysis->samples.empty())
		return false;
	analysis->averageNormal = normalize(analysis->averageNormal);
	return true;
}

static std::string
agentPlacementAnalysisJson(const AgentPlacementAnalysis &analysis)
{
	char summary[768];
	snprintf(summary, sizeof(summary),
		"{\"sample_count\":%d,\"missing_samples\":%d,\"min_clearance\":%.4f,"
		"\"max_clearance\":%.4f,\"support_relief\":%.4f,\"recommended_no_float_z\":%.4f,"
		"\"max_slope_degrees\":%.3f,\"average_normal\":[%.5f,%.5f,%.5f],\"samples\":[",
		(int)analysis.samples.size(), analysis.missingSamples, analysis.minClearance,
		analysis.maxClearance, analysis.maxRequiredZ - analysis.minRequiredZ,
		analysis.minRequiredZ, analysis.maxSlopeDegrees, analysis.averageNormal.x,
		analysis.averageNormal.y, analysis.averageNormal.z);
	std::string json = summary;
	for(size_t i = 0; i < analysis.samples.size(); i++){
		if(i) json += ',';
		const AgentSupportSample &sample = analysis.samples[i];
		char item[384];
		snprintf(item, sizeof(item),
			"{\"xy\":[%.3f,%.3f],\"support_z\":%.4f,\"ground_z\":%.4f,"
			"\"clearance\":%.4f,\"normal\":[%.5f,%.5f,%.5f]}",
			sample.support.x, sample.support.y, sample.support.z, sample.ground.z,
			sample.clearance, sample.normal.x, sample.normal.y, sample.normal.z);
		json += item;
	}
	json += "]}";
	return json;
}

static bool
placeObject(const std::vector<std::string> &fields, size_t offset,
	std::vector<ObjectInst*> &created, ObjectInst **placedHd, std::string *error)
{
	if(fields.size() < offset + 6){
		*error = "place requires model, x, y, z, heading and snap";
		return false;
	}
	int objectId;
	ObjectDef *object = resolveObject(fields[offset], &objectId);
	if(object == nil){
		*error = "unknown model: " + fields[offset];
		return false;
	}
	float x, y, z, heading;
	int snap;
	if(!parseFloat(fields[offset + 1], &x) || !parseFloat(fields[offset + 2], &y) ||
	   !parseFloat(fields[offset + 3], &z) || !parseFloat(fields[offset + 4], &heading) ||
	   !parseInt(fields[offset + 5], &snap)){
		*error = "invalid numeric placement field";
		return false;
	}

	rw::V3d position = { x, y, z };
	if(snap){
		rw::V3d ground;
		if(!getAgentGroundPlacementSurface(position, &ground)){
			*error = "no ground surface at placement";
			return false;
		}
		position.z = ground.z + GetPlacementBaseOffset(objectId);
	}
	rw::Quat rotation = MakeObjectRotationDegrees({ 0.0f, 0.0f, heading });
	SetSpawnObjectId(objectId);
	ObjectInst *spawned[4] = {};
	int count = SpawnPlaceObjectNoUndo(position, &rotation, spawned, 4);
	if(count == 0){
		*error = "Ariane could not create the object";
		return false;
	}
	for(int i = 0; i < count; i++)
		created.push_back(spawned[i]);
	*placedHd = spawned[count - 1];
	return true;
}

static bool
requireAgentSession(const std::string &requestId)
{
	if(gAgentSessionActive)
		return true;
	writeError(requestId, "begin a scratch session before mutating the scene");
	return false;
}

static void
beginAgentSession(const std::string &sessionId)
{
	gAgentSessionSnapshot.clear();
	gAgentScopedWorldSnapshot.clear();
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!isAgentSceneInstance(inst))
			continue;
		gAgentSessionSnapshot.push_back({ inst->m_id, inst->m_translation,
			inst->m_rotation, inst->m_isDeleted });
	}
	gAgentSessionId = sessionId;
	gAgentSessionActive = true;
}

static void
clearAgentSession(void)
{
	gAgentSessionSnapshot.clear();
	gAgentScopedWorldSnapshot.clear();
	gAgentSessionId.clear();
	gAgentSessionActive = false;
}

static void
restoreAgentTransform(ObjectInst *inst, const AgentSceneSnapshot &snapshot)
{
	std::vector<UndoTransform> transforms;
	if(CaptureObjectTransformTargets(inst, false, transforms)){
		PreviewObjectTransformTargets(inst, transforms, snapshot.position,
			snapshot.rotation, UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT);
		CommitObjectTransformTargets(transforms);
	}
}

static void
rollbackAgentSession(void)
{
	std::unordered_map<int32, AgentSceneSnapshot> snapshots;
	for(size_t i = 0; i < gAgentSessionSnapshot.size(); i++)
		snapshots[gAgentSessionSnapshot[i].id] = gAgentSessionSnapshot[i];

	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!isAgentSceneInstance(inst))
			continue;
		auto found = snapshots.find(inst->m_id);
		if(found == snapshots.end()){
			if(!inst->m_isDeleted)
				inst->Delete();
			continue;
		}
		const AgentSceneSnapshot &snapshot = found->second;
		if(inst->m_isDeleted && !snapshot.deleted)
			inst->Undelete();
		else if(!inst->m_isDeleted && snapshot.deleted)
			inst->Delete();
		if(!snapshot.deleted)
			restoreAgentTransform(inst, snapshot);
	}
	for(size_t i = 0; i < gAgentScopedWorldSnapshot.size(); i++){
		const AgentSceneSnapshot &snapshot = gAgentScopedWorldSnapshot[i];
		ObjectInst *inst = GetInstanceByID(snapshot.id);
		if(inst == nil)
			continue;
		// Native instances intentionally remain outside the editable IPL document,
		// so normal Delete/Undelete guards must not mark their source archive dirty.
		inst->m_isDeleted = snapshot.deleted;
		if(snapshot.deleted)
			inst->Deselect();
	}
	ClearSelection();
	ResetUndoHistory();
	clearAgentSession();
}

static int
countAgentInstances(bool includeLods)
{
	int count = 0;
	for(CPtrNode *p = instances.first; p; p = p->next){
		ObjectInst *inst = (ObjectInst*)p->item;
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted)
			continue;
		if(!includeLods && inst->m_numChildren > 0)
			continue;
		count++;
	}
	return count;
}

static std::string
agentInstanceJson(ObjectInst *inst, bool includeAgentFlag)
{
	ObjectDef *object = GetObjectDef(inst->m_objectId);
	rw::V3d degrees = GetObjectRotationDegrees(inst->m_rotation);
	char item[768];
	snprintf(item, sizeof(item),
		"{\"instance_id\":%d,\"object_key\":\"runtime:%d\",\"model_id\":%d,"
		"\"name\":\"%s\",\"position\":[%.4f,%.4f,%.4f],"
		"\"rotation\":[%.3f,%.3f,%.3f]%s%s}",
		inst->m_id, inst->m_id, inst->m_objectId,
		jsonEscape(object ? object->m_name : "").c_str(), inst->m_translation.x,
		inst->m_translation.y, inst->m_translation.z, degrees.x, degrees.y, degrees.z,
		includeAgentFlag ? ",\"agent_scene\":" : "",
		includeAgentFlag ? (isAgentSceneInstance(inst) ? "true" : "false") : "");
	return item;
}

static bool
agentInstanceInZone(ObjectInst *inst, bool useZone, float centerX, float centerY,
	float radiusSquared)
{
	if(!useZone)
		return true;
	float dx = inst->m_translation.x - centerX;
	float dy = inst->m_translation.y - centerY;
	return dx * dx + dy * dy <= radiusSquared;
}

static void
handleRequest(const std::vector<std::string> &lines)
{
	if(lines.size() < 2 || lines[0].empty() || lines[1].empty())
		return;
	const std::string &requestId = lines[0];
	const std::string &command = lines[1];

	if(command == "ping"){
		writeResponse(requestId, true, "\"result\":\"pong\"");
		return;
	}
	if(command == "capabilities"){
		writeResponse(requestId, true, std::string(
			"\"engine\":\"ariane\",\"build_id\":\"") + jsonEscape(agentBuildId()) +
			"\",\"transport\":\"unix-stream-framed-v1\","
			"\"limits\":{\"max_request_bytes\":1048576,\"max_response_bytes\":4194304,\"max_page_items\":256},"
			"\"observation\":[\"rgb_capture\",\"screen_ray_depth\",\"screen_ray_object_id\"],"
			"\"identity\":{\"instance_id\":\"process_local\",\"object_key\":\"checkpoint_manifest\"},"
			"\"commands\":[\"scene\",\"session_begin\",\"session_status\","
			"\"session_commit\",\"session_rollback\",\"catalog\",\"asset_detail\","
			"\"asset_probe\",\"asset_preview\",\"resolve_placement\",\"raycast_segment\","
			"\"inspect_zone\",\"inspect_zone_page\",\"list_page\",\"validate_zone\","
			"\"camera_context\",\"selection\",\"screen_to_world\",\"screen_grid\",\"surface_grid\",\"environment\",\"scene_bounds\","
			"\"analyze_placement\",\"fit_terrain\","
			"\"place\",\"batch\",\"transform\",\"transform3d\",\"suppress_world_models\","
			"\"delete\",\"clear\",\"list\",\"validate\",\"camera\",\"capture\","
			"\"capture_pose\",\"save\"]");
		return;
	}
	if(command == "scene"){
		if(lines.size() < 4 || lines[2].empty() || lines[3].empty()){
			writeError(requestId, "scene requires logical and physical IPL paths");
			return;
		}
		CloseIplMapDocument();
		clearAgentSession();
		strncpy(gAgentSceneLogicalPath, lines[2].c_str(), sizeof(gAgentSceneLogicalPath) - 1);
		gAgentSceneLogicalPath[sizeof(gAgentSceneLogicalPath) - 1] = '\0';
		strncpy(gAgentScenePhysicalPath, lines[3].c_str(), sizeof(gAgentScenePhysicalPath) - 1);
		gAgentScenePhysicalPath[sizeof(gAgentScenePhysicalPath) - 1] = '\0';
		SetIplMapDocument(gAgentSceneLogicalPath, gAgentScenePhysicalPath, true);
		SetCustomPlacementIpl(gAgentSceneLogicalPath, gAgentScenePhysicalPath, false);
		ClearSelection();
		ResetUndoHistory();
		gAgentSceneRevision++;
		writeResponse(requestId, true,
			std::string("\"logical_path\":\"") + jsonEscape(gAgentSceneLogicalPath) +
			"\",\"physical_path\":\"" + jsonEscape(gAgentScenePhysicalPath) + "\"");
		return;
	}
	if(command == "session_begin"){
		if(gAgentSceneLogicalPath[0] == '\0'){
			writeError(requestId, "set an agent scene before beginning a session");
			return;
		}
		if(gAgentSessionActive){
			writeError(requestId, "a scratch session is already active");
			return;
		}
		std::string sessionId = lines.size() > 2 && !lines[2].empty() ? lines[2] : requestId;
		beginAgentSession(sessionId);
		writeResponse(requestId, true, std::string("\"session_id\":\"") +
			jsonEscape(sessionId.c_str()) + "\",\"snapshot_instances\":" +
			std::to_string(gAgentSessionSnapshot.size()));
		return;
	}
	if(command == "session_status"){
		writeResponse(requestId, true, std::string("\"active\":") +
			(gAgentSessionActive ? "true" : "false") + ",\"session_id\":\"" +
			jsonEscape(gAgentSessionId.c_str()) + "\",\"live_instances\":" +
			std::to_string(countAgentInstances(false)) + ",\"logical_path\":\"" +
			jsonEscape(gAgentSceneLogicalPath) + "\",\"physical_path\":\"" +
			jsonEscape(gAgentScenePhysicalPath) + "\"");
		return;
	}
	if(command == "session_rollback"){
		if(!gAgentSessionActive){
			writeError(requestId, "no scratch session is active");
			return;
		}
		std::string sessionId = gAgentSessionId;
		rollbackAgentSession();
		gAgentSceneRevision++;
		writeResponse(requestId, true, std::string("\"rolled_back_session\":\"") +
			jsonEscape(sessionId.c_str()) + "\",\"live_instances\":" +
			std::to_string(countAgentInstances(false)));
		return;
	}
	if(command == "session_commit"){
		if(!gAgentSessionActive){
			writeError(requestId, "no scratch session is active");
			return;
		}
		std::string sessionId = gAgentSessionId;
		clearAgentSession();
		writeResponse(requestId, true, std::string("\"committed_session\":\"") +
			jsonEscape(sessionId.c_str()) + "\",\"live_instances\":" +
			std::to_string(countAgentInstances(false)));
		return;
	}
	if(command == "catalog"){
		std::string query = lines.size() > 2 ? lowercase(lines[2].c_str()) : "";
		int limit = 50;
		if(lines.size() > 3)
			parseInt(lines[3], &limit);
		limit = std::max(1, std::min(limit, 500));
		std::string objects = "\"objects\":[";
		int count = 0;
		for(int id = 0; id < NUMOBJECTDEFS && count < limit; id++){
			ObjectDef *object = GetObjectDef(id);
			if(object == nil || (!query.empty() && lowercase(object->m_name).find(query) == std::string::npos))
				continue;
			if(count++) objects += ',';
			char item[512];
			snprintf(item, sizeof(item), "{\"id\":%d,\"name\":\"%s\",\"draw_distance\":%.2f}",
				id, jsonEscape(object->m_name).c_str(), object->GetLargestDrawDist());
			objects += item;
		}
		objects += "]";
		writeResponse(requestId, true, objects);
		return;
	}
	if(command == "asset_detail"){
		if(lines.size() < 3){
			writeError(requestId, "asset_detail requires a model id or name");
			return;
		}
		int objectId;
		ObjectDef *object = resolveObject(lines[2], &objectId);
		if(object == nil){
			writeError(requestId, "unknown model");
			return;
		}
		char body[1024];
		if(object->m_colModel){
			CBox &box = object->m_colModel->boundingBox;
			snprintf(body, sizeof(body),
				"\"asset\":{\"id\":%d,\"name\":\"%s\",\"draw_distance\":%.2f,\"has_collision\":true,\"collision_bounds\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]],\"bounds_space\":\"model_local_relative_to_origin\",\"identity_axes\":{\"right\":[1,0,0],\"forward\":[0,1,0],\"up\":[0,0,1]},\"ground_offset\":%.4f,\"lod_id\":%d}",
				objectId, jsonEscape(object->m_name).c_str(), object->GetLargestDrawDist(),
				box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
				GetPlacementBaseOffset(objectId), GetLodForObject(objectId));
		}else{
			snprintf(body, sizeof(body),
				"\"asset\":{\"id\":%d,\"name\":\"%s\",\"draw_distance\":%.2f,\"has_collision\":false,\"bounds_space\":\"model_local_relative_to_origin\",\"identity_axes\":{\"right\":[1,0,0],\"forward\":[0,1,0],\"up\":[0,0,1]},\"ground_offset\":0.0,\"lod_id\":%d}",
				objectId, jsonEscape(object->m_name).c_str(), object->GetLargestDrawDist(),
				GetLodForObject(objectId));
		}
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "asset_probe"){
		if(lines.size() < 3){
			writeError(requestId, "asset_probe requires a model id or name");
			return;
		}
		int objectId = -1;
		ObjectDef *object = resolveObject(lines[2], &objectId);
		if(object == nil){
			writeResponse(requestId, true,
				std::string("\"asset\":{\"query\":\"") + jsonEscape(lines[2].c_str()) +
				"\",\"defined\":false,\"loaded_now\":false,\"renderable\":false,"
				"\"availability\":\"catalog_only\"}");
			return;
		}
		bool ensureLoaded = false;
		int ensure = 0;
		if(lines.size() > 3 && parseInt(lines[3], &ensure)) ensureLoaded = ensure != 0;
		bool loadedBefore = object->IsLoaded();
		if(ensureLoaded && !loadedBefore){
			RequestObject(objectId);
			LoadAllRequestedObjects();
		}
		bool loadedNow = object->IsLoaded();
		std::string availability = loadedNow ? "renderable" :
			(ensureLoaded ? "load_failed" : "defined_unloaded");
		char prefix[768];
		snprintf(prefix, sizeof(prefix),
			"\"asset\":{\"id\":%d,\"name\":\"%s\",\"defined\":true,"
			"\"loaded_before\":%s,\"loaded_now\":%s,\"renderable\":%s,"
			"\"availability\":\"%s\",\"has_collision\":%s,"
			"\"bounds_space\":\"model_local_relative_to_origin\","
			"\"identity_axes\":{\"right\":[1,0,0],\"forward\":[0,1,0],\"up\":[0,0,1]},"
			"\"ground_offset\":%.4f",
			objectId, jsonEscape(object->m_name).c_str(), loadedBefore ? "true" : "false",
			loadedNow ? "true" : "false", loadedNow ? "true" : "false",
			availability.c_str(), object->m_colModel ? "true" : "false",
			GetPlacementBaseOffset(objectId));
		std::string body = prefix;
		if(object->m_colModel){
			CBox &box = object->m_colModel->boundingBox;
			char bounds[320];
			snprintf(bounds, sizeof(bounds),
				",\"collision_bounds\":[[%.4f,%.4f,%.4f],[%.4f,%.4f,%.4f]]",
				box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z);
			body += bounds;
		}
		body += "}";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "resolve_placement"){
		if(lines.size() < 8){
			writeError(requestId, "resolve_placement requires model, x, y, z, heading and snap");
			return;
		}
		int objectId, snap;
		float x, y, z, heading;
		ObjectDef *object = resolveObject(lines[2], &objectId);
		if(object == nil){
			writeError(requestId, ("unknown model: " + lines[2]).c_str());
			return;
		}
		if(!parseFloat(lines[3], &x) || !parseFloat(lines[4], &y) ||
		   !parseFloat(lines[5], &z) || !parseFloat(lines[6], &heading) ||
		   !parseInt(lines[7], &snap)){
			writeError(requestId, "invalid resolve_placement field");
			return;
		}
		if(snap){
			rw::V3d ground;
			if(!getAgentGroundPlacementSurface({ x, y, z }, &ground)){
				writeError(requestId, "no ground surface at placement");
				return;
			}
			z = ground.z + GetPlacementBaseOffset(objectId);
		}
		rw::Quat rotation = MakeObjectRotationDegrees({ 0.0f, 0.0f, heading });
		AgentPlacementAnalysis analysis;
		AgentSupportContract contract = agentSupportContract(objectId);
		bool analyzed = analyzeAgentPlacement(objectId, { x, y, z }, rotation,
			contract.sampleGridSize, &analysis);
		AgentSupportEvaluation evaluation = evaluateAgentSupport(objectId, analyzed, analysis);
		char prefix[384];
		snprintf(prefix, sizeof(prefix),
			"\"placement\":{\"model_id\":%d,\"position\":[%.5f,%.5f,%.5f],"
			"\"heading\":%.3f,\"snap_resolved\":%s}", objectId, x, y, z,
			heading, snap ? "true" : "false");
		writeResponse(requestId, true, std::string(prefix) +
			",\"support_evaluation\":" + agentSupportEvaluationJson(
				evaluation, analyzed ? &analysis : nil));
		return;
	}
	if(command == "asset_preview"){
		if(lines.size() < 4){
			writeError(requestId, "asset_preview requires model and PNG path");
			return;
		}
		int objectId, size = 512;
		float angle = 0.785398163f;
		ObjectDef *object = resolveObject(lines[2], &objectId);
		if(object == nil){
			writeError(requestId, ("unknown model: " + lines[2]).c_str());
			return;
		}
		if(lines.size() > 4 && !parseFloat(lines[4], &angle)){
			writeError(requestId, "invalid asset preview angle");
			return;
		}
		if(lines.size() > 5 && !parseInt(lines[5], &size)){
			writeError(requestId, "invalid asset preview size");
			return;
		}
		char previewError[256] = { 0 };
		if(!CaptureObjectPreviewPng(objectId, lines[3].c_str(), size, angle,
		   previewError, sizeof(previewError))){
			writeError(requestId, previewError[0] ? previewError :
				"asset preview could not load or render the model");
			return;
		}
		writeResponse(requestId, true, std::string("\"path\":\"") +
			jsonEscape(lines[3].c_str()) + "\",\"model_id\":" + std::to_string(objectId) +
			",\"angle_radians\":" + std::to_string(angle));
		return;
	}
	if(command == "analyze_placement"){
		int instanceId, gridSize = 3;
		if(lines.size() < 3 || !parseInt(lines[2], &instanceId)){
			writeError(requestId, "analyze_placement requires an instance id");
			return;
		}
		if(lines.size() > 3 && !parseInt(lines[3], &gridSize)){
			writeError(requestId, "invalid placement grid size");
			return;
		}
		ObjectInst *inst = GetInstanceByID(instanceId);
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted){
			writeError(requestId, "instance does not belong to the active agent scene");
			return;
		}
		AgentPlacementAnalysis analysis;
		if(!analyzeAgentPlacement(inst->m_objectId, inst->m_translation,
		   inst->m_rotation, gridSize, &analysis)){
			writeError(requestId, "placement analysis requires collision and terrain support");
			return;
		}
		AgentSupportEvaluation evaluation = evaluateAgentSupport(
			inst->m_objectId, true, analysis);
		writeResponse(requestId, true, std::string("\"instance_id\":") +
			std::to_string(instanceId) + ",\"analysis\":" + agentPlacementAnalysisJson(analysis) +
			",\"support_evaluation\":" + agentSupportEvaluationJson(evaluation, &analysis));
		return;
	}
	if(command == "fit_terrain"){
		if(!requireAgentSession(requestId)) return;
		int instanceId;
		if(lines.size() < 4 || !parseInt(lines[2], &instanceId)){
			writeError(requestId, "fit_terrain requires instance id and building or prop profile");
			return;
		}
		ObjectInst *inst = GetInstanceByID(instanceId);
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted){
			writeError(requestId, "instance does not belong to the active agent scene");
			return;
		}
		std::string profile = lowercase(lines[3].c_str());
		if(profile != "building" && profile != "prop"){
			writeError(requestId, "fit profile must be building or prop");
			return;
		}
		AgentSupportContract contract = agentSupportContract(inst->m_objectId);
		float radius = 30.0f, step = 3.0f, maxRelief = contract.maxSupportRelief;
		if(lines.size() > 4 && !parseFloat(lines[4], &radius)){
			writeError(requestId, "invalid fit radius");
			return;
		}
		if(lines.size() > 5 && !parseFloat(lines[5], &step)){
			writeError(requestId, "invalid fit step");
			return;
		}
		if(lines.size() > 6 && !parseFloat(lines[6], &maxRelief)){
			writeError(requestId, "invalid maximum support relief");
			return;
		}
		radius = std::max(0.0f, std::min(radius, 100.0f));
		step = std::max(1.0f, std::min(step, 10.0f));
		// Callers may demand a flatter result, but may not loosen the validator's
		// shared support contract and manufacture an applied-but-invalid fit.
		maxRelief = std::max(0.05f, std::min(maxRelief, contract.maxSupportRelief));

		rw::V3d originalPosition = inst->m_translation;
		rw::V3d degrees = GetObjectRotationDegrees(inst->m_rotation);
		rw::Quat fittedRotation = MakeObjectRotationDegrees({ 0.0f, 0.0f, degrees.z });
		rw::V3d fittedPosition = originalPosition;
		AgentPlacementAnalysis bestAnalysis;
		bool found = false;
		if(profile == "building"){
			float bestScore = 1.0e30f;
			int extent = (int)ceilf(radius / step);
			for(int iy = -extent; iy <= extent; iy++){
				for(int ix = -extent; ix <= extent; ix++){
					float dx = ix * step, dy = iy * step;
					float distance = sqrtf(dx * dx + dy * dy);
					if(distance > radius + 0.001f) continue;
					rw::V3d candidate = { originalPosition.x + dx,
						originalPosition.y + dy, 0.0f };
					AgentPlacementAnalysis analysis;
					if(!analyzeAgentPlacement(inst->m_objectId, candidate,
					   fittedRotation, contract.sampleGridSize, &analysis) || analysis.missingSamples > 0)
						continue;
					float relief = analysis.maxRequiredZ - analysis.minRequiredZ;
					float score = relief + distance * 0.002f;
					if(score < bestScore){
						bestScore = score;
						bestAnalysis = analysis;
						fittedPosition = candidate;
						found = true;
					}
				}
			}
		}else if(profile == "prop"){
			AgentPlacementAnalysis uprightAnalysis;
			rw::V3d probe = { originalPosition.x, originalPosition.y, 0.0f };
			if(analyzeAgentPlacement(inst->m_objectId, probe, fittedRotation,
			   contract.sampleGridSize, &uprightAnalysis)){
				fittedRotation = BuildGroundAlignedRotationFromRotation(
					fittedRotation, uprightAnalysis.averageNormal);
				found = analyzeAgentPlacement(inst->m_objectId, probe,
					fittedRotation, contract.sampleGridSize, &bestAnalysis) &&
					bestAnalysis.missingSamples == 0;
				fittedPosition = probe;
			}
		}

		float relief = found ? bestAnalysis.maxRequiredZ - bestAnalysis.minRequiredZ : 1.0e30f;
		if(!found || relief > maxRelief){
			std::string body = "\"applied\":false,\"profile\":\"" + jsonEscape(profile.c_str()) +
				"\",\"reason\":\"no sufficiently supported placement found\"";
			if(found)
				body += ",\"best_candidate\":[" + std::to_string(fittedPosition.x) + "," +
					std::to_string(fittedPosition.y) + "],\"analysis\":" +
					agentPlacementAnalysisJson(bestAnalysis);
			writeResponse(requestId, true, body);
			return;
		}
		// Pick the middle of the exact Z interval accepted by the shared contract.
		// This leaves numerical margin on both the floating and penetration bounds.
		float minimumValidZ = bestAnalysis.maxRequiredZ - contract.maxPenetration;
		float maximumValidZ = bestAnalysis.minRequiredZ + contract.maxFloatingClearance;
		if(minimumValidZ > maximumValidZ){
			AgentSupportEvaluation evaluation = evaluateAgentSupport(
				inst->m_objectId, true, bestAnalysis);
			writeResponse(requestId, true,
				"\"applied\":false,\"profile\":\"" + jsonEscape(profile.c_str()) +
				"\",\"reason\":\"support relief has no validator-safe Z interval\","
				"\"analysis\":" + agentPlacementAnalysisJson(bestAnalysis) +
				",\"support_evaluation\":" + agentSupportEvaluationJson(evaluation, &bestAnalysis));
			return;
		}
		fittedPosition.z = (minimumValidZ + maximumValidZ) * 0.5f;
		AgentPlacementAnalysis finalAnalysis;
		bool finalAnalyzed = analyzeAgentPlacement(inst->m_objectId, fittedPosition,
			fittedRotation, contract.sampleGridSize, &finalAnalysis);
		AgentSupportEvaluation finalEvaluation = evaluateAgentSupport(
			inst->m_objectId, finalAnalyzed, finalAnalysis);
		if(!finalEvaluation.valid){
			writeResponse(requestId, true,
				"\"applied\":false,\"profile\":\"" + jsonEscape(profile.c_str()) +
				"\",\"reason\":\"best candidate does not satisfy terrain-support-v1\","
				"\"analysis\":" + (finalAnalyzed ? agentPlacementAnalysisJson(finalAnalysis) : "null") +
				",\"support_evaluation\":" + agentSupportEvaluationJson(
					finalEvaluation, finalAnalyzed ? &finalAnalysis : nil));
			return;
		}
		rw::V3d fittedDegrees = GetObjectRotationDegrees(fittedRotation);
		float relocationDistance = length(sub(fittedPosition, originalPosition));
		float rotationDelta = fabsf(fittedDegrees.x - degrees.x) +
			fabsf(fittedDegrees.y - degrees.y) + fabsf(fittedDegrees.z - degrees.z);
		if(relocationDistance < 0.0001f && rotationDelta < 0.001f){
			writeResponse(requestId, true,
				"\"applied\":false,\"profile\":\"" + jsonEscape(profile.c_str()) +
				"\",\"reason\":\"object already satisfies terrain-support-v1\","
				"\"analysis\":" + agentPlacementAnalysisJson(finalAnalysis) +
				",\"support_evaluation\":" + agentSupportEvaluationJson(finalEvaluation, &finalAnalysis));
			return;
		}
		std::vector<UndoTransform> transforms;
		if(!CaptureObjectTransformTargets(inst, false, transforms)){
			writeError(requestId, "could not capture terrain-fit transform");
			return;
		}
		PreviewObjectTransformTargets(inst, transforms, fittedPosition,
			fittedRotation, UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT);
		CommitObjectTransformTargets(transforms);
		gAgentSceneRevision++;
		char prefix[512];
		snprintf(prefix, sizeof(prefix),
			"\"applied\":true,\"profile\":\"%s\",\"position\":[%.4f,%.4f,%.4f],"
			"\"rotation\":[%.3f,%.3f,%.3f],\"relocation_distance\":%.4f,"
			"\"rotation_delta_degrees\":%.4f,\"analysis\":",
			jsonEscape(profile.c_str()).c_str(), fittedPosition.x, fittedPosition.y,
			fittedPosition.z, fittedDegrees.x, fittedDegrees.y, fittedDegrees.z,
			relocationDistance, rotationDelta);
		writeResponse(requestId, true, std::string(prefix) + agentPlacementAnalysisJson(finalAnalysis) +
			",\"support_evaluation\":" + agentSupportEvaluationJson(finalEvaluation, &finalAnalysis));
		return;
	}
	if(command == "place"){
		if(gAgentSceneLogicalPath[0] == '\0'){
			writeError(requestId, "set an agent scene before placing objects");
			return;
		}
		if(!requireAgentSession(requestId)) return;
		std::vector<ObjectInst*> created;
		ObjectInst *hd = nil;
		std::string error;
		if(!placeObject(lines, 2, created, &hd, &error)){
			writeError(requestId, error.c_str());
			return;
		}
		UndoRecordPaste(created.data(), (int)created.size());
		// Agent reviews need an unbiased viewport; editor selection tint would
		// otherwise make every newly placed asset appear bright red in captures.
		ClearSelection();
		gAgentSceneRevision++;
		char body[256];
		snprintf(body, sizeof(body), "\"instance_id\":%d,\"model_id\":%d,\"z\":%.4f,\"created_count\":%d",
			hd->m_id, hd->m_objectId, hd->m_translation.z, (int)created.size());
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "batch"){
		if(gAgentSceneLogicalPath[0] == '\0'){
			writeError(requestId, "set an agent scene before placing objects");
			return;
		}
		if(!requireAgentSession(requestId)) return;
		std::vector<ObjectInst*> created;
		std::string placed = "\"placements\":[";
		int placementCount = 0;
		for(size_t i = 2; i < lines.size(); i++){
			if(lines[i].empty()) continue;
			std::vector<std::string> fields;
			size_t start = 0;
			while(start <= lines[i].size()){
				size_t end = lines[i].find('\t', start);
				if(end == std::string::npos) end = lines[i].size();
				fields.push_back(lines[i].substr(start, end - start));
				if(end == lines[i].size()) break;
				start = end + 1;
			}
			ObjectInst *hd = nil;
			std::string error;
			if(!placeObject(fields, 0, created, &hd, &error)){
				for(size_t j = 0; j < created.size(); j++) created[j]->Delete();
				writeError(requestId, ("batch row " + std::to_string(i - 1) + ": " + error).c_str());
				return;
			}
			if(placementCount++) placed += ',';
			char item[160];
			snprintf(item, sizeof(item), "{\"instance_id\":%d,\"model_id\":%d,\"z\":%.4f}",
				hd->m_id, hd->m_objectId, hd->m_translation.z);
			placed += item;
		}
		placed += "]";
		if(created.empty()){
			writeError(requestId, "batch contains no placements");
			return;
		}
		UndoRecordPaste(created.data(), (int)created.size());
		ClearSelection();
		gAgentSceneRevision++;
		writeResponse(requestId, true, placed);
		return;
	}
	if(command == "transform3d"){
		if(!requireAgentSession(requestId)) return;
		if(lines.size() < 9){
			writeError(requestId, "transform3d requires id, position xyz and rotation xyz");
			return;
		}
		int instanceId;
		float x, y, z, pitch, roll, yaw;
		if(!parseInt(lines[2], &instanceId) || !parseFloat(lines[3], &x) ||
		   !parseFloat(lines[4], &y) || !parseFloat(lines[5], &z) ||
		   !parseFloat(lines[6], &pitch) || !parseFloat(lines[7], &roll) ||
		   !parseFloat(lines[8], &yaw)){
			writeError(requestId, "invalid transform3d field");
			return;
		}
		ObjectInst *inst = GetInstanceByID(instanceId);
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted){
			writeError(requestId, "instance does not belong to the active agent scene");
			return;
		}
		std::vector<UndoTransform> transforms;
		if(!CaptureObjectTransformTargets(inst, false, transforms)){
			writeError(requestId, "could not capture transform target");
			return;
		}
		PreviewObjectTransformTargets(inst, transforms, { x, y, z },
			MakeObjectRotationDegrees({ pitch, roll, yaw }),
			UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT);
		CommitObjectTransformTargets(transforms);
		gAgentSceneRevision++;
		writeResponse(requestId, true, "\"result\":\"transformed3d\"");
		return;
	}
	if(command == "transform"){
		if(!requireAgentSession(requestId)) return;
		if(lines.size() < 8){
			writeError(requestId, "transform requires id, x, y, z, heading and snap");
			return;
		}
		int instanceId, snap;
		float x, y, z, heading;
		if(!parseInt(lines[2], &instanceId) || !parseFloat(lines[3], &x) ||
		   !parseFloat(lines[4], &y) || !parseFloat(lines[5], &z) ||
		   !parseFloat(lines[6], &heading) || !parseInt(lines[7], &snap)){
			writeError(requestId, "invalid transform field");
			return;
		}
		ObjectInst *inst = GetInstanceByID(instanceId);
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted){
			writeError(requestId, "instance does not belong to the active agent scene");
			return;
		}
		rw::V3d position = { x, y, z };
		if(snap){
			rw::V3d ground;
			if(!getAgentGroundPlacementSurface(position, &ground)){
				writeError(requestId, "no ground surface at transform position");
				return;
			}
			position.z = ground.z + GetPlacementBaseOffset(inst->m_objectId);
		}
		std::vector<UndoTransform> transforms;
		if(!CaptureObjectTransformTargets(inst, false, transforms)){
			writeError(requestId, "could not capture transform target");
			return;
		}
		PreviewObjectTransformTargets(inst, transforms, position,
			MakeObjectRotationDegrees({ 0.0f, 0.0f, heading }), UNDO_TRANSFORM_POS | UNDO_TRANSFORM_ROT);
		CommitObjectTransformTargets(transforms);
		gAgentSceneRevision++;
		writeResponse(requestId, true, "\"result\":\"transformed\"");
		return;
	}
	if(command == "delete"){
		if(!requireAgentSession(requestId)) return;
		int instanceId;
		if(lines.size() < 3 || !parseInt(lines[2], &instanceId)){
			writeError(requestId, "delete requires an instance id");
			return;
		}
		ObjectInst *inst = GetInstanceByID(instanceId);
		if(!isAgentSceneInstance(inst) || inst->m_isDeleted){
			writeError(requestId, "instance does not belong to the active agent scene");
			return;
		}
		ObjectInst *deleted[1] = { inst };
		UndoRecordDelete(deleted, 1);
		inst->Delete();
		gAgentSceneRevision++;
		writeResponse(requestId, true, "\"result\":\"deleted\"");
		return;
	}
	if(command == "suppress_world_models"){
		if(!requireAgentSession(requestId)) return;
		if(lines.size() < 6){
			writeError(requestId, "suppress_world_models requires x, y, radius and model ids");
			return;
		}
		float centerX, centerY, radius;
		if(!parseFloat(lines[2], &centerX) || !parseFloat(lines[3], &centerY) ||
		   !parseFloat(lines[4], &radius) || radius <= 0.0f || radius > 500.0f){
			writeError(requestId, "invalid suppression zone");
			return;
		}
		std::unordered_set<int> modelIds;
		for(size_t i = 5; i < lines.size(); i++){
			int modelId;
			if(!parseInt(lines[i], &modelId) || modelId < 0 || modelId >= NUMOBJECTDEFS){
				writeError(requestId, "invalid suppression model id");
				return;
			}
			modelIds.insert(modelId);
		}
		if(modelIds.size() > 64){
			writeError(requestId, "suppression model whitelist is limited to 64 ids");
			return;
		}

		float radiusSquared = radius * radius;
		std::vector<ObjectInst*> targets;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst->m_isDeleted || isAgentSceneInstance(inst) ||
			   modelIds.find(inst->m_objectId) == modelIds.end())
				continue;
			float dx = inst->m_translation.x - centerX;
			float dy = inst->m_translation.y - centerY;
			if(dx * dx + dy * dy <= radiusSquared)
				targets.push_back(inst);
		}
		if(targets.size() > 256){
			writeError(requestId, "suppression zone matched more than 256 instances");
			return;
		}

		std::unordered_set<int32> captured;
		for(size_t i = 0; i < gAgentScopedWorldSnapshot.size(); i++)
			captured.insert(gAgentScopedWorldSnapshot[i].id);
		std::string suppressed = "\"suppressed\":[";
		for(size_t i = 0; i < targets.size(); i++){
			ObjectInst *inst = targets[i];
			if(captured.insert(inst->m_id).second)
				gAgentScopedWorldSnapshot.push_back({ inst->m_id, inst->m_translation,
					inst->m_rotation, inst->m_isDeleted });
			if(i) suppressed += ',';
			char item[256];
			snprintf(item, sizeof(item),
				"{\"instance_id\":%d,\"model_id\":%d,\"position\":[%.3f,%.3f,%.3f]}",
				inst->m_id, inst->m_objectId, inst->m_translation.x,
				inst->m_translation.y, inst->m_translation.z);
			suppressed += item;
			// This is a runtime-only visibility override. ObjectInst::Delete refuses
			// native instances outside the active document and would also imply a
			// persistent IPL edit, neither of which is wanted for a scratch proposal.
			inst->m_isDeleted = true;
			inst->Deselect();
		}
		suppressed += "],\"suppressed_count\":" + std::to_string(targets.size());
		gAgentSceneRevision++;
		writeResponse(requestId, true, suppressed);
		return;
	}
	if(command == "clear"){
		if(!requireAgentSession(requestId)) return;
		std::vector<ObjectInst*> deleted;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(isAgentSceneInstance(inst) && !inst->m_isDeleted)
				deleted.push_back(inst);
		}
		gAgentSceneRevision++;
		if(!deleted.empty()){
			UndoRecordDelete(deleted.data(), (int)deleted.size());
			for(size_t i = 0; i < deleted.size(); i++) deleted[i]->Delete();
		}
		writeResponse(requestId, true,
			std::string("\"deleted_count\":") + std::to_string(deleted.size()));
		return;
	}
	if(command == "selection"){
		std::string body = "\"items\":["; int count = 0;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst->m_isDeleted || !inst->m_selected) continue;
			if(count == 256){ writeError(requestId, "selection exceeds 256 objects; narrow the selection"); return; }
			if(count++) body += ',';
			body += agentInstanceJson(inst, true);
		}
		writeResponse(requestId, true, body + "]"); return;
	}
	if(command == "environment"){
		if(lines.size() != 2 && lines.size() != 7){
			writeError(requestId, "environment accepts no fields or hour minute weather_a weather_b blend");
			return;
		}
		if(lines.size() == 7){
			int hour, minute, a, b; float blend;
			if(!parseInt(lines[2], &hour) || !parseInt(lines[3], &minute) ||
			   !parseInt(lines[4], &a) || !parseInt(lines[5], &b) || !parseFloat(lines[6], &blend) ||
			   hour < 0 || hour > 23 || minute < 0 || minute > 59 || a < 0 || b < 0 ||
			   a >= params.numWeathers || b >= params.numWeathers || blend < 0 || blend > 1){
				writeError(requestId, "invalid environment settings"); return;
			}
			currentHour = hour; currentMinute = minute;
			Weather::oldWeather = a; Weather::newWeather = b; Weather::interpolation = blend;
			gAgentSceneRevision++;
		}
		char head[256];
		snprintf(head, sizeof(head), "\"hour\":%d,\"minute\":%d,\"weather_a\":%d,\"weather_b\":%d,\"blend\":%.4f,\"weathers\":[",
			currentHour, currentMinute, Weather::oldWeather, Weather::newWeather, Weather::interpolation);
		std::string body = head;
		for(int i = 0; i < params.numWeathers; i++){
			if(i) body += ',';
			body += "{\"id\":" + std::to_string(i) + ",\"name\":\"" + jsonEscape(params.weatherInfo[i].name) + "\"}";
		}
		writeResponse(requestId, true, body + "]"); return;
	}
	if(command == "surface_grid"){
		float x, y, z, radius; int grid;
		if(lines.size() != 7 || !parseFloat(lines[2], &x) || !parseFloat(lines[3], &y) ||
		   !parseFloat(lines[4], &z) || !parseFloat(lines[5], &radius) || !parseInt(lines[6], &grid) ||
		   grid < 2 || grid > 25 || radius <= 0 || radius > 250){
			writeError(requestId, "surface_grid requires xyz radius(0,250] grid[2,25]"); return;
		}
		std::string body = "\"samples\":[";
		for(int row = 0; row < grid; row++) for(int col = 0; col < grid; col++){
			if(row || col) body += ',';
			rw::V3d probe = { x - radius + 2 * radius * col / (grid-1), y - radius + 2 * radius * row / (grid-1), z + 2 };
			rw::V3d hit, normal;
			char item[384];
			if(GetGroundPlacementSurface(probe, &hit, &normal, true))
				snprintf(item, sizeof(item), "{\"hit\":true,\"point\":[%.5f,%.5f,%.5f],\"normal\":[%.5f,%.5f,%.5f]}",
					hit.x, hit.y, hit.z, normal.x, normal.y, normal.z);
			else snprintf(item, sizeof(item), "{\"hit\":false,\"probe\":[%.5f,%.5f,%.5f]}", probe.x, probe.y, probe.z);
			body += item;
		}
		writeResponse(requestId, true, body + "],\"grid\":" + std::to_string(grid)); return;
	}
	if(command == "screen_grid"){
		int columns, rows, revision;
		if(lines.size() != 5 || !parseInt(lines[2], &columns) || !parseInt(lines[3], &rows) ||
		   !parseInt(lines[4], &revision) || columns < 2 || columns > 64 || rows < 2 || rows > 48 ||
		   revision != (int)gAgentCameraRevision){
			writeError(requestId, "screen_grid requires columns[2,64] rows[2,48] current camera revision"); return;
		}
		rw::V3d forward = rw::normalize(rw::sub(TheCamera.m_target, TheCamera.m_position));
		rw::V3d right = rw::scale(rw::normalize(rw::cross(forward, TheCamera.m_up)), TheCamera.m_rwcam->viewWindow.x);
		rw::V3d up = rw::scale(rw::normalize(rw::cross(right, forward)), TheCamera.m_rwcam->viewWindow.y);
		std::string body = "\"samples\":[";
		for(int row = 0; row < rows; row++) for(int col = 0; col < columns; col++){
			if(row || col) body += ',';
			float u = (col + 0.5f) / columns, v = (row + 0.5f) / rows;
			Ray ray; ray.start = TheCamera.m_position;
			ray.dir = rw::normalize(rw::add(forward, rw::add(rw::scale(right, 2*u-1), rw::scale(up, 1-2*v))));
			rw::V3d hit; float depth = 0;
			ObjectInst *inst = GetVisibleInstUnderRay(ray, &hit, &depth);
			char item[512];
			if(inst) snprintf(item, sizeof(item),
				"{\"hit\":true,\"pixel\":[%.1f,%.1f],\"point\":[%.5f,%.5f,%.5f],\"depth\":%.5f,\"instance_id\":%d,\"model_id\":%d,\"agent_scene\":%s}",
				u*sk::globals.width, v*sk::globals.height, hit.x, hit.y, hit.z, depth, inst->m_id, inst->m_objectId,
				isAgentSceneInstance(inst) ? "true" : "false");
			else snprintf(item, sizeof(item), "{\"hit\":false,\"pixel\":[%.1f,%.1f]}", u*sk::globals.width, v*sk::globals.height);
			body += item;
		}
		writeResponse(requestId, true, body + "]"); return;
	}
	if(command == "camera_context"){
		rw::V3d forward = rw::normalize(rw::sub(TheCamera.m_target, TheCamera.m_position));
		rw::V3d right = rw::normalize(rw::cross(forward, TheCamera.m_up));
		rw::V3d up = rw::normalize(rw::cross(right, forward));
		char body[1024];
		snprintf(body, sizeof(body),
			"\"camera\":{\"position\":[%.5f,%.5f,%.5f],\"target\":[%.5f,%.5f,%.5f],"
			"\"forward\":[%.6f,%.6f,%.6f],\"right\":[%.6f,%.6f,%.6f],"
			"\"up\":[%.6f,%.6f,%.6f],\"fov\":%.3f,\"aspect_ratio\":%.6f,"
			"\"viewport\":[%d,%d],\"near_plane\":%.4f,\"far_plane\":%.4f,"
			"\"projection\":\"perspective\",\"camera_revision\":%u}",
			TheCamera.m_position.x, TheCamera.m_position.y, TheCamera.m_position.z,
			TheCamera.m_target.x, TheCamera.m_target.y, TheCamera.m_target.z,
			forward.x, forward.y, forward.z, right.x, right.y, right.z,
			up.x, up.y, up.z, TheCamera.m_fov, TheCamera.m_aspectRatio,
			sk::globals.width, sk::globals.height, TheCamera.m_rwcam->nearPlane,
			TheCamera.m_rwcam->farPlane, gAgentCameraRevision);
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "raycast_segment"){
		if(lines.size() < 8){
			writeError(requestId, "raycast_segment requires start xyz and target xyz");
			return;
		}
		float values[6], targetTolerance = 1.0f;
		for(int i = 0; i < 6; i++){
			if(!parseFloat(lines[i + 2], &values[i])){
				writeError(requestId, "invalid raycast_segment field");
				return;
			}
		}
		if(lines.size() > 8 && (!parseFloat(lines[8], &targetTolerance) || targetTolerance < 0.0f)){
			writeError(requestId, "invalid raycast_segment target tolerance");
			return;
		}
		rw::V3d start = { values[0], values[1], values[2] };
		rw::V3d target = { values[3], values[4], values[5] };
		float distance = rw::length(rw::sub(target, start));
		if(distance < 0.001f){
			writeError(requestId, "raycast_segment endpoints must differ");
			return;
		}
		rw::V3d hit;
		float hitDistance = 0.0f;
		ObjectInst *inst = getAgentInstUnderSegment(
			start, target, &hit, &hitDistance, targetTolerance);
		if(inst){
			ObjectDef *object = GetObjectDef(inst->m_objectId);
			char body[768];
			snprintf(body, sizeof(body),
				"\"ray\":{\"visible\":false,\"distance\":%.5f,\"target_tolerance\":%.3f,"
				"\"hit\":{\"point\":[%.5f,%.5f,%.5f],\"distance\":%.5f,"
				"\"fraction\":%.6f,\"instance_id\":%d,\"model_id\":%d,\"name\":\"%s\","
				"\"agent_scene\":%s}}",
				distance, targetTolerance, hit.x, hit.y, hit.z, hitDistance,
				hitDistance / distance, inst->m_id, inst->m_objectId,
				jsonEscape(object ? object->m_name : "").c_str(),
				isAgentSceneInstance(inst) ? "true" : "false");
			writeResponse(requestId, true, body);
		}else{
			char body[256];
			snprintf(body, sizeof(body),
				"\"ray\":{\"visible\":true,\"distance\":%.5f,\"target_tolerance\":%.3f,\"hit\":null}",
				distance, targetTolerance);
			writeResponse(requestId, true, body);
		}
		return;
	}
	if(command == "screen_to_world"){
		if(lines.size() < 6){
			writeError(requestId, "screen_to_world requires pixel x/y and viewport width/height");
			return;
		}
		float pixelX, pixelY;
		int width, height;
		if(!parseFloat(lines[2], &pixelX) || !parseFloat(lines[3], &pixelY) ||
		   !parseInt(lines[4], &width) || !parseInt(lines[5], &height) ||
		   width <= 0 || height <= 0 || width != sk::globals.width ||
		   height != sk::globals.height || pixelX < 0.0f || pixelY < 0.0f ||
		   pixelX >= width || pixelY >= height){
			writeError(requestId, "invalid or stale screen_to_world viewport or pixel");
			return;
		}
		float ndcX = (pixelX + 0.5f) / width * 2.0f - 1.0f;
		float ndcY = (pixelY + 0.5f) / height * 2.0f - 1.0f;
		rw::V3d forward = rw::normalize(rw::sub(TheCamera.m_target, TheCamera.m_position));
		rw::V3d right = rw::scale(rw::normalize(rw::cross(forward, TheCamera.m_up)),
			TheCamera.m_rwcam->viewWindow.x);
		rw::V3d up = rw::scale(rw::normalize(rw::cross(right, forward)),
			TheCamera.m_rwcam->viewWindow.y);
		Ray ray;
		ray.start = TheCamera.m_position;
		ray.dir = rw::normalize(rw::add(forward,
			rw::add(rw::scale(right, ndcX), rw::scale(up, -ndcY))));
		rw::V3d hit;
		float hitT = 0.0f;
		ObjectInst *inst = GetVisibleInstUnderRay(ray, &hit, &hitT);
		if(inst){
			char body[640];
			snprintf(body, sizeof(body),
				"\"hit\":true,\"source\":\"geometry\",\"point\":[%.5f,%.5f,%.5f],"
				"\"depth\":%.5f,\"instance_id\":%d,\"object_key\":\"runtime:%d\","
				"\"model_id\":%d,\"agent_scene\":%s,\"camera_revision\":%u",
				hit.x, hit.y, hit.z, hitT, inst->m_id, inst->m_id,
				inst->m_objectId, isAgentSceneInstance(inst) ? "true" : "false",
				gAgentCameraRevision);
			writeResponse(requestId, true, body);
			return;
		}
		float t = fabsf(ray.dir.z) < 0.001f ? 50.0f :
			(TheCamera.m_target.z - ray.start.z) / ray.dir.z;
		t = std::max(1.0f, std::min(t, 5000.0f));
		rw::V3d projected = rw::add(ray.start, rw::scale(ray.dir, t));
		rw::V3d normal = { 0.0f, 0.0f, 1.0f };
		if(!GetGroundPlacementSurface(projected, &hit, &normal, true)){
			writeResponse(requestId, true, "\"hit\":false");
			return;
		}
		char body[512];
		snprintf(body, sizeof(body),
			"\"hit\":true,\"source\":\"ground_fallback\",\"point\":[%.5f,%.5f,%.5f],"
			"\"normal\":[%.6f,%.6f,%.6f],\"depth\":%.5f,\"camera_revision\":%u",
			hit.x, hit.y, hit.z, normal.x, normal.y, normal.z,
			rw::length(rw::sub(hit, ray.start)), gAgentCameraRevision);
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "list_page"){
		int offset = 0, limit = 8;
		if(lines.size() > 2) parseInt(lines[2], &offset);
		if(lines.size() > 3) parseInt(lines[3], &limit);
		offset = std::max(0, offset);
		limit = std::max(1, std::min(limit, 256));
		bool useZone = lines.size() > 6;
		float centerX = 0.0f, centerY = 0.0f, radius = 0.0f;
		if(useZone && (!parseFloat(lines[4], &centerX) || !parseFloat(lines[5], &centerY) ||
		   !parseFloat(lines[6], &radius) || radius <= 0.0f)){
			writeError(requestId, "invalid list_page zone");
			return;
		}
		std::vector<ObjectInst*> matches;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(!isAgentSceneInstance(inst) || inst->m_isDeleted || inst->m_numChildren > 0 ||
			   !agentInstanceInZone(inst, useZone, centerX, centerY, radius * radius))
				continue;
			matches.push_back(inst);
		}
		std::sort(matches.begin(), matches.end(), [](ObjectInst *a, ObjectInst *b) {
			return a->m_id < b->m_id;
		});
		std::string body = "\"items\":[";
		int end = std::min((int)matches.size(), offset + limit);
		for(int i = offset; i < end; i++){
			if(i > offset) body += ',';
			body += agentInstanceJson(matches[i], false);
		}
		body += "],\"page\":{\"offset\":" + std::to_string(offset) +
			",\"limit\":" + std::to_string(limit) + ",\"returned\":" +
			std::to_string(std::max(0, end - offset)) + ",\"total\":" +
			std::to_string(matches.size()) + ",\"next_offset\":";
		body += end < (int)matches.size() ? std::to_string(end) : "null";
		body += "}";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "inspect_zone_page"){
		if(lines.size() < 7){
			writeError(requestId, "inspect_zone_page requires x, y, radius, offset and limit");
			return;
		}
		float centerX, centerY, radius;
		int offset, limit;
		if(!parseFloat(lines[2], &centerX) || !parseFloat(lines[3], &centerY) ||
		   !parseFloat(lines[4], &radius) || !parseInt(lines[5], &offset) ||
		   !parseInt(lines[6], &limit) || radius <= 0.0f){
			writeError(requestId, "invalid inspect_zone_page field");
			return;
		}
		offset = std::max(0, offset);
		limit = std::max(1, std::min(limit, 256));
		std::vector<ObjectInst*> matches;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst->m_isDeleted || inst->m_numChildren > 0 ||
			   !agentInstanceInZone(inst, true, centerX, centerY, radius * radius))
				continue;
			matches.push_back(inst);
		}
		std::sort(matches.begin(), matches.end(), [](ObjectInst *a, ObjectInst *b) {
			return a->m_id < b->m_id;
		});
		std::string body = "\"items\":[";
		int end = std::min((int)matches.size(), offset + limit);
		for(int i = offset; i < end; i++){
			if(i > offset) body += ',';
			body += agentInstanceJson(matches[i], true);
		}
		body += "],\"page\":{\"offset\":" + std::to_string(offset) +
			",\"limit\":" + std::to_string(limit) + ",\"returned\":" +
			std::to_string(std::max(0, end - offset)) + ",\"total\":" +
			std::to_string(matches.size()) + ",\"next_offset\":";
		body += end < (int)matches.size() ? std::to_string(end) : "null";
		body += "}";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "list"){
		std::string body = "\"instances\":[";
		int count = 0;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(!isAgentSceneInstance(inst) || inst->m_isDeleted)
				continue;
			ObjectDef *object = GetObjectDef(inst->m_objectId);
			if(count++) body += ',';
			char item[640];
			rw::V3d degrees = GetObjectRotationDegrees(inst->m_rotation);
			snprintf(item, sizeof(item),
				"{\"instance_id\":%d,\"model_id\":%d,\"name\":\"%s\",\"position\":[%.4f,%.4f,%.4f],\"rotation\":[%.3f,%.3f,%.3f]}",
				inst->m_id, inst->m_objectId, jsonEscape(object ? object->m_name : "").c_str(),
				inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
				degrees.x, degrees.y, degrees.z);
			body += item;
		}
		body += "]";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "scene_bounds"){
		bool found = false;
		float minX = 0.0f, minY = 0.0f, minZ = 0.0f;
		float maxX = 0.0f, maxY = 0.0f, maxZ = 0.0f;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(!isAgentSceneInstance(inst) || inst->m_isDeleted || inst->m_numChildren > 0)
				continue;
			if(!found){
				minX = maxX = inst->m_translation.x;
				minY = maxY = inst->m_translation.y;
				minZ = maxZ = inst->m_translation.z;
				found = true;
			}else{
				minX = std::min(minX, inst->m_translation.x);
				minY = std::min(minY, inst->m_translation.y);
				minZ = std::min(minZ, inst->m_translation.z);
				maxX = std::max(maxX, inst->m_translation.x);
				maxY = std::max(maxY, inst->m_translation.y);
				maxZ = std::max(maxZ, inst->m_translation.z);
			}
		}
		if(!found){
			writeError(requestId, "the active agent scene is empty");
			return;
		}
		char body[512];
		snprintf(body, sizeof(body),
			"\"bounds\":{\"min\":[%.4f,%.4f,%.4f],\"max\":[%.4f,%.4f,%.4f],\"center\":[%.4f,%.4f,%.4f]},\"instance_count\":%d",
			minX, minY, minZ, maxX, maxY, maxZ,
			(minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f,
			countAgentInstances(false));
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "inspect_zone"){
		if(lines.size() < 5){
			writeError(requestId, "inspect_zone requires center x, y and radius");
			return;
		}
		float centerX, centerY, radius;
		if(!parseFloat(lines[2], &centerX) || !parseFloat(lines[3], &centerY) ||
		   !parseFloat(lines[4], &radius) || radius <= 0.0f){
			writeError(requestId, "invalid inspect_zone field");
			return;
		}
		int limit = 500;
		if(lines.size() > 5) parseInt(lines[5], &limit);
		limit = std::max(1, std::min(limit, 2000));
		float radiusSquared = radius * radius;
		std::string body = "\"objects\":[";
		int count = 0;
		for(CPtrNode *p = instances.first; p && count < limit; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(inst->m_isDeleted || inst->m_numChildren > 0)
				continue;
			float dx = inst->m_translation.x - centerX;
			float dy = inst->m_translation.y - centerY;
			if(dx * dx + dy * dy > radiusSquared)
				continue;
			ObjectDef *object = GetObjectDef(inst->m_objectId);
			if(count++) body += ',';
			char item[640];
			snprintf(item, sizeof(item),
				"{\"instance_id\":%d,\"model_id\":%d,\"name\":\"%s\",\"position\":[%.3f,%.3f,%.3f],\"agent_scene\":%s}",
				inst->m_id, inst->m_objectId, jsonEscape(object ? object->m_name : "").c_str(),
				inst->m_translation.x, inst->m_translation.y, inst->m_translation.z,
				isAgentSceneInstance(inst) ? "true" : "false");
			body += item;
		}
		body += "],\"truncated\":";
		body += count >= limit ? "true" : "false";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "validate_zone"){
		if(lines.size() < 7){
			writeError(requestId, "validate_zone requires x, y, radius, offset and limit");
			return;
		}
		float centerX, centerY, radius;
		int offset, limit;
		if(!parseFloat(lines[2], &centerX) || !parseFloat(lines[3], &centerY) ||
		   !parseFloat(lines[4], &radius) || !parseInt(lines[5], &offset) ||
		   !parseInt(lines[6], &limit) || radius <= 0.0f){
			writeError(requestId, "invalid validate_zone field");
			return;
		}
		offset = std::max(0, offset);
		limit = std::max(1, std::min(limit, 256));
		std::vector<ObjectInst*> matches;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(!isAgentSceneInstance(inst) || inst->m_isDeleted || inst->m_numChildren > 0 ||
			   !agentInstanceInZone(inst, true, centerX, centerY, radius * radius))
				continue;
			matches.push_back(inst);
		}
		std::sort(matches.begin(), matches.end(), [](ObjectInst *a, ObjectInst *b) {
			return a->m_id < b->m_id;
		});
		std::string body = "\"items\":[";
		int end = std::min((int)matches.size(), offset + limit);
		int issueCount = 0;
		for(int i = offset; i < end; i++){
			ObjectInst *inst = matches[i];
			AgentPlacementAnalysis analysis;
			AgentSupportContract contract = agentSupportContract(inst->m_objectId);
			bool analyzed = analyzeAgentPlacement(inst->m_objectId, inst->m_translation,
				inst->m_rotation, contract.sampleGridSize, &analysis);
			AgentSupportEvaluation evaluation = evaluateAgentSupport(
				inst->m_objectId, analyzed, analysis);
			float relief = analyzed ? analysis.maxRequiredZ - analysis.minRequiredZ : 0.0f;
			if(!evaluation.valid) issueCount++;
			if(i > offset) body += ',';
			char item[384];
			snprintf(item, sizeof(item),
				"{\"instance_id\":%d,\"valid\":%s,\"floating\":%s,\"embedded\":%s,"
				"\"unsupported_slope\":%s,\"min_clearance\":%.3f,\"max_clearance\":%.3f,"
				"\"support_relief\":%.3f}", inst->m_id, evaluation.valid ? "true" : "false",
				evaluation.floating ? "true" : "false",
				evaluation.embedded ? "true" : "false",
				evaluation.unsupportedRelief ? "true" : "false", analyzed ? analysis.minClearance : 0.0f,
				analyzed ? analysis.maxClearance : 0.0f, relief);
			body += item;
		}
		body += "],\"issue_count_in_page\":" + std::to_string(issueCount) +
			",\"page\":{\"offset\":" + std::to_string(offset) +
			",\"limit\":" + std::to_string(limit) + ",\"returned\":" +
			std::to_string(std::max(0, end - offset)) + ",\"total\":" +
			std::to_string(matches.size()) + ",\"next_offset\":";
		body += end < (int)matches.size() ? std::to_string(end) : "null";
		body += "}";
		writeResponse(requestId, true, body);
		return;
	}
	if(command == "validate"){
		std::vector<ObjectInst*> sceneInstances;
		for(CPtrNode *p = instances.first; p; p = p->next){
			ObjectInst *inst = (ObjectInst*)p->item;
			if(isAgentSceneInstance(inst) && !inst->m_isDeleted && inst->m_numChildren == 0)
				sceneInstances.push_back(inst);
		}
		std::string supportIssues = "\"support_issues\":[";
		int numSupportIssues = 0;
		for(size_t i = 0; i < sceneInstances.size(); i++){
			ObjectInst *inst = sceneInstances[i];
			AgentPlacementAnalysis analysis;
			AgentSupportContract contract = agentSupportContract(inst->m_objectId);
			bool analyzed = analyzeAgentPlacement(inst->m_objectId, inst->m_translation,
				inst->m_rotation, contract.sampleGridSize, &analysis);
			AgentSupportEvaluation evaluation = evaluateAgentSupport(
				inst->m_objectId, analyzed, analysis);
			if(evaluation.valid)
				continue;
			if(numSupportIssues++) supportIssues += ',';
			char issue[640];
			snprintf(issue, sizeof(issue),
				"{\"instance_id\":%d,\"model_id\":%d,\"profile\":\"%s\","
				"\"floating\":%s,\"embedded\":%s,\"unsupported_slope\":%s,"
				"\"min_clearance\":%.4f,\"max_clearance\":%.4f,\"support_relief\":%.4f,"
				"\"missing_samples\":%d,\"support_contract\":\"terrain-support-v1\"}",
				inst->m_id, inst->m_objectId, contract.profile,
				evaluation.floating ? "true" : "false",
				evaluation.embedded ? "true" : "false",
				evaluation.unsupportedRelief ? "true" : "false",
				analyzed ? analysis.minClearance : 0.0f,
				analyzed ? analysis.maxClearance : 0.0f,
				analyzed ? analysis.maxRequiredZ - analysis.minRequiredZ : 0.0f,
				analyzed ? analysis.missingSamples : contract.sampleGridSize * contract.sampleGridSize);
			supportIssues += issue;
		}
		supportIssues += "]";

		std::string overlaps = "\"building_overlaps\":[";
		int numOverlaps = 0;
		for(size_t i = 0; i < sceneInstances.size(); i++){
			CRect a = sceneInstances[i]->GetBoundRect();
			float aw = a.right - a.left, ah = a.top - a.bottom;
			if(aw < 3.0f || ah < 3.0f) continue;
			for(size_t j = i + 1; j < sceneInstances.size(); j++){
				CRect b = sceneInstances[j]->GetBoundRect();
				float bw = b.right - b.left, bh = b.top - b.bottom;
				if(bw < 3.0f || bh < 3.0f) continue;
				float overlapX = std::min(a.right, b.right) - std::max(a.left, b.left);
				float overlapY = std::min(a.top, b.top) - std::max(a.bottom, b.bottom);
				if(overlapX <= 1.0f || overlapY <= 1.0f || overlapX * overlapY <= 4.0f)
					continue;
				if(numOverlaps++) overlaps += ',';
				char issue[192];
				snprintf(issue, sizeof(issue),
					"{\"a\":%d,\"b\":%d,\"overlap_area\":%.3f}",
					sceneInstances[i]->m_id, sceneInstances[j]->m_id, overlapX * overlapY);
				overlaps += issue;
			}
		}
		overlaps += "]";

		std::string duplicates = "\"exact_duplicates\":[";
		std::string propOverlaps = "\"prop_overlaps\":[";
		int numDuplicates = 0, numPropOverlaps = 0;
		for(size_t i = 0; i < sceneInstances.size(); i++){
			ObjectInst *aInst = sceneInstances[i];
			ObjectDef *aObj = GetObjectDef(aInst->m_objectId);
			if(aObj == nil || aObj->m_colModel == nil) continue;
			CRect a = aInst->GetBoundRect();
			float aMinZ = aInst->m_translation.z + aObj->m_colModel->boundingBox.min.z;
			float aMaxZ = aInst->m_translation.z + aObj->m_colModel->boundingBox.max.z;
			for(size_t j = i + 1; j < sceneInstances.size(); j++){
				ObjectInst *bInst = sceneInstances[j];
				ObjectDef *bObj = GetObjectDef(bInst->m_objectId);
				if(bObj == nil || bObj->m_colModel == nil) continue;
				float centerDistance = rw::length(rw::sub(aInst->m_translation, bInst->m_translation));
				bool duplicate = aInst->m_objectId == bInst->m_objectId && centerDistance < 0.01f &&
					GetQuaternionSimilarity(aInst->m_rotation, bInst->m_rotation) > 0.9999f;
				if(duplicate){
					if(numDuplicates++) duplicates += ',';
					duplicates += "{\"a\":" + std::to_string(aInst->m_id) +
						",\"b\":" + std::to_string(bInst->m_id) +
						",\"model_id\":" + std::to_string(aInst->m_objectId) + "}";
					continue;
				}
				CRect b = bInst->GetBoundRect();
				float overlapX = std::min(a.right, b.right) - std::max(a.left, b.left);
				float overlapY = std::min(a.top, b.top) - std::max(a.bottom, b.bottom);
				float bMinZ = bInst->m_translation.z + bObj->m_colModel->boundingBox.min.z;
				float bMaxZ = bInst->m_translation.z + bObj->m_colModel->boundingBox.max.z;
				float overlapZ = std::min(aMaxZ, bMaxZ) - std::max(aMinZ, bMinZ);
				float aw = std::max(0.001f, a.right - a.left);
				float ah = std::max(0.001f, a.top - a.bottom);
				float bw = std::max(0.001f, b.right - b.left);
				float bh = std::max(0.001f, b.top - b.bottom);
				bool bothBuildings = aw >= 3.0f && ah >= 3.0f && bw >= 3.0f && bh >= 3.0f;
				float footprintRatio = overlapX > 0.0f && overlapY > 0.0f ?
					(overlapX * overlapY) / std::min(aw * ah, bw * bh) : 0.0f;
				if(bothBuildings || overlapZ <= 0.05f || footprintRatio < 0.35f)
					continue;
				if(numPropOverlaps++) propOverlaps += ',';
				char issue[256];
				snprintf(issue, sizeof(issue),
					"{\"a\":%d,\"b\":%d,\"footprint_overlap_ratio\":%.3f,\"vertical_overlap\":%.3f}",
					aInst->m_id, bInst->m_id, footprintRatio, overlapZ);
				propOverlaps += issue;
			}
		}
		duplicates += "]";
		propOverlaps += "]";
		writeResponse(requestId, true, std::string("\"valid\":") +
			(numSupportIssues == 0 && numOverlaps == 0 && numDuplicates == 0 &&
			 numPropOverlaps == 0 ? "true" : "false") +
			",\"instance_count\":" + std::to_string(sceneInstances.size()) + "," +
			supportIssues + "," + overlaps + "," + duplicates + "," + propOverlaps);
		return;
	}
	if(command == "camera"){
		if(lines.size() < 9){
			writeError(requestId, "camera requires position xyz, target xyz and fov");
			return;
		}
		float values[7];
		for(int i = 0; i < 7; i++)
			if(!parseFloat(lines[i + 2], &values[i])){
				writeError(requestId, "invalid camera field");
				return;
			}
		rw::V3d position = { values[0], values[1], values[2] };
		rw::V3d target = { values[3], values[4], values[5] };
		rw::V3d direction = rw::sub(target, position);
		if(rw::length(direction) < 0.001f){
			writeError(requestId, "camera position and target must differ");
			return;
		}
		AgentCameraPose pose;
		pose.position = position;
		pose.target = target;
		// Process() applies zero-input orbit updates every frame. Reset both up
		// vectors so a previously invalid camera cannot poison the requested view.
		pose.up = fabsf(rw::normalize(direction).z) > 0.995f ?
			rw::V3d{ 0.0f, 1.0f, 0.0f } : rw::V3d{ 0.0f, 0.0f, 1.0f };
		pose.fov = values[6];
		applyCameraPose(pose);
		gAgentCameraRevision++;
		writeResponse(requestId, true, std::string("\"result\":\"camera_set\",\"camera_revision\":") +
			std::to_string(gAgentCameraRevision));
		return;
	}
	if(command == "capture"){
		if(lines.size() < 3 || lines[2].empty()){
			writeError(requestId, "capture requires a PNG path");
			return;
		}
		strncpy(gAgentCapturePath, lines[2].c_str(), sizeof(gAgentCapturePath) - 1);
		gAgentCapturePath[sizeof(gAgentCapturePath) - 1] = '\0';
		gAgentCaptureLabel = lines.size() > 3 ? lines[3] : "current";
		gAgentCapturePose = currentCameraPose();
		gAgentCaptureCameraRevision = gAgentCameraRevision;
		gAgentCaptureRestore = false;
		gAgentPendingRequestId = requestId;
		gAgentCapturePending = true;
		return;
	}
	if(command == "capture_pose"){
		if(lines.size() < 14 || lines[2].empty()){
			writeError(requestId, "capture_pose requires path, label, position, target, up and fov");
			return;
		}
		AgentCameraPose requested;
		if(!parseFloat(lines[4], &requested.position.x) ||
		   !parseFloat(lines[5], &requested.position.y) ||
		   !parseFloat(lines[6], &requested.position.z) ||
		   !parseFloat(lines[7], &requested.target.x) ||
		   !parseFloat(lines[8], &requested.target.y) ||
		   !parseFloat(lines[9], &requested.target.z) ||
		   !parseFloat(lines[10], &requested.up.x) ||
		   !parseFloat(lines[11], &requested.up.y) ||
		   !parseFloat(lines[12], &requested.up.z) ||
		   !parseFloat(lines[13], &requested.fov) ||
		   rw::length(rw::sub(requested.target, requested.position)) < 0.001f ||
		   rw::length(requested.up) < 0.001f ||
		   rw::length(rw::cross(rw::normalize(rw::sub(requested.target, requested.position)),
		                        rw::normalize(requested.up))) < 0.001f){
			writeError(requestId, "invalid capture_pose field");
			return;
		}
		if(lines.size() > 14){
			int expected;
			if(!parseInt(lines[14], &expected) || (uint32)expected != gAgentCameraRevision){
				writeResponse(requestId, false, std::string("\"error\":\"camera revision changed\","
					"\"camera_revision\":") + std::to_string(gAgentCameraRevision));
				return;
			}
		}
		strncpy(gAgentCapturePath, lines[2].c_str(), sizeof(gAgentCapturePath) - 1);
		gAgentCapturePath[sizeof(gAgentCapturePath) - 1] = '\0';
		gAgentCaptureLabel = lines[3];
		gAgentCaptureRestorePose = currentCameraPose();
		gAgentCaptureRestore = true;
		applyCameraPose(requested);
		gAgentCameraRevision++;
		gAgentCapturePose = currentCameraPose();
		gAgentCaptureCameraRevision = gAgentCameraRevision;
		gAgentPendingRequestId = requestId;
		gAgentCapturePending = true;
		return;
	}
	if(command == "save"){
		if(gAgentSceneLogicalPath[0] == '\0'){
			writeError(requestId, "no active agent scene");
			return;
		}
		if(gAgentSessionActive){
			writeError(requestId, "commit or rollback the scratch session before saving");
			return;
		}
		FileLoader::BinaryIplSaveResult result = FileLoader::SaveScene(gAgentSceneLogicalPath);
		if(result.numFailedFiles || result.numFailedImages){
			writeError(requestId, "Ariane reported a save failure");
			return;
		}
		writeResponse(requestId, true,
			std::string("\"physical_path\":\"") + jsonEscape(gAgentScenePhysicalPath) + "\"");
		return;
	}
	writeError(requestId, "unknown command");
}

void
AgentBridgeUpdate(void)
{
	if(!gAgentBridgeInitialized)
		initializeBridge();
	trackLiveCamera();
	if(!gAgentBridgeEnabled || gAgentCapturePending)
		return;
	std::vector<std::string> lines;
	if(readRequest(lines))
		handleRequest(lines);
}

void
AgentBridgeCaptureAfterWorldRender(void)
{
	if(!gAgentCapturePending || Scene.camera == nil || Scene.camera->frameBuffer == nil)
		return;
	rw::Image *image = Scene.camera->frameBuffer->toImage();
	bool captured = image != nil;
	if(image != nil){
		rw::writePNG(image, gAgentCapturePath);
		image->destroy();
	}
	bool restored = false;
	if(gAgentCaptureRestore){
		// Input is processed before the bridge each frame, and capture completes in
		// the same frame. A different revision here nevertheless fails closed if a
		// future renderer makes capture asynchronous.
		if(gAgentCameraRevision == gAgentCaptureCameraRevision){
			applyCameraPose(gAgentCaptureRestorePose);
			gAgentCameraRevision++;
			restored = true;
		}
	}
	std::string body = std::string("\"path\":\"") + jsonEscape(gAgentCapturePath) +
		"\",\"label\":\"" + jsonEscape(gAgentCaptureLabel.c_str()) +
		"\",\"actual_pose\":" + cameraPoseJson(gAgentCapturePose) +
		",\"capture_camera_revision\":" + std::to_string(gAgentCaptureCameraRevision) +
		",\"camera_revision\":" + std::to_string(gAgentCameraRevision) +
		",\"restored\":" + (restored ? "true" : "false");
	if(captured)
		writeResponse(gAgentPendingRequestId, true, body);
	else
		writeResponse(gAgentPendingRequestId, false,
			std::string("\"error\":\"could not read the camera framebuffer\",") + body);
	gAgentCapturePending = false;
	gAgentCaptureRestore = false;
	gAgentCaptureLabel.clear();
	gAgentPendingRequestId.clear();
}
