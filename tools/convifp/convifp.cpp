#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <rw.h>
#include <args.h>

using namespace rw;

char *argv0;

enum
{
	HAS_ROT = 1,
	HAS_TRANS = 2,
	HAS_SCALE = 4
};

struct KeyFrame
{
	Quat rotation;
	float time;
	V3d translation;
	V3d scale;
};

struct AnimNode
{
	char name[24];
	int nodeId;
	int unknownId;
	int type;
	int numKeyFrames;
	KeyFrame *keyFrames;
};

struct AnimHierarchy
{
	char name[24];
	int unknown;
	int numNodes;
	AnimNode *nodes;
};

struct AnimPackage
{
	char name[24];
	int numAnimations;
	AnimHierarchy *animations;
};

struct AnimFile
{
	int numPackages;
	AnimPackage *packages;
};

int rounded(int n) { return (n + 3) & ~3; }

struct IfpHeader {
	char ident[4];
	uint32 size;
};

AnimFile*
LoadIFP(FILE *f)
{
	#define ROUNDSIZE(x) if((x) & 3) (x) += 4 - ((x)&3)
	IfpHeader anpk, info, name, dgan, cpan, anim;
	char buf[256];
	int i, j, k, l;

	AnimFile *anfile = new AnimFile;

	fread(&anpk, 1, sizeof(IfpHeader), f);
	if(strncmp(anpk.ident, "ANLF", 4) == 0){
		ROUNDSIZE(anpk.size);
		fread(buf, 1, anpk.size, f);
		anfile->numPackages = *(int*)buf;
	}else if(strncmp(anpk.ident, "ANPK", 4) == 0){
		fseek(f, -8, 1);
		anfile->numPackages = 1;
	}
	anfile->packages = new AnimPackage[anfile->numPackages];

	for(i = 0; i < anfile->numPackages; i++){
		// block name
		fread(&anpk, 1, sizeof(IfpHeader), f);
		ROUNDSIZE(anpk.size);
		fread(&info, 1, sizeof(IfpHeader), f);
		ROUNDSIZE(info.size);
		fread(buf, 1, info.size, f);
		AnimPackage *animPack = &anfile->packages[i];
		strncpy(animPack->name, buf+4, 24);
		animPack->numAnimations = *(int*)buf;
		animPack->animations = new AnimHierarchy[animPack->numAnimations];

		for(j = 0; j < animPack->numAnimations; j++){
			AnimHierarchy *hier = &animPack->animations[j];

			// animation name
			fread(&name, 1, sizeof(IfpHeader), f);
			ROUNDSIZE(name.size);
			fread(buf, 1, name.size, f);
			strncpy(hier->name, buf, 24);

			// DG info has number of nodes/sequences
			fread(&dgan, 1, sizeof(IfpHeader), f);
			ROUNDSIZE(dgan.size);
			fread(&info, 1, sizeof(IfpHeader), f);
			ROUNDSIZE(info.size);
			fread(buf, 1, info.size, f);
			hier->numNodes = *(int*)buf;
			hier->unknown = *(int*)(buf+4);
			hier->nodes = new AnimNode[hier->numNodes];

			AnimNode *node = hier->nodes;
			for(k = 0; k < hier->numNodes; k++, node++){
				// Each node has a name and key frames
				fread(&cpan, 1, sizeof(IfpHeader), f);
				ROUNDSIZE(dgan.size);
				fread(&anim, 1, sizeof(IfpHeader), f);
				ROUNDSIZE(anim.size);
				fread(buf, 1, anim.size, f);
				node->numKeyFrames = *(int*)(buf+28);
				node->nodeId = *(int*)(buf+40);
				if(anim.size > 44)
					node->unknownId = *(int*)(buf+44);
				else
					node->unknownId = -2;	// hack to mark write out as 44 bytes

				strncpy(node->name, buf, 24);
				if(node->numKeyFrames == 0)
					continue;
				node->keyFrames = new KeyFrame[node->numKeyFrames];

				fread(&info, 1, sizeof(IfpHeader), f);
				if(strncmp(info.ident, "KR00", 4) == 0)
					node->type = HAS_ROT;
				else if(strncmp(info.ident, "KRT0", 4) == 0)
					node->type = HAS_ROT | HAS_TRANS;
				else if(strncmp(info.ident, "KRTS", 4) == 0)
					node->type = HAS_ROT | HAS_TRANS | HAS_SCALE;
				else
					assert(0);

				KeyFrame *kf = node->keyFrames;
				for(l = 0; l < node->numKeyFrames; l++, kf++){
					if(node->type & HAS_ROT){
						fread(&kf->rotation, 1, 16, f);
						kf->rotation.x = -kf->rotation.x;
						kf->rotation.y = -kf->rotation.y;
						kf->rotation.z = -kf->rotation.z;
					}
					if(node->type & HAS_TRANS)
						fread(&kf->translation, 1, 12, f);
					if(node->type & HAS_SCALE)
						fread(&kf->scale, 1, 12, f);
					fread(&kf->time, 1, 4, f);
				}
			}
		}
	}

	return anfile;
}

