all: colcode_vs_gl3.inc colcode_fs_gl3.inc

colcode_vs_gl3.inc: colcode.vert
	(echo 'const char *colcode_vert_src =';\
	 sed 's/..*/"&\\n"/' colcode.vert;\
	 echo ';') >colcode_vs_gl3.inc

colcode_fs_gl3.inc: colcode.frag
	(echo 'const char *colcode_frag_src =';\
	 sed 's/..*/"&\\n"/' colcode.frag;\
	 echo ';') >colcode_fs_gl3.inc
