#ifdef _WIN32
#include <windows.h>
#endif

#include <GL/glut.h>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <vector>
#include <cstdio>
#include <cstring>
#include <string>

// ================= Configuration =================
enum GameState { STATE_MENU=0, STATE_WAITING=1, STATE_COUNTDOWN=2, STATE_RACING=3, STATE_FINISHED=4 };
static GameState gameState = STATE_MENU;

const int NUM_LANES = 5;
const float LANE_WIDTH = 3.0f;
const float ROAD_HALF_WIDTH = (NUM_LANES * LANE_WIDTH) / 2.0f;
const float WORLD_SPEED_BASE = 50.0f;
static int windowWidth = 1024, windowHeight = 720;
const float startLineZ = -10.0f;
const float finishLineZ = 700.0f;
// ==================================================

// ================= Player / Input =================
static bool keyStates[256] = {0};
static float playerOffsetX = 0.0f;
static float targetOffsetX = 0.0f;
static float steerSpeed = 0.16f;
static float forwardSpeed = 0.0f;
static int playerLane = 2;

// countdown
static bool startCountDown = false;
static int countValue = 3;
static float lastCountSec = 0.0f;

// overlay / menu
static bool menuOverlay = false;
static GameState savedState = STATE_MENU;
static float savedPlayerZ = 0.0f;
static float savedPlayerSpeed = 0.0f;
static float savedOffsetX = 0.0f;
static float savedTargetOffsetX = 0.0f;
static bool firstLaunch = true;

// a flag that marks the countdown was started as a result of resuming from overlay
static bool countdownFromOverlay = false;

// timing
static float elapsedSinceStart = 0.0f;

// ========== NEW: race timer & score & collision helpers ==========
static float raceTimer = 60.0f;         // seconds remaining
static bool timerActive = false;
static bool timeOver = false;
static float savedRaceTimer = 60.0f;    // saved when opening overlay
static bool savedTimerActive = false;
static bool countdownPaused = false;
static int scoreValue = 0;
static std::vector<char> overtaken;     // overtaken flags per opponent
static std::vector<int> collisionCooldown; // frames cooldown to avoid repeated penalty
// ==================================================================

// ================= Car struct and containers =================
struct Car {
    int lane;
    float z;
    float speed;
    float colorR, colorG, colorB;
    bool isPlayer;
    bool finished;
    int finishPos;
};
static std::vector<Car> cars;
static std::vector<int> finishOrder;

// ========== Clouds ==========
struct Cloud {
    float x, y, z;
    float speed;
    float scale;
};
static std::vector<Cloud> clouds;

// ========== Fireworks ==========
struct FWParticle {
    float x,y,z;
    float vx,vy,vz;
    float life;   // seconds remaining
    float r,g,b;
};
static std::vector<FWParticle> fireworks;
static bool fireworksActive = false;

// ========== Game Over Flag ==========
static bool playerLost = false;
// ============================

// ================= Helpers =================
inline void setColor(float r, float g, float b) { glColor3f(r,g,b); }

static void drawCube(float sx, float sy, float sz) {
    glPushMatrix();
    glScalef(sx, sy, sz);
    glutSolidCube(1.0);
    glPopMatrix();
}

static void drawWheel(float radius, float width) {
    glPushMatrix();
    glRotatef(90,0,1,0);
    glutSolidTorus(width*0.28f, radius, 12, 24);
    glPopMatrix();
}

static void drawTree() {
    // trunk
    glPushMatrix();
    setColor(0.45f,0.25f,0.08f);
    glTranslatef(0,1.0f,0);
    glScalef(0.4f,2.0f,0.4f);
    glutSolidCube(1.0);
    glPopMatrix();
    // foliage
    setColor(0.05f,0.6f,0.05f);
    glPushMatrix();
    glTranslatef(0,2.2f,0);
    glutSolidCone(1.0,2.0,12,2);
    glTranslatef(0,1.0f,0);
    glutSolidCone(0.8,1.6,12,2);
    glTranslatef(0,0.7f,0);
    glutSolidCone(0.6,1.2,12,2);
    glPopMatrix();
}

static void drawBuilding(float width, float height, float depth) {
    setColor(0.6f,0.6f,0.65f);
    glPushMatrix();
    glTranslatef(0,height/2.0f,0);
    glScalef(width,height,depth);
    glutSolidCube(1.0);
    glPopMatrix();

    // windows
    setColor(0.05f,0.1f,0.25f);
    for (float y = 1.0f; y < height; y += 2.0f) {
        for (float x = -width/2.0f + 0.6f; x < width/2.0f - 0.4f; x += 1.2f) {
            glPushMatrix();
            glTranslatef(x, y, depth/2.0f + 0.01f);
            glScalef(0.6f,0.8f,0.01f);
            glutSolidCube(1.0);
            glPopMatrix();
        }
    }
}

// ================= Road / Scene drawing =================
static void drawRoad() {
    // ground
    glPushMatrix();
    setColor(0.2f,0.6f,0.2f);
    glBegin(GL_QUADS);
        glVertex3f(-300.0f,-0.01f,-300.0f);
        glVertex3f(300.0f,-0.01f,-300.0f);
        glVertex3f(300.0f,-0.01f,800.0f);
        glVertex3f(-300.0f,-0.01f,800.0f);
    glEnd();
    glPopMatrix();

    // road surface
    setColor(0.12f,0.12f,0.12f);
    glBegin(GL_QUADS);
        glVertex3f(-ROAD_HALF_WIDTH-1.0f,0.0f,-200.0f);
        glVertex3f( ROAD_HALF_WIDTH+1.0f,0.0f,-200.0f);
        glVertex3f( ROAD_HALF_WIDTH+1.0f,0.0f,800.0f);
        glVertex3f(-ROAD_HALF_WIDTH-1.0f,0.0f,800.0f);
    glEnd();

    // lane dividers
    setColor(1.0f,1.0f,1.0f);
    glLineWidth(2.0f);
    for (int i = 0; i <= NUM_LANES; ++i) {
        float x = -ROAD_HALF_WIDTH + i * LANE_WIDTH;
        glBegin(GL_LINES);
            glVertex3f(x, 0.01f, -200.0f);
            glVertex3f(x, 0.01f, 800.0f);
        glEnd();
    }

    // center dashes
    setColor(1.0f,0.9f,0.0f);
    glLineWidth(1.0f);
    for (float z = -200.0f; z < 800.0f; z += 6.0f) {
        glBegin(GL_LINES);
            glVertex3f(-0.2f, 0.02f, z);
            glVertex3f( 0.2f, 0.02f, z);
        glEnd();
    }
}

