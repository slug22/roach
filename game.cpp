// ============================================================================
// game.cpp
// COMPLETE VERSION WITH MOVEABLE PEBBLES
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
#include "utils.h"
#include <map>
#include <string>
#include <queue>

using std::map;
using std::string;
using std::vector;

// Define GL_CLAMP_TO_EDGE if not available
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

// ============================================================================
// CONFIGURATION / CONSTANTS
// ============================================================================

const int TILE_SIZE = 32;
const int WIN_W = 800;
const int WIN_H = 600;

int currLevel = 0;

const int COLS = WIN_W / TILE_SIZE; // 25 columns
const int ROWS = WIN_H / TILE_SIZE; // 18 rows

const float PLAYER_SPEED = 2.0f;

int placeMode = 1; // 1=place, 2=burn
int bagCount = 10;

bool justTeleported = false;
int spawnPortalID = -1;
map<string, int> inventory = {{"berry", 0}, {"item", 10}};

// ============================================================================
// HELPERS & STRUCTURES
// ============================================================================

struct PairHash {
    std::size_t operator()(const std::pair<int, int>& p) const {
        return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
    }
};

struct Sprite {
    float x, y;
    Texture* tex;
    long long burnEndTime = 0;
};

struct Portal {
    int gridX, gridY;
    int portalID;
    int targetLevel;
    int targetPortalID;
};

struct Berry {
    int gridX, gridY;
    int berryID;
};

struct Enemy {
    float x, y;
    float speed;
    float angle;
    Texture* tex;
    bool alive;
};

struct Pebble {
    float x, y;
    bool isBeingPushed;
    long long pushStartTime;
    float pushDirX, pushDirY;
    bool isSliding;
    int targetGridX, targetGridY;
    float slideProgress;
};

struct BurnCheckEvent {
    int gridX, gridY;
};

struct AStarNode {
    int x, y;
    float g;
    float h;
    int parentX, parentY;
    
    float f() const { return g + h; }
    
    bool operator>(const AStarNode& other) const {
        return f() > other.f();
    }
};

struct EnemyPath {
    std::vector<std::pair<int, int>> path;
    int currentStep = 0;
    int framesUntilRecalc = 0;
    bool isMoving = false;
    int startGridX = 0, startGridY = 0;
    int targetGridX = 0, targetGridY = 0;
    float moveProgress = 0.0f;
};

// ============================================================================
// GLOBAL STATE
// ============================================================================

Texture playerTex, itemTex, wallTex, floorTex, flameTex, holeTex, berryTex, antTex, deadantTex, pebbleTex;
Sprite player;

std::vector<Sprite> items;
std::vector<std::vector<int>> fires;
std::vector<Portal> portals;
std::vector<Berry> berries;
std::vector<Enemy> enemies;
std::vector<Pebble> pebbles;

std::unordered_set<std::pair<int, int>, PairHash> occupiedPositions;

bool keys[256] = {false};

std::vector<BurnCheckEvent> spreadQueue;
std::map<Enemy*, EnemyPath> enemyPaths;

int (*levelTiles)[25] = nullptr;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

bool checkCollision(float newX, float newY);
bool checkPebbleCollision(float x, float y, Pebble* ignorePebble = nullptr);

// ============================================================================
// INVENTORY
// ============================================================================

void addItemtoinventory(string item, int number) {
    inventory[item] += number;
}

// ============================================================================
// COLLISION
// ============================================================================

bool checkPebbleCollision(float x, float y, Pebble* ignorePebble) {
    int gx = x / TILE_SIZE;
    int gy = y / TILE_SIZE;
    
    for (auto& pebble : pebbles) {
        if (&pebble == ignorePebble) continue;
        
        int px = pebble.x / TILE_SIZE;
        int py = pebble.y / TILE_SIZE;
        
        if (gx == px && gy == py) {
            return true;
        }
    }
    return false;
}

bool checkCollision(float newX, float newY) {
    int gx = newX / TILE_SIZE;
    int gy = newY / TILE_SIZE;

    if (gx < 0 || gx >= COLS || gy < 0 || gy >= ROWS)
        return true;

    if (levelTiles[gy][gx] == 1)
        return true;

    // Check pebble collision
    for (const auto& pebble : pebbles) {
        int px = pebble.x / TILE_SIZE;
        int py = pebble.y / TILE_SIZE;
        if (gx == px && gy == py) {
            return true;
        }
    }

    return false;
}