// size of CPAN chunk
uint32
GetNodeSize(AnimNode *node)
{
	uint32 sz;

	// ANIM
	if(node->unknownId == -2)	// hack
		sz = 8 + 44;
	else
		sz = 8 + 48;

	if(node->numKeyFrames == 0)
		return sz;

	// K...
	sz += 8;
	sz += 4*node->numKeyFrames;	// time
	if(node->type & HAS_ROT)
		sz += 16*node->numKeyFrames;
	if(node->type & HAS_TRANS)
		sz += 12*node->numKeyFrames;
	if(node->type & HAS_SCALE)
		sz += 12*node->numKeyFrames;
	return sz;
}

// size of DGAN chunk
uint32
GetAnimSize(AnimHierarchy *hier)
{
	int i;
	uint32 sz;

	//  INFO
	sz = 8 + 8;
	for(i = 0; i < hier->numNodes; i++){
		// CPAN
		sz += 8 + GetNodeSize(&hier->nodes[i]);
	}
	return sz;
}

void
writestr(const char *s, FILE *f)
{
	char buf[64];
	memset(buf, 0, 64);
	strncpy(buf, s, 64);
	fwrite(buf, 1, rounded(strlen(s)+1), f);
}

void
WriteIFP(AnimPackage *pack, FILE *f)
{
	int i, j, k;
	uint32 sz;
	static IfpHeader anpk = { { 'A', 'N', 'P', 'K' }, 0 };
	static IfpHeader info = { { 'I', 'N', 'F', 'O' }, 0 };
	static IfpHeader name = { { 'N', 'A', 'M', 'E' }, 0 };
	static IfpHeader dgan = { { 'D', 'G', 'A', 'N' }, 0 };
	static IfpHeader cpan = { { 'C', 'P', 'A', 'N' }, 0 };
	static IfpHeader anim = { { 'A', 'N', 'I', 'M' }, 0 };
	static IfpHeader kf;
	uint8 buf[256];

	// INFO
	sz = 8 + 4 + rounded(strlen(pack->name)+1);
	for(i = 0; i < pack->numAnimations; i++){
		// NAME
		sz += 8 + rounded(strlen(pack->animations[i].name)+1);
		// DGAN
		sz += 8 + GetAnimSize(&pack->animations[i]);
	}

	anpk.size = sz;
	fwrite(&anpk, 1, sizeof(IfpHeader), f);
	info.size = 4 + strlen(pack->name)+1;
	fwrite(&info, 1, sizeof(IfpHeader), f);
	fwrite(&pack->numAnimations, 1, 4, f);
	writestr(pack->name, f);

	for(i = 0; i < pack->numAnimations; i++){
		AnimHierarchy *hier = &pack->animations[i];
		name.size = strlen(hier->name)+1;
		fwrite(&name, 1, sizeof(IfpHeader), f);
		writestr(hier->name, f);

		dgan.size = GetAnimSize(hier);
		fwrite(&dgan, 1, sizeof(IfpHeader), f);

		info.size = 8;
		fwrite(&info, 1, sizeof(IfpHeader), f);
		*(int*)(buf) = hier->numNodes;
		*(int*)(buf+4) = hier->unknown;	// unused
		fwrite(buf, 1, 8, f);

		for(j = 0; j < hier->numNodes; j++){
			AnimNode *node = &hier->nodes[j];
			cpan.size = GetNodeSize(node);
			fwrite(&cpan, 1, sizeof(IfpHeader), f);

			if(node->unknownId == -2)	// hack
				anim.size = 44;
			else
				anim.size = 48;
			fwrite(&anim, 1, sizeof(IfpHeader), f);
			memset(buf, 0, 48);
			strncpy((char*)buf, node->name, 24);
			*(int*)(buf+24) = 0;	// unk unused
			*(int*)(buf+28) = node->numKeyFrames;
			*(int*)(buf+32) = 0;	// unused. always 0. first key frame?
			*(int*)(buf+36) = node->numKeyFrames-1;	// unused
			*(int*)(buf+40) = node->nodeId;
			*(int*)(buf+44) = node->unknownId;
			fwrite(buf, 1, anim.size, f);

			if(node->numKeyFrames == 0)
				continue;

			kf.ident[0] = 'K';
			kf.size = 4;
			if(node->type & HAS_ROT){
				kf.ident[1] = 'R';
				kf.size += 16;
			}else
				kf.ident[1] = '0';
			if(node->type & HAS_TRANS){
				kf.ident[2] = 'T';
				kf.size += 12;
			}else
				kf.ident[2] = '0';
			if(node->type & HAS_SCALE){
				kf.ident[3] = 'S';
				kf.size += 12;
			}else
				kf.ident[3] = '0';
			kf.size *= node->numKeyFrames;
			fwrite(&kf, 1, sizeof(IfpHeader), f);

			KeyFrame *kf = node->keyFrames;
			for(k = 0; k < node->numKeyFrames; k++, kf++){
				if(node->type & HAS_ROT){
					kf->rotation.x = -kf->rotation.x;
					kf->rotation.y = -kf->rotation.y;
					kf->rotation.z = -kf->rotation.z;
					fwrite(&kf->rotation, 1, 16, f);
					kf->rotation.x = -kf->rotation.x;
					kf->rotation.y = -kf->rotation.y;
					kf->rotation.z = -kf->rotation.z;
				}
				if(node->type & HAS_TRANS)
					fwrite(&kf->translation, 1, 12, f);
				if(node->type & HAS_SCALE)
					fwrite(&kf->scale, 1, 12, f);
				fwrite(&kf->time, 1, 4, f);
			}

		}
	}
}