// ================= New: draw finish line as checkerboard (static & non-flicker) =================
static void drawFinishLineVisual() {
    glDisable(GL_LIGHTING);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);

    const float squareSize = 1.0f;
    const float length = 8.0f;
    float leftX = -ROAD_HALF_WIDTH;
    float rightX =  ROAD_HALF_WIDTH;
    float startZ = finishLineZ - length/2.0f;
    float endZ   = finishLineZ + length/2.0f;
    float y = 0.03f;

    int ix = 0;
    for (float x = leftX; x < rightX; x += squareSize, ++ix) {
        int iz = 0;
        for (float z = startZ; z < endZ; z += squareSize, ++iz) {
            bool white = ((ix + iz) % 2) == 0;
            if (white) setColor(1.0f,1.0f,1.0f);
            else setColor(0.0f,0.0f,0.0f);

            glBegin(GL_QUADS);
                glVertex3f(x,       y, z);
                glVertex3f(x+squareSize, y, z);
                glVertex3f(x+squareSize, y, z+squareSize);
                glVertex3f(x,       y, z+squareSize);
            glEnd();
        }
    }

    setColor(1.0f,0.0f,0.0f);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
        glVertex3f(leftX, y+0.001f, startZ);
        glVertex3f(rightX, y+0.001f, startZ);
        glVertex3f(leftX, y+0.001f, endZ);
        glVertex3f(rightX, y+0.001f, endZ);
    glEnd();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_LIGHTING);
}
// ===============================================================================================

static void drawCarModel(const Car &c) {
    float laneCenterX = -ROAD_HALF_WIDTH + (c.lane + 0.5f) * LANE_WIDTH;
    float xPos = laneCenterX + (c.isPlayer ? playerOffsetX : 0.0f);
    float zPos = c.z;

    glPushMatrix();
    glTranslatef(xPos, 0, zPos);

    // body
    setColor(c.colorR, c.colorG, c.colorB);
    glPushMatrix();
    glTranslatef(0.0f, 0.8f, 0.0f);
    drawCube(1.6f, 0.6f, 3.0f);
    glPopMatrix();

    // roof
    setColor(c.colorR*0.9f, c.colorG*0.9f, c.colorB*0.9f);
    glPushMatrix();
    glTranslatef(0.0f, 1.35f, -0.2f);
    glScalef(1.0f, 0.5f, 1.4f);
    glutSolidCube(1.0);
    glPopMatrix();

    // windows
    setColor(0.05f, 0.15f, 0.35f);
    glPushMatrix();
    glTranslatef(0.0f, 1.25f, -0.5f);
    glScalef(0.9f, 0.35f, 0.6f);
    glutSolidCube(1.0);
    glPopMatrix();

    // wheels
    setColor(0.05f,0.05f,0.05f);
    glPushMatrix();
    glTranslatef(1.0f, 0.4f, 1.0f);
    drawWheel(0.45f, 0.2f);
    glTranslatef(-2.0f, 0.0f, 0.0f);
    drawWheel(0.45f, 0.2f);
    glTranslatef(2.0f, 0.0f, -2.0f);
    drawWheel(0.45f, 0.2f);
    glTranslatef(-2.0f, 0.0f, 0.0f);
    drawWheel(0.45f, 0.2f);
    glPopMatrix();

    // finished flag
    if (c.finished) {
        setColor(1.0f,0.85f,0.0f);
        glPushMatrix();
        glTranslatef(0.0f, 2.2f, 0.0f);
        glRotatef(-45, 0,1,0);
        glBegin(GL_TRIANGLES);
            glVertex3f(0,0,0);
            glVertex3f(0.6f,0.2f,0);
            glVertex3f(0,0.4f,0);
        glEnd();
        glPopMatrix();
    }

    glPopMatrix();
}

