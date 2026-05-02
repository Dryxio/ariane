uniform sampler2D tex0;
uniform vec4 u_postfxCol1;
uniform vec4 u_postfxCol2;

FSIN vec4 v_color;
FSIN vec2 v_tex0;
FSIN float v_fog;

void
main(void)
{
	vec4 fb = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	vec4 color = fb + fb*u_postfxCol1*u_postfxCol1.a*2 + fb*u_postfxCol2*u_postfxCol2.a*2;
	color.a = 1.0;

	FRAGCOLOR(color);
}
