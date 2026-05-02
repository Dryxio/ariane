const char *radiosityLeeds_frag_src =
"uniform sampler2D tex0;\n"
"uniform vec4 u_postfxParams;\n"

"#define limit (u_postfxParams.x)\n"
"#define intensity (u_postfxParams.y)\n"
"#define numPasses (u_postfxParams.z)\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"
"FSIN float v_fog;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 fb = texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));\n"
"	vec3 hilights = clamp(fb.rgb - vec3(limit/2.0), 0, 1);\n"
"	fb.rgb += hilights*intensity*4*numPasses;\n"
"	fb.a = 1.0;\n"

"	FRAGCOLOR(fb);\n"
"}\n"
;
