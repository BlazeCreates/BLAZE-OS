#include <Arduino.h>
extern void returnToGamesMenu();
extern void drawSpriteMask(const char* mask, int x, int y, int w, int h, uint16_t color, TFT_eSprite* spr);

static float rf_carX = 80;
static float rf_carY = 100;
static float rf_speed = 0;
static float rf_dist = 0;
static float rf_fuel = 100;
static int rf_score = 0;
static bool rf_gameOver = false;
static uint32_t rf_eqTime = 0;
static int rf_state = 0; // 0=menu, 1=play

const char* rf_car_mask = 
"  1111  "
" 111111 "
" 110011 "
"  1111  "
"11111111"
"11000011"
"11000011"
"11111111"
" 111111 "
" 110011 "
" 110011 "
" 111111 "
"11111111"
"11111111"
"  1111  "
"  1111  ";

struct RFCar { float x; float y; int type; bool active; };
static RFCar rf_enemies[5];

void resetRoadFighter() {
    rf_carX = 80; rf_carY = 100; rf_speed = 0; rf_dist = 0; rf_fuel = 100;
    rf_score = 0; rf_gameOver = false; rf_state = 1;
    for(int i=0; i<5; i++) rf_enemies[i].active = false;
}

void handleRoadFighterButtons() {
    if (digitalRead(BTN_EQ_PIN) == LOW) {
        if (rf_eqTime == 0) rf_eqTime = millis();
        else if (millis() - rf_eqTime > 500) { returnToGamesMenu(); delay(200); return; }
    } else { rf_eqTime = 0; }

    if (rf_state == 0 && digitalRead(BTN_PLAY_PIN) == LOW) { resetRoadFighter(); delay(200); }
    if (rf_gameOver && digitalRead(BTN_PLAY_PIN) == LOW) { resetRoadFighter(); delay(200); }
}

void drawRFCar(int x, int y, uint16_t color) {
    drawSpriteMask(rf_car_mask, x-4, y-8, 8, 16, color, &gameSprite);
}

void updateRoadFighter() {
    if (rf_state == 1 && !rf_gameOver) {
        if (digitalRead(BTN_PLAY_PIN) == LOW) { rf_speed += 2.0; if(rf_speed > 350) rf_speed = 350; }
        else if (digitalRead(BTN_EQ_PIN) == LOW && rf_eqTime > 0 && millis() - rf_eqTime < 1000) { rf_speed -= 4.0; }
        else { rf_speed -= 1.0; }
        if (rf_speed < 0) rf_speed = 0;
        
        if (digitalRead(BTN_PREV_PIN) == LOW && rf_speed > 0) rf_carX -= 1.5;
        if (digitalRead(BTN_NEXT_PIN) == LOW && rf_speed > 0) rf_carX += 1.5;
        if (rf_carX < 45) rf_carX = 45; if (rf_carX > 115) rf_carX = 115;

        rf_dist += rf_speed / 100.0;
        rf_fuel -= (rf_speed / 5000.0) + 0.01;
        if (rf_fuel <= 0) rf_gameOver = true;
        rf_score = (int)rf_dist;

        for(int i=0; i<5; i++) {
            if (!rf_enemies[i].active) {
                if (random(100) < 2) {
                    rf_enemies[i].active = true;
                    rf_enemies[i].x = random(45, 115);
                    rf_enemies[i].y = -20;
                    rf_enemies[i].type = (random(10) > 8) ? 1 : 0; 
                }
            } else {
                rf_enemies[i].y += (rf_speed - (rf_enemies[i].type == 1 ? 100 : 180)) / 20.0;
                if (rf_enemies[i].y > 140) rf_enemies[i].active = false;
                
                if (abs(rf_carX - rf_enemies[i].x) < 7 && abs(rf_carY - rf_enemies[i].y) < 14) {
                    if (rf_enemies[i].type == 1) { rf_fuel += 15; if(rf_fuel>100) rf_fuel=100; rf_enemies[i].active = false; }
                    else { rf_speed = 0; rf_fuel -= 5; rf_enemies[i].active = false; }
                }
            }
        }
    }

    gameSprite.fillSprite(0x2589); // Authentic green grass
    gameSprite.fillRect(36, 0, 88, 128, 0x4208); // Authentic dark asphalt
    
    // Checkered borders
    int yOffset = (int)rf_dist % 20;
    for(int y = -20; y < 128; y += 10) {
        gameSprite.fillRect(34, y + (yOffset%10), 4, 5, TFT_WHITE);
        gameSprite.fillRect(34, y + 5 + (yOffset%10), 4, 5, TFT_RED);
        gameSprite.fillRect(122, y + (yOffset%10), 4, 5, TFT_WHITE);
        gameSprite.fillRect(122, y + 5 + (yOffset%10), 4, 5, TFT_RED);
    }
    
    // Lane markings
    for(int y = -20; y < 128; y += 20) {
        gameSprite.fillRect(79, y + yOffset, 2, 10, TFT_WHITE);
    }
    
    if (rf_state == 1) {
        for(int i=0; i<5; i++) {
            if (rf_enemies[i].active) drawRFCar(rf_enemies[i].x, rf_enemies[i].y, rf_enemies[i].type == 1 ? TFT_MAGENTA : TFT_BLUE);
        }
        drawRFCar(rf_carX, rf_carY, TFT_RED);
        
        gameSprite.setTextColor(TFT_WHITE);
        gameSprite.drawString(String((int)rf_speed) + "KM", 2, 2, 1);
        gameSprite.drawString(String(rf_score) + "M", 2, 12, 1);
        gameSprite.fillRect(132, 110 - (int)rf_fuel, 8, (int)rf_fuel, TFT_YELLOW); 
        gameSprite.drawRect(131, 9, 10, 102, TFT_WHITE);
        gameSprite.drawString("E", 134, 112, 1);
        gameSprite.drawString("F", 134, 1, 1);
        
        if (rf_gameOver) {
            gameSprite.setTextColor(TFT_RED);
            gameSprite.drawString("OUT OF FUEL", 80 - gameSprite.textWidth("OUT OF FUEL", 1)/2, 60, 1);
        }
    } else {
        gameSprite.setTextColor(TFT_YELLOW);
        gameSprite.drawString("ROAD FIGHTER", 45, 40, 1);
        gameSprite.setTextColor(TFT_WHITE);
        gameSprite.drawString("Hold EQ: Exit", 45, 60, 1);
        gameSprite.drawString("Press PLAY", 50, 80, 1);
    }
    
    gameSprite.pushSprite(0, 0);
}
