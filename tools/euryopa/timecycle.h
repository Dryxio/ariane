namespace Timecycle
{
	struct ColourSet
	{
		rw::RGBAf amb;
		rw::RGBAf amb_obj;
		rw::RGBAf amb_bl;
		rw::RGBAf amb_obj_bl;
		rw::RGBAf dir;
		rw::RGBAf skyTop;
		rw::RGBAf skyBottom;
		rw::RGBAf sunCore;
		rw::RGBAf sunCorona;
		float sunSz;
		float sprSz;
		float sprBght;
		float shdw;
		float lightShd;
		float poleShd;	// treeshd in III
		float farClp;
		float fogSt;
		float lightOnGround;
		rw::RGBAf lowCloud;
		rw::RGBAf fluffyCloudTop;
		rw::RGBAf fluffyCloudBottom;
		rw::RGBAf water;
		rw::RGBAf postfx1;	// also blur
		rw::RGBAf postfx2;
		float cloudAlpha;
		float radiosityLimit;
		float radiosityIntensity;
		float waterFogAlpha;
		float dirMult;
		// neo
		float lightMapIntensity;
	};
	extern ColourSet currentColours;
	extern rw::RGBAf currentFogColour;
	extern rw::RGBA belowHorizonColour;

	void Initialize(void);
	void InitNeoWorldTweak(void);
	void AddBox(CBox box, int farClp, int extraCol, float extraStrength, float falloff, float lodDist);
	void Update(void);
	void SetLights(void);
	void RenderBoxes(void);
}
