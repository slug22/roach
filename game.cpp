// ============================================================================
// game.cpp
// CLEAN REWRITE — NEW PORTAL SYSTEM (All portals loaded from Levels.h)
// ============================================================================

#include <GL/glut.h>
#include <GL/gl.h>
#include <vector>
#include <unordered_set>
#include <utility>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <chrono>
#include "Levels.h"

// Define GL_CLAMP_TO_EDGE if not available
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// ============================================================================
// CONFIGURATION / CONSTANTS
// ============================================================================

const int TILE_SIZE = 32;
const int WIN_W = 800;
const int WIN_H = 600;

int currLevel = 0; // active level index

// For grid dimensions derived from the window size:
const int COLS = WIN_W / TILE_SIZE; // 25 columns
const int ROWS = WIN_H / TILE_SIZE; // 18 rows

const float PLAYER_SPEED = 2.0f;

int placeMode = 1; // 1=place, 2=burn
int bagCount = 3;

bool justTeleported = false;
int spawnPortalID = -1; // The portal ID we appeared on

// ============================================================================
// HELPERS & STRUCTURES
// ============================================================================

struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

struct Texture {
    GLuint id;
    int w, h;
};

struct Sprite {
    float x, y;     // pixel coordinates
    Texture* tex;
    long long burnEndTime = 0;  // fire timer
};

struct Portal {
    int gridX, gridY;       // tile location
    int portalID;           // logical portal ID (2,3,4...)
    int targetLevel;        // level index to teleport to
    int targetPortalID;     // portal ID to spawn on in target level
};

struct BurnCheckEvent {
    int gridX, gridY;
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

Texture playerTex, itemTex, wallTex, floorTex, flameTex, holeTex;
Sprite player;

std::vector<Sprite> items;
std::vector<Portal> portals;

std::unordered_set<std::pair<int, int>, PairHash> occupiedPositions;

bool keys[256] = {false};

std::vector<BurnCheckEvent> spreadQueue;

// Instead of tileValue[][], we now store pointer to the tiles inside Levels.h:
int (*levelTiles)[25] = nullptr;

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

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

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

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
    for(int i = 0; text[i]; i++)
        glutBitmapCharacter(GLUT_BITMAP_9_BY_15, text[i]);
}

// ============================================================================
// PORTAL SYSTEM (NEW — from Levels.h ONLY)
// ============================================================================

// Load all portals from Levels.h for current level
void loadPortals() {
    portals.clear();

    const std::vector<PortalDef>& defs = Levels[currLevel].portals;
    for (const auto& def : defs) {
        Portal P;
        P.gridX = def.x;
        P.gridY = def.y;
        P.portalID = def.portalID;
        P.targetLevel = def.targetLevel;
        P.targetPortalID = def.targetPortalID;

        portals.push_back(P);

        printf("Loaded Portal ID %d at [%d,%d] -> Level %d, PortalID %d\n",
               P.portalID, P.gridX, P.gridY, P.targetLevel, P.targetPortalID);
    }
}

Portal* findPortalByID(int portalID) {
    for (auto& p : portals)
        if (p.portalID == portalID)
            return &p;
    return nullptr;
}

// ============================================================================
// LEVEL LOADING
// ============================================================================

void loadLevel(int levelIndex, int fromPortalID = -1) {
    if (levelIndex < 0 || levelIndex >= NUM_LEVELS) {
        printf("Invalid level: %d\n", levelIndex);
        return;
    }

    currLevel = levelIndex;

    // Load the tile data pointer
    levelTiles = Levels[currLevel].tiles;

    items.clear();
    occupiedPositions.clear();
    spreadQueue.clear();
    bagCount = 3;

    loadPortals();

    // SPAWN LOGIC
    if (fromPortalID >= 0) {
        Portal* spawnP = findPortalByID(fromPortalID);
        if (spawnP) {
            player.x = spawnP->gridX * TILE_SIZE;
            player.y = spawnP->gridY * TILE_SIZE;
            spawnPortalID = fromPortalID;
            justTeleported = true;

            printf("Spawned at portal ID %d in level %d\n", fromPortalID, currLevel);
        }
        else {
            // fallback spawn
            player.x = 64;
            player.y = 64;
            spawnPortalID = -1;
            justTeleported = false;
        }
    }
    else {
        // Normal first spawn
        player.x = 64;
        player.y = 64;
        spawnPortalID = -1;
        justTeleported = false;
    }

    printf("\n=== Loaded Level %d ===\n", currLevel);
}

// ============================================================================
// COLLISION + PORTAL CHECK
// ============================================================================

