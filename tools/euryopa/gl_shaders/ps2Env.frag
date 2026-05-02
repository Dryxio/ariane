uniform sampler2D tex0;

FSIN vec4 v_color;
FSIN vec2 v_tex0;

void
main(void)
{
	vec4 color = v_color*texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));
	color.a = 1.0;
	FRAGCOLOR(color);
}

