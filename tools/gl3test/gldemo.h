#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <rw.h>
#include <rwgta.h>

#include "input.h"
#include "camera.h"

/* less screaming */
#define nil NULL

int init(void);
void shutdown(void);
void update(double t);
void display(void);
void pollinput(GLFWwindow*);
void keypress(GLFWwindow*, int key, int scancode, int action, int mods);

