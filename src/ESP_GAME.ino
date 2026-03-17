#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <pgmspace.h>

#include "wall_tex.h"
#include "floor_tex.h"

// ghost
#include "ghost33.h"
#include "ghost67.h"
#include "ghost96.h"

// ------------ TFT / INPUT ------------
static TFT_eSPI tft = TFT_eSPI();
static TFT_eSprite frame = TFT_eSprite(&tft);

static const int W = 160;
static const int H = 128;

static const int JOY_X  = 34;
static const int JOY_Y  = 35;
static const int JOY_SW = 32;

// ------------ HARDWARE PINS (EDIT THESE) ------------
// Backlight pin must be a real GPIO wired to TFT BL through a transistor or direct BL input.
static const int BL_PIN = 27;      // CHANGE to your backlight control GPIO
static const int BUZZER_PIN = 25;  // CHANGE to your buzzer GPIO (passive buzzer preferred)

// ESP32 PWM channel for buzzer
static const int BUZZ_CH = 0;

// ------------ MAP ------------
static const int MAP_W = 16;
static const int MAP_H = 16;

// 0 = empty, 1 = wall, 2 = goal
static const uint8_t worldMap[MAP_H][MAP_W] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,2,1},
  {1,0,1,1,1,0,1,0,1,1,1,1,1,0,0,1},
  {1,0,1,0,0,0,0,0,1,0,0,0,1,0,0,1},
  {1,0,1,0,1,1,1,0,1,0,1,0,1,0,0,1},
  {1,0,0,0,1,0,0,0,0,0,1,0,0,0,0,1},
  {1,0,1,0,1,0,1,1,1,0,1,1,1,1,0,1},
  {1,0,1,0,0,0,1,0,0,0,0,0,0,1,0,1},
  {1,0,1,1,1,0,1,0,1,1,1,1,0,1,0,1},
  {1,0,0,0,1,0,0,0,1,0,0,1,0,0,0,1},
  {1,1,1,0,1,1,1,0,1,0,1,1,1,1,0,1},
  {1,0,0,0,0,0,0,0,1,0,0,0,0,1,0,1},
  {1,0,1,1,1,1,1,0,1,1,1,1,0,1,0,1},
  {1,0,0,0,0,0,1,0,0,0,0,1,0,0,0,1},
  {1,0,1,1,1,0,1,1,1,1,0,1,1,1,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
};

// ------------ MATH CONSTANTS ------------
static const float PI_F = 3.14159265f;
static const float FOV  = 1.04719755f;     // 60 deg
static const float HALF_FOV = FOV * 0.5f;

// ------------ GAME STATE ------------
enum class GameMode : uint8_t { Start, Play, Win, Dead };
static GameMode mode = GameMode::Start;

static float px = 1.5f, py = 1.5f;
static float yaw = 0.0f;

static float gx = 14.5f, gy = 14.5f;

static float zBuffer[W];

static float hp = 100.0f;

static uint8_t levelIndex = 0;   // 0..2
static bool gameWin = false;
static bool gameLose = false;

// ------------ DIFFICULTY (per level) ------------
struct LevelParams {
  float ghostSpeed;
  float drainPerSec;
  float dangerDist;
};

static const LevelParams LEVELS[3] = {
  {1.15f, 16.0f, 0.75f},  // EASY
  {1.35f, 22.0f, 0.70f},  // NORMAL
  {1.60f, 30.0f, 0.70f},  // HARD
};

// ------------ BFS PATHING ------------
static uint8_t distMap[MAP_H][MAP_W];
static uint32_t nextBfsMs = 0;
struct Node { uint8_t x, y; };
static Node q[MAP_W * MAP_H];

// ------------ COLLISION ------------
static const float PLAYER_R = 0.28f;
static const float GHOST_R  = 0.18f;

static const uint16_t TRANSPARENT_KEY = 0x0000;

// ------------ INPUT ------------
struct InputState { float turn; float move; bool press; };

// ------------ BACKLIGHT FLICKER (HORROR NOISE) ------------
static uint32_t blNextToggleMs = 0;
static uint32_t blBurstEndMs = 0;
static uint32_t blCooldownUntilMs = 0;
static bool blIsOn = true;

// Small RNG
static uint32_t rngState = 0xA5A5A5A5u;
static inline uint32_t rng32() {
  rngState = rngState * 1664525u + 1013904223u;
  return rngState;
}
static inline uint32_t randRange(uint32_t lo, uint32_t hi) { // inclusive
  uint32_t r = rng32();
  return lo + (r % (hi - lo + 1));
}
static inline void backlightWrite(bool on) {
  blIsOn = on;
  digitalWrite(BL_PIN, on ? HIGH : LOW);
}