static void drawSceneObjects(float timeElapsed) {
    // sky (flat)
    glDisable(GL_LIGHTING);
    setColor(0.53f,0.81f,0.92f);
    glBegin(GL_QUADS);
        glVertex3f(-300,-10,-300);
        glVertex3f( 300,-10,-300);
        glVertex3f( 300,200,800);
        glVertex3f(-300,200,800);
    glEnd();

    // clouds (draw after sky)
    for (size_t i=0;i<clouds.size();++i) {
        Cloud &c = clouds[i];
        glPushMatrix();
        glTranslatef(c.x, c.y, c.z);
        glScalef(c.scale, c.scale, c.scale);
        // simple cloud made of overlapping spheres
        setColor(1.0f,1.0f,1.0f);
        glTranslatef(-1.0f,0.0f,0.0f); glutSolidSphere(1.0, 12, 12);
        glTranslatef(0.8f,0.2f,0.0f); glutSolidSphere(1.1, 12, 12);
        glTranslatef(0.8f,-0.1f,0.0f); glutSolidSphere(0.9, 12, 12);
        glPopMatrix();
    }

    glEnable(GL_LIGHTING);

    // buildings and trees
    for (float z = -200.0f; z < 800.0f; z += 25.0f) {
        glPushMatrix();
        glTranslatef(-ROAD_HALF_WIDTH - 8.0f, 0.0f, z);
        drawBuilding(6.0f + fmod(z,7.0f), 10.0f + fmod(z,9.0f), 8.0f);
        glPopMatrix();

        glPushMatrix();
        glTranslatef(ROAD_HALF_WIDTH + 8.0f, 0.0f, z + 5.0f);
        drawBuilding(6.0f + fmod(z,5.0f), 8.0f + fmod(z+3.0f,11.0f), 6.0f);
        glPopMatrix();
    }

    for (float z = -200.0f; z < 800.0f; z += 8.0f) {
        glPushMatrix();
        glTranslatef(-ROAD_HALF_WIDTH - 4.0f, 0.0f, z + fmod(z*5,3));
        drawTree();
        glPopMatrix();

        glPushMatrix();
        glTranslatef(ROAD_HALF_WIDTH + 4.5f, 0.0f, z + fmod(z*7,5));
        drawTree();
        glPopMatrix();
    }

    drawRoad();
    drawFinishLineVisual();

    for (size_t i = 0; i < cars.size(); ++i) drawCarModel(cars[i]);
}

// ================= Initialization of cars =================
static Car makeRandomCar(int lane, bool isPlayer) {
    Car c;
    c.lane = lane;
    c.z = startLineZ;
    c.speed = 0.0f;
    c.colorR = (float)(rand()%100)/100.0f;
    c.colorG = (float)(rand()%100)/100.0f;
    c.colorB = (float)(rand()%100)/100.0f;
    if (isPlayer) {
        c.colorR = 1.0f; c.colorG = 0.0784f; c.colorB = 0.5765f;
        c.isPlayer = true;
    } else {
        c.isPlayer = false;
    }
    c.finished = false;
    c.finishPos = 0;
    return c;
}

static void initClouds() {
    clouds.clear();
    // create a few clouds spread across z and x
    for (int i=0;i<7;i++){
        Cloud c;
        c.x = (float)(rand()%200) - 100.0f;
        c.y = 40.0f + (rand()%20);
        c.z = -200.0f + (float)(rand()%1000);
        c.speed = 2.0f + (rand()%80)/10.0f;
        c.scale = 0.9f + (rand()%50)/100.0f;
        clouds.push_back(c);
    }
}

static void initCarsAtStartLine() {
    cars.clear();
    finishOrder.clear();
    cars.push_back(makeRandomCar(playerLane, true));

    // put 4 opponents in specific lanes (skipping player's lane if desired)
    int opponents[4] = {0,1,3,4};
    for (int i=0;i<4;i++) cars.push_back(makeRandomCar(opponents[i], false));

    // reset timer & score & states
    raceTimer = 60.0f;
    timerActive = false;
    timeOver = false;
    scoreValue = 0;
    playerLost = false;

    // reset overtaken and collisionCooldown arrays
    overtaken.clear();
    collisionCooldown.clear();
    overtaken.resize(cars.size());
    collisionCooldown.resize(cars.size());
    for (size_t i=0;i<cars.size();++i) {
        overtaken[i] = 0;
        collisionCooldown[i] = 0;
    }

    // init clouds
    initClouds();

    // fireworks off
    fireworksActive = false;
    fireworks.clear();
}

// ================= Camera & Lighting =================
static void setupLight() {
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat lightpos[] = { -50.0f, 80.0f, 50.0f, 1.0f };
    GLfloat ambient[]  = { 0.25f, 0.25f, 0.25f, 1.0f };
    GLfloat diffuse[]  = { 0.9f,  0.9f,  0.9f,  1.0f };
    GLfloat spec[]     = { 0.3f,  0.3f,  0.3f,  1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
}

static void setCamera() {
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    Car playerCar = cars.size() ? cars[0] : makeRandomCar(playerLane,true);
    float laneCenterX = -ROAD_HALF_WIDTH + (playerCar.lane + 0.5f) * LANE_WIDTH;
    float camX = laneCenterX + playerOffsetX;
    float camY = 6.0f;
    float camZ = playerCar.z - 14.0f;
    float lookX = laneCenterX + playerOffsetX;
    float lookY = 1.5f;
    float lookZ = playerCar.z + 6.0f;
    gluLookAt(camX, camY, camZ, lookX, lookY, lookZ, 0,1,0);
}

// ================= HUD / 2D UI =================
static void drawText2D(float x, float y, const char *str) {
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    // Set text color to WHITE
    glColor3f(1.0f, 1.0f, 1.0f);

    glRasterPos2f(x,y);
    for (int i=0; str[i]; ++i)
        glutBitmapCharacter(GLUT_BITMAP_TIMES_ROMAN_24, str[i]);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_LIGHTING);
}

static void drawButton(float x, float y, float w, float h, float r, float g, float b, const char* text, float skew=25.0f) {
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(r,g,b);
    glBegin(GL_QUADS);
        glVertex2f(x + skew,     y + h);
        glVertex2f(x + w + skew, y + h);
        glVertex2f(x + w,        y);
        glVertex2f(x,            y);
    glEnd();
    glColor3f(r*0.15f, g*0.15f, b*0.15f);
    glLineWidth(3);
    glBegin(GL_LINE_LOOP);
        glVertex2f(x + skew,     y + h);
        glVertex2f(x + w + skew, y + h);
        glVertex2f(x + w,        y);
        glVertex2f(x,            y);
    glEnd();
    // text centered
    float avgW = 9.0f;
    float tx = x + w/2 - (strlen(text)*avgW)/2 + skew/2;
    float ty = y + h/2 - 6;
    glColor3f(1,1,1);
    glRasterPos2f(tx, ty);
    for (int i=0;text[i];++i) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, text[i]);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
}

