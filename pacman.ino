#include <Arduino.h>
extern void returnToGamesMenu();
extern void drawSpriteMask(const char* mask, int x, int y, int w, int h, uint16_t color, TFT_eSprite* spr);

static int pac_x = 1; static int pac_y = 1;
static int pac_dir = 0; static int pac_nextDir = 0;
static int pac_score = 0;
static uint32_t pac_lastMove = 0;
static uint32_t pac_eqTime = 0;
static bool pac_gameOver = false;
static int pac_state = 0;

const char* ghost_mask = 
"  1111  "
" 111111 "
"11011011"
"10010011"
"11111111"
"11111111"
"11111111"
"10100101";

static const uint8_t pac_map_init[15][19] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,2,2,2,2,2,2,2,2,1,2,2,2,2,2,2,2,2,1},
  {1,2,1,1,2,1,1,1,2,1,2,1,1,1,2,1,1,2,1},
  {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
  {1,2,1,1,2,1,2,1,1,1,1,1,2,1,2,1,1,2,1},
  {1,2,2,2,2,1,2,2,2,1,2,2,2,1,2,2,2,2,1},
  {1,1,1,1,2,1,1,1,0,1,0,1,1,1,2,1,1,1,1},
  {0,0,0,1,2,1,0,0,0,3,0,0,0,1,2,1,0,0,0},
  {1,1,1,1,2,1,0,1,1,0,1,1,0,1,2,1,1,1,1},
  {1,2,2,2,2,2,2,1,0,0,0,1,2,2,2,2,2,2,1},
  {1,2,1,1,2,1,2,1,1,1,1,1,2,1,2,1,1,2,1},
  {1,2,2,1,2,1,2,2,2,1,2,2,2,1,2,1,2,2,1},
  {1,1,2,1,2,1,1,1,2,1,2,1,1,1,2,1,2,1,1},
  {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};
static uint8_t pac_map[15][19];

struct Ghost { int x; int y; int dir; uint32_t lastMove; };
static Ghost pac_ghost = {9, 7, 3, 0};

void resetPacMan() {
    pac_x = 1; pac_y = 1; pac_dir = 0; pac_nextDir = 0; pac_score = 0; pac_gameOver = false; pac_state = 1;
    pac_ghost.x = 9; pac_ghost.y = 7; pac_ghost.dir = 3;
    for(int y=0; y<15; y++) for(int x=0; x<19; x++) pac_map[y][x] = pac_map_init[y][x];
}

void handlePacManButtons() {
    if (digitalRead(BTN_EQ_PIN) == LOW) {
        if (pac_eqTime == 0) pac_eqTime = millis();
        else if (millis() - pac_eqTime > 500) { returnToGamesMenu(); delay(200); return; }
        pac_nextDir = 1; 
    } else { pac_eqTime = 0; }

    if (digitalRead(BTN_PLAY_PIN) == LOW) { 
        if (pac_state == 0 || pac_gameOver) { resetPacMan(); delay(200); } 
        else pac_nextDir = 3; 
    }
    if (digitalRead(BTN_PREV_PIN) == LOW) pac_nextDir = 2; 
    if (digitalRead(BTN_NEXT_PIN) == LOW) pac_nextDir = 0; 
}

void updatePacMan() {
    gameSprite.fillSprite(TFT_BLACK);
    
    if (pac_state == 1 && !pac_gameOver) {
        uint32_t t = millis();
        if (t - pac_lastMove > 150) {
            pac_lastMove = t;
            int dx=0, dy=0;
            if (pac_nextDir==0) dx=1; else if(pac_nextDir==1) dy=1; else if(pac_nextDir==2) dx=-1; else if(pac_nextDir==3) dy=-1;
            int nx = (pac_x + dx + 19) % 19; int ny = pac_y + dy;
            if (pac_map[ny][nx] != 1 && pac_map[ny][nx] != 3) { pac_dir = pac_nextDir; }
            
            dx=0; dy=0;
            if (pac_dir==0) dx=1; else if(pac_dir==1) dy=1; else if(pac_dir==2) dx=-1; else if(pac_dir==3) dy=-1;
            nx = (pac_x + dx + 19) % 19; ny = pac_y + dy;
            if (pac_map[ny][nx] != 1 && pac_map[ny][nx] != 3) {
                pac_x = nx; pac_y = ny;
                if (pac_map[pac_y][pac_x] == 2) { pac_map[pac_y][pac_x] = 0; pac_score += 10; }
            }
        }
        
        if (t - pac_ghost.lastMove > 200) {
            pac_ghost.lastMove = t;
            int bestDir = pac_ghost.dir; float minDist = 9999;
            for(int d=0; d<4; d++) {
                int dx=0, dy=0;
                if (d==0) dx=1; else if(d==1) dy=1; else if(d==2) dx=-1; else if(d==3) dy=-1;
                if (d == (pac_ghost.dir + 2) % 4) continue;
                int gx = (pac_ghost.x + dx + 19) % 19; int gy = pac_ghost.y + dy;
                if (pac_map[gy][gx] != 1) {
                    float dist = abs(gx - pac_x) + abs(gy - pac_y);
                    if (dist < minDist) { minDist = dist; bestDir = d; }
                }
            }
            pac_ghost.dir = bestDir;
            int dx=0, dy=0;
            if (pac_ghost.dir==0) dx=1; else if(pac_ghost.dir==1) dy=1; else if(pac_ghost.dir==2) dx=-1; else if(pac_ghost.dir==3) dy=-1;
            pac_ghost.x = (pac_ghost.x + dx + 19) % 19; pac_ghost.y += dy;
            if (pac_ghost.x == pac_x && pac_ghost.y == pac_y) pac_gameOver = true;
        }
    }

    int ox = (160 - (19*8))/2;
    int oy = (128 - (15*8))/2 + 4;
    for(int y=0; y<15; y++) {
        for(int x=0; x<19; x++) {
            if (pac_map[y][x] == 1) {
                // Authentic Arcade Double-Line Auto-Tiling!
                gameSprite.fillRect(ox + x*8, oy + y*8, 8, 8, 0x01DF); // Bright Blue
                gameSprite.fillRect(ox + x*8 + 2, oy + y*8 + 2, 4, 4, TFT_BLACK); 
                
                if (x>0 && pac_map[y][x-1] == 1) gameSprite.fillRect(ox + x*8, oy + y*8 + 2, 2, 4, TFT_BLACK);
                if (x<18 && pac_map[y][x+1] == 1) gameSprite.fillRect(ox + x*8 + 6, oy + y*8 + 2, 2, 4, TFT_BLACK);
                if (y>0 && pac_map[y-1][x] == 1) gameSprite.fillRect(ox + x*8 + 2, oy + y*8, 4, 2, TFT_BLACK);
                if (y<14 && pac_map[y+1][x] == 1) gameSprite.fillRect(ox + x*8 + 2, oy + y*8 + 6, 4, 2, TFT_BLACK);
            }
            else if (pac_map[y][x] == 2) gameSprite.fillRect(ox + x*8 + 3, oy + y*8 + 3, 2, 2, 0xFD20);
            else if (pac_map[y][x] == 3) gameSprite.drawLine(ox+x*8, oy+y*8+4, ox+x*8+8, oy+y*8+4, TFT_PINK);
        }
    }
    
    // Draw 8x8 Ghost (Perfectly aligned in tile)
    drawSpriteMask(ghost_mask, ox + pac_ghost.x*8, oy + pac_ghost.y*8, 8, 8, TFT_RED, &gameSprite);
    
    // Draw Authentic Pac-Man (Radius 4 fits 8x8 tile cleanly)
    int px = ox + pac_x*8 + 4;
    int py = oy + pac_y*8 + 4;
    gameSprite.fillCircle(px, py, 4, TFT_YELLOW); // Pacman body
    
    // Animate mouth opening and closing based on time
    if ((millis() / 150) % 2 == 0) { 
        if (pac_dir == 0) gameSprite.fillTriangle(px, py, px+5, py-4, px+5, py+4, TFT_BLACK); // Right
        else if (pac_dir == 1) gameSprite.fillTriangle(px, py, px-4, py+5, px+4, py+5, TFT_BLACK); // Down
        else if (pac_dir == 2) gameSprite.fillTriangle(px, py, px-5, py-4, px-5, py+4, TFT_BLACK); // Left
        else if (pac_dir == 3) gameSprite.fillTriangle(px, py, px-4, py-5, px+4, py-5, TFT_BLACK); // Up
    } else {
        if (pac_dir == 0) gameSprite.drawLine(px, py, px+4, py, TFT_BLACK);
        else if (pac_dir == 1) gameSprite.drawLine(px, py, px, py+4, TFT_BLACK);
        else if (pac_dir == 2) gameSprite.drawLine(px, py, px-4, py, TFT_BLACK);
        else if (pac_dir == 3) gameSprite.drawLine(px, py, px, py-4, TFT_BLACK);
    }

    if (pac_state == 1) {
        gameSprite.setTextColor(TFT_WHITE);
        gameSprite.drawString("SCORE: " + String(pac_score), 2, 2, 1);
        if (pac_gameOver) {
            gameSprite.setTextColor(TFT_RED);
            gameSprite.drawString("GAME OVER", 50, 60, 2);
        }
    } else {
        gameSprite.setTextColor(TFT_YELLOW);
        gameSprite.drawString("PAC-MAN", 55, 40, 2);
        gameSprite.setTextColor(TFT_WHITE);
        gameSprite.drawString("Hold EQ: Exit", 45, 70, 1);
        gameSprite.drawString("Press PLAY", 50, 90, 1);
    }
    
    gameSprite.pushSprite(0, 0);
}