bool checkCollision(float newX, float newY) {
    int gx = newX / TILE_SIZE;
    int gy = newY / TILE_SIZE;

    if (gx < 0 || gx >= COLS || gy < 0 || gy >= ROWS)
        return true;

    if (levelTiles[gy][gx] == 1)
        return true;

    return false;
}

void checkPortalCollision() {
    int gx = player.x / TILE_SIZE;
    int gy = player.y / TILE_SIZE;

    // If we just teleported, wait until we move OFF the spawn tile
    if (justTeleported && spawnPortalID >= 0) {
        Portal* spawnP = findPortalByID(spawnPortalID);
        if (spawnP && (spawnP->gridX != gx || spawnP->gridY != gy)) {
            justTeleported = false;
            printf("Player moved off spawn portal — teleport enabled.\n");
        }
        return;
    }

    // Check all portals
    for (const auto& p : portals) {
        if (gx == p.gridX && gy == p.gridY) {
            printf("Entered Portal ID %d — going to Level %d\n",
                   p.portalID, p.targetLevel);

            loadLevel(p.targetLevel, p.targetPortalID);
            glutPostRedisplay();
            return;
        }
    }
}

// ============================================================================
// UPDATE LOOP (PLAYER MOVEMENT)
// ============================================================================

void update(int) {
    float dx = 0, dy = 0;
    if (keys['w']||keys['W']) dy -= PLAYER_SPEED;
    if (keys['s']||keys['S']) dy += PLAYER_SPEED;
    if (keys['a']||keys['A']) dx -= PLAYER_SPEED;
    if (keys['d']||keys['D']) dx += PLAYER_SPEED;

    const float COLLISION_HEIGHT = TILE_SIZE/4.0f;
    const float COLLISION_TOP_OFFSET = TILE_SIZE - COLLISION_HEIGHT;
    const float INSET = 1.0f;

    // ----------- X movement ----------
    float nextX = player.x + dx;
    bool blockX = false;

    if (checkCollision(nextX+INSET,                     player.y+COLLISION_TOP_OFFSET)) blockX = true;
    if (checkCollision(nextX+INSET,                     player.y+TILE_SIZE-INSET))       blockX = true;
    if (checkCollision(nextX+TILE_SIZE-INSET,           player.y+COLLISION_TOP_OFFSET)) blockX = true;
    if (checkCollision(nextX+TILE_SIZE-INSET,           player.y+TILE_SIZE-INSET))      blockX = true;

    if (!blockX) player.x = nextX;

    // ----------- Y movement ----------
    float nextY = player.y + dy;
    bool blockY = false;

    if (checkCollision(player.x+INSET,                  nextY+COLLISION_TOP_OFFSET)) blockY = true;
    if (checkCollision(player.x+TILE_SIZE-INSET,        nextY+COLLISION_TOP_OFFSET)) blockY = true;
    if (checkCollision(player.x+INSET,                  nextY+TILE_SIZE-INSET))      blockY = true;
    if (checkCollision(player.x+TILE_SIZE-INSET,        nextY+TILE_SIZE-INSET))      blockY = true;

    if (!blockY) player.y = nextY;

    checkPortalCollision();

    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// ============================================================================
// FIRE / BURN LOGIC
// ============================================================================

void checkAndPropagateBurn(int gx, int gy) {
    int nbr[8][2] = {
        {gx, gy-1}, {gx, gy+1}, {gx-1, gy}, {gx+1, gy},{gx-1, gy-1}, {gx-1, gy+1}, {gx+1, gy-1}, {gx+1, gy+1}
    };

    for (int i=0;i<8;i++) {
        int nx = nbr[i][0];
        int ny = nbr[i][1];

        if (nx<0||nx>=COLS||ny<0||ny>=ROWS) continue;

        for (auto& s : items) {
            if ((int)(s.x/TILE_SIZE) == nx &&
                (int)(s.y/TILE_SIZE) == ny &&
                s.tex == &itemTex)
            {
                s.tex = &flameTex;
                s.burnEndTime = getCurrentTimeMillis() + 500;
                printf("Fire spread to [%d,%d]\n", nx, ny);
                break;
            }
        }
    }
}

void burnTimerFunc(int) {
    long long now = getCurrentTimeMillis();
    bool redraw = false;

    // remove finished burning items
    for (auto it = items.begin(); it != items.end();) {
        if (it->tex == &flameTex && now >= it->burnEndTime) {
            int gx = it->x / TILE_SIZE;
            int gy = it->y / TILE_SIZE;

            spreadQueue.push_back({gx, gy});
            occupiedPositions.erase({gx, gy});

            it = items.erase(it);
            redraw = true;
        }
        else {
            ++it;
        }
    }

    for (auto& e : spreadQueue)
        checkAndPropagateBurn(e.gridX, e.gridY);

    spreadQueue.clear();

    if (redraw) glutPostRedisplay();

    glutTimerFunc(50, burnTimerFunc, 1);
}

// ============================================================================
// INPUT
// ============================================================================

void mouse(int button, int state, int x, int y) {
    if (button != GLUT_LEFT_BUTTON || state != GLUT_DOWN)
        return;

    int gx = x/TILE_SIZE;
    int gy = y/TILE_SIZE;

    // placing mode
    if (placeMode == 1 && bagCount > 0) {
        if (gx>=0&&gx<COLS&&gy>=0&&gy<ROWS) {
            if (levelTiles[gy][gx] == 0) {
                if (!occupiedPositions.count({gx,gy})) {
                    Sprite s;
                    s.x = gx*TILE_SIZE;
                    s.y = gy*TILE_SIZE;
                    s.tex = &itemTex;
                    items.push_back(s);

                    occupiedPositions.insert({gx,gy});
                    bagCount--;
                }
            }
            else {
                printf("Cannot place item on wall.\n");
            }
        }
    }

    // burning mode
    else if (placeMode == 2) {
        for (auto& s : items) {
            if ((int)(s.x/TILE_SIZE)==gx &&
                (int)(s.y/TILE_SIZE)==gy &&
                s.tex == &itemTex)
            {
                s.tex = &flameTex;
                s.burnEndTime = getCurrentTimeMillis() + 500;
                break;
            }
        }
    }
}

void keyboard(unsigned char key,int,int) {
    keys[key] = true;

    switch(key) {
        case '1':
            placeMode = 1;
            break;
        case '2':
            placeMode = 2;
            break;
        case 'c': case 'C':
            items.clear();
            occupiedPositions.clear();
            break;
        case 27: exit(0);
    }
}

void keyboardUp(unsigned char key,int,int) {
    keys[key] = false;
}

// ============================================================================
// RENDERING
// ============================================================================

void display() {
    glClearColor(0.1f,0.1f,0.1f,1);
    glClear(GL_COLOR_BUFFER_BIT);

    glLoadIdentity();

    // --- draw tiles ---
    for (int r=0;r<ROWS;r++) {
        for (int c=0;c<COLS;c++) {
            float px = c*TILE_SIZE;
            float py = r*TILE_SIZE;

            if (levelTiles[r][c] == 1)
                drawQuad(px,py,TILE_SIZE,TILE_SIZE, wallTex.id);
            else
                drawQuad(px,py,TILE_SIZE,TILE_SIZE, floorTex.id);
        }
    }

    // --- draw portals ---
    for (auto& p : portals) {
        float px = p.gridX*TILE_SIZE;
        float py = p.gridY*TILE_SIZE;
        drawQuad(px, py, TILE_SIZE, TILE_SIZE, holeTex.id);
    }

    // --- items ---
    for (auto& s : items)
        drawQuad(s.x, s.y, TILE_SIZE, TILE_SIZE, s.tex->id);

    // --- player ---
    drawQuad(player.x, player.y, TILE_SIZE, TILE_SIZE, player.tex->id);

    // --- UI ---
    glDisable(GL_TEXTURE_2D);
    char buf1[64];
    sprintf(buf1, "Level: %d/%d", currLevel+1, NUM_LEVELS);
    renderText(10,20,buf1);

    char buf2[64];
    sprintf(buf2, "Bags: %d", bagCount);
    renderText(10,40,buf2);

    char buf3[64];
    sprintf(buf3, "Mode: %s", (placeMode==1 ? "Place" : "Burn"));
    renderText(10,60,buf3);
    glEnable(GL_TEXTURE_2D);

    glutSwapBuffers();
}

void reshape(int w,int h) {
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0,w,h,0,-1,1);
    glMatrixMode(GL_MODELVIEW);
}

// ============================================================================
// INIT + MAIN
// ============================================================================

void init() {
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    playerTex = loadTexture("player.png");
    itemTex   = loadTexture("item.png");
    wallTex   = loadTexture("wall.png");
    floorTex  = loadTexture("floor.png");
    flameTex  = loadTexture("flame.png");
    holeTex   = loadTexture("hole.png");

    player.tex = &playerTex;

    loadLevel(0);
}

int main(int argc,char** argv) {
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);
    glutInitWindowSize(WIN_W,WIN_H);
    glutCreateWindow("Portal-Level Game — Clean Rewrite");

    init();

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutMouseFunc(mouse);

    glutTimerFunc(0, update, 0);
    glutTimerFunc(50, burnTimerFunc, 1);

    glutMainLoop();
    return 0;
}