static void drawMenuBackground() {
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(0,800,0,600);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glColor3f(1.0f,0.95f,0.6f);
    glBegin(GL_QUADS);
        glVertex2f(0,0); glVertex2f(800,0); glVertex2f(800,600); glVertex2f(0,600);
    glEnd();

    // checkerboards top and bottom
    for (int i=0;i<40;i++){
        glColor3f((i%2)?1:0, (i%2)?1:0, (i%2)?1:0);
        glBegin(GL_QUADS);
            glVertex2f(i*20, 580);
            glVertex2f(i*20+20,580);
            glVertex2f(i*20+20,600);
            glVertex2f(i*20,600);
        glEnd();
        glBegin(GL_QUADS);
            glVertex2f(i*20, 0);
            glVertex2f(i*20+20,0);
            glVertex2f(i*20+20,20);
            glVertex2f(i*20,20);
        glEnd();
    }

    // random decorations (deterministic-ish)
    glColor3f(1,1,1);
    for (int i=0;i<12;i++){
        float x = (rand()%700)+50, y = (rand()%450)+100;
        glBegin(GL_LINE_LOOP);
            for (int a=0;a<20;a++){
                float ang = a * 3.14159f*2.0f / 20.0f;
                glVertex2f(x + cos(ang)*15, y + sin(ang)*15);
            }
        glEnd();
    }
    glEnable(GL_DEPTH_TEST);
}