void
DumpAnimPackage(AnimPackage *pack)
{
	int i, j, k;

	printf("\"%s\" %d {\n", pack->name, pack->numAnimations);
	for(i = 0; i < pack->numAnimations; i++){
		AnimHierarchy *hier = &pack->animations[i];
		printf("  \"%s\" %d {\n", hier->name, hier->numNodes);
		for(j = 0; j < hier->numNodes; j++){
			AnimNode *node = &hier->nodes[j];
			printf("    \"%s\" %d %d %d {\n", node->name, node->nodeId, node->type, node->numKeyFrames);
			for(k = 0; k < node->numKeyFrames; k++){
				KeyFrame *kf = &node->keyFrames[k];
				printf("      %g", kf->time);
				if(node->type & HAS_ROT)
					printf("  %g %g %g %g", kf->rotation.x, kf->rotation.y, kf->rotation.z, kf->rotation.w);
				if(node->type & HAS_TRANS)
					printf("  %g %g %g", kf->translation.x, kf->translation.y, kf->translation.z);
				if(node->type & HAS_SCALE)
					printf("  %g %g %g", kf->scale.x, kf->scale.y, kf->scale.z);
				printf("\n");
			}
			printf("    }\n");
		}
		printf("  }\n");
	}
	printf("}\n");
}

int
FindAnimation(AnimPackage *pack, const char *name)
{
	int i;
	for(i = 0; i < pack->numAnimations; i++)
		if(strcmp_ci(pack->animations[i].name, name) == 0)
			return i;
	return -1;
}

// Merge pack1 into pack2
void
MergePackages(AnimPackage *pack1, AnimPackage *pack2)
{
	int i, x;

	for(i = 0; i < pack2->numAnimations; i++){
		x = FindAnimation(pack1, pack2->animations[i].name);
		if(x >= 0)
			pack1->animations[x] = pack2->animations[i];
		else
			printf("couldn't find anim %s\n", pack2->animations[i].name);
	}
}