// ============================================================================
// PORTAL SYSTEM
// ============================================================================

void loadPortals() {
    portals.clear();
    const std::vector<PortalDef>& defs = Levels[currLevel].portals;
    bagCount = 10;
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

void loadBerries() {
    berries.clear();
    const std::vector<BerryDef>& defs = Levels[currLevel].berries;
    for (const auto& def : defs) {
        Berry B;
        B.gridX = def.x;
        B.gridY = def.y;
        B.berryID = def.berryID;
        berries.push_back(B);
        printf("Loaded Berry ID %d at [%d,%d]\n", B.berryID, B.gridX, B.gridY);
    }
}

void loadEnemies() {
    enemies.clear();
    const std::vector<EnemyDef>& defs = Levels[currLevel].enemies;
    for (const auto& def : defs) {
        Enemy E;
        E.x = def.x * TILE_SIZE;
        E.y = def.y * TILE_SIZE;
        E.speed = 1.0f;
        E.angle = 0.0f;
        E.tex = &antTex;
        E.alive = true;
        enemies.push_back(E);
        printf("Loaded Enemy ID %d at [%d,%d]\n", def.enemyID, def.x, def.y);
    }
}

void loadPebbles() {
    pebbles.clear();
    const std::vector<PebbleDef>& defs = Levels[currLevel].pebbles;
    for (const auto& def : defs) {
        Pebble P;
        P.x = def.x * TILE_SIZE;
        P.y = def.y * TILE_SIZE;
        P.isBeingPushed = false;
        P.pushStartTime = 0;
        P.pushDirX = 0.0f;
        P.pushDirY = 0.0f;
        P.isSliding = false;
        P.targetGridX = 0;
        P.targetGridY = 0;
        P.slideProgress = 0.0f;
        pebbles.push_back(P);
        printf("Loaded Pebble ID %d at [%d,%d]\n", def.pebbleID, def.x, def.y);
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
    levelTiles = Levels[currLevel].tiles;

    items.clear();
    occupiedPositions.clear();
    spreadQueue.clear();
    bagCount = 10;

    loadPortals();
    loadBerries();
    loadEnemies();
    loadPebbles();
    
    if (fromPortalID >= 0) {
        Portal* spawnP = findPortalByID(fromPortalID);
        if (spawnP) {
            player.x = spawnP->gridX * TILE_SIZE;
            player.y = spawnP->gridY * TILE_SIZE;
            spawnPortalID = fromPortalID;
            justTeleported = true;
            printf("Spawned at portal ID %d in level %d\n", fromPortalID, currLevel);
        } else {
            player.x = 64;
            player.y = 64;
            spawnPortalID = -1;
            justTeleported = false;
        }
    } else {
        player.x = 64;
        player.y = 64;
        spawnPortalID = -1;
        justTeleported = false;
    }

    printf("\n=== Loaded Level %d ===\n", currLevel);
}

// ============================================================================
// PORTAL + ITEM CHECKS
// ============================================================================

void checkPortalCollision() {
    int gx = player.x / TILE_SIZE;
    int gy = player.y / TILE_SIZE;

    if (justTeleported && spawnPortalID >= 0) {
        Portal* spawnP = findPortalByID(spawnPortalID);
        if (spawnP && (spawnP->gridX != gx || spawnP->gridY-1 != gy)) {
            justTeleported = false;
            printf("Player moved off spawn portal -- teleport enabled.\n");
        }
        return;
    }

    for (const auto& p : portals) {
        if (gx == p.gridX && gy == p.gridY-1) {
            printf("Entered Portal ID %d -- going to Level %d\n", p.portalID, p.targetLevel);
            loadLevel(p.targetLevel, p.targetPortalID);
            glutPostRedisplay();
            return;
        }
    }
}

void checkItemPickup() {
    int gx = player.x / TILE_SIZE;
    int gy = player.y / TILE_SIZE;
    for (auto it = berries.begin(); it != berries.end(); ) {
        if (gx == it->gridX && gy == it->gridY-1) {
            printf("pickedup berry\n");
            addItemtoinventory("berry", 1);
            it = berries.erase(it);
            glutPostRedisplay();
            return;
        } else {
            ++it;
        }
    }
}

void checkEnemyCollision() {
    for (const auto& enemy : enemies) {
        if (!enemy.alive) continue;
        float dx = player.x - enemy.x;
        float dy = player.y - enemy.y;
        float dist = sqrt(dx * dx + dy * dy);
        
        if (dist < TILE_SIZE * 0.8f) {
            printf("Hit by enemy! Reloading level...\n");
            loadLevel(currLevel);
            return;
        }
    }
}

void checkEnemyFire() {
    for (auto it = enemies.begin(); it != enemies.end(); ) {
        for (const auto& fire : fires) {
            float fireX = fire[0] * TILE_SIZE;
            float fireY = fire[1] * TILE_SIZE;
            
            float dx = fireX - it->x;
            float dy = fireY - it->y;
            float dist = sqrt(dx * dx + dy * dy);
            
            if (dist < TILE_SIZE * 0.8f) {
                printf("Enemy roasted!\n");
                it->tex = &deadantTex;
                it->alive = false;
                break;
            }
        }
        it++;
    }
}

// ============================================================================
// PEBBLE SYSTEM
// ============================================================================
inline void getPlayerFeetGrid(int& gx, int& gy) {
    const float COLLISION_HEIGHT = TILE_SIZE / 4.0f;
    const float COLLISION_TOP_OFFSET = TILE_SIZE - COLLISION_HEIGHT;

    float feetX = player.x + TILE_SIZE * 0.5f;
    float feetY = player.y + COLLISION_TOP_OFFSET + COLLISION_HEIGHT * 0.5f;

    gx = (int)(feetX / TILE_SIZE);
    gy = (int)(feetY / TILE_SIZE);
}

void startPushingPebble(float /*playerX*/, float /*playerY*/,
                        float pushDirX, float pushDirY)
{
    int playerGridX, playerGridY;
    getPlayerFeetGrid(playerGridX, playerGridY);

    int checkGridX = playerGridX + (pushDirX > 0 ? 1 : (pushDirX < 0 ? -1 : 0));
    int checkGridY = playerGridY + (pushDirY > 0 ? 1 : (pushDirY < 0 ? -1 : 0));

    for (auto& pebble : pebbles) {
        if (pebble.isSliding) continue;

        int pebbleGridX = (int)(pebble.x / TILE_SIZE);
        int pebbleGridY = (int)(pebble.y / TILE_SIZE);

        if (pebbleGridX == checkGridX &&
            pebbleGridY == checkGridY)
        {
            if (!pebble.isBeingPushed) {
                pebble.isBeingPushed = true;
                pebble.pushStartTime = getCurrentTimeMillis();
                pebble.pushDirX = pushDirX;
                pebble.pushDirY = pushDirY;

                printf(
                    "PUSH pebble at [%d,%d] from feet [%d,%d] dir [%.0f,%.0f]\n",
                    pebbleGridX, pebbleGridY,
                    playerGridX, playerGridY,
                    pushDirX, pushDirY
                );
            }
            return;
        }
    }
}

void stopPushingPebbles() {
    for (auto& pebble : pebbles) {
        if (pebble.isBeingPushed) {
            pebble.isBeingPushed = false;
            printf("Stopped pushing pebble.\n");
        }
    }
}

void updatePebbles() {
    long long now = getCurrentTimeMillis();
    
    for (auto& pebble : pebbles) {
        // Check if push duration reached 0.5 seconds
        if (pebble.isBeingPushed && !pebble.isSliding) {
            if (now - pebble.pushStartTime >= 500) {
                // Try to slide the pebble
                int currentGridX = (int)round(pebble.x / TILE_SIZE);
                int currentGridY = (int)round(pebble.y / TILE_SIZE);
                
                int targetX = currentGridX + (int)pebble.pushDirX;
                int targetY = currentGridY + (int)pebble.pushDirY;
                
                printf("Trying to slide pebble from [%d,%d] to [%d,%d]\n", 
                       currentGridX, currentGridY, targetX, targetY);
                
                // Check if target tile is free
                if (!checkCollision(targetX * TILE_SIZE, targetY * TILE_SIZE) &&
                    !checkPebbleCollision(targetX * TILE_SIZE, targetY * TILE_SIZE, &pebble)) {
                    
                    // Start sliding!
                    pebble.isSliding = true;
                    pebble.targetGridX = targetX;
                    pebble.targetGridY = targetY;
                    pebble.slideProgress = 0.0f;
                    pebble.isBeingPushed = false;
                    printf("Pebble sliding to [%d,%d]!\n", targetX, targetY);
                } else {
                    pebble.isBeingPushed = false;
                    printf("Can't slide pebble - blocked!\n");
                }
            }
        }
        
        // Handle sliding animation
        if (pebble.isSliding) {
            const float SLIDE_SPEED = 0.1f; // Adjust for faster/slower slide
            pebble.slideProgress += SLIDE_SPEED;
            
            if (pebble.slideProgress >= 1.0f) {
                // Reached target
                pebble.x = pebble.targetGridX * TILE_SIZE;
                pebble.y = pebble.targetGridY * TILE_SIZE;
                pebble.isSliding = false;
                pebble.slideProgress = 0.0f;
                printf("Pebble finished sliding at [%d,%d].\n", pebble.targetGridX, pebble.targetGridY);
            } else {
                // Interpolate position - calculate start position from current and target
                int startGridX = pebble.targetGridX - (int)pebble.pushDirX;
                int startGridY = pebble.targetGridY - (int)pebble.pushDirY;
                
                float startX = startGridX * TILE_SIZE;
                float startY = startGridY * TILE_SIZE;
                float targetX = pebble.targetGridX * TILE_SIZE;
                float targetY = pebble.targetGridY * TILE_SIZE;
                
                pebble.x = startX + (targetX - startX) * pebble.slideProgress;
                pebble.y = startY + (targetY - startY) * pebble.slideProgress;
            }
        }
    }
}

// ============================================================================
// ENEMY AI
// ============================================================================

float heuristic(int x1, int y1, int x2, int y2) {
    return abs(x1 - x2) + abs(y1 - y2);
}

bool findPathAStar(int startX, int startY, int goalX, int goalY, std::vector<std::pair<int, int>>& outPath) {
    outPath.clear();
    
    if (startX == goalX && startY == goalY) return false;
    if (startX < 0 || startX >= COLS || startY < 0 || startY >= ROWS) return false;
    if (goalX < 0 || goalX >= COLS || goalY < 0 || goalY >= ROWS) return false;
    if (levelTiles[goalY][goalX] == 1) return false;
    
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openSet;
    bool closedSet[ROWS][COLS] = {false};
    std::map<std::pair<int,int>, std::pair<int,int>> cameFrom;
    float gScore[ROWS][COLS];
    
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            gScore[r][c] = 1e9;
        }
    }
    
    gScore[startY][startX] = 0;
    openSet.push({startX, startY, 0, heuristic(startX, startY, goalX, goalY), -1, -1});
    
    int dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
    
    int iterations = 0;
    const int MAX_ITERATIONS = 500;
    
    while (!openSet.empty() && iterations < MAX_ITERATIONS) {
        iterations++;
        
        AStarNode current = openSet.top();
        openSet.pop();
        
        if (closedSet[current.y][current.x]) continue;
        closedSet[current.y][current.x] = true;
        
        if (current.parentX >= 0 && current.parentY >= 0) {
            cameFrom[{current.x, current.y}] = {current.parentX, current.parentY};
        }
        
        if (current.x == goalX && current.y == goalY) {
            std::vector<std::pair<int, int>> reversePath;
            int cx = goalX, cy = goalY;
            
            while (cameFrom.count({cx, cy})) {
                reversePath.push_back({cx, cy});
                auto parent = cameFrom[{cx, cy}];
                cx = parent.first;
                cy = parent.second;
            }
            
            for (int i = reversePath.size() - 1; i >= 0; i--) {
                outPath.push_back(reversePath[i]);
            }
            
            return true;
        }
        
        for (int i = 0; i < 4; i++) {
            int nx = current.x + dirs[i][0];
            int ny = current.y + dirs[i][1];
            
            if (nx < 0 || nx >= COLS || ny < 0 || ny >= ROWS) continue;
            if (levelTiles[ny][nx] == 1) continue;
            if (closedSet[ny][nx]) continue;
            
            float tentativeG = current.g + 1.0f;
            
            if (tentativeG < gScore[ny][nx]) {
                gScore[ny][nx] = tentativeG;
                float h = heuristic(nx, ny, goalX, goalY);
                openSet.push({nx, ny, tentativeG, h, current.x, current.y});
            }
        }
    }
    
    return false;
}