// ------------ NEW: GM COUNTER BUZZER (GOAL FINDER) ------------
static uint32_t gmNextMs = 0;
static uint32_t gmStopMs = 0;
static bool gmOn = false;

static inline void gmStop() {
  ledcWriteTone(BUZZ_CH, 0);
  gmOn = false;
}

static void findGoalCenter(float &ox, float &oy) {
  for (int y = 0; y < MAP_H; y++) {
    for (int x = 0; x < MAP_W; x++) {
      if (worldMap[y][x] == 2) {
        ox = x + 0.5f;
        oy = y + 0.5f;
        return;
      }
    }
  }
  ox = 1.5f;
  oy = 1.5f;
}

static inline float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
static inline float wrapAngle(float a) {
  while (a > PI_F) a -= 2.0f * PI_F;
  while (a < -PI_F) a += 2.0f * PI_F;
  return a;
}
static inline bool isWallCell(int x, int y) {
  if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return true;
  return (worldMap[y][x] == 1);
}
static inline float readAxisNorm(int pin) {
  int v = analogRead(pin);
  return (v - 2048) / 2048.0f;
}
static inline uint16_t blend565(uint16_t under, uint16_t over, uint8_t alpha) {
  uint8_t ur = (under >> 11) & 0x1F;
  uint8_t ug = (under >> 5) & 0x3F;
  uint8_t ub = under & 0x1F;

  uint8_t orr = (over >> 11) & 0x1F;
  uint8_t og  = (over >> 5) & 0x3F;
  uint8_t ob  = over & 0x1F;

  uint8_t r = (uint8_t)((ur * (255 - alpha) + orr * alpha) / 255);
  uint8_t g = (uint8_t)((ug * (255 - alpha) + og  * alpha) / 255);
  uint8_t b = (uint8_t)((ub * (255 - alpha) + ob  * alpha) / 255);

  return (r << 11) | (g << 5) | b;
}

static void gmCounterTick(uint32_t nowMs) {
  if (mode != GameMode::Play) {
    gmStop();
    gmNextMs = 0;
    return;
  }

  float tx, ty;
  findGoalCenter(tx, ty);

  float d = hypotf(tx - px, ty - py);

  // ~max diagonal distance in 16x16 grid
  const float maxD = 22.7f;

  float p = 1.0f - (d / maxD);   // 0..1
  p = clampf(p, 0.0f, 1.0f);

  // stronger response when close
  float s = p * p;               // 0..1

  // clicks: far slow, near fast
  uint32_t period = (uint32_t)(900.0f - s * 840.0f); // 900..60
  if (period < 60) period = 60;
  if (period > 900) period = 900;

  uint32_t onMs = (uint32_t)(8.0f + s * 12.0f);      // 8..20

  float freq = 1200.0f + s * 1100.0f;                // 1200..2300

  // jitter like real GM clicks
  uint32_t jitter = (uint32_t)(period * 0.18f);
  if (jitter < 2) jitter = 2;
  uint32_t pj = period;
  pj += (uint32_t)(rng32() % (jitter * 2 + 1)) - jitter;
  if (pj < 45) pj = 45;

  if (gmNextMs == 0) gmNextMs = nowMs;

  if (nowMs >= gmNextMs) {
    ledcWriteTone(BUZZ_CH, freq);
    gmStopMs = nowMs + onMs;
    gmOn = true;
    gmNextMs = nowMs + pj;
  }

  if (gmOn && nowMs >= gmStopMs) {
    gmStop();
  }
}

// ------------ INPUT READ ------------
static InputState inputRead() {
  InputState in{};
  float jx = readAxisNorm(JOY_X);
  float jy = readAxisNorm(JOY_Y);

  const float DZ = 0.12f;
  if (fabsf(jx) < DZ) jx = 0.0f;
  if (fabsf(jy) < DZ) jy = 0.0f;

  in.turn = -jx;
  in.move = -jy;

  static uint8_t last = 1;
  uint8_t cur = (uint8_t)digitalRead(JOY_SW);
  in.press = (last == 1 && cur == 0);
  last = cur;
  return in;
}

// ------------ TEXTURE READERS ------------
static inline uint16_t wallRead(int x, int y) {
  return pgm_read_word(&WALL_TEX_DATA[y * WALL_W + x]);
}
static inline uint16_t floorRead(int x, int y) {
  return pgm_read_word(&FLOOR_TEX_DATA[y * FLOOR_W + x]);
}

