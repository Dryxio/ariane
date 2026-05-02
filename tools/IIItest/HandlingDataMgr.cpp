#include "III.h"
#include "FileMgr.h"

tHandlingData cHandlingDataMgr::data[NUMHANDLINGS];

void
cHandlingDataMgr::Initialise(void)
{
	cHandlingDataMgr::LoadHandlingData();
}


void
cHandlingDataMgr::LoadHandlingData(void)
{
	char name[14];
	FileHandle file;
	char *line;
	int abs, flags, front, rear;
	CFileMgr::SetDir("DATA");
	file = CFileMgr::OpenFile("HANDLING.CFG", "rb");
	CFileMgr::SetDir("");
	if(file == nil)
		return;
	int i = 0;
	tHandlingData *d;
	while(line = CFileLoader::LoadLine(file)){
		if(line[0] == ';')
			continue;
		sscanf(line, "%s", name);
		i = GetHandlingId(name);
		assert(i >= 0);
		assert(i < NUMHANDLINGS);
		d = &data[i];
		// TODO: do this better
		sscanf(line, "%s %f %f %f %f %f %f %f %d %f %f %f"
		             "%c %f %f %c %c"
		             "%f %f %d %f %f %f %f %f %d"
		             "%f %f %f %x %d %d",
		       name, &d->fMass, &d->Dimensions.x, &d->Dimensions.y, &d->Dimensions.z,
		       &d->CenterOfMass.x, &d->CenterOfMass.y, &d->CenterOfMass.z,
		       &d->nPercentSubmerged, &d->fTractionMultiplier, &d->fTractionLoss, &d->fTractionBias,
		       &d->trans.nNumberOfGears, &d->trans.fMaxVelocity, &d->trans.fEngineAcceleration,
		       &d->trans.nDriveType, &d->trans.nEngineType,
		       &d->fBrakeDeceleration, &d->fBrakeBias, &abs, &d->fSteeringLock, &d->fSuspensionForceLevel,
		       &d->fSuspensionDampingLevel, &d->fSeatOffsetDistance, &d->fCollisionDamageMultiplier, &d->nMonetaryValue,
		       &d->fSuspensionUpperLimit, &d->fSuspensionLowerLimit, &d->fSuspensionBias,
		       &flags, &front, &rear);
		d->trans.fEngineAcceleration *= 0.4f;
		d->bABS = !!abs;
		d->trans.Flags = d->Flags = flags;
		d->FrontLights = front;
		d->RearLights = rear;
		ConvertDataToGameUnits(d);
	}
	CFileMgr::CloseFile(file);
}

void
cHandlingDataMgr::ConvertDataToGameUnits(tHandlingData *d)
{
	d->trans.fEngineAcceleration /= 2500.0f;
	d->trans.fMaxVelocity /= 180.0f;
	d->fBrakeDeceleration /= 2500.0f;
	d->fTurnMass = CVector2D(d->Dimensions).MagnitudeSqr() * d->fMass / 12.0f;
	if(d->fTurnMass >= 10.0f)
		d->fTurnMass *= 5.0f;
	d->fInvMass = 1.0f/d->fMass;
	// TODO: more
}

int
cHandlingDataMgr::GetHandlingId(const char *ident)
{
	static const char *idents[] = {
		"LANDSTAL",
		"IDAHO",
		"STINGER",
		"LINERUN",
		"PEREN",
		"SENTINEL",
		"PATRIOT",
		"FIRETRUK",
		"TRASH",
		"STRETCH",
		"MANANA",
		"INFERNUS",
		"BLISTA",
		"PONY",
		"MULE",
		"CHEETAH",
		"AMBULAN",
		"FBICAR",
		"MOONBEAM",
		"ESPERANT",
		"TAXI",
		"KURUMA",
		"BOBCAT",
		"MRWHOOP",
		"BFINJECT",
		"POLICE",
		"ENFORCER",
		"SECURICA",
		"BANSHEE",
		"PREDATOR",
		"BUS",
		"RHINO",
		"BARRACKS",
		"TRAIN",
		"HELI",
		"DODO",
		"COACH",
		"CABBIE",
		"STALLION",
		"RUMPO",
		"RCBANDIT",
		"BELLYUP",
		"MRWONGS",
		"MAFIA",
		"YARDIE",
		"YAKUZA",
		"DIABLOS",
		"COLUMB",
		"HOODS",
		"AIRTRAIN",
		"DEADDODO",
		"SPEEDER",
		"REEFER",
		"PANLANT",
		"FLATBED",
		"YANKEE",
		"BORGNINE",
		nil
	};
	int i = 0;
	for(const char **id = idents; id; id++, i++)
		if(strcmp(*id, ident) == 0)
			return i;
	return -1;
}