void updateEnemies() {
    for (auto& enemy : enemies) {
        if (!enemy.alive) continue;
        
        if (enemyPaths.find(&enemy) == enemyPaths.end()) {
            enemyPaths[&enemy] = EnemyPath();
        }
        
        EnemyPath& pathData = enemyPaths[&enemy];
        
        int enemyGridX, enemyGridY;
        if (!pathData.isMoving) {
            enemyGridX = (int)round(enemy.x / TILE_SIZE);
            enemyGridY = (int)round(enemy.y / TILE_SIZE);
        } else {
            enemyGridX = pathData.startGridX;
            enemyGridY = pathData.startGridY;
        }
        
        const float COLLISION_HEIGHT = TILE_SIZE / 4.0f;
        const float COLLISION_TOP_OFFSET = TILE_SIZE - COLLISION_HEIGHT;
        float playerFeetY = player.y + COLLISION_TOP_OFFSET + (COLLISION_HEIGHT / 2.0f);
        
        int playerGridX = (int)(player.x / TILE_SIZE);
        int playerGridY = (int)(playerFeetY / TILE_SIZE);
        
        if (!pathData.isMoving && (pathData.framesUntilRecalc <= 0 || pathData.path.empty())) {
            std::vector<std::pair<int, int>> newPath;
            if (findPathAStar(enemyGridX, enemyGridY, playerGridX, playerGridY, newPath)) {
                pathData.path = newPath;
                pathData.currentStep = 0;
                pathData.framesUntilRecalc = 30;
            } else {
                pathData.path.clear();
                pathData.framesUntilRecalc = 30;
                continue;
            }
        }
        
        pathData.framesUntilRecalc--;
        
        if (!pathData.isMoving) {
            if (!pathData.path.empty() && pathData.currentStep < pathData.path.size()) {
                pathData.startGridX = enemyGridX;
                pathData.startGridY = enemyGridY;
                pathData.targetGridX = pathData.path[pathData.currentStep].first;
                pathData.targetGridY = pathData.path[pathData.currentStep].second;
                pathData.moveProgress = 0.0f;
                pathData.isMoving = true;
                
                int dx = pathData.targetGridX - pathData.startGridX;
                int dy = pathData.targetGridY - pathData.startGridY;
                
                if (dx > 0) enemy.angle = -90;
                else if (dx < 0) enemy.angle = 90;
                else if (dy > 0) enemy.angle = 0;
                else if (dy < 0) enemy.angle = 180;
            }
        }
        
        if (pathData.isMoving) {
            const float MOVE_SPEED = 0.05f;
            pathData.moveProgress += MOVE_SPEED;
            
            if (pathData.moveProgress >= 1.0f) {
                pathData.moveProgress = 1.0f;
                pathData.isMoving = false;
                pathData.currentStep++;
                
                enemy.x = pathData.targetGridX * TILE_SIZE;
                enemy.y = pathData.targetGridY * TILE_SIZE;
                
                if (pathData.currentStep >= pathData.path.size()) {
                    pathData.framesUntilRecalc = 0;
                }
            } else {
                float startX = pathData.startGridX * TILE_SIZE;
                float startY = pathData.startGridY * TILE_SIZE;
                float targetX = pathData.targetGridX * TILE_SIZE;
                float targetY = pathData.targetGridY * TILE_SIZE;
                
                enemy.x = startX + (targetX - startX) * pathData.moveProgress;
                enemy.y = startY + (targetY - startY) * pathData.moveProgress;
            }
        }
    }
}

