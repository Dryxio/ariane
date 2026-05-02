#define ANIMGROUPS \
	GROUPDEF(man, "man", "ped", 1) \
	  ANIMDEF("walk_civi", 0, 0x162) \
	  ANIMDEF("run_civi", 1, 0x162) \
	  ANIMDEF("sprint_panic", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	  ANIMDEF("walk_start", 4, 0x40) \
	  ANIMDEF("run_stop", 5, 0x44) \
	  ANIMDEF("run_stopR", 6, 0x44) \
	  ANIMDEF("idle_hbhb", 7, 0x12) \
	  ANIMDEF("idle_hbhb", 8, 0x12) \
	  ANIMDEF("idle_tired", 9, 0x2) \
	  ANIMDEF("idle_armed", 10, 0x12) \
	  ANIMDEF("idle_chat", 11, 0x12) \
	  ANIMDEF("idle_taxi", 12, 0x18) \
	  ANIMDEF("KO_shot_front", 13, 0x1050) \
	  ANIMDEF("KO_shot_front", 14, 0x1050) \
	  ANIMDEF("KO_shot_front", 15, 0x1050) \
	  ANIMDEF("KO_shot_front", 16, 0x1050) \
	  ANIMDEF("KO_shot_face", 17, 0x50) \
	  ANIMDEF("KO_shot_stom", 18, 0x1050) \
	  ANIMDEF("KO_shot_arml", 19, 0x1010) \
	  ANIMDEF("KO_shot_armR", 20, 0x1010) \
	  ANIMDEF("KO_shot_legl", 21, 0x50) \
	  ANIMDEF("KO_shot_legR", 22, 0x50) \
	  ANIMDEF("BIKE_fall_off", 23, 0x10) \
	  ANIMDEF("BIKE_fall_off", 24, 0x10) \
	  ANIMDEF("KO_skid_front", 25, 0x10) \
	  ANIMDEF("KO_spin_R", 26, 0x10) \
	  ANIMDEF("KO_skid_back", 27, 0x1010) \
	  ANIMDEF("KO_spin_L", 28, 0x10) \
	  ANIMDEF("SHOT_partial", 29, 0x418) \
	  ANIMDEF("SHOT_leftP", 30, 0x418) \
	  ANIMDEF("SHOT_partial", 31, 0x418) \
	  ANIMDEF("SHOT_rightP", 32, 0x418) \
	  ANIMDEF("HIT_front", 33, 0x58) \
	  ANIMDEF("HIT_L", 34, 0x18) \
	  ANIMDEF("HIT_back", 35, 0x58) \
	  ANIMDEF("HIT_R", 36, 0x18) \
	  ANIMDEF("FLOOR_hit", 37, 0x14) \
	  ANIMDEF("HIT_wall", 38, 0x58) \
	  ANIMDEF("FLOOR_hit_f", 39, 0x1014) \
	  ANIMDEF("HIT_behind", 40, 0x18) \
	  ANIMDEF("FIGHTIDLE", 41, 0x2) \
	  ANIMDEF("FIGHT2IDLE", 42, 0x18) \
	  ANIMDEF("FIGHTsh_F", 43, 0x58) \
	  ANIMDEF("FIGHTppunch", 44, 0x414) \
	  ANIMDEF("l_hook", 45, 0x58) \
	  ANIMDEF("r_hook", 46, 0x58) \
	  ANIMDEF("uppercut", 47, 0x58) \
	  ANIMDEF("headbutt", 48, 0x58) \
	  ANIMDEF("frontkick", 49, 0x58) \
	  ANIMDEF("roundhouse", 50, 0x58) \
	  ANIMDEF("hit_l_hook", 51, 0x58) \
	  ANIMDEF("hit_r_hook", 52, 0x58) \
	  ANIMDEF("hit_uppercut", 53, 0x58) \
	  ANIMDEF("hit_headbutt", 54, 0x58) \
	  ANIMDEF("hit_frontkick", 55, 0x58) \
	  ANIMDEF("hit_rhouse", 56, 0x58) \
	  ANIMDEF("bomber", 57, 0x18) \
	  ANIMDEF("punchR", 58, 0x18) \
	  ANIMDEF("FIGHTppunch", 59, 0x18) \
	  ANIMDEF("KICK_floor", 60, 0x18) \
	  ANIMDEF("WEAPON_throwu", 61, 0x18) \
	  ANIMDEF("FIGHTsh_back", 62, 0x58) \
	  ANIMDEF("car_jackedRHS", 63, 0x14) \
	  ANIMDEF("car_LjackedRHS", 64, 0x14) \
	  ANIMDEF("car_jackedLHS", 65, 0x14) \
	  ANIMDEF("car_LjackedLHS", 66, 0x14) \
	  ANIMDEF("CAR_Qjack", 67, 0x18) \
	  ANIMDEF("CAR_Qjacked", 68, 0x14) \
	  ANIMDEF("CAR_align_LHS", 69, 0x18) \
	  ANIMDEF("CAR_alignHI_LHS", 70, 0x18) \
	  ANIMDEF("CAR_open_LHS", 71, 0x18) \
	  ANIMDEF("CAR_doorlocked_LHS", 72, 0x18) \
	  ANIMDEF("CAR_pullout_LHS", 73, 0x18) \
	  ANIMDEF("CAR_pulloutL_LHS", 74, 0x18) \
	  ANIMDEF("CAR_getin_LHS", 75, 0x18) \
	  ANIMDEF("CAR_getinL_LHS", 76, 0x18) \
	  ANIMDEF("CAR_closedoor_LHS", 77, 0x18) \
	  ANIMDEF("CAR_closedoorL_LHS", 78, 0x18) \
	  ANIMDEF("CAR_rolldoor", 79, 0x18) \
	  ANIMDEF("CAR_rolldoorLO", 80, 0x18) \
	  ANIMDEF("CAR_jumpin_LHS", 81, 0x18) \
	  ANIMDEF("CAR_getout_LHS", 82, 0x18) \
	  ANIMDEF("CAR_getoutL_LHS", 83, 0x18) \
	  ANIMDEF("CAR_close_LHS", 84, 0x18) \
	  ANIMDEF("CAR_align_RHS", 85, 0x18) \
	  ANIMDEF("CAR_alignHI_RHS", 86, 0x18) \
	  ANIMDEF("CAR_open_RHS", 87, 0x18) \
	  ANIMDEF("CAR_doorlocked_RHS", 88, 0x18) \
	  ANIMDEF("CAR_pullout_RHS", 89, 0x18) \
	  ANIMDEF("CAR_pulloutL_RHS", 90, 0x18) \
	  ANIMDEF("CAR_getin_RHS", 91, 0x18) \
	  ANIMDEF("CAR_getinL_RHS", 92, 0x18) \
	  ANIMDEF("CAR_closedoor_RHS", 93, 0x18) \
	  ANIMDEF("CAR_closedoorL_RHS", 94, 0x18) \
	  ANIMDEF("CAR_shuffle_RHS", 95, 0x18) \
	  ANIMDEF("CAR_Lshuffle_RHS", 96, 0x18) \
	  ANIMDEF("CAR_sit", 97, 0x4) \
	  ANIMDEF("CAR_Lsit", 98, 0x4) \
	  ANIMDEF("CAR_sitp", 99, 0x4) \
	  ANIMDEF("CAR_sitpLO", 100, 0x4) \
	  ANIMDEF("DRIVE_L", 101, 0x2014) \
	  ANIMDEF("Drive_R", 102, 0x2014) \
	  ANIMDEF("Drive_LO_l", 103, 0x2014) \
	  ANIMDEF("Drive_LO_R", 104, 0x2014) \
	  ANIMDEF("Driveby_L", 105, 0x2014) \
	  ANIMDEF("Driveby_R", 106, 0x2014) \
	  ANIMDEF("DrivebyL_L", 107, 0x2014) \
	  ANIMDEF("DrivebyL_R", 108, 0x2014) \
	  ANIMDEF("CAR_LB", 109, 0x2014) \
	  ANIMDEF("DRIVE_BOAT", 110, 0x2004) \
	  ANIMDEF("DRIVE_BOAT_L", 111, 0x2014) \
	  ANIMDEF("DRIVE_BOAT_R", 112, 0x2014) \
	  ANIMDEF("DRIVE_BOAT_back", 113, 0x2014) \
	  ANIMDEF("BIKE_pickupR", 114, 0x18) \
	  ANIMDEF("BIKE_pickupL", 115, 0x18) \
	  ANIMDEF("BIKE_pullupR", 116, 0x18) \
	  ANIMDEF("BIKE_pullupL", 117, 0x18) \
	  ANIMDEF("BIKE_elbowR", 118, 0x18) \
	  ANIMDEF("BIKE_elbowL", 119, 0x18) \
	  ANIMDEF("BIKE_fall_off", 120, 0x54) \
	  ANIMDEF("BIKE_fallR", 121, 0x54) \
	  ANIMDEF("CAR_getout_RHS", 122, 0x18) \
	  ANIMDEF("CAR_getoutL_RHS", 123, 0x18) \
	  ANIMDEF("CAR_close_RHS", 124, 0x18) \
	  ANIMDEF("car_hookertalk", 125, 0x12) \
	  ANIMDEF("TRAIN_getin", 126, 0x4) \
	  ANIMDEF("TRAIN_getout", 127, 0xc) \
	  ANIMDEF("CAR_crawloutRHS", 128, 0x18) \
	  ANIMDEF("CAR_crawloutRHS", 129, 0x18) \
	  ANIMDEF("CAR_rollout_LHS", 130, 0x10d4) \
	  ANIMDEF("CAR_rollout_RHS", 131, 0x10d4) \
	  ANIMDEF("Getup", 132, 0x58) \
	  ANIMDEF("Getup", 133, 0x58) \
	  ANIMDEF("Getup", 134, 0x58) \
	  ANIMDEF("Getup_front", 135, 0x58) \
	  ANIMDEF("JUMP_launch", 136, 0x58) \
	  ANIMDEF("JUMP_glide", 137, 0x14) \
	  ANIMDEF("JUMP_land", 138, 0x58) \
	  ANIMDEF("FALL_fall", 139, 0x4) \
	  ANIMDEF("FALL_glide", 140, 0x14) \
	  ANIMDEF("FALL_land", 141, 0x58) \
	  ANIMDEF("FALL_collapse", 142, 0x56) \
	  ANIMDEF("FALL_back", 143, 0x14) \
	  ANIMDEF("FALL_front", 144, 0x1014) \
	  ANIMDEF("EV_step", 145, 0x58) \
	  ANIMDEF("EV_dive", 146, 0x1050) \
	  ANIMDEF("commando_roll", 147, 0x1040) \
	  ANIMDEF("XPRESSscratch", 148, 0x218) \
	  ANIMDEF("roadcross", 149, 0x12) \
	  ANIMDEF("TURN_180", 150, 0x18) \
	  ANIMDEF("ARRESTgun", 151, 0x50) \
	  ANIMDEF("DROWN", 152, 0x10) \
	  ANIMDEF("DUCK_down", 153, 0x14) \
	  ANIMDEF("DUCK_low", 154, 0x14) \
	  ANIMDEF("WEAPON_crouch", 155, 0x14) \
	  ANIMDEF("RBLOCK_Cshoot", 156, 0x1) \
	  ANIMDEF("handsup", 157, 0x50) \
	  ANIMDEF("handsCOWER", 158, 0x58) \
	  ANIMDEF("FUCKU", 159, 0x41c) \
	  ANIMDEF("PHONE_in", 160, 0x18) \
	  ANIMDEF("PHONE_out", 161, 0x18) \
	  ANIMDEF("PHONE_talk", 162, 0x16) \
	  ANIMDEF("SEAT_down", 163, 0x1c) \
	  ANIMDEF("SEAT_up", 164, 0x1c) \
	  ANIMDEF("SEAT_idle", 165, 0x1e) \
	  ANIMDEF("SEAT_down", 166, 0x1c) \
	  ANIMDEF("ATM", 167, 0x1c) \
	  ANIMDEF("abseil", 168, 0x14) \
	GROUPDEFEND \
	GROUPDEF(van, "van", "van", 1) \
	  ANIMDEF("VAN_openL", 170, 0x18) \
	  ANIMDEF("VAN_getinL", 171, 0x18) \
	  ANIMDEF("VAN_closeL", 172, 0x18) \
	  ANIMDEF("VAN_getoutL", 173, 0x18) \
	  ANIMDEF("VAN_open", 174, 0x18) \
	  ANIMDEF("VAN_getin", 175, 0x18) \
	  ANIMDEF("VAN_close", 176, 0x18) \
	  ANIMDEF("VAN_getout", 177, 0x18) \
	GROUPDEFEND \
	GROUPDEF(coach, "coach", "coach", 1) \
	  ANIMDEF("COACH_opnL", 178, 0x18) \
	  ANIMDEF("COACH_opnR", 179, 0x18) \
	  ANIMDEF("COACH_inL", 180, 0x18) \
	  ANIMDEF("COACH_inR", 181, 0x18) \
	  ANIMDEF("COACH_outL", 182, 0x18) \
	GROUPDEFEND \
	GROUPDEF(bikes, "bikes", "bikes", 1) \
	  ANIMDEF("BIKEs_Ride", 183, 0x4) \
	  ANIMDEF("BIKEs_Still", 184, 0x2014) \
	  ANIMDEF("BIKEs_Left", 185, 0x2010) \
	  ANIMDEF("BIKEs_Right", 186, 0x2010) \
	  ANIMDEF("BIKEs_Back", 187, 0x2010) \
	  ANIMDEF("BIKEs_Fwd", 188, 0x2010) \
	  ANIMDEF("BIKEs_pushes", 189, 0x2016) \
	  ANIMDEF("BIKEs_jumponR", 190, 0x18) \
	  ANIMDEF("BIKEs_jumponL", 191, 0x18) \
	  ANIMDEF("BIKEs_kick", 192, 0x18) \
	  ANIMDEF("BIKEs_hit", 193, 0x18) \
	  ANIMDEF("BIKEs_getoffRHS", 194, 0x18) \
	  ANIMDEF("BIKEs_getoffLHS", 195, 0x18) \
	  ANIMDEF("BIKEs_getoffBACK", 196, 0x58) \
	  ANIMDEF("BIKEs_drivebyLHS", 197, 0x2014) \
	  ANIMDEF("BIKEs_drivebyRHS", 198, 0x2014) \
	  ANIMDEF("BIKEs_drivebyFT", 199, 0x2014) \
	  ANIMDEF("BIKEs_passenger", 200, 0x2004) \
	GROUPDEFEND \
	GROUPDEF(bikev, "bikev", "bikev", 1) \
	  ANIMDEF("BIKEv_Ride", 183, 0x4) \
	  ANIMDEF("BIKEv_Still", 184, 0x2014) \
	  ANIMDEF("BIKEv_Left", 185, 0x2010) \
	  ANIMDEF("BIKEv_Right", 186, 0x2010) \
	  ANIMDEF("BIKEv_Back", 187, 0x2010) \
	  ANIMDEF("BIKEv_Fwd", 188, 0x2010) \
	  ANIMDEF("BIKEv_pushes", 189, 0x2016) \
	  ANIMDEF("BIKEv_jumponR", 190, 0x18) \
	  ANIMDEF("BIKEv_jumponL", 191, 0x18) \
	  ANIMDEF("BIKEv_kick", 192, 0x18) \
	  ANIMDEF("BIKEv_hit", 193, 0x18) \
	  ANIMDEF("BIKEv_getoffRHS", 194, 0x18) \
	  ANIMDEF("BIKEv_getoffLHS", 195, 0x18) \
	  ANIMDEF("BIKEv_getoffBACK", 196, 0x58) \
	  ANIMDEF("BIKEv_drivebyLHS", 197, 0x2014) \
	  ANIMDEF("BIKEv_drivebyRHS", 198, 0x2014) \
	  ANIMDEF("BIKEv_drivebyFT", 199, 0x2014) \
	  ANIMDEF("BIKEv_passenger", 200, 0x2004) \
	GROUPDEFEND \
	GROUPDEF(bikeh, "bikeh", "bikeh", 1) \
	  ANIMDEF("BIKEh_Ride", 183, 0x4) \
	  ANIMDEF("BIKEh_Still", 184, 0x2014) \
	  ANIMDEF("BIKEh_Left", 185, 0x2010) \
	  ANIMDEF("BIKEh_Right", 186, 0x2010) \
	  ANIMDEF("BIKEh_Back", 187, 0x2010) \
	  ANIMDEF("BIKEh_Fwd", 188, 0x2010) \
	  ANIMDEF("BIKEh_pushes", 189, 0x2016) \
	  ANIMDEF("BIKEh_jumponR", 190, 0x18) \
	  ANIMDEF("BIKEh_jumponL", 191, 0x18) \
	  ANIMDEF("BIKEh_kick", 192, 0x18) \
	  ANIMDEF("BIKEh_hit", 193, 0x18) \
	  ANIMDEF("BIKEh_getoffRHS", 194, 0x18) \
	  ANIMDEF("BIKEh_getoffLHS", 195, 0x18) \
	  ANIMDEF("BIKEh_getoffBACK", 196, 0x58) \
	  ANIMDEF("BIKEh_drivebyLHS", 197, 0x2014) \
	  ANIMDEF("BIKEh_drivebyRHS", 198, 0x2014) \
	  ANIMDEF("BIKEh_drivebyFT", 199, 0x2014) \
	  ANIMDEF("BIKEh_passenger", 200, 0x2004) \
	GROUPDEFEND \
	GROUPDEF(biked, "biked", "biked", 1) \
	  ANIMDEF("BIKEd_Ride", 183, 0x4) \
	  ANIMDEF("BIKEd_Still", 184, 0x2014) \
	  ANIMDEF("BIKEd_Left", 185, 0x2010) \
	  ANIMDEF("BIKEd_Right", 186, 0x2010) \
	  ANIMDEF("BIKEd_Back", 187, 0x2010) \
	  ANIMDEF("BIKEd_Fwd", 188, 0x2010) \
	  ANIMDEF("BIKEd_pushes", 189, 0x2016) \
	  ANIMDEF("BIKEd_jumponR", 190, 0x18) \
	  ANIMDEF("BIKEd_jumponL", 191, 0x18) \
	  ANIMDEF("BIKEd_kick", 192, 0x18) \
	  ANIMDEF("BIKEd_hit", 193, 0x18) \
	  ANIMDEF("BIKEd_getoffRHS", 194, 0x18) \
	  ANIMDEF("BIKEd_getoffLHS", 195, 0x18) \
	  ANIMDEF("BIKEd_getoffBACK", 196, 0x58) \
	  ANIMDEF("BIKEd_drivebyLHS", 197, 0x2014) \
	  ANIMDEF("BIKEd_drivebyRHS", 198, 0x2014) \
	  ANIMDEF("BIKEd_drivebyFT", 199, 0x2014) \
	  ANIMDEF("BIKEd_passenger", 200, 0x2004) \
	GROUPDEFEND \
	GROUPDEF(unarmed, "unarmed", "ped", 1) \
	  ANIMDEF("punchR", 201, 0x18) \
	  ANIMDEF("KICK_floor", 202, 0x18) \
	  ANIMDEF("FIGHTppunch", 203, 0x418) \
	GROUPDEFEND \
	GROUPDEF(screwdrv, "screwdrv", "ped", 1) \
	  ANIMDEF("r_hook", 201, 0x18) \
	  ANIMDEF("r_hook", 202, 0x18) \
	  ANIMDEF("FIGHTppunch", 203, 0x418) \
	  ANIMDEF("FIGHTIDLE", 204, 0x2) \
	  ANIMDEF("r_hook", 205, 0x58) \
	GROUPDEFEND \
	GROUPDEF(knife, "knife", "knife", 1) \
	  ANIMDEF("WEAPON_knife_1", 201, 0x18) \
	  ANIMDEF("WEAPON_knife_2", 202, 0x18) \
	  ANIMDEF("knife_part", 203, 0x418) \
	  ANIMDEF("WEAPON_knifeidle", 204, 0x2) \
	  ANIMDEF("WEAPON_knife_3", 205, 0x58) \
	GROUPDEFEND \
	GROUPDEF(baseball, "baseball", "baseball", 1) \
	  ANIMDEF("WEAPON_bat_h", 201, 0x18) \
	  ANIMDEF("WEAPON_bat_v", 202, 0x18) \
	  ANIMDEF("BAT_PART", 203, 0x18) \
	  ANIMDEF("WEAPON_bat_h", 204, 0x2) \
	  ANIMDEF("WEAPON_golfclub", 205, 0x18) \
	GROUPDEFEND \
	GROUPDEF(golfclub, "golfclub", "baseball", 1) \
	  ANIMDEF("WEAPON_bat_h", 201, 0x18) \
	  ANIMDEF("WEAPON_golfclub", 202, 0x18) \
	  ANIMDEF("BAT_PART", 203, 0x18) \
	  ANIMDEF("WEAPON_bat_h", 204, 0x2) \
	  ANIMDEF("WEAPON_bat_v", 205, 0x18) \
	GROUPDEFEND \
	GROUPDEF(chainsaw, "chainsaw", "chainsaw", 1) \
	  ANIMDEF("WEAPON_csaw", 201, 0x18) \
	  ANIMDEF("WEAPON_csawlo", 202, 0x18) \
	  ANIMDEF("csaw_part", 203, 0x418) \
	GROUPDEFEND \
	GROUPDEF(python, "python", "python", 1) \
	  ANIMDEF("python_fire", 201, 0x18) \
	  ANIMDEF("python_crouchfire", 202, 0x18) \
	  ANIMDEF("python_reload", 203, 0x18) \
	  ANIMDEF("python_crouchreload", 204, 0x18) \
	GROUPDEFEND \
	GROUPDEF(colt45, "colt45", "colt45", 1) \
	  ANIMDEF("colt45_fire", 201, 0x18) \
	  ANIMDEF("colt45_crouchfire", 202, 0x18) \
	  ANIMDEF("colt45_reload", 203, 0x18) \
	  ANIMDEF("colt45_crouchreload", 204, 0x18) \
	  ANIMDEF("colt45_cop", 205, 0x18) \
	GROUPDEFEND \
	GROUPDEF(shotgun, "shotgun", "shotgun", 1) \
	  ANIMDEF("shotgun_fire", 201, 0x18) \
	  ANIMDEF("shotgun_crouchfire", 202, 0x18) \
	GROUPDEFEND \
	GROUPDEF(buddy, "buddy", "buddy", 1) \
	  ANIMDEF("buddy_fire", 201, 0x18) \
	  ANIMDEF("buddy_crouchfire", 202, 0x18) \
	  ANIMDEF("buddy_reload", 203, 0x18) \
	GROUPDEFEND \
	GROUPDEF(tec, "tec", "tec", 1) \
	  ANIMDEF("TEC_fire", 201, 0x18) \
	  ANIMDEF("TEC_crouchfire", 202, 0x18) \
	  ANIMDEF("TEC_reload", 203, 0x18) \
	  ANIMDEF("TEC_crouchreload", 204, 0x18) \
	GROUPDEFEND \
	GROUPDEF(uzi, "uzi", "uzi", 1) \
	  ANIMDEF("UZI_fire", 201, 0x18) \
	  ANIMDEF("UZI_crouchfire", 202, 0x18) \
	  ANIMDEF("UZI_reload", 203, 0x18) \
	  ANIMDEF("UZI_crouchreload", 204, 0x18) \
	GROUPDEFEND \
	GROUPDEF(rifle, "rifle", "rifle", 1) \
	  ANIMDEF("RIFLE_fire", 201, 0x18) \
	  ANIMDEF("RIFLE_crouchfire", 202, 0x18) \
	  ANIMDEF("RIFLE_load", 203, 0x18) \
	  ANIMDEF("RIFLE_crouchload", 204, 0x18) \
	GROUPDEFEND \
	GROUPDEF(m60, "m60", "m60", 1) \
	  ANIMDEF("M60_fire", 201, 0x18) \
	  ANIMDEF("M60_fire", 202, 0x18) \
	  ANIMDEF("M60_reload", 203, 0x18) \
	GROUPDEFEND \
	GROUPDEF(sniper, "sniper", "sniper", 1) \
	  ANIMDEF("sniper_fire", 201, 0x18) \
	  ANIMDEF("sniper_fire", 202, 0x18) \
	  ANIMDEF("sniper_reload", 203, 0x18) \
	GROUPDEFEND \
	GROUPDEF(grenade, "grenade", "grenade", 1) \
	  ANIMDEF("WEAPON_throw", 201, 0x18) \
	  ANIMDEF("WEAPON_throwu", 202, 0x18) \
	  ANIMDEF("WEAPON_start_throw", 203, 0x18) \
	GROUPDEFEND \
	GROUPDEF(flame, "flame", "flame", 1) \
	  ANIMDEF("FLAME_fire", 201, 0x18) \
	GROUPDEFEND \
	GROUPDEF(rocketla, "rocketla", "rocketla", 1) \
	  ANIMDEF("rocket_fire", 201, 0x18) \
	  ANIMDEF("rocket_fire", 202, 0x18) \
	  ANIMDEF("rocket_reload", 203, 0x18) \
	GROUPDEFEND \
	GROUPDEF(medic, "medic", "medic", 1) \
	  ANIMDEF("CPR", 210, 0x18) \
	GROUPDEFEND \
	GROUPDEF(sunbathe, "sunbathe", "sunbathe", 1) \
	  ANIMDEF("bather", 206, 0x12) \
	GROUPDEFEND \
	GROUPDEF(playidles, "playidles", "playidles", 1) \
	  ANIMDEF("stretch", 211, 0x1c) \
	  ANIMDEF("time", 212, 0x1c) \
	  ANIMDEF("shldr", 213, 0x1c) \
	  ANIMDEF("strleg", 214, 0x1c) \
	GROUPDEFEND \
	GROUPDEF(riot, "riot", "riot", 1) \
	  ANIMDEF("riot_angry", 215, 0x1a) \
	  ANIMDEF("riot_angry_b", 216, 0x1a) \
	  ANIMDEF("riot_chant", 217, 0x1a) \
	  ANIMDEF("riot_punches", 218, 0x1a) \
	  ANIMDEF("riot_shout", 219, 0x1a) \
	  ANIMDEF("riot_challenge", 220, 0x18) \
	  ANIMDEF("riot_fuku", 221, 0x18) \
	GROUPDEFEND \
	GROUPDEF(strip, "strip", "strip", 1) \
	  ANIMDEF("strip_A", 222, 0x1a) \
	  ANIMDEF("strip_B", 223, 0x1a) \
	  ANIMDEF("strip_C", 224, 0x1a) \
	  ANIMDEF("strip_D", 225, 0x1a) \
	  ANIMDEF("strip_E", 226, 0x18) \
	  ANIMDEF("strip_F", 227, 0x1a) \
	  ANIMDEF("strip_G", 228, 0x18) \
	GROUPDEFEND \
	GROUPDEF(lance, "lance", "lance", 1) \
	  ANIMDEF("lance", 206, 0x12) \
	GROUPDEFEND \
	GROUPDEF(player, "player", "ped", 1) \
	  ANIMDEF("walk_player", 0, 0x162) \
	  ANIMDEF("run_player", 1, 0x162) \
	  ANIMDEF("SPRINT_civi", 2, 0x162) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(playerrocket, "playerrocket", "ped", 1) \
	  ANIMDEF("walk_rocket", 0, 0x162) \
	  ANIMDEF("run_rocket", 1, 0x162) \
	  ANIMDEF("sprint_rocket", 2, 0x162) \
	  ANIMDEF("idle_rocket", 3, 0x2) \
	  ANIMDEF("walk_start_rocket", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(player1armed, "player1armed", "ped", 1) \
	  ANIMDEF("walk_player", 0, 0x162) \
	  ANIMDEF("run_1armed", 1, 0x162) \
	  ANIMDEF("SPRINT_civi", 2, 0x162) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(player2armed, "player2armed", "ped", 1) \
	  ANIMDEF("walk_armed", 0, 0x162) \
	  ANIMDEF("run_armed", 1, 0x162) \
	  ANIMDEF("playersprint_armed", 2, 0x162) \
	  ANIMDEF("idle_armed", 3, 0x2) \
	  ANIMDEF("walk_start_armed", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(playerBBBat, "playerBBBat", "ped", 1) \
	  ANIMDEF("walk_player", 0, 0x162) \
	  ANIMDEF("run_1armed", 1, 0x162) \
	  ANIMDEF("SPRINT_civi", 2, 0x162) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(aicsaw2, "aicsaw2", "ped", 1) \
	  ANIMDEF("walk_aicsaw", 0, 0x162) \
	  ANIMDEF("run_aicsaw", 1, 0x162) \
	  ANIMDEF("run_aicsaw", 2, 0x162) \
	  ANIMDEF("IDLE_csaw", 3, 0x2) \
	  ANIMDEF("walk_start_aicsaw", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(playercsaw, "playercsaw", "ped", 1) \
	  ANIMDEF("walk_csaw", 0, 0x162) \
	  ANIMDEF("run_csaw", 1, 0x162) \
	  ANIMDEF("sprint_csaw", 2, 0x162) \
	  ANIMDEF("IDLE_csaw", 3, 0x2) \
	  ANIMDEF("walk_start_csaw", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(shuffle, "shuffle", "ped", 1) \
	  ANIMDEF("WALK_shuffle", 0, 0x162) \
	  ANIMDEF("RUN_civi", 1, 0x162) \
	  ANIMDEF("SPRINT_civi", 2, 0x162) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(oldman, "oldman", "ped", 1) \
	  ANIMDEF("walk_old", 0, 0x162) \
	  ANIMDEF("run_civi", 1, 0x162) \
	  ANIMDEF("sprint_civi", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(gang1, "gang1", "ped", 1) \
	  ANIMDEF("walk_gang1", 0, 0x162) \
	  ANIMDEF("run_gang1", 1, 0x162) \
	  ANIMDEF("sprint_civi", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(gang2, "gang2", "ped", 1) \
	  ANIMDEF("walk_gang2", 0, 0x162) \
	  ANIMDEF("run_gang1", 1, 0x162) \
	  ANIMDEF("sprint_civi", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(fatman, "fatman", "ped", 1) \
	  ANIMDEF("walk_fat", 0, 0x162) \
	  ANIMDEF("run_civi", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(oldfatman, "oldfatman", "ped", 1) \
	  ANIMDEF("walk_fatold", 0, 0x162) \
	  ANIMDEF("run_fatold", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(jogger, "jogger", "ped", 1) \
	  ANIMDEF("JOG_maleA", 0, 0x162) \
	  ANIMDEF("run_civi", 1, 0x162) \
	  ANIMDEF("sprint_civi", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(woman, "woman", "ped", 1) \
	  ANIMDEF("woman_walknorm", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(shopping, "shopping", "ped", 1) \
	  ANIMDEF("woman_walkshop", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_run", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(busywoman, "busywoman", "ped", 1) \
	  ANIMDEF("woman_walkbusy", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(sexywoman, "sexywoman", "ped", 1) \
	  ANIMDEF("woman_walksexy", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(fatwoman, "fatwoman", "ped", 1) \
	  ANIMDEF("walk_fat", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(oldwoman, "oldwoman", "ped", 1) \
	  ANIMDEF("woman_walkold", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(jogwoman, "jogwoman", "ped", 1) \
	  ANIMDEF("JOG_maleB", 0, 0x162) \
	  ANIMDEF("woman_run", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("woman_idlestance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(panicchunky, "panicchunky", "ped", 1) \
	  ANIMDEF("run_fatold", 0, 0x162) \
	  ANIMDEF("woman_runpanic", 1, 0x162) \
	  ANIMDEF("woman_runpanic", 2, 0x162) \
	  ANIMDEF("idle_stance", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(skate, "skate", "skate", 1) \
	  ANIMDEF("skate_run", 0, 0x162) \
	  ANIMDEF("skate_sprint", 1, 0x162) \
	  ANIMDEF("skate_sprint", 2, 0x162) \
	  ANIMDEF("skate_idle", 3, 0x2) \
	GROUPDEFEND \
	GROUPDEF(playerback, "playerback", "ped", 1) \
	  ANIMDEF("walk_back", 0, 0x162) \
	  ANIMDEF("run_back", 1, 0x162) \
	  ANIMDEF("run_back", 2, 0x162) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start_back", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(playerleft, "playerleft", "ped", 1) \
	  ANIMDEF("walk_left", 0, 0x1e2) \
	  ANIMDEF("run_left", 1, 0x1e2) \
	  ANIMDEF("run_left", 2, 0x1e2) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start_left", 4, 0xc0) \
	GROUPDEFEND \
	GROUPDEF(playerright, "playerright", "ped", 1) \
	  ANIMDEF("walk_right", 0, 0x1e2) \
	  ANIMDEF("run_right", 1, 0x1e2) \
	  ANIMDEF("run_right", 2, 0x1e2) \
	  ANIMDEF("IDLE_STANCE", 3, 0x2) \
	  ANIMDEF("walk_start_right", 4, 0xc0) \
	GROUPDEFEND \
	GROUPDEF(rocketback, "rocketback", "ped", 1) \
	  ANIMDEF("walk_rocket_back", 0, 0x162) \
	  ANIMDEF("run_rocket_back", 1, 0x162) \
	  ANIMDEF("run_rocket_back", 2, 0x162) \
	  ANIMDEF("idle_rocket", 3, 0x2) \
	  ANIMDEF("walkst_rocket_back", 4, 0x40) \
	GROUPDEFEND \
	GROUPDEF(rocketleft, "rocketleft", "ped", 1) \
	  ANIMDEF("walk_rocket_left", 0, 0x1e2) \
	  ANIMDEF("run_rocket_left", 1, 0x1e2) \
	  ANIMDEF("run_rocket_left", 2, 0x1e2) \
	  ANIMDEF("idle_rocket", 3, 0x2) \
	  ANIMDEF("walkst_rocket_left", 4, 0xc0) \
	GROUPDEFEND \
	GROUPDEF(rocketright, "rocketright", "ped", 1) \
	  ANIMDEF("walk_rocket_right", 0, 0x1e2) \
	  ANIMDEF("run_rocket_right", 1, 0x1e2) \
	  ANIMDEF("run_rocket_right", 2, 0x1e2) \
	  ANIMDEF("idle_rocket", 3, 0x2) \
	  ANIMDEF("walkst_rocket_right", 4, 0xc0) \
	GROUPDEFEND \
	GROUPDEF(MPNote, "MPNote", "MPNote", 1) \
	  ANIMDEF("MPNote", 229, 0x210) \
	  ANIMDEF("MPNoteloop", 230, 0x212) \
	GROUPDEFEND \
	GROUPDEF(CS_MISC, "CS_MISC", "CS_MISC", 1) \
	  ANIMDEF("IDLE_look", 231, 0x218) \
	  ANIMDEF("IDLE_no", 232, 0x218) \
	  ANIMDEF("IDLE_yes", 233, 0x218) \
	  ANIMDEF("IDLE_chat2", 234, 0x218) \
	  ANIMDEF("IDLE_cough", 235, 0x218) \
	  ANIMDEF("IDLE_giggle_female", 236, 0x218) \
	  ANIMDEF("IDLE_tough_chat", 237, 0x218) \
	  ANIMDEF("IDLE_cell_talk", 238, 0x218) \
	GROUPDEFEND \
	GROUPDEF(DONH2, "DONH2", "DONH2", 1) \
	  ANIMDEF("DONH2_camera", 239, 0x218) \
	GROUPDEFEND \
	GROUPDEF(DONH3, "DONH3", "DONH3", 1) \
	  ANIMDEF("DONH3_happy", 240, 0x214) \
	GROUPDEFEND \
	GROUPDEF(JDT2, "JDT2", "JDT2", 1) \
	  ANIMDEF("JDT2_anxious_talk", 241, 0x218) \
	  ANIMDEF("JDT2_shrug", 242, 0x218) \
	GROUPDEFEND \
	GROUPDEF(JDT4, "JDT4", "JDT4", 1) \
	  ANIMDEF("JDT4_dildo_talk", 243, 0x218) \
	GROUPDEFEND \
	GROUPDEF(JDT5, "JDT5", "JDT5", 1) \
	  ANIMDEF("JDT5_calm_down", 244, 0x218) \
	  ANIMDEF("JDT5_point", 245, 0x218) \
	GROUPDEFEND \
	GROUPDEF(JDT6, "JDT6", "JDT6", 1) \
	  ANIMDEF("JDT6_pickup", 246, 0x218) \
	  ANIMDEF("JDT6_doorknock", 247, 0x218) \
	GROUPDEFEND \
	GROUPDEF(MAR1, "MAR1", "MAR1", 1) \
	  ANIMDEF("MAR1_skirt", 248, 0x218) \
	GROUPDEFEND \
	GROUPDEF(MAR2, "MAR2", "MAR2", 1) \
	  ANIMDEF("MAR2_cell_answer", 249, 0x218) \
	  ANIMDEF("MAR2_cell_end", 250, 0x218) \
	  ANIMDEF("MAR2_cell_talk", 251, 0x218) \
	  ANIMDEF("MAR2_foot_tap", 252, 0x218) \
	GROUPDEFEND \
	GROUPDEF(MAR3, "MAR3", "MAR3", 1) \
	  ANIMDEF("MAR3_hoochy", 253, 0x218) \
	  ANIMDEF("MAR3_note_idle", 254, 0x218) \
	  ANIMDEF("MAR3_note_pick", 255, 0x218) \
	GROUPDEFEND \
	GROUPDEF(SAL1, "SAL1", "SAL1", 1) \
	  ANIMDEF("SAL1_briefcase_down", 256, 0x218) \
	GROUPDEFEND \
	GROUPDEF(SAL2, "SAL2", "SAL2", 1) \
	  ANIMDEF("SAL2_idle_seated", 257, 0x218) \
	  ANIMDEF("SAL2_seat_to_stand", 258, 0x218) \
	GROUPDEFEND \
	GROUPDEF(SAL3, "SAL3", "SAL3", 1) \
	  ANIMDEF("SAL3_seated_talk", 259, 0x218) \
	  ANIMDEF("SAL3_seated_idle", 260, 0x218) \
	  ANIMDEF("SAL3_sit_down", 261, 0x210) \
	GROUPDEFEND \
	GROUPDEF(SAL4, "SAL4", "SAL4", 1) \
	  ANIMDEF("SAL4_dustdown", 262, 0x218) \
	  ANIMDEF("SAL4_girlrun", 263, 0x218) \
	GROUPDEFEND \
	GROUPDEF(SAL6, "SAL6", "SAL6", 1) \
	  ANIMDEF("SAL6_angry_seated", 264, 0x218) \
	  ANIMDEF("SAL6_idle_seated", 265, 0x218) \
	GROUPDEFEND \
	GROUPDEF(SAL7, "SAL7", "SAL7", 1) \
	  ANIMDEF("SAL7_lookout", 266, 0x218) \
	GROUPDEFEND \
	GROUPDEF(VIC2, "VIC2", "VIC2", 1) \
	  ANIMDEF("VIC2_point_angry", 267, 0x218) \
	GROUPDEFEND \
	GROUPDEF(VIC3, "VIC3", "VIC3", 1) \
	  ANIMDEF("VIC3_waft", 268, 0x218) \
	  ANIMDEF("VIC3_pickup_roll", 269, 0x218) \
	GROUPDEFEND \
	GROUPDEF(VIC4, "VIC4", "VIC4", 1) \
	  ANIMDEF("VIC4_carry_box", 270, 0x218) \
	  ANIMDEF("VIC4_cell_look", 271, 0x218) \
	  ANIMDEF("VIC4_crate_idle", 272, 0x218) \
	GROUPDEFEND \
	GROUPDEF(VIC6, "VIC6", "VIC6", 1) \
	  ANIMDEF("VIC6_cell_angry", 273, 0x218) \
	GROUPDEFEND \
	GROUPDEF(TOURIST, "TOURIST", "TOURIST", 1) \
	  ANIMDEF("tourist3", 274, 0x218) \
	  ANIMDEF("tourist2", 275, 0x218) \
	  ANIMDEF("tourist1", 276, 0x218) \
	GROUPDEFEND \
	GROUPDEF(MAC2, "MAC2", "MAC2", 1) \
	  ANIMDEF("MAC2_plead", 277, 0x218) \
	GROUPDEFEND \
	GROUPDEF(VIC7, "VIC7", "VIC7", 1) \
	  ANIMDEF("VIC7_prod_with_foot", 278, 0x218) \
	GROUPDEFEND