// ------------ COLLISION: circle vs wall tiles ------------
static inline bool circleHitsWall(float x, float y, float r) {
  int minX = (int)floorf(x - r);
  int maxX = (int)floorf(x + r);
  int minY = (int)floorf(y - r);
  int maxY = (int)floorf(y + r);

  for (int ty = minY; ty <= maxY; ty++) {
    for (int tx = minX; tx <= maxX; tx++) {
      if (!isWallCell(tx, ty)) continue;

      float cx = (float)tx + 0.5f;
      float cy = (float)ty + 0.5f;

      float dx = fabsf(x - cx) - 0.5f;
      float dy = fabsf(y - cy) - 0.5f;

      float ax = (dx > 0) ? dx : 0;
      float ay = (dy > 0) ? dy : 0;

      if (ax * ax + ay * ay <= r * r) return true;
    }
  }
  return false;
}

static void pushOutOfWalls(float &x, float &y, float r) {
  for (int iter = 0; iter < 12; iter++) {
    if (!circleHitsWall(x, y, r)) return;

    int cx = (int)floorf(x);
    int cy = (int)floorf(y);

    float bestDX = 0, bestDY = 0;
    float bestLen2 = 1e30f;

    for (int ty = cy - 1; ty <= cy + 1; ty++) {
      for (int tx = cx - 1; tx <= cx + 1; tx++) {
        if (!isWallCell(tx, ty)) continue;

        float left   = (float)tx;
        float right  = (float)tx + 1.0f;
        float top    = (float)ty;
        float bottom = (float)ty + 1.0f;

        float nx = clampf(x, left, right);
        float ny = clampf(y, top, bottom);

        float dx = x - nx;
        float dy = y - ny;
        float d2 = dx*dx + dy*dy;

        if (d2 < bestLen2) {
          bestLen2 = d2;
          bestDX = dx;
          bestDY = dy;
        }
      }
    }

    float len = sqrtf(bestDX*bestDX + bestDY*bestDY);
    if (len < 0.0001f) {
      x += 0.02f;
      y += 0.02f;
    } else {
      float push = 0.05f;
      x += (bestDX / len) * push;
      y += (bestDY / len) * push;
    }
  }
}

static void moveWithCollision(float &x, float &y, float dx, float dy, float r) {
  const int STEPS = 3;
  dx /= (float)STEPS;
  dy /= (float)STEPS;

  for (int i = 0; i < STEPS; i++) {
    float nx = x + dx;
    if (!circleHitsWall(nx, y, r)) x = nx;

    float ny = y + dy;
    if (!circleHitsWall(x, ny, r)) y = ny;

    pushOutOfWalls(x, y, r);
  }

  if (isWallCell((int)x, (int)y)) {
    pushOutOfWalls(x, y, r);
  }
}

// ------------ BFS ------------
static void buildDistMapFromPlayer() {
  for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) distMap[y][x] = 255;

  int sx = (int)px, sy = (int)py;
  if (sx < 0 || sy < 0 || sx >= MAP_W || sy >= MAP_H) return;

  distMap[sy][sx] = 0;
  int head = 0, tail = 0;
  q[tail++] = {(uint8_t)sx, (uint8_t)sy};

  while (head != tail) {
    Node n = q[head++];
    uint8_t d = distMap[n.y][n.x];

    const int dx4[4] = { 1, -1, 0, 0 };
    const int dy4[4] = { 0, 0, 1, -1 };

    for (int k = 0; k < 4; k++) {
      int nx = (int)n.x + dx4[k];
      int ny = (int)n.y + dy4[k];
      if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
      if (worldMap[ny][nx] == 1) continue;
      if (distMap[ny][nx] != 255) continue;

      distMap[ny][nx] = (uint8_t)(d + 1);
      q[tail++] = {(uint8_t)nx, (uint8_t)ny};
    }
  }
}

static void bfsMaybeUpdate(uint32_t nowMs) {
  if (nowMs >= nextBfsMs) {
    nextBfsMs = nowMs + 250;
    buildDistMapFromPlayer();
  }
}

// ------------ PLAYER ------------
static void playerUpdate(const InputState& in, float dt) {
  const float turnRadPerSec = 2.2f;
  yaw = wrapAngle(yaw + in.turn * turnRadPerSec * dt);

  float moveAxis = -in.move; // UP = forward
  const float moveUnitsPerSec = 2.2f;

  float step = moveAxis * moveUnitsPerSec * dt;
  float dx = cosf(yaw) * step;
  float dy = sinf(yaw) * step;

  moveWithCollision(px, py, dx, dy, PLAYER_R);
}

