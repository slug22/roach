#include <GL/glut.h>
#include <GL/gl.h>
#include <vector>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <chrono> // Required for timing and flame duration
#include <vector> // Already included, but ensuring it's recognized for std::vector

// Define GL_CLAMP_TO_EDGE if not available
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#include "game.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// --- Constants & Config ---
const int TILE_SIZE = 32;
const int WIN_W = 800;
const int WIN_H = 600;

// Calculate grid dimensions based on window size
const int COLS = WIN_W / TILE_SIZE;
const int ROWS = WIN_H / TILE_SIZE;

const float PLAYER_SPEED = 2.0f; // Pixels per frame
int placeMode = 1; // 1 = Place Item, 2 = Burn Item

// --- Structs ---
struct Texture {
    GLuint id;
    int w, h;
};

struct Sprite {
    float x, y; // Top-left coordinate in pixels
    Texture* tex;
    // Timer to track how long a sprite has been "burning" in milliseconds
    long long burnEndTime = 0; 
};

// NEW: Structure to hold data for the burn event queue (used to spread fire)
struct BurnCheckEvent {
    int gridX, gridY;
};

// --- Global State ---
Texture playerTex, itemTex, wallTex, floorTex, flameTex; // flameTex added
Sprite player;
std::vector<Sprite> items;
bool keys[256] = {false};
// Queue to hold coordinates of tiles that just finished burning 
std::vector<BurnCheckEvent> spreadQueue; 

// Simple Level Data: 0 = Floor, 1 = Wall
// 25 Columns x 19 Rows (approx fits 800x600)
int levelData[19][25] = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,1,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};

// --- Helper Functions ---

// Gets current time in milliseconds since epoch
long long getCurrentTimeMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

Texture loadTexture(const char* path) {
    Texture tex;
    int channels;
    // Force 4 channels (RGBA) so we don't worry about alignment
    unsigned char* data = stbi_load(path, &tex.w, &tex.h, &channels, 4);
    
    if (!data) {
        printf("Failed to load texture: %s. Using fallback.\n", path);
        tex.id = 0;
        return tex;
    }
    
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    
    // Pixel-art friendly filtering (Nearest Neighbor)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.w, tex.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    stbi_image_free(data);
    return tex;
}

// Draw a sprite at specific pixel coordinates
void drawQuad(float x, float y, float w, float h, GLuint texId) {
    glBindTexture(GL_TEXTURE_2D, texId);
    glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x, y);
        glTexCoord2f(1, 0); glVertex2f(x + w, y);
        glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
        glTexCoord2f(0, 1); glVertex2f(x, y + h);
    glEnd();
}

void init() {
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Load PNGs (Ensure these exist in your folder)
    playerTex = loadTexture("player.png");
    itemTex   = loadTexture("item.png");
    wallTex   = loadTexture("wall.png");
    floorTex  = loadTexture("floor.png");
    flameTex  = loadTexture("flame.png"); // Load flame texture
    
    // Start player at grid index (2, 2)
    player.x = 2 * TILE_SIZE;
    player.y = 2 * TILE_SIZE;
    player.tex = &playerTex;
}

void display() {
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    
    // 1. Draw Map (Walls and Floors)
    for (int r = 0; r < 19; r++) {
        for (int c = 0; c < 25; c++) {
            float px = c * TILE_SIZE;
            float py = r * TILE_SIZE;
            
            if (levelData[r][c] == 1) {
                drawQuad(px, py, TILE_SIZE, TILE_SIZE, wallTex.id);
            } else {
                drawQuad(px, py, TILE_SIZE, TILE_SIZE, floorTex.id);
            }
        }
    }
    
    // 2. Draw Placed Items
    for (const auto& item : items) {
        drawQuad(item.x, item.y, TILE_SIZE, TILE_SIZE, item.tex->id);
    }
    
    // 3. Draw Player
    drawQuad(player.x, player.y, TILE_SIZE, TILE_SIZE, player.tex->id);
    
    glutSwapBuffers();
}

bool checkCollision(float newX, float newY) {
    // 1. Convert pixel coordinates to grid indices
    int gridX = (int)newX / TILE_SIZE;
    int gridY = (int)newY / TILE_SIZE;

    // 2. Check bounds (prevent going outside the map)
    if (gridX < 0 || gridX >= COLS || gridY < 0 || gridY >= ROWS) {
        return true; // Treat outside world as solid wall
    }

    // 3. Check level data (1 = Wall)
    if (levelData[gridY][gridX] == 1) {
        return true;
    }

    return false;
}

