uniform sampler2D tex0;
uniform vec4 u_postfxParams;

#define limit (u_postfxParams.x)
#define intensity (u_postfxParams.y)
#define numPasses (u_postfxParams.z)

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec4 fb = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	vec3 hilights = clamp(fb.rgb - vec3(limit/2.0), 0, 1);
	fb.rgb += hilights*intensity*4*numPasses;
	fb.a = 1.0;

	FRAGCOLOR(fb);
}