// ============================================================================
// UPDATE LOOP
// ============================================================================

void update(int) {
    float dx = 0, dy = 0;
    float pushDirX = 0, pushDirY = 0;
    bool isPushing = false;
    
    if (keys['w']||keys['W']) { dy -= PLAYER_SPEED; pushDirY = -1; isPushing = true; }
    if (keys['s']||keys['S']) { dy += PLAYER_SPEED; pushDirY = 1; isPushing = true; }
    if (keys['a']||keys['A']) { dx -= PLAYER_SPEED; pushDirX = -1; isPushing = true; }
    if (keys['d']||keys['D']) { dx += PLAYER_SPEED; pushDirX = 1; isPushing = true; }

    const float COLLISION_HEIGHT = TILE_SIZE/4.0f;
    const float COLLISION_TOP_OFFSET = TILE_SIZE - COLLISION_HEIGHT;
    const float INSET = 1.0f;

    float nextX = player.x + dx;
    bool blockX = false;

    if (checkCollision(nextX+INSET, player.y+COLLISION_TOP_OFFSET)) blockX = true;
    if (checkCollision(nextX+INSET, player.y+TILE_SIZE-INSET)) blockX = true;
    if (checkCollision(nextX+TILE_SIZE-INSET, player.y+COLLISION_TOP_OFFSET)) blockX = true;
    if (checkCollision(nextX+TILE_SIZE-INSET, player.y+TILE_SIZE-INSET)) blockX = true;

    if (blockX && pushDirX != 0 && isPushing) {
        startPushingPebble(player.x, player.y, pushDirX, 0);
    }

    if (!blockX) player.x = nextX;

    float nextY = player.y + dy;
    bool blockY = false;

    if (checkCollision(player.x+INSET, nextY+COLLISION_TOP_OFFSET)) blockY = true;
    if (checkCollision(player.x+TILE_SIZE-INSET, nextY+COLLISION_TOP_OFFSET)) blockY = true;
    if (checkCollision(player.x+INSET, nextY+TILE_SIZE-INSET)) blockY = true;
    if (checkCollision(player.x+TILE_SIZE-INSET, nextY+TILE_SIZE-INSET)) blockY = true;

    if (blockY && pushDirY != 0 && isPushing) {
        startPushingPebble(player.x, player.y, 0, pushDirY);
    }

    if (!blockY) player.y = nextY;

    // Stop pushing if not blocked or not pressing keys
    if (!isPushing || (!blockX && !blockY)) {
        stopPushingPebbles();
    }

    updatePebbles();
    updateEnemies();
    checkPortalCollision();
    checkItemPickup();
    checkEnemyCollision();
    checkEnemyFire();
    
    glutPostRedisplay();
    glutTimerFunc(16, update, 0);
}