static void drawMainMenu(bool isOverlay) {
    drawMenuBackground();
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();

    int btnW = 300, btnH = 70;
    int cx = (windowWidth - btnW)/2;
    int y1 = windowHeight - 300;
    int y2 = y1 - 110;
    int y3 = y2 - 110;

    if (!isOverlay) {
        // Main menu: only Start and Exit
        drawButton(cx,y1,btnW,btnH,1.0f,0.1f,0.1f,"Start");
        drawButton(cx,y2,btnW,btnH,1.0f,0.1f,0.1f,"Exit");
    } else {
        // Overlay menu: Continue, Restart, Exit
        drawButton(cx,y1,btnW,btnH,1.0f,0.1f,0.1f,"Continue");
        drawButton(cx,y2,btnW,btnH,1.0f,0.1f,0.1f,"Restart");
        drawButton(cx,y3,btnW,btnH,1.0f,0.1f,0.1f,"Exit");
    }

    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void drawSmallMenuButton() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity(); gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();

   float x = 3.0f, y = windowHeight - 40.0f, w = 30.0f, h = 30.0f;


    // menu icon lines
    glColor3f(0,0,0);
    float bx = x + 5, by = y + h/2;
    glBegin(GL_LINES);
        glVertex2f(bx,by+6); glVertex2f(bx+20,by+6);
        glVertex2f(bx,by);   glVertex2f(bx+20,by);
        glVertex2f(bx,by-6); glVertex2f(bx+20,by-6);
    glEnd();

    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

// ===== Separation helper: ensures no overlapping at all between any two cars =====
// minimal allowed separations (x and z) so that models never intersect
const float MIN_DX = 1.2f;   // side separation threshold
const float MIN_DZ = 2.2f;   // front/back separation threshold

static void enforceNoOverlapAll() {
    // For every pair, if overlap in both axes, separate them.
    // Preference: don't move the player backwards/forwards abruptly unless necessary:
    // - If pair involves player, move the opponent away in z to avoid overlapping,
    //   and also slightly in x if same lane.
    // - If both opponents, move both half the required separation.
    for (size_t a=0;a<cars.size();++a) {
        for (size_t b=a+1;b<cars.size();++b) {
            // compute world positions (x)
            float ax = -ROAD_HALF_WIDTH + (cars[a].lane + 0.5f) * LANE_WIDTH + (cars[a].isPlayer ? playerOffsetX : 0.0f);
            float az = cars[a].z;
            float bx = -ROAD_HALF_WIDTH + (cars[b].lane + 0.5f) * LANE_WIDTH + (cars[b].isPlayer ? playerOffsetX : 0.0f);
            float bz = cars[b].z;

            float dx = ax - bx;
            float dz = az - bz;

            if (fabs(dx) < MIN_DX && fabs(dz) < MIN_DZ) {
                // separation needed
                float needDx = (MIN_DX - fabs(dx));
                float needDz = (MIN_DZ - fabs(dz));
                // prefer to resolve in Z first (front/back)
                if (cars[a].isPlayer && !cars[b].isPlayer) {
                    // push opponent b backward (reduce overlap) by needDz + small margin
                    if (dz >= 0) { // a is ahead of b
                        cars[b].z -= (needDz + 0.05f);
                    } else {
                        cars[b].z += (needDz + 0.05f);
                    }
                    // if same lane, nudge x slightly
                    if (fabs(dx) < MIN_DX) {
                        float pushx = (dx >= 0) ? 0.25f : -0.25f;
                        // move opponent lane offset by adjusting its lane center via lane index is fixed,
                        // but we can nudge their z slightly more to avoid overlap (do not change lane index).
                        cars[b].z -= pushx * 0.1f;
                    }
                } else if (!cars[a].isPlayer && cars[b].isPlayer) {
                    // symmetric
                    if (dz >= 0) { // a is ahead of b (opponent ahead of player)
                        cars[a].z -= (needDz + 0.05f);
                    } else {
                        cars[a].z += (needDz + 0.05f);
                    }
                    if (fabs(dx) < MIN_DX) {
                        float pushx = (dx >= 0) ? 0.25f : -0.25f;
                        cars[a].z -= pushx * 0.1f;
                    }
                } else {
                    // both opponents or both players (shouldn't be two players)
                    // move both half the required distance along z
                    if (dz >= 0) {
                        cars[a].z += (needDz*0.5f + 0.02f);
                        cars[b].z -= (needDz*0.5f + 0.02f);
                    } else {
                        cars[a].z -= (needDz*0.5f + 0.02f);
                        cars[b].z += (needDz*0.5f + 0.02f);
                    }
                    // if dx small, nudge them slightly left/right in world by tiny z changes (no lane change)
                }
                // clamp to avoid pushing beyond finish line (if finished, keep them at finish)
                if (cars[a].finished) cars[a].z = finishLineZ;
                if (cars[b].finished) cars[b].z = finishLineZ;
            }
        }
    }
}

// ================== Update loop ==================
static void updateScene(int value) {
    static int lastTime = 0;
    int now = glutGet(GLUT_ELAPSED_TIME);
    if (lastTime == 0) lastTime = now;
    float dt = (now - lastTime) / 1000.0f;
    lastTime = now;
    elapsedSinceStart += dt;

    if (startCountDown) {
        lastCountSec += dt;
        if (lastCountSec >= 1.0f) {
            lastCountSec = 0.0f;
            countValue--;
            #ifdef _WIN32
                Beep(1000,200);
            #else
                printf("\a");
            #endif
            if (countValue <= 0) {
                startCountDown = false;
                // If countdown was from overlay resume, resume from saved positions/speeds
                if (countdownFromOverlay) {
                    // Do NOT reset positions to start line.
                    // Restore forwardSpeed (player) to saved speed
                    forwardSpeed = savedPlayerSpeed;
                    // restore timer state saved when overlay opened
                    raceTimer = savedRaceTimer;
                    timerActive = savedTimerActive;
                    // keep opponents' speeds as they were (they were frozen while overlay)
                    countdownFromOverlay = false;
                    gameState = STATE_RACING;
                    // resume timer if it wasn't already over
                    if (!timeOver) timerActive = true;
                } else {
                    // original fresh-start behavior (start from start line and randomize opponents' speeds)
                    gameState = STATE_RACING;
                    forwardSpeed = 0.0f;
                    for (size_t i=1;i<cars.size();++i) {
                        cars[i].speed = WORLD_SPEED_BASE * (0.9f + (rand()%60)/100.0f);
                        cars[i].z = startLineZ;
                        cars[i].finished = false;
                        cars[i].finishPos = 0;
                    }
                    finishOrder.clear();
                    // reset timer for fresh start
                    raceTimer = 60.0f;
                    timerActive = true;
                    timeOver = false;
                    scoreValue = 0;
                    playerLost = false;
                    // reset overtaken/collision flags
                    overtaken.clear(); collisionCooldown.clear();
                    overtaken.resize(cars.size());
                    collisionCooldown.resize(cars.size());
                    for (size_t i=0;i<cars.size();++i){ overtaken[i]=0; collisionCooldown[i]=0; }
                }
            }
        }
    }

    // pause if overlay open -> freeze everything (no movement)
    if (menuOverlay) {
        glutPostRedisplay();
        glutTimerFunc(16, updateScene, 0);
        return;
    }

    // decrement collision cooldown frames
    for (size_t i=0;i<collisionCooldown.size();++i) {
        if (collisionCooldown[i] > 0) collisionCooldown[i]--;
    }

    // update timer if active and race not over
    if (timerActive && !timeOver && gameState == STATE_RACING) {
        raceTimer -= dt;
        if (raceTimer <= 0.0f) {
            raceTimer = 0.0f;
            timeOver = true;
            // freeze all cars
            forwardSpeed = 0.0f;
            for (size_t i=0;i<cars.size();++i) {
                cars[i].speed = 0.0f;
            }
            timerActive = false;
        }
    }

    // player input only in RACING and not timeOver
    if (gameState == STATE_RACING && !timeOver && !playerLost) {
        if (keyStates['w'] || keyStates['W']) forwardSpeed += 6.0f * dt;
        else if (keyStates['s'] || keyStates['S']) forwardSpeed -= 8.0f * dt;
        else forwardSpeed *= 0.98f;

        if (forwardSpeed > 12.0f) forwardSpeed = 12.0f;
        if (forwardSpeed < -3.0f) forwardSpeed = -3.0f;

        if (keyStates['d'] || keyStates['D']) targetOffsetX -= steerSpeed;
        if (keyStates['a'] || keyStates['A']) targetOffsetX += steerSpeed;

        float laneLimit = (LANE_WIDTH*(NUM_LANES-1))/2.0f + 0.8f;
        if (targetOffsetX > laneLimit) targetOffsetX = laneLimit;
        if (targetOffsetX < -laneLimit) targetOffsetX = -laneLimit;
        playerOffsetX += (targetOffsetX - playerOffsetX) * 6.0f * dt;
    } else {
        // when not racing or timeOver, don't allow forward movement
        if (cars.size()>0 && cars[0].finished) forwardSpeed = 0.0f;
        else forwardSpeed = 0.0f;
    }

    // update player (only move when in RACING and not finished and not timeOver)
    if (cars.size()>0) {
        if (!cars[0].finished && gameState == STATE_RACING && !timeOver && !playerLost) {
            cars[0].z += forwardSpeed * dt * 8.0f;
            // prevent going behind start
            if (cars[0].z < startLineZ - 5.0f) cars[0].z = startLineZ - 5.0f;
        }

        // clamp if player crossed finish line
        if (!cars[0].finished && cars[0].z >= finishLineZ) {
            // player reached finish
            cars[0].finished = true;
            cars[0].z = finishLineZ;                // lock exactly on finish line
            cars[0].finishPos = (int)finishOrder.size() + 1;
            finishOrder.push_back(0);
            forwardSpeed = 0.0f;                    // stop player movement

            // immediately set final game state and play celebratory or loss sound
            bool playerWon = (cars[0].finishPos == 1);
            gameState = STATE_FINISHED;

            if (playerWon) {
                // start fireworks
                fireworksActive = true;
                fireworks.clear();
                // spawn a burst of particles above player
                float laneCenterX = -ROAD_HALF_WIDTH + (cars[0].lane + 0.5f) * LANE_WIDTH;
                float px = laneCenterX + playerOffsetX;
                float pz = cars[0].z;
                for (int i=0;i<80;i++){
                    FWParticle p;
                    p.x = px + ((rand()%100)-50)/50.0f;
                    p.y = 6.0f + ((rand()%100)/100.0f)*4.0f;
                    p.z = pz + 1.5f + ((rand()%100)-50)/200.0f;
                    float a = (float)(rand()%360) * 3.14159f/180.0f;
                    float s = 4.0f + (rand()%100)/25.0f;
                    p.vx = cos(a) * s * ((rand()%100)/100.0f + 0.3f);
                    p.vy = 2.0f + (rand()%100)/50.0f;
                    p.vz = sin(a) * s * ((rand()%100)/100.0f + 0.3f);
                    p.life = 2.0f + (rand()%100)/100.0f * 2.0f;
                    p.r = (float)(rand()%100)/100.0f;
                    p.g = (float)(rand()%100)/100.0f;
                    p.b = (float)(rand()%100)/100.0f;
                    fireworks.push_back(p);
                }

                #ifdef _WIN32
                    Beep(1500,300);
                    Beep(2000,200);
                #else
                    printf("\a");
                #endif
            } else {
                playerLost = true;
                #ifdef _WIN32
                    Beep(600,300);
                #else
                    printf("\a");
                #endif
            }
        }
    }

    // update opponents (only if not timeOver)
    for (size_t i=1;i<cars.size();++i) {
        // if opponent already finished, lock at finish line and skip movement
        if (cars[i].finished) {
            // ensure they stay exactly at finish line (don't drift)
            if (cars[i].z > finishLineZ) cars[i].z = finishLineZ;
            continue;
        }

        // only move opponents while the race is active and not timeOver
        if (gameState == STATE_RACING && !timeOver && !playerLost) {
            if (cars[i].speed < 0.001f) cars[i].speed = WORLD_SPEED_BASE;
            cars[i].z += cars[i].speed * dt;
        }

        // detect opponent crossing finish
        if (!cars[i].finished && cars[i].z >= finishLineZ) {
            cars[i].finished = true;
            cars[i].z = finishLineZ; // clamp to finish line
            cars[i].finishPos = (int)finishOrder.size() + 1;
            finishOrder.push_back((int)i);

            // Check if this opponent finished before player
            if (!cars[0].finished) {
                playerLost = true;
                gameState = STATE_FINISHED;
                forwardSpeed = 0.0f;
                #ifdef _WIN32
                    Beep(600,300);
                #else
                    printf("\a");
                #endif
            }
        }
    }

    // If all finished (edge-case), ensure state is FINISHED
    bool allFinished = true;
    for (size_t i=0;i<cars.size();++i) if (!cars[i].finished) { allFinished = false; break; }
    if (allFinished && gameState == STATE_RACING) {
        gameState = STATE_FINISHED;
        // Check if player won or lost
        if (cars.size()>0 && cars[0].finishPos == 1) {
            #ifdef _WIN32
                Beep(1500,300);
                Beep(2000,200);
            #else
                printf("\a");
            #endif
        } else {
            playerLost = true;
            #ifdef _WIN32
                Beep(600,300);
            #else
                printf("\a");
            #endif
        }
    }

    // NEW OVERTAKE detection: dynamic overtaking system
    if (cars.size() > 1 && gameState == STATE_RACING && !timeOver && !playerLost) {
        for (size_t i=1;i<cars.size();++i) {
            if (!cars[i].finished && !cars[0].finished) {
                // Check if player is ahead of opponent
                bool playerAhead = (cars[0].z > cars[i].z);

                // If player is ahead and we haven't recorded this overtake yet
                if (playerAhead && overtaken[i] == 0) {
                    scoreValue += 1;
                    overtaken[i] = 1; // Mark as overtaken
                }
                // If opponent is ahead of player and we previously overtook them
                else if (!playerAhead && overtaken[i] == 1) {
                    scoreValue -= 1;
                    overtaken[i] = 0; // Reset so we can score again when we overtake
                }
            }
        }
    }

    // basic collision with opponents (side collisions) - improved: apply penalty and separation to avoid overlapping
    for (size_t i=1;i<cars.size();++i) {
        if (cars[i].finished || cars[0].finished) continue;
        if (gameState != STATE_RACING) continue;
        if (timeOver || playerLost) continue;

        float laneCenterPlayer = -ROAD_HALF_WIDTH + (cars[0].lane + 0.5f) * LANE_WIDTH + playerOffsetX;
        float laneCenterOther  = -ROAD_HALF_WIDTH + (cars[i].lane + 0.5f) * LANE_WIDTH;
        float dx = laneCenterPlayer - laneCenterOther;
        float dz = cars[0].z - cars[i].z;

        if (fabs(dx) < 1.2f && fabs(dz) < 2.0f) {
            // if we have a recent collision cooldown, skip penalty
            if (collisionCooldown[i] == 0) {
                // apply penalty (allow negative scores)
                scoreValue -= 1;
                collisionCooldown[i] = 45; // ~0.7s of cooldown (45 frames @ ~60fps)
            }
            // nudge player sideways away from other car so they don't overlap
            float push = (dx >= 0.0f) ? 0.35f : -0.35f;
            playerOffsetX += push;
            // apply small backward bump
            cars[0].z -= 1.5f;
            // apply speed penalty
            forwardSpeed = -4.0f;
            // clamp playerOffsetX to lane limits
            float laneLimit = (LANE_WIDTH*(NUM_LANES-1))/2.0f + 0.8f;
            if (playerOffsetX > laneLimit) playerOffsetX = laneLimit;
            if (playerOffsetX < -laneLimit) playerOffsetX = -laneLimit;

            // ensure separation (move opponent slightly forward/back to avoid overlap)
            if (dz >= 0) { // player ahead of opponent -> push opponent backward
                cars[i].z -= (MIN_DZ - fabs(dz)) * 0.6f + 0.05f;
            } else {
                cars[i].z += (MIN_DZ - fabs(dz)) * 0.6f + 0.05f;
            }
            if (cars[i].finished) cars[i].z = finishLineZ;
        }
    }

    // enforce global no-overlap for all cars (player & opponents & opponents between themselves)
    enforceNoOverlapAll();

    // Update clouds positions (only when not overlay; overlay is handled earlier)
    for (size_t i=0;i<clouds.size();++i) {
        Cloud &c = clouds[i];
        c.x += c.speed * dt * 0.15f;
        // wrap around when too far
        if (c.x > 220.0f) c.x = -220.0f;
        if (c.x < -220.0f) c.x = 220.0f;
        // slowly drift on z too
        c.z += (sin(elapsedSinceStart*0.1f + i) * 0.02f);
    }

    // update fireworks if active
    if (fireworksActive) {
        for (size_t i=0;i<fireworks.size();++i) {
            FWParticle &p = fireworks[i];
            p.life -= dt;
            if (p.life > 0.0f) {
                p.x += p.vx * dt;
                p.y += p.vy * dt;
                p.z += p.vz * dt;
                // gravity
                p.vy -= 9.0f * dt * 0.6f;
                // slight air drag
                p.vx *= (1.0f - 0.2f*dt);
                p.vz *= (1.0f - 0.2f*dt);
            }
        }
        // remove dead particles
        size_t write = 0;
        for (size_t i=0;i<fireworks.size();++i) {
            if (fireworks[i].life > 0.0f) { fireworks[write++] = fireworks[i]; }
        }
        fireworks.resize(write);
        if (fireworks.empty()) fireworksActive = false;
    }

    glutPostRedisplay();
    glutTimerFunc(16, updateScene, 0);
}


// ================= Input handlers =================
static void keyboardDown(unsigned char key, int x, int y) {
    keyStates[key] = true;
    if (key == 32) { // SPACE
        if (gameState == STATE_WAITING) {
            startCountDown = true;
            countdownFromOverlay = false; // fresh start behavior
            countValue = 3;
            lastCountSec = 0.0f;
            for (size_t i=0;i<cars.size();++i) { cars[i].finished = false; cars[i].finishPos = 0; }
            finishOrder.clear();
            if (cars.size()>0) cars[0].z = startLineZ;
            forwardSpeed = 0.0f;
            firstLaunch = false;
            gameState = STATE_COUNTDOWN;
        }
        // NOTE: pressing SPACE does NOT restart when STATE_FINISHED (per your request)
    }
}

static void keyboardUp(unsigned char key, int x, int y) {
    keyStates[key] = false;
}

// ================= Mouse (menus) =================
static void mouseClick(int button, int state, int x, int y) {
    int my = windowHeight - y;
    if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
        // small menu icon area (top-left) -> match drawSmallMenuButton's exact rectangle:
        // drawSmallMenuButton uses: float x = 3.0f, y = windowHeight - 40.0f, w = 30.0f, h = 30.0f;
        int iconLeft = 3;
        int iconTop  = windowHeight - 40;
        int iconRight = iconLeft + 30;
        int iconBottom = iconTop + 30;

        // TOP-LEFT ICON: only open overlay if NOT in MAIN MENU
        if (x >= iconLeft && x <= iconRight && my >= iconTop && my <= iconBottom) {
            if (gameState == STATE_MENU) {
                // do nothing when in main menu
                return;
            } else {
                // Open overlay: save state and freeze
                menuOverlay = true;
                savedState = gameState;
                if (cars.size()>0) savedPlayerZ = cars[0].z;
                savedPlayerSpeed = forwardSpeed;
                savedOffsetX = playerOffsetX;
                savedTargetOffsetX = targetOffsetX;
                // save timer state and pause timer
                savedRaceTimer = raceTimer;
                savedTimerActive = timerActive;
                timerActive = false;
                return;
            }
        }

        // if not in menu: ignore other clicks
        if (!(gameState == STATE_MENU || menuOverlay)) return;

        int btnW = 300, btnH = 70;
        int cx = (windowWidth - btnW)/2;
        int y1 = windowHeight - 300;
        int y2 = y1 - 110;
        int y3 = y2 - 110;

        // NOTE: main menu shows Start & Exit only; overlay shows Continue, Restart, Exit

        if (menuOverlay) {
            // Continue button (overlay)
            if (x > cx && x < cx+btnW && my > y1 && my < y1+btnH) {
                // start countdown to resume from paused state
                menuOverlay = false;
                countdownFromOverlay = true;
                startCountDown = true;
                countValue = 3;
                lastCountSec = 0.0f;
                // freeze forwardSpeed until countdown completes; savedPlayerSpeed will be restored after countdown
                forwardSpeed = 0.0f;
                gameState = STATE_COUNTDOWN;
                return;
            }

            // Restart button (overlay)
            if (x > cx && x < cx+btnW && my > y2 && my < y2+btnH) {
                // full restart same as clicking Start -> go to waiting + init cars
                initCarsAtStartLine();
                playerOffsetX = targetOffsetX = 0.0f;
                forwardSpeed = 0.0f;
                gameState = STATE_WAITING;
                firstLaunch = false;
                menuOverlay = false;
                return;
            }

            // Exit (overlay)
            if (x > cx && x < cx+btnW && my > y3 && my < y3+btnH) {
                exit(0);
            }
            return;
        }

        // If here: not menuOverlay but gameState == STATE_MENU (main menu)
        // Main menu: Start and Exit (y1 = Start, y2 = Exit)
        if (x > cx && x < cx+btnW && my > y1 && my < y1+btnH) {
            // START
            gameState = STATE_WAITING;
            firstLaunch = false;
            initCarsAtStartLine();
            playerOffsetX = targetOffsetX = 0.0f;
            forwardSpeed = 0.0f;
            menuOverlay = false;
            return;
        }

        if (x > cx && x < cx+btnW && my > y2 && my < y2+btnH) {
            // EXIT from main menu
            exit(0);
        }
    }
}

