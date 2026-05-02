const char *radiosity_frag_src =
"uniform sampler2D tex0;\n"
"uniform vec4 u_postfxParams;\n"

"#define limit (u_postfxParams.x)\n"
"#define intensity (u_postfxParams.y)\n"
"#define renderpasses (u_postfxParams.z)\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 fb = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));\n"
"	vec4 color = fb + clamp(2*fb - limit, 0, 1)*intensity*renderpasses;\n"
"	color.a = 1.0;\n"

"	FRAGCOLOR(color);\n"
"}\n"
;