static const char *inputFiles[] = {
	"ped_ps2.ifp",
	"ped_pc.ifp",
	"baseball.ifp",
	"biked.ifp",
	"bikeh.ifp",
	"bikes.ifp",
	"bikev.ifp",
	"buddy.ifp",
	"chainsaw.ifp",
	"coach.ifp",
	"colt45.ifp",
	"flame.ifp",
	"grenade.ifp",
	"knife.ifp",
	"lance.ifp",
	"m60.ifp",
	"medic.ifp",
	"playidles.ifp",
	"python.ifp",
	"rifle.ifp",
	"riot.ifp",
	"shotgun.ifp",
	"skate.ifp",
	"sniper.ifp",
	"strip.ifp",
	"sunbathe.ifp",
	"sword.ifp",
	"tec.ifp",
	"uzi.ifp",
	"van.ifp",
	nil
};
AnimPackage *animFiles[200];
int numFiles;

struct {
	const char *output;
	const char *input;
} animNames[] = {
	{ "ARRESTgun", "ARRESTgun" },
	{ "CAR_LB", "CAR_LB" },
	{ "CAR_Lshuffle_RHS", "CAR_Lshuffle_RHS" },
	{ "CAR_Lsit", "CAR_Lsit" },
	{ "CAR_Qjack", "CAR_Qjack" },
	{ "CAR_Qjacked", "CAR_Qjacked" },
	{ "CAR_alignHI_LHS", "CAR_alignHI_LHS" },
	{ "CAR_alignHI_RHS", "CAR_alignHI_RHS" },
	{ "CAR_align_LHS", "CAR_align_LHS" },
	{ "CAR_align_RHS", "CAR_align_RHS" },
	{ "CAR_close_LHS", "CAR_close_LHS" },
	{ "CAR_close_RHS", "CAR_close_RHS" },
	{ "CAR_closedoorL_LHS", "CAR_closedoorL_LHS" },
	{ "CAR_closedoorL_RHS", "CAR_closedoorL_RHS" },
	{ "CAR_closedoor_LHS", "CAR_closedoor_LHS" },
	{ "CAR_closedoor_RHS", "CAR_closedoor_RHS" },
	{ "CAR_crawloutRHS", "CAR_crawloutRHS" },
	{ "CAR_doorlocked_LHS", "CAR_doorlocked_LHS" },
	{ "CAR_doorlocked_RHS", "CAR_doorlocked_RHS" },
	{ "CAR_getinL_LHS", "CAR_getinL_LHS" },
	{ "CAR_getinL_RHS", "CAR_getinL_RHS" },
	{ "CAR_getin_LHS", "CAR_getin_LHS" },
	{ "CAR_getin_RHS", "CAR_getin_RHS" },
	{ "CAR_getoutL_LHS", "CAR_getoutL_LHS" },
	{ "CAR_getoutL_RHS", "CAR_getoutL_RHS" },
	{ "CAR_getout_LHS", "CAR_getout_LHS" },
	{ "CAR_getout_RHS", "CAR_getout_RHS" },
	{ "CAR_open_LHS", "CAR_open_LHS" },
	{ "CAR_open_RHS", "CAR_open_RHS" },
	{ "CAR_pulloutL_LHS", "CAR_pulloutL_LHS" },
	{ "CAR_pulloutL_RHS", "CAR_pulloutL_RHS" },
	{ "CAR_pullout_LHS", "CAR_pullout_LHS" },
	{ "CAR_pullout_RHS", "CAR_pullout_RHS" },
	{ "CAR_rolldoor", "CAR_rolldoor" },
	{ "CAR_rolldoorLO", "CAR_rolldoorLO" },
	{ "CAR_shuffle_RHS", "CAR_shuffle_RHS" },
	{ "CAR_sit", "CAR_sit" },
	{ "CAR_sitp", "CAR_sitp" },
	{ "CAR_sitpLO", "CAR_sitpLO" },
	{ "COACH_inL", "COACH_inL" },
	{ "COACH_inR", "COACH_inR" },
	{ "COACH_opnL", "COACH_opnL" },
	{ "COACH_opnR", "COACH_opnR" },
	{ "COACH_outL", "COACH_outL" },
	{ "CPR", "CPR" },
	{ "DRIVE_BOAT", "DRIVE_BOAT" },
	{ "DRIVE_L", "DRIVE_L" },
	{ "DROWN", "DROWN" },
	{ "DUCK_down", "DUCK_down" },
	{ "DUCK_low", "DUCK_low" },
	{ "Drive_LO_R", "Drive_LO_R" },
	{ "Drive_LO_l", "Drive_LO_l" },
	{ "Drive_R", "Drive_R" },
	{ "Driveby_L", "Driveby_L" },
	{ "Driveby_R", "Driveby_R" },
	{ "EV_dive", "EV_dive" },
	{ "EV_step", "EV_step" },
	{ "FALL_collapse", "FALL_collapse" },
	{ "FALL_fall", "FALL_fall" },
	{ "FALL_glide", "FALL_glide" },
	{ "FALL_land", "FALL_land" },
	{ "FIGHT2IDLE", "FIGHT2IDLE" },
	{ "FIGHTIDLE", "FIGHTIDLE" },
	{ "FIGHTLhook", "FIGHTLhook" },
	{ "FIGHTbodyblow", "FIGHTbodyblow" },
	{ "FIGHThead", "FIGHThead" },
	{ "FIGHTkick", "FIGHTkick" },
	{ "FIGHTknee", "FIGHTknee" },
	{ "FIGHTlngkck", "FIGHTlngkck" },
	{ "FIGHTppunch", "FIGHTppunch" },
	{ "FIGHTpunch", "FIGHTpunch" },
	{ "FIGHTrndhse", "FIGHTrndhse" },
	{ "FIGHTsh_F", "FIGHTsh_F" },
	{ "FLOOR_hit", "FLOOR_hit" },
	{ "FLOOR_hit_f", "FLOOR_hit_f" },
	{ "FUCKU", "FUCKU" },
	{ "Getup", "Getup" },
	{ "Getup_front", "Getup_front" },
	{ "HIT_L", "HIT_L" },
	{ "HIT_R", "HIT_R" },
	{ "HIT_back", "HIT_back" },
	{ "HIT_behind", "HIT_behind" },
	{ "HIT_bodyblow", "HIT_bodyblow" },
	{ "HIT_chest", "HIT_chest" },
	{ "HIT_front", "HIT_front" },
	{ "HIT_head", "HIT_head" },
	{ "HIT_walk", "HIT_walk" },
	{ "HIT_wall", "HIT_wall" },
	{ "IDLE_STANCE", "IDLE_STANCE" },
	{ "IDLE_csaw", "IDLE_csaw" },
	{ "JOG_maleA", "JOG_maleA" },
	{ "JOG_maleB", "JOG_maleB" },
	{ "JUMP_glide", "JUMP_glide" },
	{ "JUMP_land", "JUMP_land" },
	{ "JUMP_launch", "JUMP_launch" },
	{ "KD_left", "KD_left" },
	{ "KD_right", "KD_right" },
	{ "KICK_floor", "KICK_floor" },
	{ "KO_shot_armR", "KO_shot_armR" },
	{ "KO_shot_arml", "KO_shot_arml" },
	{ "KO_shot_face", "KO_shot_face" },
	{ "KO_shot_front", "KO_shot_front" },
	{ "KO_shot_legR", "KO_shot_legR" },
	{ "KO_shot_legl", "KO_shot_legl" },
	{ "KO_shot_stom", "KO_shot_stom" },
	{ "KO_skid_back", "KO_skid_back" },
	{ "KO_skid_front", "KO_skid_front" },
	{ "KO_spin_L", "KO_spin_L" },
	{ "KO_spin_R", "KO_spin_R" },
	{ "PHONE_in", "PHONE_in" },
	{ "PHONE_out", "PHONE_out" },
	{ "PHONE_talk", "PHONE_talk" },
	{ "RBLOCK_Cshoot", "RBLOCK_Cshoot" },
	{ "RUN_civi", "RUN_civi" },
	{ "SHOT_leftP", "SHOT_leftP" },
	{ "SHOT_partial", "SHOT_partial" },
	{ "SHOT_rightP", "SHOT_rightP" },
	{ "SPRINT_civi", "SPRINT_civi" },
	{ "TRAIN_getin", "TRAIN_getin" },
	{ "TRAIN_getout", "TRAIN_getout" },
	{ "TURN_180", "TURN_180" },
	{ "VAN_close", "VAN_close" },
	{ "VAN_closeL", "VAN_closeL" },
	{ "VAN_getin", "VAN_getin" },
	{ "VAN_getinL", "VAN_getinL" },
	{ "VAN_getout", "VAN_getout" },
	{ "VAN_getoutL", "VAN_getoutL" },
	{ "VAN_open", "VAN_open" },
	{ "VAN_openL", "VAN_openL" },
	{ "WALK_shuffle", "WALK_shuffle" },
	{ "WEAPON_AK_body", "RIFLE_fire" },
	{ "WEAPON_AK_rload", "RIFLE_load" },
	{ "WEAPON_bat_h", "WEAPON_bat_h" },
	{ "WEAPON_bat_v", "WEAPON_bat_v" },
	{ "WEAPON_hgun_body", "colt45_fire" },
	{ "WEAPON_hgun_rload", "colt45_reload" },
	{ "WEAPON_pump", "WEAPON_pump" },
	{ "WEAPON_sniper", "WEAPON_sniper" },
	{ "WEAPON_start_throw", "WEAPON_start_throw" },
	{ "WEAPON_throw", "WEAPON_throw" },
	{ "WEAPON_throwu", "WEAPON_throwu" },
	{ "XPRESSscratch", "XPRESSscratch" },
	{ "bomber", "bomber" },
	{ "car_LjackedLHS", "car_LjackedLHS" },
	{ "car_LjackedRHS", "car_LjackedRHS" },
	{ "car_hookertalk", "car_hookertalk" },
	{ "car_jackedLHS", "car_jackedLHS" },
	{ "car_jackedRHS", "car_jackedRHS" },
	{ "handsCOWER", "handsCOWER" },
	{ "handsup", "handsup" },
	{ "idle_armed", "idle_armed" },
	{ "idle_cam", "idle_cam" },
	{ "idle_chat", "idle_chat" },
	{ "idle_csaw", "idle_csaw" },
	{ "idle_hbhb", "idle_hbhb" },
	{ "idle_rocket", "idle_rocket" },
	{ "idle_stance", "idle_stance" },
	{ "idle_taxi", "idle_taxi" },
	{ "idle_tired", "idle_tired" },
	{ "punchR", "punchR" },
	{ "roadcross", "roadcross" },
	{ "run_1armed", "run_1armed" },
	{ "run_armed", "run_armed" },
	{ "run_back", "run_back" },
	{ "run_civi", "run_civi" },
	{ "run_csaw", "run_csaw" },
	{ "run_csaw_back", "run_csaw_back" },
	{ "run_csaw_left", "run_csaw_left" },
	{ "run_csaw_right", "run_csaw_right" },
	{ "run_fatold", "run_fatold" },
	{ "run_gang1", "run_gang1" },
	{ "run_left", "run_left" },
	{ "run_player", "run_player" },
	{ "run_right", "run_right" },
	{ "run_rocket", "run_rocket" },
	{ "run_rocket_back", "run_rocket_back" },
	{ "run_rocket_left", "run_rocket_left" },
	{ "run_rocket_right", "run_rocket_right" },
	{ "run_stop", "run_stop" },
	{ "run_stopR", "run_stopR" },
	{ "skate_idle", "skate_idle" },
	{ "skate_run", "skate_run" },
	{ "skate_sprint", "skate_sprint" },
	{ "sprint_civi", "sprint_civi" },
	{ "sprint_panic", "sprint_panic" },
	{ "walk_back", "walk_back" },
	{ "walk_civi", "walk_civi" },
	{ "walk_csaw", "walk_csaw" },
	{ "walk_csaw_back", "walk_csaw_back" },
	{ "walk_csaw_left", "walk_csaw_left" },
	{ "walk_csaw_right", "walk_csaw_right" },
	{ "walk_fat", "walk_fat" },
	{ "walk_fatold", "walk_fatold" },
	{ "walk_gang1", "walk_gang1" },
	{ "walk_gang2", "walk_gang2" },
	{ "walk_left", "walk_left" },
	{ "walk_old", "walk_old" },
	{ "walk_player", "walk_player" },
	{ "walk_right", "walk_right" },
	{ "walk_rocket", "walk_rocket" },
	{ "walk_rocket_back", "walk_rocket_back" },
	{ "walk_rocket_left", "walk_rocket_left" },
	{ "walk_rocket_right", "walk_rocket_right" },
	{ "walk_start", "walk_start" },
	{ "walk_start_back", "walk_start_back" },
	{ "walk_start_csaw", "walk_start_csaw" },
	{ "walk_start_left", "walk_start_left" },
	{ "walk_start_right", "walk_start_right" },
	{ "walk_start_rocket", "walk_start_rocket" },
	{ "walkst_csaw_back", "walkst_csaw_back" },
	{ "walkst_csaw_left", "walkst_csaw_left" },
	{ "walkst_csaw_right", "walkst_csaw_right" },
	{ "walkst_rocket_back", "walkst_rocket_back" },
	{ "walkst_rocket_left", "walkst_rocket_left" },
	{ "walkst_rocket_right", "walkst_rocket_right" },
	{ "woman_idlestance", "woman_idlestance" },
	{ "woman_run", "woman_run" },
	{ "woman_runpanic", "woman_runpanic" },
	{ "woman_walkbusy", "woman_walkbusy" },
	{ "woman_walknorm", "woman_walknorm" },
	{ "woman_walkold", "woman_walkold" },
	{ "woman_walksexy", "woman_walksexy" },
	{ "woman_walkshop", "woman_walkshop" },
	{ nil, nil }
};