// ================= Celebration (UI) =================
static void drawConfettiOverPlayer(bool win) {
    if (cars.size()==0) return;
    float laneCenterX = -ROAD_HALF_WIDTH + (cars[0].lane + 0.5f) * LANE_WIDTH;
    float px = laneCenterX + playerOffsetX;
    float pz = cars[0].z;

    glDisable(GL_LIGHTING);
    glPushMatrix();
    glTranslatef(px, 2.5f, pz + 1.0f);

    int pieces = 30;
    for (int i=0;i<pieces;i++){
        float t = elapsedSinceStart * 3.0f + i;
        float sx = cos(t*1.37f + i) * (0.5f + fmod(i*7,3)*0.2f);
        float sy = fabs(sin(t*0.9f + i))*1.5f + 0.2f;
        float sz = sin(t*1.11f + i) * 0.5f;
        float r = (float)((i*37)%100)/100.0f;
        float g = (float)((i*61)%100)/100.0f;
        float b = (float)((i*23)%100)/100.0f;
        glColor3f(r,g,b);
        glBegin(GL_TRIANGLES);
            glVertex3f(sx, sy, sz);
            glVertex3f(sx+0.08f, sy-0.06f, sz);
            glVertex3f(sx-0.06f, sy-0.06f, sz+0.04f);
        glEnd();
    }
    glPopMatrix();
    glEnable(GL_LIGHTING);
}

