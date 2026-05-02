uniform sampler2D tex0;
uniform vec4 u_postfxParams;

#define limit (u_postfxParams.x)
#define intensity (u_postfxParams.y)
#define renderpasses (u_postfxParams.z)

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec4 fb = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	vec4 color = fb + clamp(2*fb - limit, 0, 1)*intensity*renderpasses;
	color.a = 1.0;

	FRAGCOLOR(color);
}
