const char *ps2Env_frag_src =
"uniform sampler2D tex0;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 color = v_color*texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y));\n"
"	color.a = 1.0;\n"
"	FRAGCOLOR(color);\n"
"}\n"

;