// ------------ GHOST ------------
static void ghostUpdate(float dt, uint32_t nowMs) {
  bfsMaybeUpdate(nowMs);

  int cx = (int)gx, cy = (int)gy;
  if (cx < 0 || cy < 0 || cx >= MAP_W || cy >= MAP_H) return;

  int bestx = cx, besty = cy;
  uint8_t best = distMap[cy][cx];

  const int dx4[4] = { 1, -1, 0, 0 };
  const int dy4[4] = { 0, 0, 1, -1 };

  for (int k = 0; k < 4; k++) {
    int nx = cx + dx4[k];
    int ny = cy + dy4[k];
    if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H) continue;
    if (distMap[ny][nx] < best) {
      best = distMap[ny][nx];
      bestx = nx;
      besty = ny;
    }
  }

  float tx = bestx + 0.5f;
  float ty = besty + 0.5f;

  float vx = tx - gx;
  float vy = ty - gy;
  float d = sqrtf(vx*vx + vy*vy);

  if (d > 0.01f) {
    float speed = LEVELS[levelIndex].ghostSpeed;
    float dx = (vx / d) * speed * dt;
    float dy = (vy / d) * speed * dt;
    moveWithCollision(gx, gy, dx, dy, GHOST_R);
  }
}

// ------------ HEALTH ------------
static void healthUpdate(float dt) {
  float d = hypotf(gx - px, gy - py);

  float dangerDist = LEVELS[levelIndex].dangerDist;
  float drainPerSec = LEVELS[levelIndex].drainPerSec;

  const float regenPerSec = 3.5f;

  if (d < dangerDist) {
    float t = (dangerDist - d) / dangerDist;
    hp -= (drainPerSec * (0.35f + 0.65f * t)) * dt;
  } else {
    hp += regenPerSec * dt;
  }

  hp = clampf(hp, 0.0f, 100.0f);
}

static void gameCheckWinLose() {
  if (worldMap[(int)py][(int)px] == 2) gameWin = true;
  if (hp <= 0.0f) gameLose = true;
}

// ------------ RENDER: SPRITE BLIT ------------
static void drawSpriteKeyed(int dstX, int dstY, const uint16_t* src, int sw, int sh,
                            uint16_t key, bool blend, uint8_t alpha) {
  for (int y = 0; y < sh; y++) {
    int sy = dstY + y;
    if (sy < 0 || sy >= H) continue;
    for (int x = 0; x < sw; x++) {
      int sx = dstX + x;
      if (sx < 0 || sx >= W) continue;

      uint16_t c = pgm_read_word(&src[y * sw + x]);
      if (c == key) continue;

      if (!blend) {
        frame.drawPixel(sx, sy, c);
      } else {
        uint16_t under = frame.readPixel(sx, sy);
        frame.drawPixel(sx, sy, blend565(under, c, alpha));
      }
    }
  }
}