AnimHierarchy*
findAnimInAll(const char *name)
{
	int i, x;
	for(i = 0; i < numFiles; i++){
		x = FindAnimation(animFiles[i], name);
		if(x >= 0)
			return &animFiles[i]->animations[x];
	}
	return nil;
}

void
buildCombinedFile(void)
{
	FILE *f;
	for(numFiles = 0; inputFiles[numFiles]; numFiles++){
		f = fopen(inputFiles[numFiles], "rb");
		if(f == nil){
			printf("botch\n");
			exit(1);
		}
//if(strcmp(inputFiles[numFiles], "van.ifp") == 0)
//DebugBreak();
		animFiles[numFiles] = &LoadIFP(f)->packages[0];
		fclose(f);
	}
	printf("loaded %d files\n", numFiles);

	AnimPackage *pak = new AnimPackage;
	strcpy(pak->name, "ped");
	pak->numAnimations = 0;
	pak->animations = new AnimHierarchy[300];

	int i;
	for(i = 0; animNames[i].output; i++){
		AnimHierarchy *anim = findAnimInAll(animNames[i].input);
		if(anim == nil)
			printf("couldn't find %s\n", animNames[i].input);
		else{
			strcpy(anim->name, animNames[i].output);
			pak->animations[pak->numAnimations++] = *anim;
		}
	}

	f = fopen("ped_miami.ifp", "wb");
	if(f == nil)
		exit(1);
	WriteIFP(pak, f);
	fclose(f);
}

void
usage(void)
{
	exit(1);
}

int
main(int argc, char *argv[])
{
	FILE *f;
	AnimFile *anfile, *anf2;
	char *merge;
	char *output;

	merge = nil;
	output = nil;

//buildCombinedFile();
//return 0;

	ARGBEGIN{
	case 'm':
		merge = EARGF(usage());
		break;
	case 'o':
		output = EARGF(usage());
		break;
	}ARGEND;
	if(argc < 1)
		usage();

	f = fopen(argv[0], "rb");
	if(f == nil)
		exit(1);
	anfile = LoadIFP(f);
	fclose(f);

	if(merge){
		f = fopen(merge, "rb");
		if(f == nil)
			exit(1);
		anf2 = LoadIFP(f);
		fclose(f);
		MergePackages(&anfile->packages[0], &anf2->packages[0]);
	}

	if(output){
		f = fopen(output, "wb");
		if(f == nil)
			exit(1);
		WriteIFP(&anfile->packages[0], f);
		fclose(f);
	}else
		DumpAnimPackage(&anfile->packages[0]);
    
	return 0;
}