void update(int value) {
    float dx = 0;
    float dy = 0;

    if (keys['w'] || keys['W']) dy -= PLAYER_SPEED;
    if (keys['s'] || keys['S']) dy += PLAYER_SPEED;
    if (keys['a'] || keys['A']) dx -= PLAYER_SPEED;
    if (keys['d'] || keys['D']) dx += PLAYER_SPEED;

    // --- Collision Zone Definition (Bottom 1/4th) ---
    const float COLLISION_HEIGHT = TILE_SIZE / 4.0f; 
    const float COLLISION_TOP_Y_OFFSET = TILE_SIZE - COLLISION_HEIGHT;
    const float HORIZONTAL_INSET = 1.0f;

    // --- X AXIS MOVEMENT ---
    float nextX = player.x + dx;
    bool colX = false;
    
    // Check Left Side (Top of collision zone)
    if (checkCollision(nextX + HORIZONTAL_INSET, player.y + COLLISION_TOP_Y_OFFSET)) colX = true;
    // Check Left Side (Bottom of sprite)
    if (checkCollision(nextX + HORIZONTAL_INSET, player.y + TILE_SIZE - HORIZONTAL_INSET)) colX = true;
    
    // Check Right Side (Top of collision zone)
    if (checkCollision(nextX + TILE_SIZE - HORIZONTAL_INSET, player.y + COLLISION_TOP_Y_OFFSET)) colX = true;
    // Check Right Side (Bottom of sprite)
    if (checkCollision(nextX + TILE_SIZE - HORIZONTAL_INSET, player.y + TILE_SIZE - HORIZONTAL_INSET)) colX = true;

    if (!colX) {
        player.x = nextX;
    }

    // --- Y AXIS MOVEMENT ---
    float nextY = player.y + dy;
    bool colY = false;
    
    // Check Top-Left of the collision zone
    if (checkCollision(player.x + HORIZONTAL_INSET, nextY + COLLISION_TOP_Y_OFFSET)) colY = true;
    // Check Top-Right of the collision zone
    if (checkCollision(player.x + TILE_SIZE - HORIZONTAL_INSET, nextY + COLLISION_TOP_Y_OFFSET)) colY = true;
    
    // Check Bottom-Left of the collision zone (bottom of sprite)
    if (checkCollision(player.x + HORIZONTAL_INSET, nextY + TILE_SIZE - HORIZONTAL_INSET)) colY = true;
    // Check Bottom-Right of the collision zone (bottom of sprite)
    if (checkCollision(player.x + TILE_SIZE - HORIZONTAL_INSET, nextY + TILE_SIZE - HORIZONTAL_INSET)) colY = true;

    if (!colY) {
        player.y = nextY;
    }

    glutPostRedisplay();
    glutTimerFunc(16, update, 0); // Main game loop timer (approx 60 FPS)
}


// Function to check neighbors and start the burn timer on them.
void checkAndPropagateBurn(int gridX, int gridY) {
    // Check 4 orthogonal neighbors (top, bottom, left, right)
    int neighbors[4][2] = {
        {gridX, gridY - 1}, // Top
        {gridX, gridY + 1}, // Bottom
        {gridX - 1, gridY}, // Left
        {gridX + 1, gridY}  // Right
    };

    for (int i = 0; i < 4; ++i) {
        int nx = neighbors[i][0];
        int ny = neighbors[i][1];
        
        // Check map bounds
        if (nx >= 0 && nx < COLS && ny >= 0 && ny < ROWS) {
            
            // Find the item at the neighbor grid cell
            for (auto& item : items) {
                if ((int)(item.x / TILE_SIZE) == nx && (int)(item.y / TILE_SIZE) == ny) {
                    
                    // Found an item. Check if it's currently unburnt.
                    if (item.tex == &itemTex) { 
                        item.tex = &flameTex; // Change to flame texture
                        item.burnEndTime = getCurrentTimeMillis() + 500; // Set 500ms timer
                        
                        printf("Spread burn to Grid[%d, %d]\n", nx, ny);
                        
                        glutPostRedisplay();
                    }
                    // Since we are only interested in one item per tile, we can break
                    break;
                }
            }
        }
    }
}

