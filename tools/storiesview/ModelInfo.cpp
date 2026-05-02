#include "storiesview.h"


int CModelInfo::msNumModelInfos;
CBaseModelInfo **CModelInfo::ms_modelInfoPtrs;

void
CModelInfo::Load(int n, CBaseModelInfo **mi)
{
	msNumModelInfos = n;
	ms_modelInfoPtrs = mi;
	// TODO: game sets up render callbacks here
}

CBaseModelInfo*
CModelInfo::Get(int n)
{
	return ms_modelInfoPtrs[n];
}
