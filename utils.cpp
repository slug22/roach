#include <GL/freeglut.h>
#include <GL/glext.h>   // <-- REQUIRED for GL_CLAMP_TO_EDGE
#include "utils.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <chrono>


long long getCurrentTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

Texture loadTexture(const char* path) {
    Texture tex;
    int channels;
    unsigned char* data = stbi_load(path, &tex.w, &tex.h, &channels, 4);

    if (!data) {
        printf("Failed to load texture: %s\n", path);
        tex.id = 0;
        return tex;
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    stbi_image_free(data);
    return tex;
}

void drawQuad(float x, float y, float w, float h, GLuint texId) {
    glBindTexture(GL_TEXTURE_2D, texId);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x,     y);
        glTexCoord2f(1, 0); glVertex2f(x + w, y);
        glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
        glTexCoord2f(0, 1); glVertex2f(x,     y + h);
    glEnd();
}

void renderText(float x, float y, const char* text) {
    glColor3f(1,1,1);
    glRasterPos2f(x, y);
    for (int i = 0; text[i]; i++)
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15, text[i]);
}
void drawQuadRotated(float x, float y, float w, float h, GLuint texID, float angleDegrees) {
    glBindTexture(GL_TEXTURE_2D, texID);
    
    // Save current matrix
    glPushMatrix();
    
    // Move to center of sprite
    glTranslatef(x + w/2.0f, y + h/2.0f, 0.0f);
    
    // Rotate around center
    glRotatef(angleDegrees, 0.0f, 0.0f, 1.0f);
    
    // Draw quad centered at origin
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(-w/2.0f, -h/2.0f);
        glTexCoord2f(1, 0); glVertex2f( w/2.0f, -h/2.0f);
        glTexCoord2f(1, 1); glVertex2f( w/2.0f,  h/2.0f);
        glTexCoord2f(0, 1); glVertex2f(-w/2.0f,  h/2.0f);
    glEnd();
    
    // Restore matrix
    glPopMatrix();
}