// ------------ RENDER: 3D SCENE ------------
static void renderSceneTexturedToFrame() {
  float dirX = cosf(yaw);
  float dirY = sinf(yaw);

  float planeLen = tanf(HALF_FOV);
  float planeX = -dirY * planeLen;
  float planeY =  dirX * planeLen;

  frame.fillRect(0, 0, W, H / 2, TFT_DARKCYAN);

  // floor cast, 2 pixels per sample
  float posZ = 0.5f * H;
  for (int y = H / 2; y < H; y++) {
    float p = (float)(y - H / 2);
    if (p < 1.0f) p = 1.0f;

    float rowDist = posZ / p;

    float rayDirX0 = dirX - planeX;
    float rayDirY0 = dirY - planeY;
    float rayDirX1 = dirX + planeX;
    float rayDirY1 = dirY + planeY;

    float stepX = rowDist * (rayDirX1 - rayDirX0) / (float)W;
    float stepY = rowDist * (rayDirY1 - rayDirY0) / (float)W;

    float floorX = px + rowDist * rayDirX0;
    float floorY = py + rowDist * rayDirY0;

    for (int x = 0; x < W; x += 2) {
      int cellX = (int)floorX;
      int cellY = (int)floorY;

      float fracX = floorX - cellX;
      float fracY = floorY - cellY;

      int texX = (int)(fracX * FLOOR_W);
      int texY = (int)(fracY * FLOOR_H);

      texX = (int)clampf((float)texX, 0, FLOOR_W - 1);
      texY = (int)clampf((float)texY, 0, FLOOR_H - 1);

      uint16_t c = floorRead(texX, texY);
      frame.drawPixel(x, y, c);
      if (x + 1 < W) frame.drawPixel(x + 1, y, c);

      floorX += stepX * 2.0f;
      floorY += stepY * 2.0f;
    }
  }

  // walls + zBuffer
  for (int x = 0; x < W; x++) {
    float cameraX = 2.0f * x / (float)W - 1.0f;
    float rayDirX = dirX + planeX * cameraX;
    float rayDirY = dirY + planeY * cameraX;

    int mapX = (int)px;
    int mapY = (int)py;

    float deltaDistX = (rayDirX == 0.0f) ? 1e30f : fabsf(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0.0f) ? 1e30f : fabsf(1.0f / rayDirY);

    float sideDistX, sideDistY;
    int stepX, stepY;

    if (rayDirX < 0) { stepX = -1; sideDistX = (px - mapX) * deltaDistX; }
    else            { stepX =  1; sideDistX = (mapX + 1.0f - px) * deltaDistX; }

    if (rayDirY < 0) { stepY = -1; sideDistY = (py - mapY) * deltaDistY; }
    else            { stepY =  1; sideDistY = (mapY + 1.0f - py) * deltaDistY; }

    int hit = 0;
    int side = 0;

    for (int iter = 0; iter < 128 && !hit; iter++) {
      if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
      else                       { sideDistY += deltaDistY; mapY += stepY; side = 1; }
      if (isWallCell(mapX, mapY)) hit = 1;
    }

    float dist = 1000.0f;
    if (hit) dist = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);

    if (dist < 0.35f) dist = 0.35f;

    zBuffer[x] = dist;

    int lineH = (int)(H / dist);
    if (lineH > H) lineH = H;
    if (lineH < 1) lineH = 1;

    int wallTop = (H - lineH) / 2;

    float hitPos = (side == 0) ? (py + dist * rayDirY) : (px + dist * rayDirX);
    hitPos -= floorf(hitPos);

    int texX = (int)(hitPos * WALL_W);
    texX = (int)clampf((float)texX, 0, WALL_W - 1);

    if (side == 0 && rayDirX > 0) texX = WALL_W - 1 - texX;
    if (side == 1 && rayDirY < 0) texX = WALL_W - 1 - texX;

    for (int y = 0; y < lineH; y++) {
      int sy = wallTop + y;
      if (sy < 0 || sy >= H) continue;

      int texY = (y * WALL_H) / lineH;
      texY = (int)clampf((float)texY, 0, WALL_H - 1);

      uint16_t c = wallRead(texX, texY);

      if (side == 1) {
        uint8_t r = (c >> 11) & 0x1F;
        uint8_t g = (c >> 5) & 0x3F;
        uint8_t b = c & 0x1F;
        r = (r * 3) / 4;
        g = (g * 3) / 4;
        b = (b * 3) / 4;
        c = (r << 11) | (g << 5) | b;
      }

      frame.drawPixel(x, sy, c);
    }
  }
}

// ------------ RENDER: GHOST (3 LOD + crossfade) ------------
static void renderGhostLOD3(float xw, float yw) {
  float dx = xw - px;
  float dy = yw - py;

  float dist = sqrtf(dx*dx + dy*dy);
  if (dist < 0.001f) dist = 0.001f;

  float ang = atan2f(dy, dx);
  float rel = wrapAngle(ang - yaw);
  if (fabsf(rel) > HALF_FOV) return;

  float t = (rel + HALF_FOV) / FOV;
  int sxCenter = (int)(t * (W - 1));
  if (sxCenter < 0 || sxCenter >= W) return;

  if (dist > zBuffer[sxCenter] + 0.05f) return;

  const float d1 = 4.8f;
  const float d2 = 2.7f;
  const float band = 0.35f;

  const uint16_t* A = ghost33; int Aw = GHOST33_W; int Ah = GHOST33_H;
  const uint16_t* B = nullptr; int Bw = 0; int Bh = 0;
  uint8_t alpha = 0;

  if (dist > d1 + band) {
    A = ghost33; Aw = GHOST33_W; Ah = GHOST33_H;
  } else if (dist > d1 - band) {
    A = ghost33; Aw = GHOST33_W; Ah = GHOST33_H;
    B = ghost67; Bw = GHOST67_W; Bh = GHOST67_H;
    float u = (d1 + band - dist) / (2.0f * band);
    alpha = (uint8_t)(clampf(u, 0.0f, 1.0f) * 255);
  } else if (dist > d2 + band) {
    A = ghost67; Aw = GHOST67_W; Ah = GHOST67_H;
  } else if (dist > d2 - band) {
    A = ghost67; Aw = GHOST67_W; Ah = GHOST67_H;
    B = ghost96; Bw = GHOST96_W; Bh = GHOST96_H;
    float u = (d2 + band - dist) / (2.0f * band);
    alpha = (uint8_t)(clampf(u, 0.0f, 1.0f) * 255);
  } else {
    A = ghost96; Aw = GHOST96_W; Ah = GHOST96_H;
  }

  int floorY = (H / 2) + (H / 3);
  int leftA = sxCenter - (Aw / 2);
  int topA  = floorY - Ah;

  drawSpriteKeyed(leftA, topA, A, Aw, Ah, TRANSPARENT_KEY, false, 0);

  if (B) {
    int leftB = sxCenter - (Bw / 2);
    int topB  = floorY - Bh;
    drawSpriteKeyed(leftB, topB, B, Bw, Bh, TRANSPARENT_KEY, true, alpha);
  }
}

