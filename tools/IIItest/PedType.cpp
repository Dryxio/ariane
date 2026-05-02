#include "III.h"

int
CPedType::FindPedType(const char *name)
{
	static StrAssoc types[] = {
		{ "PLAYER1",   CPedType::Player1 },
		{ "PLAYER2",   CPedType::Player2 },
		{ "PLAYER3",   CPedType::Player3 },
		{ "PLAYER4",   CPedType::Player4 },
		{ "CIVMALE",   CPedType::CivMale },
		{ "CIVFEMALE", CPedType::CivFemale },
		{ "COP",       CPedType::Cop },
		{ "GANG1",     CPedType::Gang1 },
		{ "GANG2",     CPedType::Gang2 },
		{ "GANG3",     CPedType::Gang3 },
		{ "GANG4",     CPedType::Gang4 },
		{ "GANG5",     CPedType::Gang5 },
		{ "GANG6",     CPedType::Gang6 },
		{ "GANG7",     CPedType::Gang7 },
		{ "GANG8",     CPedType::Gang8 },
		{ "GANG9",     CPedType::Gang9 },
		{ "EMERGENCY", CPedType::Emergency },
		{ "FIREMAN",   CPedType::Fireman },
		{ "CRIMINAL",  CPedType::Criminal },
		{ "SPECIAL",   CPedType::Special },
		{ "PROSTITUE", CPedType::Prostitute },
		{ "", CPedType::Invalid }
	};
	return StrAssoc::get(types, name);
}
