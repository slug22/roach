#pragma once
#include <GL/freeglut.h>
#include <string>

struct Texture {
    GLuint id;
    int w, h;
};

// Time
long long getCurrentTimeMillis();

// Texture loading
Texture loadTexture(const char* path);

// Drawing helpers
void drawQuad(float x, float y, float w, float h, GLuint texId);

// Text rendering
void renderText(float x, float y, const char* text);
void drawQuadRotated(float x, float y, float w, float h, GLuint texID, float angleDegrees);