// ------------ HUD (small health bar) ------------
static void renderHealthBarSmall() {
  int x = 4, y = 4, w = 46, h = 5;
  frame.drawRect(x - 1, y - 1, w + 2, h + 2, TFT_WHITE);
  frame.fillRect(x, y, w, h, TFT_BLACK);

  int fill = (int)(w * (hp / 100.0f));
  fill = (int)clampf((float)fill, 0, w);

  uint16_t c = (hp > 60) ? TFT_GREEN : (hp > 30 ? TFT_YELLOW : TFT_RED);
  if (fill > 0) frame.fillRect(x, y, fill, h, c);
}

// ------------ ARCADE GUI using current textures only ------------
static void drawTiledBG_FloorScaled() {
  for (int y = 0; y < H; y++) {
    int ty = (y * FLOOR_H) / H;
    if (ty < 0) ty = 0;
    if (ty >= FLOOR_H) ty = FLOOR_H - 1;

    for (int x = 0; x < W; x++) {
      int tx = (x * FLOOR_W) / W;
      if (tx < 0) tx = 0;
      if (tx >= FLOOR_W) tx = FLOOR_W - 1;
      frame.drawPixel(x, y, floorRead(tx, ty));
    }
  }
}

static void drawWallBorder(int thick) {
  if (thick < 1) thick = 1;

  for (int y = 0; y < H; y++) {
    int ty = (y * WALL_H) / H;
    if (ty < 0) ty = 0;
    if (ty >= WALL_H) ty = WALL_H - 1;

    for (int x = 0; x < thick; x++) {
      int tx = (x * WALL_W) / thick;
      if (tx < 0) tx = 0;
      if (tx >= WALL_W) tx = WALL_W - 1;

      uint16_t c = wallRead(tx, ty);
      frame.drawPixel(x, y, c);
      frame.drawPixel(W - 1 - x, y, c);
    }
  }

  for (int x = 0; x < W; x++) {
    int tx = (x * WALL_W) / W;
    if (tx < 0) tx = 0;
    if (tx >= WALL_W) tx = WALL_W - 1;

    for (int y = 0; y < thick; y++) {
      int ty = (y * WALL_H) / thick;
      if (ty < 0) ty = 0;
      if (ty >= WALL_H) ty = WALL_H - 1;

      uint16_t c = wallRead(tx, ty);
      frame.drawPixel(x, y, c);
      frame.drawPixel(x, H - 1 - y, c);
    }
  }
}

static void drawScanlines(uint8_t step) {
  for (int y = 0; y < H; y += step) {
    for (int x = 0; x < W; x++) {
      uint16_t under = frame.readPixel(x, y);
      frame.drawPixel(x, y, blend565(under, TFT_BLACK, 70));
    }
  }
}

static void drawArcadePanel(int x, int y, int w, int h) {
  frame.fillRect(x, y, w, h, TFT_BLACK);
  frame.drawRect(x, y, w, h, TFT_WHITE);
  frame.drawRect(x + 2, y + 2, w - 4, h - 4, TFT_DARKGREY);
  frame.fillRect(x + 5, y + 5, 3, 3, TFT_WHITE);
  frame.fillRect(x + w - 8, y + 5, 3, 3, TFT_WHITE);
  frame.fillRect(x + 5, y + h - 8, 3, 3, TFT_WHITE);
  frame.fillRect(x + w - 8, y + h - 8, 3, 3, TFT_WHITE);
}

static void drawCenteredText(int y, const char* s, uint8_t size, uint16_t fg, uint16_t bg) {
  frame.setTextSize(size);
  frame.setTextColor(fg, bg);
  frame.setTextFont(1);

  int tw = frame.textWidth(s);
  int x = (W - tw) / 2;
  if (x < 0) x = 0;

  frame.setCursor(x, y);
  frame.print(s);
}

static const char* levelName(uint8_t i) {
  if (i == 0) return "EASY";
  if (i == 1) return "NORMAL";
  return "HARD";
}