// ============================================================================
// FIRE / BURN LOGIC
// ============================================================================

void checkAndPropagateBurn(int gx, int gy) {
    int nbr[8][2] = {
        {gx, gy-1}, {gx, gy+1}, {gx-1, gy}, {gx+1, gy},
        {gx-1, gy-1}, {gx-1, gy+1}, {gx+1, gy-1}, {gx+1, gy+1}
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
                fires.push_back({nx,ny});
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

    for (auto it = items.begin(); it != items.end();) {
        if (it->tex == &flameTex && now >= it->burnEndTime) {
            int gx = it->x / TILE_SIZE;
            int gy = it->y / TILE_SIZE;
            fires = {};
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

    else if (placeMode == 2) {
        for (auto& s : items) {
            if ((int)(s.x/TILE_SIZE)==gx &&
                (int)(s.y/TILE_SIZE)==gy &&
                s.tex == &itemTex)
            {
                s.tex = &flameTex;
                fires.push_back({gx,gy});
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
    
    // --- draw berries ---
    for (auto& p : berries) {
        float px = p.gridX*TILE_SIZE;
        float py = p.gridY*TILE_SIZE;
        drawQuad(px, py, TILE_SIZE, TILE_SIZE, berryTex.id);
    }    

    // --- items ---
    for (auto& s : items)
        drawQuad(s.x, s.y, TILE_SIZE, TILE_SIZE, s.tex->id);

    // --- pebbles ---
    for (auto& p : pebbles)
        drawQuad(p.x, p.y, TILE_SIZE, TILE_SIZE, pebbleTex.id);

    // --- enemies ---
    for (auto& e : enemies)
        drawQuadRotated(e.x, e.y, TILE_SIZE, TILE_SIZE, e.tex->id, e.angle);

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
    
    char buf4[64];
    sprintf(buf4, "Berries: %d", inventory["berry"]);
    renderText(10,80,buf4);
    
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
    berryTex  = loadTexture("berry.png");
    antTex    = loadTexture("ant.png");
    deadantTex = loadTexture("deadant.png");
    pebbleTex = loadTexture("pebble.png");

    player.tex = &playerTex;

    loadLevel(0);
}

int main(int argc,char** argv) {
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);
    glutInitWindowSize(WIN_W,WIN_H);
    glutCreateWindow("Portal-Level Game — With Pebbles!");

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