// Function to handle item removal and initiate the fire spread
void burnTimerFunc(int value) {
    long long currentTime = getCurrentTimeMillis();
    bool needsRedisplay = false;
    
    // 1. Handle Removal of Burned Items and Queue Spread Events
    for (auto it = items.begin(); it != items.end(); ) {
        if (it->tex == &flameTex && currentTime >= it->burnEndTime) {
            // Item finished burning, add its coordinates to the spread queue
            spreadQueue.push_back({(int)(it->x / TILE_SIZE), (int)(it->y / TILE_SIZE)});
            
            printf("Item at Grid[%d, %d] removed. Scheduling spread.\n", 
                   (int)(it->x / TILE_SIZE), (int)(it->y / TILE_SIZE));
            
            it = items.erase(it); // erase returns the iterator to the next element
            needsRedisplay = true;
        } else {
            ++it;
        }
    }

    // 2. Handle Fire Propagation from Spread Queue
    for(const auto& event : spreadQueue) {
        checkAndPropagateBurn(event.gridX, event.gridY);
    }
    spreadQueue.clear(); // Clear the queue after processing

    if (needsRedisplay) {
        glutPostRedisplay();
    }
    
    // Re-schedule this timer function for the next check (e.g., every 50ms)
    // The value 1 ensures it's distinct from update(0).
    glutTimerFunc(50, burnTimerFunc, 1); 
}

void mouse(int button, int state, int x, int y) {
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        
        int gridX = x / TILE_SIZE;
        int gridY = y / TILE_SIZE;

        if (placeMode == 1) { // --- PLACE MODE ---
            
            if (gridX >= 0 && gridX < 25 && gridY >= 0 && gridY < 19) {
                if (levelData[gridY][gridX] == 0) {
                    
                    Sprite item;
                    item.x = gridX * TILE_SIZE;
                    item.y = gridY * TILE_SIZE;
                    item.tex = &itemTex;
                    items.push_back(item);
                    
                    printf("Placed item at Grid[%d, %d]\n", gridX, gridY);
                } else {
                    printf("Cannot place item on a wall!\n");
                }
            }
        } else if (placeMode == 2) { // --- BURN MODE ---
            
            bool itemClicked = false;
            // Find and start the burn on the clicked item
            for (auto& item : items) {
                if ((int)(item.x / TILE_SIZE) == gridX && (int)(item.y / TILE_SIZE) == gridY) {
                    
                    if (item.tex == &itemTex) { // Only burn unburnt items
                        itemClicked = true;
                        
                        printf("Initial burn started at Grid[%d, %d]\n", gridX, gridY);
                        item.tex = &flameTex;
                        item.burnEndTime = getCurrentTimeMillis() + 500;
                        
                        // The burnTimerFunc will handle the delayed spread when burnEndTime is hit.
                        
                        glutPostRedisplay();
                        break;
                    }
                }
            }
            if (!itemClicked) {
                 printf("No unburnt item found at Grid[%d, %d] to burn.\n", gridX, gridY);
            }
        }
    }
}

void keyboard(unsigned char key, int x, int y) {
    keys[key] = true;
    if (key == 'c' || key == 'C') items.clear();
    if (key == '1') {
        placeMode = 1;
        printf("Switched to Place Mode (1).\n");
    }
    if (key == '2') {
        placeMode = 2;
        printf("Switched to Burn Mode (2).\n");
    }
    if (key == 27) exit(0);
}

void keyboardUp(unsigned char key, int x, int y) {
    keys[key] = false;
}

void reshape(int w, int h) {
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    
    // Set coordinate system to Pixels: (0,0) top-left, (w,h) bottom-right
    glOrtho(0, w, h, 0, -1, 1);
    
    glMatrixMode(GL_MODELVIEW);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WIN_W, WIN_H);
    glutCreateWindow("Tile System Demo (Gradual Burn Effect)");
    
    init();
    
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutMouseFunc(mouse);
    
    // Timer 0: Main game loop (movement, display updates)
    glutTimerFunc(0, update, 0); 
    // Timer 1: Fire/item removal and propagation logic (checks every 50ms)
    glutTimerFunc(50, burnTimerFunc, 1); 
    
    printf("\nControls:\n");
    printf("WASD - Move Player\n");
    printf("1 - Switch to PLACE MODE\n");
    printf("2 - Switch to BURN MODE\n");
    printf("--- In PLACE MODE (1) ---\n");
    printf("CLICK - Place item (Snaps to Floor tiles only)\n");
    printf("--- In BURN MODE (2) ---\n");
    printf("CLICK - Start a spreading fire on an item (500ms delay per tile)\n");
    printf("C - Clear all items\n");
    
    glutMainLoop();
    return 0;
}