static void renderStartScreenArcade(uint32_t nowMs) {
  drawTiledBG_FloorScaled();
  drawWallBorder(10);

  int px0 = 10, py0 = 12, pw = W - 20, ph = 104;
  drawArcadePanel(px0, py0, pw, ph);

  drawCenteredText(20, "Run Run Run!", 1, TFT_YELLOW, TFT_BLACK);
  drawCenteredText(34, "MAZERUN", 2, TFT_WHITE, TFT_BLACK);
  drawCenteredText(56, "SELECT LEVEL", 1, TFT_CYAN, TFT_BLACK);

  int bx = 28, by = 70, bw = W - 56, bh = 16;
  frame.drawRect(bx, by, bw, bh, TFT_WHITE);
  frame.fillRect(bx + 1, by + 1, bw - 2, bh - 2, TFT_BLACK);

  frame.setTextSize(1);
  frame.setTextColor(TFT_GREEN, TFT_BLACK);
  frame.setTextFont(1);

  char buf[20];
  snprintf(buf, sizeof(buf), "< %s >", levelName(levelIndex));
  int tw = frame.textWidth(buf);
  int tx = bx + (bw - tw) / 2;
  int ty = by + (bh - 6) / 2;

  frame.setCursor(tx, ty);
  frame.print(buf);

  bool blink = ((nowMs / 450) % 2) == 0;
  drawCenteredText(92, "PRESS TO START", 1, blink ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);

  drawScanlines(3);
  frame.pushSprite(0, 0);
}

static void renderDeathScreenArcade(uint32_t nowMs) {
  drawTiledBG_FloorScaled();
  drawWallBorder(10);

  int px0 = 10, py0 = 12, pw = W - 20, ph = 104;
  drawArcadePanel(px0, py0, pw, ph);

  drawCenteredText(28, "GAME OVER", 2, TFT_RED, TFT_BLACK);

  frame.setTextFont(1);
  frame.setTextSize(1);
  frame.setTextColor(TFT_WHITE, TFT_BLACK);

  char line1[32];
  snprintf(line1, sizeof(line1), "LEVEL: %s", levelName(levelIndex));
  int x1 = (W - frame.textWidth(line1)) / 2;
  if (x1 < 0) x1 = 0;
  frame.setCursor(x1, 60);
  frame.print(line1);

  char line2[32];
  snprintf(line2, sizeof(line2), "HP: %d/100", (int)hp);
  int x2 = (W - frame.textWidth(line2)) / 2;
  if (x2 < 0) x2 = 0;
  frame.setCursor(x2, 72);
  frame.print(line2);

  bool blink = ((nowMs / 450) % 2) == 0;
  drawCenteredText(92, "PRESS TO RETRY", 1, blink ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);

  drawScanlines(3);
  frame.pushSprite(0, 0);
}

static void renderWinScreenArcade(uint32_t nowMs) {
  drawTiledBG_FloorScaled();
  drawWallBorder(10);

  int px0 = 10, py0 = 12, pw = W - 20, ph = 104;
  drawArcadePanel(px0, py0, pw, ph);

  drawCenteredText(26, "STAGE CLEAR", 2, TFT_GREEN, TFT_BLACK);
  drawCenteredText(54, "YOU ESCAPED", 1, TFT_WHITE, TFT_BLACK);

  bool blink = ((nowMs / 450) % 2) == 0;
  drawCenteredText(92, "PRESS TO PLAY AGAIN", 1, blink ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);

  drawScanlines(3);
  frame.pushSprite(0, 0);
}

// ------------ HORROR BACKLIGHT FLICKER TICK ------------
static void ghostNearFlickerTick(uint32_t nowMs) {
  if (mode != GameMode::Play) {
    blBurstEndMs = 0;
    blNextToggleMs = 0;
    blCooldownUntilMs = 0;
    if (!blIsOn) backlightWrite(true);
    return;
  }

  float d = hypotf(gx - px, gy - py);

  const float nearDist = 2.2f;
  const float veryNearDist = 1.4f;

  const uint32_t cooldownMs = 230;
  const uint32_t burstMinMs = 120;
  const uint32_t burstMaxMs = 260;

  const uint32_t onMinMs  = 10;
  const uint32_t onMaxMs  = 48;
  const uint32_t offMinMs = 6;
  const uint32_t offMaxMs = 42;

  if (d >= nearDist) {
    blBurstEndMs = 0;
    blNextToggleMs = 0;
    if (!blIsOn) backlightWrite(true);
    return;
  }

  float t = 0.0f;
  if (d <= veryNearDist) t = 1.0f;
  else {
    t = (nearDist - d) / (nearDist - veryNearDist);
    t = clampf(t, 0.0f, 1.0f);
  }

  if (blBurstEndMs == 0 && nowMs >= blCooldownUntilMs) {
    uint32_t p = (uint32_t)(55 + t * 190); // 55..245 out of 1000
    if ((rng32() % 1000) < p) {
      uint32_t burstLen = randRange(burstMinMs, burstMaxMs);
      blBurstEndMs = nowMs + burstLen;
      blNextToggleMs = nowMs;
    }
  }

  if (blBurstEndMs != 0) {
    if (nowMs >= blBurstEndMs) {
      blBurstEndMs = 0;
      blCooldownUntilMs = nowMs + cooldownMs;
      if (!blIsOn) backlightWrite(true);
      return;
    }

    if (nowMs >= blNextToggleMs) {
      uint32_t offBias = (uint32_t)(360 + t * 360); // 360..720 out of 1000

      bool nextOn = !blIsOn;

      if (nextOn) {
        if ((rng32() % 1000) < offBias) nextOn = false;
      } else {
        if (t > 0.75f && (rng32() % 1000) < 350) {
          nextOn = false;
        }
      }

      backlightWrite(nextOn);

      uint32_t dur = blIsOn ? randRange(onMinMs, onMaxMs) : randRange(offMinMs, offMaxMs);

      if (t > 0.80f) {
        if (!blIsOn) dur += randRange(0, 30);
        else dur += randRange(0, 18);
      }

      blNextToggleMs = nowMs + dur;
    }
  }
}

