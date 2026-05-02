#ifndef _HANDLINGDATAMGR_H_
#define _HANDLINGDATAMGR_H_

// NOT reversed data, just read from file
struct Transmission {
	char nDriveType;
	char nEngineType;
	char nNumberOfGears;
	uchar Flags;
	float fEngineAcceleration;
	float fMaxVelocity;
};
struct tHandlingData {
	int ident;
	float fMass;
	float fInvMass;
	float fTurnMass;
	CVector Dimensions;
	CVector CenterOfMass;
	int nPercentSubmerged;
	float fTractionMultiplier;
	float fTractionLoss;
	float fTractionBias;
	Transmission trans;

	float fBrakeDeceleration;
	float fBrakeBias;
	bool bABS;
	float fSteeringLock;
	float fSuspensionForceLevel;
	float fSuspensionDampingLevel;
	float fSeatOffsetDistance;
	float fCollisionDamageMultiplier;
	int nMonetaryValue;

	float fSuspensionUpperLimit;
	float fSuspensionLowerLimit;
	float fSuspensionBias;
	uchar Flags;
	uchar FrontLights;
	uchar RearLights;
};

class cHandlingDataMgr
{
	static tHandlingData data[NUMHANDLINGS];
public:
	static void Initialise(void);
	static void LoadHandlingData(void);
	static int  GetHandlingId(const char *ident);
	static void ConvertDataToGameUnits(tHandlingData *d);
	static tHandlingData *GetHandlingData(int id) { return &data[id]; }
};

#endif
