const char *colcode_frag_src =
"uniform sampler2D tex0;\n"

"FSIN vec4 v_color;\n"
"FSIN vec2 v_tex0;\n"

"void\n"
"main(void)\n"
"{\n"
"	vec4 color = v_color;\n"
"	color.a *= texture(tex0, vec2(v_tex0.x, 1.0-v_tex0.y)).a;\n"
"	DoAlphaTest(color.a);\n"
"	FRAGCOLOR(color);\n"
"}\n"

;