// ------------ GAME FLOW ------------
static void resetRun() {
  px = 1.5f; py = 1.5f;
  yaw = 0.0f;

  gx = 14.5f; gy = 14.5f;

  hp = 100.0f;
  gameWin = false;
  gameLose = false;

  pushOutOfWalls(px, py, PLAYER_R);
  pushOutOfWalls(gx, gy, GHOST_R);
  buildDistMapFromPlayer();

  gmNextMs = 0;
  gmStop();
}

static void gameTick(float dt, uint32_t nowMs) {
  InputState in = inputRead();
  playerUpdate(in, dt);
  ghostUpdate(dt, nowMs);
  healthUpdate(dt);
  gameCheckWinLose();

  if (gameWin) { mode = GameMode::Win; }
  if (gameLose) { mode = GameMode::Dead; }
}

static void renderTick(uint32_t nowMs) {
  if (mode == GameMode::Start) {
    renderStartScreenArcade(nowMs);
    return;
  }
  if (mode == GameMode::Dead) {
    renderDeathScreenArcade(nowMs);
    return;
  }
  if (mode == GameMode::Win) {
    renderWinScreenArcade(nowMs);
    return;
  }

  renderSceneTexturedToFrame();
  renderGhostLOD3(gx, gy);
  renderHealthBarSmall();
  frame.pushSprite(0, 0);
}

static void menuTick(uint32_t nowMs) {
  (void)nowMs;
  InputState in = inputRead();

  static uint32_t lastMoveMs = 0;
  if (millis() - lastMoveMs > 160) {
    if (in.turn > 0.45f) {
      if (levelIndex < 2) levelIndex++;
      lastMoveMs = millis();
    } else if (in.turn < -0.45f) {
      if (levelIndex > 0) levelIndex--;
      lastMoveMs = millis();
    }
  }

  if (in.press) {
    resetRun();
    mode = GameMode::Play;
  }
}

static void endTick() {
  InputState in = inputRead();
  if (in.press) {
    mode = GameMode::Start;
  }
}

// ------------ SETUP / LOOP ------------
void setup() {
  pinMode(JOY_SW, INPUT_PULLUP);

  pinMode(BL_PIN, OUTPUT);
  backlightWrite(true);

  // Buzzer PWM setup
  ledcSetup(BUZZ_CH, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZ_CH);
  gmStop();

  tft.init();
  tft.setRotation(1);
  tft.setSwapBytes(true);

  frame.setColorDepth(16);
  frame.setSwapBytes(true);
  frame.createSprite(W, H);
  frame.fillSprite(TFT_BLACK);

  analogReadResolution(12);
  analogSetPinAttenuation(JOY_X, ADC_11db);
  analogSetPinAttenuation(JOY_Y, ADC_11db);

  rngState ^= (uint32_t)micros();

  resetRun();
  mode = GameMode::Start;
}

void loop() {
  static uint32_t lastMs = millis();
  uint32_t nowMs = millis();

  float dt = (nowMs - lastMs) / 1000.0f;
  if (dt < 0.001f) dt = 0.001f;
  if (dt > 0.05f)  dt = 0.05f;
  lastMs = nowMs;

  if (mode == GameMode::Start) menuTick(nowMs);
  else if (mode == GameMode::Play) gameTick(dt, nowMs);
  else endTick();

  // FEATURES
  ghostNearFlickerTick(nowMs);
  gmCounterTick(nowMs);

  renderTick(nowMs);

  delay(16);
}