static void drawFireworks() {
    if (!fireworksActive) return;
    glDisable(GL_LIGHTING);
    for (size_t i=0;i<fireworks.size();++i){
        FWParticle &p = fireworks[i];
        if (p.life <= 0.0f) continue;
        float alpha = fmax(0.0f, fmin(1.0f, p.life / 2.5f));
        glColor4f(p.r, p.g, p.b, alpha);
        glPushMatrix();
        glTranslatef(p.x, p.y, p.z);
        glutSolidSphere(0.12f,8,6);
        glPopMatrix();
    }
    glEnable(GL_LIGHTING);
}

// ================= Display =================
static void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 3D scene except when at main menu (not overlay)
    if (!(gameState == STATE_MENU && !menuOverlay)) {
        setCamera();
        setupLight();
        drawSceneObjects(elapsedSinceStart);

        // Draw fireworks in 3D scene
        if (fireworksActive) {
            drawFireworks();
        }
    }

    // UI overlay: switch to 2D
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    glOrtho(0, windowWidth, 0, windowHeight, -1,1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    if (gameState == STATE_MENU && !menuOverlay) drawMainMenu(false);
    if (!(gameState == STATE_MENU && !menuOverlay)) drawSmallMenuButton();
    if (menuOverlay) drawMainMenu(true);

    // ===== draw timer & score at top-right =====
    {
        char buf[64];
        int tx = windowWidth - 160;
        int ty = windowHeight - 30;
        // Timer (integer seconds)
        int secs = (int)ceil(raceTimer);
        sprintf(buf, "Time: %d", secs);
        drawText2D((float)tx, (float)ty, buf);

        // Score below timer
        sprintf(buf, "Score: %d", scoreValue);
        drawText2D((float)tx, (float)(ty - 28), buf);
    }

    if (startCountDown) {
        char buf[32];
        if (countValue > 0) sprintf(buf, "%d", countValue);
        else sprintf(buf, "Go!");
        drawText2D(windowWidth/2 - 20, windowHeight/2, buf);
    }

    // ONLY show finished/confetti/fireworks/timeover texts when menuOverlay is NOT active
    if (!menuOverlay) {
        if (gameState == STATE_FINISHED) {
            bool playerWon = false;
            if (cars.size()>0 && cars[0].finished && cars[0].finishPos == 1) playerWon = true;

            if (playerWon) {
                drawConfettiOverPlayer(playerWon);
            } else if (playerLost) {
                drawText2D(windowWidth/2 - 80, windowHeight/2 + 60, "You Lost :(");
            }
        }

        // If time over -> display message and ensure everything frozen
        if (timeOver) {
            drawText2D(windowWidth/2 - 120, windowHeight/2 + 60, "Time is Over :(");
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);

    glutSwapBuffers();
}

// ================= Window / init =================
static void reshape(int w, int h) {
    windowWidth = w; windowHeight = h;
    glViewport(0,0,w,h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, (double)w/(double)h, 0.1, 1000.0);
    glMatrixMode(GL_MODELVIEW);
}

static void initGL() {
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glClearColor(0.53f,0.81f,0.92f,1.0f);
    srand((unsigned)time(NULL));
    initCarsAtStartLine();
    setupLight();
}

// ================= Main =================
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(windowWidth, windowHeight);
    glutCreateWindow("Racing3D - cleaned");
    initGL();
    glutMouseFunc(mouseClick);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboardDown);
    glutKeyboardUpFunc(keyboardUp);
    glutTimerFunc(16, updateScene, 0);
    glutMainLoop();
    return 0;
}
