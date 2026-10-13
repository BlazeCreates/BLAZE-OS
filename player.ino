#include <SPI.h>
#include <TFT_eSPI.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Preferences.h>
#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --------------------------------------------------------
// WIFI CONFIGURATION (EDIT THESE FOR YOUR NETWORKS)
// --------------------------------------------------------
const char* wifi_ssids[3] = {"", "", ""};
const char* wifi_passwords[3] = {"", "", ""};

// --------------------------------------------------------
// HARDWARE PINS & CONFIG
// --------------------------------------------------------
#define BTN_PREV_PIN  32
#define BTN_PLAY_PIN  33
#define BTN_NEXT_PIN  25
#define BTN_EQ_PIN    26
#define DFPLAYER_RX   16
#define DFPLAYER_TX   17
#define TFT_BL_PIN    27 

#define COLOR_BG      0x10A2 
#define COLOR_ACCENT  0xFD00 
#define COLOR_TEXT    0xFFFF 
#define COLOR_DIM     0x7BEF 
#define COLOR_RECORD  0x2104 
#define COLOR_SHEEN   0x39E7 
#define COLOR_LABEL   0xE8E4 
#define COLOR_BAR_OUT      TFT_WHITE
#define COLOR_BAR_FILL     TFT_CYAN

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite discSprite = TFT_eSprite(&tft);
TFT_eSprite gameSprite = TFT_eSprite(&tft); 

#define DISC_RADIUS 32
#define DISC_SIZE (DISC_RADIUS * 2)

HardwareSerial dfSerial(2);
DFRobotDFPlayerMini dfPlayer;
Preferences preferences;

enum AppMode { MODE_PLAYER, MODE_GAMES_MENU, MODE_SNAKE, MODE_BLACKJACK, MODE_PONG, MODE_FLAPPY, MODE_TETRIS, MODE_DOOM, MODE_STOPWATCH, MODE_TIMER, MODE_GALAGA, MODE_DINO, MODE_SIMON, MODE_TTT, MODE_PACMAN, MODE_ROADFIGHTER, MODE_WEATHER, MODE_RESET_SCORES };
AppMode currentMode = MODE_PLAYER;

struct PlayerState {
  uint16_t currentTrack = 1, totalTracks = 0; bool isPlaying = false, loopEnabled = false;
  uint8_t currentEQ = 0, volume = 25, brightnessLevel = 3; 
  uint32_t trackElapsedSec = 0, lastTickTime = 0; float discAngle = 0.0; bool forceFullRedraw = true;
  int lastBarFill = -1, lastDrawnSec = -1, lastDrawnTrack = -1, lastPlayState = -1, lastLoopState = -1;
  String temporaryMessage = ""; uint32_t messageClearTime = 0;
  int lastH[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
} state;

struct UtilityState {
  bool swRunning = false; uint32_t swStartMs = 0; uint32_t swAccumMs = 0; bool needsFullRedraw = true;
  bool tmrRunning = false; uint32_t tmrEndMs = 0; uint32_t tmrSetSecs = 60; uint32_t tmrTotalSecs = 60; bool tmrAlert = false; bool alertDrawn = false;
} util;

struct WeatherState {
  bool fetching; bool success;
  String timeStr; String dateStr; String tempStr; String condStr; String location;
  int iconType;
  int hour; int minute; int second; uint32_t lastTick;
} weatherApp; // <-- This line creates the global instance the compiler is looking for!

uint32_t comboHoldStartTime = 0; bool comboTriggered = false;
int menuSelection = 0, lastMenuSelection = -1;
int snakeHigh = 0, bjWins = 0, pongHigh = 0, flappyHigh = 0, tetrisHigh = 0, doomHigh = 0, galagaHigh = 0, dinoHigh = 0, simonHigh = 0;

const int MENU_ITEMS = 17;
const String menuNames[MENU_ITEMS] = { "Snake", "Blackjack", "Pong", "Flappy", "Tetris", "DOOM", "Stopwatch", "Timer", "Galaga", "Dino Run", "Simon", "TicTacToe", "Pac-Man", "Road Fighter", "Weather & Time", "Reset Scores", "Exit to MP3" };
int menuScroll = 0;

struct SnakeGame { int x[320], y[320]; int len, dir, nextDir, appleX, appleY, score, lastScore; bool gameOver; uint32_t lastMove, eqPressTime; bool inMenu; int menuCursor; int mode; int speedLevel; int px, py; bool pAct; uint32_t pTime; } snake;
struct BJGame { int pCards[10], dCards[10]; int pCount, dCount, state; String resultMsg; bool needsRedraw; } bj;
struct PongGame { float ballX, ballY, ballDX, ballDY; int pY, aiY, score, lastScore; bool gameOver; uint32_t lastFrame; } pong;
struct FlappyGame { float y, dy; int pipeX, pipeGapY, score, lastScore; bool gameOver; uint32_t lastFrame; int frameCount; bool isPaused; } flappy;
struct TetrisGame { uint8_t board[20][10]; int x, y; uint8_t type, rot; int score; bool gameOver; uint32_t lastDrop; int dropSpeed; uint32_t eqPressTime; } tetris;
struct GalagaGame { float pX; int score, lastScore, level; bool gameOver; uint32_t lastFrame; float bx[5], by[5]; bool bAct[5]; float ex[10], ey[10]; bool eAct[10]; int eType[10]; int eDir; float eSpeed; int frameCount; } galaga;
struct DinoGame { float y, dy; int score, lastScore; bool gameOver; uint32_t lastFrame; float cx[3]; int type[3]; bool cAct[3]; float speed; int frameCount; bool isDucking; bool inMenu; int menuCursor; bool menuNeedsRedraw; int mode; bool isPaused; } dino;
struct SimonGame { uint8_t seq[50]; int seqLen; int step; bool gameOver; int score; bool showing; uint32_t lastFrame; int flashIdx; uint32_t flashTimer; } simon;
struct TTTGame { int board[9]; int cursor; int mode; int turn; int winner; bool needsRedraw; } ttt;
struct ResetScoresState { int cursor; bool needsRedraw; } resetScoresMenu;

const uint16_t TETROMINO[7][4] = { { 0x0F00, 0x2222, 0x00F0, 0x4444 }, { 0x44C0, 0x8E00, 0x6440, 0x0E20 }, { 0x4460, 0x0E80, 0xC440, 0x2E00 }, { 0xCC00, 0xCC00, 0xCC00, 0xCC00 }, { 0x06C0, 0x8C40, 0x6C00, 0x4620 }, { 0x0E40, 0x4C40, 0x4E00, 0x4640 }, { 0x0C60, 0x4C80, 0xC600, 0x2640 } };
const uint16_t TETRIS_COLORS[8] = { TFT_BLACK, TFT_CYAN, TFT_BLUE, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_MAGENTA, TFT_RED };

// --------------------------------------------------------
// SPRITE MASKS & PROCEDURAL TEXTURES
// --------------------------------------------------------
uint16_t texWall[1024];
uint16_t texDoor[1024];

const uint16_t doomPal[10] = { 0xF81F, 0x0000, 0xCE79, 0x8410, 0xF800, 0xFFFF, 0xCA22, 0x07E0, 0xFED0, 0x001F };

const char* texMonster1 =
"0000000000000000"
"0000011111100000"
"0000144444410000"
"0001444444441000"
"0014454444544100"
"0014151441514100"
"0144444444444410"
"0144411111144410"
"0144155555514410"
"0144151515114410"
"0014411111144100"
"0001444444444410"
"0000144444444100"
"0000001444444100"
"0000000011111100"
"0000000000000000";

const char* texMonster2 =
"0000000000000000"
"0000011111100000"
"0000144444410000"
"0001444444441000"
"0014454444544100"
"0014151441514100"
"0144444444444410"
"0144411111144410"
"0144155555514410"
"0144151515114410"
"0014411111144100"
"0001444444444410"
"0000144000444100"
"0000144000444100"
"0000111000111000"
"0000000000000000";

const char* texMonster3 =
"0000000000000000"
"0000011111100000"
"0000144444410000"
"0001444444441000"
"0014454444544100"
"0014151441514100"
"0144444444444410"
"0144411111144410"
"0144155555514410"
"0144151515114410"
"0014411111144100"
"0001444444444410"
"0001444000014400"
"0001444000014400"
"0001110000011100"
"0000000000000000";

const char* texMonsterBlue1 =
"0000000000000000"
"0000011111100000"
"0000199999910000"
"0001999999991000"
"0019959999599100"
"0019151991519100"
"0199999999999910"
"0199911111199910"
"0199155555519910"
"0199151515119910"
"0019911111199100"
"0001999999999910"
"0000199999999100"
"0000001999999100"
"0000000011111100"
"0000000000000000";

const char* texMonsterBlue2 =
"0000000000000000"
"0000011111100000"
"0000199999910000"
"0001999999991000"
"0019959999599100"
"0019151991519100"
"0199999999999910"
"0199911111199910"
"0199155555519910"
"0199151515119910"
"0019911111199100"
"0001999999999910"
"0000199000499100"
"0000199000499100"
"0000111000111000"
"0000000000000000";

const char* texMonsterBlue3 =
"0000000000000000"
"0000011111100000"
"0000199999910000"
"0001999999991000"
"0019959999599100"
"0019151991519100"
"0199999999999910"
"0199911111199910"
"0199155555519910"
"0199151515119910"
"0019911111199100"
"0001999999999910"
"0001999000019900"
"0001999000019900"
"0001110000011100"
"0000000000000000";

const char* texAmmo =        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000011111111110000001777777771000000177111177100000017717717710000001771111771000000177777777100000017777777710000001777777771000000111111111100000000000000000000000000000000000";
const char* texHealth =      "0000000000000000000000000000000000000000000000000000000000000000000000000000000000011111111110000001555555551000000155544555100000015554455510000001544444451000000154444445100000015554455510000001555445551000000111111111100000000000000000000000000000000000";
const char* texExit =        "0000011111100000000016666661000000016666666610000016666116666100016661155116661016661555555166611661555555551661166155555555166116615555555516611661555555551661166615555551666101666115511666100016666116666100000166666666100000001666666100000000011111100000";
const char* texPistol =      "0000000000000000000000011000000000000012210000000000012552100000000001355310000000001235532100000000122332210000000013222231000000012333333210000001333333331000000111111111100000133333333331000013311111133100001771000017710000177100001771000011110000111100";
const char* texShotgun =     "0000000110000000000000122100000000000122221000000000122112210000000122111122100000122211112221000012521111252100012252111125221001222211112222100133331111333310133333333333333113333111111333311777100000017771177100000000177117100000000001711100000000000011";
const char* texFace =        "00001111111100000001666666661000001666666666610001666666666666100168888888888610168881888818886118881518815188811888888888888881188888111188888118888881188888811888111111118881018881555518881001888811118888100018888888888810000011888888110000000011111100000";
const char* texFaceHurt=     "00001111111100000001666666661000001664466666610001666444666666100168844888888610168881888818886118481418815184811448888888888841148888111188888118888881188448811888111111114481018881444418881001888811118888100018888888888810000011888888110000000011111100000";
const char* texArmor =       "0000000000000000000066000066000000066600006660000066666006666600006666666666660000611666666116000061166116611600006666611666660000611661166116000061166666611600006666611666660000066661166660000006116666116000000611666611600000006666666600000000000000000000";

const char* texGalagaShip = "0000110000000011000000011110000001111000011111111001101101101111111111111111111111000000111000000001";
const char* texGalagaEnemy1 = "0001111000011111111011011110111111111111111111111100100001000101001010100011000100000000000000000000";
const char* texGalagaEnemy2 = "0001111000011111111011011110111111111111111111111101001100101000000001010000001000000000000000000000";
const char* texGalagaEnemy3 = "0010000100011100111011111111111110110111111111111101111111100011111100011000011011000000110000000000";
const char* texGalagaEnemy4 = "0010000100011100111011111111111110110111111111111101111111100011111100110000001101100001100000000000";
const char* texGalagaEnemy5 = "0111111110111111111111011110111111111111111111111101111111100011111100001100110000100001000100000010";
const char* texGalagaEnemy6 = "0111111110111111111111011110111111111111111111111101111111100011111100000111100000110011000110000110";

const char* texCactus1 = "000000000000000000000001100000000000000110000000000001011000000000001101100100000000110110110000000011111011000000000111111100000000000110110000000000011000000000000001100000000000000110000000000000011000000000000001100000000000000110000000";
const char* texBird1 = "000000011000000000000111100000000000111110000000000111111000000011111111111111001111111111110110001111111111110000001111100000000000011000000000000000000000000000000000000000000000000000000000";
const char* texBird2 = "000000000000000000000000000000000000000000000000000000000000000000000000000000000001111110000000011111111111111001111111111110110111111111111110001111111100000000001111000000000000011000000000";

const char* texDinoRun1 = 
  "0000000011111100"
  "0000000010111110"
  "0000000011111110"
  "0000000011111110"
  "0000000011111100"
  "0000000011110000"
  "0000000011111110"
  "0100000111111000"
  "0100001111111000"
  "0110011111110000"
  "0111111111110000"
  "0011111111100000"
  "0001111111000000"
  "0000110011000000"
  "0000100001000000"
  "0000110001100000";

const char* texDinoRun2 = 
  "0000000011111100"
  "0000000010111110"
  "0000000011111110"
  "0000000011111110"
  "0000000011111100"
  "0000000011110000"
  "0000000011111110"
  "0100000111111000"
  "0100001111111000"
  "0110011111110000"
  "0111111111110000"
  "0011111111100000"
  "0001111111000000"
  "0000110011000000"
  "0000000001000000"
  "0000000001100000";

const char* texDinoDuck1 = 
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000001111111"
  "000000000000000001011111"
  "000000000000000001111111"
  "000000001111111111111111"
  "000000111111111111111100"
  "000001111111111111111000"
  "000111111111111111100000"
  "011111111111111110000000"
  "000000110000011000000000"
  "000000110000011000000000"
  "000000110000000000000000";

const char* texDinoDuck2 = 
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000000000000"
  "000000000000000001111111"
  "000000000000000001011111"
  "000000000000000001111111"
  "000000001111111111111111"
  "000000111111111111111100"
  "000001111111111111111000"
  "000111111111111111100000"
  "011111111111111110000000"
  "000000110000011000000000"
  "000000000000011000000000"
  "000000000000011000000000";

const char* texFlappy1 =
"0000011111100000"
"0001122223311000"
"0012222233331000"
"0122222231131000"
"0122222233331110"
"0122222221114441"
"0121112221444441"
"1333331221444441"
"1333331222111110"
"0133312222222210"
"0011111111111100"
"0000000000000000";

const char* texFlappy2 =
"0000011111100000"
"0001122223311000"
"0012222233331000"
"0122222231131000"
"0122222233331110"
"0122222221114441"
"0122222221444441"
"0122111221444441"
"0121333122111110"
"0013333312222210"
"0001333111111100"
"0000111000000000";

const uint16_t flappyPal[4] = { TFT_BLACK, TFT_YELLOW, TFT_WHITE, TFT_ORANGE };

// --------------------------------------------------------
// DOOM MAP - 4 rooms with center arena
// --------------------------------------------------------
const uint8_t E1M1[24][24] = {
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,2,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
  {1,1,2,1,1,1,1,0,0,1,0,0,0,1,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,0,1,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,0,0,0,1,0,0,0,1,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
  {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
};
uint8_t doomMap[32][32];

struct Door { int x, y; float offset; bool opening; }; Door doors[10]; int doorCount = 0;
struct DoomSprite { float x, y; bool active; int type; float dist; uint32_t lastAttack; };
struct DoomGame { float posX, posY, dirX, dirY, planeX, planeY; bool gameOver; int level, health, armor, ammo, magAmmo; bool isShooting, isReloading; uint32_t reloadTimer, eqPressTime, lastMoveTime, lastShotTime; DoomSprite entities[20]; String message; uint32_t messageTimer; int kills; } doom;

// --------------------------------------------------------
// BUTTON HANDLING & CLASS
// --------------------------------------------------------
class Button {
public:
  uint8_t pin; bool lastState = HIGH; uint32_t pressTime = 0, lastHoldTime = 0; bool isLongPressHandled = false;
  Button(uint8_t p) : pin(p) {} void init() { pinMode(pin, INPUT_PULLUP); }
};
Button btnPrev(BTN_PREV_PIN); Button btnPlay(BTN_PLAY_PIN); Button btnNext(BTN_NEXT_PIN); Button btnEQ(BTN_EQ_PIN);
const uint32_t DEBOUNCE_MS = 50, LONG_PRESS_MS = 500, HOLD_REPEAT_MS = 150; 

void playCurrentTrack(); void togglePlayPause(); void cycleBrightness(); void nextTrack(); void prevTrack();
void cycleEQ(); void toggleLoop(); void volUp(); void volDown(); void showMessage(String msg); void forcePlayerRedraw();
void initSnake(); void initBlackjack(); void initPong(); void initFlappy() { 
  flappy.y = 60; 
  flappy.dy = 0; 
  flappy.pipeX = 160; 
  flappy.pipeGapY = random(15, 50); 
  flappy.score = 0; 
  flappy.gameOver = false; 
  flappy.lastFrame = 0; 
  flappy.frameCount = 0; 
} void initTetris(); void initDoomLevel(); void initDoom();
void drawGamesMenu(); void openGamesMenu(); void returnToGamesMenu(); void updateGamesMenuSelection(); void updateIcons(); void updatePlaybackProgress();
void handleStopwatchButtons(); void updateStopwatch(); void handleTimerButtons(); void updateTimer();
void initGalaga(); void handleGalagaButtons(); void updateGalaga();
void initDino(); void handleDinoButtons(); void updateDino();
void initSimon(); void handleSimonButtons(); void updateSimon(); void drawSimonBoard(int highlight);
void initTTT(); void handleTTTButtons(); void updateTTT(); void drawTTTMenu(); void drawTTTBoard();
void initWeather(); void updateWeather(); void handleWeatherButtons();
void handleResetButtons(); void updateResetMenu();

// New Games
void handlePacManButtons(); void updatePacMan();
void handleRoadFighterButtons(); void updateRoadFighter();

// --------------------------------------------------------
// SETUP & PROCEDURAL TEXTURES
// --------------------------------------------------------
void drawSpriteMask(const char* mask, int x, int y, int w, int h, uint16_t color, TFT_eSprite* spr = nullptr) {
  for(int iy=0; iy<h; iy++) {
    for(int ix=0; ix<w; ix++) {
      if(mask[iy*w + ix] == '1') {
        if (spr) spr->drawPixel(x+ix, y+iy, color);
        else tft.drawPixel(x+ix, y+iy, color);
      }
    }
  }
}

void drawIndexedSprite(const char* mask, int x, int y, int w, int h, const uint16_t* pal, TFT_eSprite* spr = nullptr) {
  for(int iy=0; iy<h; iy++) {
    for(int ix=0; ix<w; ix++) {
      char c = mask[iy*w + ix];
      if(c != '0') {
        int idx = c - '1';
        if (spr) spr->drawPixel(x+ix, y+iy, pal[idx]);
        else tft.drawPixel(x+ix, y+iy, pal[idx]);
      }
    }
  }
}

void generateTextures() {
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      int r = 160, g = 110, b = 90; bool mortar = false; 
      if (y % 8 == 0 || y % 8 == 7) mortar = true;
      if ((y / 8) % 2 == 0) { if (x % 16 == 0 || x % 16 == 15) mortar = true; } else { if ((x + 8) % 16 == 0 || (x + 8) % 16 == 15) mortar = true; }
      if (mortar) { r=90; g=80; b=70; } else { int noise = (x*17 + y*23) % 25 - 12; r = constrain(r + noise, 0, 255); g = constrain(g + noise, 0, 255); b = constrain(b + noise, 0, 255); }
      texWall[y * 32 + x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }
  for (int y = 0; y < 32; y++) {
    for (int x = 0; x < 32; x++) {
      int r = 100, g = 110, b = 120;
      if (x < 2 || x > 29 || y < 2 || y > 29 || abs(x-16)<2) { r=50; g=60; b=70; } else if (y % 4 == 0) { r=80; g=90; b=100; }
      if (abs(x-16)<3 && abs(y-16)<3) { r=200; g=50; b=50; }
      texDoor[y * 32 + x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
  }
}

void setup() {
  Serial.begin(115200); tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  
  uint16_t SHAD = 0x2104; 
  tft.fillTriangle(71, 27, 91, 27, 81, 6, SHAD); 
  tft.fillTriangle(71, 55, 91, 55, 81, 76, SHAD); 
  tft.fillTriangle(67, 31, 67, 51, 46, 41, SHAD); 
  tft.fillTriangle(95, 31, 95, 51, 116, 41, SHAD); 
  tft.fillRect(71, 29, 20, 24, SHAD);
  tft.fillRect(69, 31, 24, 20, SHAD);
  
  tft.fillTriangle(70, 26, 90, 26, 80, 5, TFT_RED); 
  tft.fillTriangle(70, 54, 90, 54, 80, 75, TFT_YELLOW); 
  tft.fillTriangle(66, 30, 66, 50, 45, 40, 0x06A0); 
  tft.fillTriangle(94, 30, 94, 50, 115, 40, 0x051F); 
  tft.fillRect(70, 28, 20, 24, 0x9136); 
  tft.fillRect(68, 30, 24, 20, 0x9136); 
  
  int tw = tft.textWidth("B L A Z E   O S", 2);
  tft.setTextColor(SHAD); tft.drawString("B L A Z E   O S", 80 - tw/2 + 1, 83, 2);
  tft.setTextColor(TFT_WHITE); tft.drawString("B L A Z E   O S", 80 - tw/2, 82, 2);
  
  tft.drawPixel(80, 5, TFT_BLACK);
  tft.drawFastHLine(79, 6, 3, TFT_BLACK);
  tft.drawFastHLine(79, 7, 3, TFT_RED);
  
  for (int iter = 0; iter < 2; iter++) {
     for (int j = 0; j <= 12; j++) {
         uint16_t glow = tft.color565(j*21, 0, j*21); 
         tft.drawRoundRect(40, 2, 80, 78, 8, glow);
         tft.drawRoundRect(41, 3, 78, 76, 7, glow);
         delay(15);
     }
     for (int j = 12; j >= 0; j--) {
         uint16_t glow = tft.color565(j*21, 0, j*21); 
         tft.drawRoundRect(40, 2, 80, 78, 8, glow);
         tft.drawRoundRect(41, 3, 78, 76, 7, glow);
         delay(15);
     }
  }
  tft.drawRoundRect(40, 2, 80, 78, 8, TFT_BLACK);
  tft.drawRoundRect(41, 3, 78, 76, 7, TFT_BLACK);
  
  tft.drawLine(0, 100, 160, 100, 0x18E3); 
  
  int by = 103;
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString("[ **** ] sys_core_init", 2, by, 1);
  btnPrev.init(); btnPlay.init(); btnNext.init(); btnEQ.init();
  discSprite.createSprite(DISC_SIZE, DISC_SIZE); discSprite.setPivot(DISC_RADIUS, DISC_RADIUS); 
  gameSprite.createSprite(160, 128); 
  generateTextures(); 
  delay(150);
  tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("  OK  ", 8, by, 1);
  by += 8;
  
  preferences.begin("blaze", false); 
  state.currentTrack = preferences.getUShort("track", 1); state.brightnessLevel = preferences.getUShort("brightness", 3); 
  snakeHigh = preferences.getInt("snake_hi", 0); bjWins = preferences.getInt("bj_hi", 0); pongHigh = preferences.getInt("pong_hi", 0); flappyHigh = preferences.getInt("flap_hi", 0); tetrisHigh = preferences.getInt("tet_hi", 0); doomHigh = preferences.getInt("doom_hi", 0);
  galagaHigh = preferences.getInt("galaga_hi", 0); dinoHigh = preferences.getInt("dino_hi", 0); simonHigh = preferences.getInt("simon_hi", 0);
  pinMode(TFT_BL_PIN, OUTPUT); int pwmValue = (state.brightnessLevel == 0) ? 15 : (state.brightnessLevel == 1) ? 80 : (state.brightnessLevel == 2) ? 160 : 255; analogWrite(TFT_BL_PIN, pwmValue);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString("[ **** ] mnt_dfplayer", 2, by, 1);
  dfSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX, DFPLAYER_TX); 
  bool dfReady = dfPlayer.begin(dfSerial, true, true);
  if (!dfReady) { 
      tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawString(" FAIL ", 8, by, 1); by += 8;
      delay(800); 
      state.totalTracks = 0;
  } else {
      tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("  OK  ", 8, by, 1); by += 8;
      dfPlayer.volume(state.volume); 
      dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
      
      tft.setTextColor(TFT_WHITE, TFT_BLACK); tft.drawString("[ **** ] mnt_sd_volume", 2, by, 1);
      delay(200); 
      
      state.totalTracks = dfPlayer.readFileCounts();
      int retries = 0; 
      while ((state.totalTracks == 0 || state.totalTracks == 65535) && retries < 2) { 
          delay(400); 
          state.totalTracks = dfPlayer.readFileCounts(); 
          retries++; 
      }
      
      if (state.totalTracks == 0 || state.totalTracks == 65535) {
          tft.setTextColor(TFT_RED, TFT_BLACK); tft.drawString(" FAIL ", 8, by, 1);
          state.totalTracks = 0;
      } else {
          tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString("  OK  ", 8, by, 1);
      }
      delay(800);
  }
  
  if (state.totalTracks == 0 || state.totalTracks == 65535) {
      openGamesMenu();
  } else {
      if (state.currentTrack > state.totalTracks || state.currentTrack == 0) state.currentTrack = 1;
      playCurrentTrack();
  }
}

void loop() {
  if (dfPlayer.available() && dfPlayer.readType() == DFPlayerPlayFinished && state.trackElapsedSec > 1) { if (state.loopEnabled) playCurrentTrack(); else nextTrack(); }
  if (state.isPlaying && (millis() - state.lastTickTime >= 1000)) { state.lastTickTime = millis(); state.trackElapsedSec++; }
  if (digitalRead(BTN_PREV_PIN) == LOW && digitalRead(BTN_NEXT_PIN) == LOW) {
    if (comboHoldStartTime == 0) comboHoldStartTime = millis(); else if (millis() - comboHoldStartTime > 2000 && !comboTriggered) { comboTriggered = true; if (currentMode == MODE_PLAYER) openGamesMenu(); }
  } else { comboHoldStartTime = 0; comboTriggered = false; }

  if (util.tmrRunning && millis() > util.tmrEndMs) { util.tmrRunning = false; util.tmrAlert = true; util.tmrSetSecs = 0; }

  if (util.tmrAlert) {
     if (!util.alertDrawn) {
       tft.fillRect(20, 40, 120, 40, TFT_RED); tft.drawRect(20, 40, 120, 40, TFT_WHITE);
       tft.setTextColor(TFT_WHITE, TFT_RED); tft.drawString(" TIMER DONE! ", 27, 48, 2); 
       tft.drawString(" Press EQ to stop ", 25, 65, 1);
       util.alertDrawn = true;
     }
     if (digitalRead(BTN_EQ_PIN) == LOW) { 
        util.tmrAlert = false; util.alertDrawn = false; delay(300);
        if (currentMode == MODE_PLAYER) forcePlayerRedraw(); 
        else if (currentMode == MODE_GAMES_MENU) { lastMenuSelection = -1; drawGamesMenu(); } 
        else if (currentMode == MODE_BLACKJACK) bj.needsRedraw = true; 
        else if (currentMode == MODE_STOPWATCH || currentMode == MODE_TIMER) { tft.fillScreen(TFT_BLACK); util.needsFullRedraw = true; }
        else if (currentMode == MODE_TTT) ttt.needsRedraw = true;
     }
     delay(5); return;
  }

  if (!comboTriggered) { 
    if (currentMode == MODE_PLAYER) handlePlayerButtons(); else if (currentMode == MODE_GAMES_MENU) handleMenuButtons(); else if (currentMode == MODE_SNAKE) handleSnakeButtons(); else if (currentMode == MODE_BLACKJACK) handleBlackjackButtons(); else if (currentMode == MODE_PONG) handlePongButtons(); else if (currentMode == MODE_FLAPPY) handleFlappyButtons(); else if (currentMode == MODE_TETRIS) handleTetrisButtons(); else if (currentMode == MODE_DOOM) handleDoomButtons(); else if (currentMode == MODE_STOPWATCH) handleStopwatchButtons(); else if (currentMode == MODE_TIMER) handleTimerButtons(); else if (currentMode == MODE_GALAGA) handleGalagaButtons(); else if (currentMode == MODE_DINO) handleDinoButtons(); else if (currentMode == MODE_SIMON) handleSimonButtons(); else if (currentMode == MODE_TTT) handleTTTButtons(); else if (currentMode == MODE_PACMAN) handlePacManButtons(); else if (currentMode == MODE_ROADFIGHTER) handleRoadFighterButtons(); else if (currentMode == MODE_WEATHER) handleWeatherButtons(); else if (currentMode == MODE_RESET_SCORES) handleResetButtons();
  }
  if (currentMode == MODE_PLAYER) updatePlayerDisplay(); else if (currentMode == MODE_GAMES_MENU) updateGamesMenuSelection(); else if (currentMode == MODE_SNAKE) updateSnake(); else if (currentMode == MODE_BLACKJACK) updateBlackjack(); else if (currentMode == MODE_PONG) updatePong(); else if (currentMode == MODE_FLAPPY) updateFlappy(); else if (currentMode == MODE_TETRIS) updateTetris(); else if (currentMode == MODE_DOOM) updateDoom(); else if (currentMode == MODE_STOPWATCH) updateStopwatch(); else if (currentMode == MODE_TIMER) updateTimer(); else if (currentMode == MODE_GALAGA) updateGalaga(); else if (currentMode == MODE_DINO) updateDino(); else if (currentMode == MODE_SIMON) updateSimon(); else if (currentMode == MODE_TTT) updateTTT(); else if (currentMode == MODE_PACMAN) updatePacMan(); else if (currentMode == MODE_ROADFIGHTER) updateRoadFighter(); else if (currentMode == MODE_WEATHER) updateWeather(); else if (currentMode == MODE_RESET_SCORES) updateResetMenu();
  delay(5); 
}

// --------------------------------------------------------
// PLAYER UI, MENU, GAMES, & UTILITIES
// --------------------------------------------------------
void processButton(Button &btn, void (*onShortPress)(), void (*onLongPress)(), void (*onHold)()) { bool currentState = digitalRead(btn.pin); uint32_t now = millis(); if (currentState == LOW && btn.lastState == HIGH) { btn.pressTime = now; btn.lastHoldTime = now; btn.isLongPressHandled = false; } else if (currentState == LOW && btn.lastState == LOW) { if (now - btn.pressTime > LONG_PRESS_MS) { if (!btn.isLongPressHandled && onLongPress) { btn.isLongPressHandled = true; onLongPress(); } if (onHold && (now - btn.lastHoldTime > HOLD_REPEAT_MS)) { btn.isLongPressHandled = true; onHold(); btn.lastHoldTime = now; } } } else if (currentState == HIGH && btn.lastState == LOW) { if ((now - btn.pressTime > DEBOUNCE_MS) && !btn.isLongPressHandled) { if (onShortPress) onShortPress(); } } btn.lastState = currentState; }
void handlePlayerButtons() { processButton(btnPrev, prevTrack, nullptr, volDown); processButton(btnNext, nextTrack, nullptr, volUp); processButton(btnPlay, togglePlayPause, cycleBrightness, nullptr); processButton(btnEQ, cycleEQ, toggleLoop, nullptr); }
void playCurrentTrack() { dfPlayer.play(state.currentTrack); state.isPlaying = true; state.trackElapsedSec = 0; state.lastTickTime = millis(); forcePlayerRedraw(); preferences.putUShort("track", state.currentTrack); }
void togglePlayPause() { if (state.isPlaying) { dfPlayer.pause(); state.isPlaying = false; } else { dfPlayer.start(); state.isPlaying = true; state.lastTickTime = millis(); } }
void nextTrack() { state.currentTrack++; if (state.currentTrack > state.totalTracks) state.currentTrack = 1; playCurrentTrack(); }
void prevTrack() { if (state.trackElapsedSec > 3) playCurrentTrack(); else { state.currentTrack--; if (state.currentTrack < 1) state.currentTrack = state.totalTracks; playCurrentTrack(); } }
void volUp() { if (state.volume < 30) { state.volume++; dfPlayer.volume(state.volume); } showMessage("Vol: " + String(state.volume)); }
void volDown() { if (state.volume > 0) { state.volume--; dfPlayer.volume(state.volume); } showMessage("Vol: " + String(state.volume)); }
void cycleBrightness() { state.brightnessLevel++; if (state.brightnessLevel > 3) state.brightnessLevel = 0; int pwmValue = (state.brightnessLevel == 0) ? 15 : (state.brightnessLevel == 1) ? 80 : (state.brightnessLevel == 2) ? 160 : 255; analogWrite(TFT_BL_PIN, pwmValue); preferences.putUShort("brightness", state.brightnessLevel); showMessage("Bright: " + String(state.brightnessLevel + 1)); }
void cycleEQ() { state.currentEQ++; if (state.currentEQ > 5) state.currentEQ = 0; switch(state.currentEQ) { case 0: dfPlayer.EQ(DFPLAYER_EQ_NORMAL); showMessage("EQ: Norm"); break; case 1: dfPlayer.EQ(DFPLAYER_EQ_POP); showMessage("EQ: Pop"); break; case 2: dfPlayer.EQ(DFPLAYER_EQ_ROCK); showMessage("EQ: Rock"); break; case 3: dfPlayer.EQ(DFPLAYER_EQ_JAZZ); showMessage("EQ: Jazz"); break; case 4: dfPlayer.EQ(DFPLAYER_EQ_CLASSIC); showMessage("EQ: Class"); break; case 5: dfPlayer.EQ(DFPLAYER_EQ_BASS); showMessage("EQ: Bass"); break; } }
void toggleLoop() { state.loopEnabled = !state.loopEnabled; showMessage(state.loopEnabled ? "Loop: On" : "Loop: Off"); }
void showMessage(String msg) { state.temporaryMessage = msg; state.messageClearTime = millis() + 1500; tft.fillRect(80, 30, 80, 50, COLOR_BG); tft.setTextColor(COLOR_ACCENT, COLOR_BG); tft.drawString(msg, 120 - (tft.textWidth(msg, 2)/2), 45, 2); }
void forcePlayerRedraw() { state.forceFullRedraw = true; state.lastBarFill = -1; state.lastDrawnSec = -1; state.lastDrawnTrack = -1; state.lastPlayState = -1; state.lastLoopState = -1; for(int i=0; i<8; i++) state.lastH[i] = -1; }
void updatePlayerDisplay() { if (state.forceFullRedraw) { tft.fillScreen(COLOR_BG); tft.drawLine(0, 20, 160, 20, COLOR_DIM); tft.setTextColor(COLOR_DIM, COLOR_BG); tft.drawString("BLAZE MP3", 50, 4, 1); discSprite.fillSprite(COLOR_BG); discSprite.fillCircle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS, COLOR_RECORD); discSprite.fillTriangle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS + 30, DISC_RADIUS - 15, DISC_RADIUS + 15, DISC_RADIUS - 30, COLOR_SHEEN); discSprite.fillTriangle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS - 30, DISC_RADIUS + 15, DISC_RADIUS - 15, DISC_RADIUS + 30, COLOR_SHEEN); discSprite.drawCircle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS - 4, 0x18E3); discSprite.drawCircle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS - 12, 0x18E3); discSprite.drawCircle(DISC_RADIUS, DISC_RADIUS, DISC_RADIUS - 20, 0x18E3); discSprite.fillCircle(DISC_RADIUS, DISC_RADIUS, 12, COLOR_LABEL); discSprite.drawCircle(DISC_RADIUS, DISC_RADIUS, 12, TFT_BLACK); discSprite.fillCircle(DISC_RADIUS, DISC_RADIUS, 3, COLOR_BG); discSprite.fillCircle(DISC_RADIUS, DISC_RADIUS, 1, TFT_WHITE); tft.drawRect(8, 113, 144, 8, COLOR_BAR_OUT); tft.fillRect(10, 115, 140, 4, COLOR_BG); state.lastBarFill = -1; state.forceFullRedraw = false; } static uint32_t lastVisUpdate = 0; static bool wasPlaying = false; const uint16_t visColors[8] = { TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, 0x03EF, TFT_BLUE, TFT_MAGENTA }; if (millis() - lastVisUpdate > 120 || state.isPlaying != wasPlaying) { lastVisUpdate = millis(); wasPlaying = state.isPlaying; for(int i = 0; i < 8; i++) { int h = state.isPlaying ? random(2, 14) : 2; if (h != state.lastH[i]) { tft.fillRect(5 + (i * 5), 4, 3, 14 - h, COLOR_BG); tft.fillRect(5 + (i * 5), 18 - h, 3, h, visColors[i]); state.lastH[i] = h; } } } if (state.isPlaying) { state.discAngle += 1.2; if (state.discAngle >= 360.0) state.discAngle -= 360.0; } tft.setPivot(45, 60); discSprite.pushRotated(state.discAngle, COLOR_BG); if (state.temporaryMessage == "" && state.currentTrack != state.lastDrawnTrack) { tft.fillRect(80, 30, 80, 50, COLOR_BG); tft.setTextColor(COLOR_TEXT, COLOR_BG); String tNum = String(state.currentTrack); tft.drawString(tNum, 120 - (tft.textWidth(tNum, 4)/2), 35, 4); tft.setTextColor(COLOR_DIM, COLOR_BG); tft.drawString("TRACK", 106, 62, 1); state.lastDrawnTrack = state.currentTrack; } else if (state.temporaryMessage != "" && millis() > state.messageClearTime) { state.temporaryMessage = ""; state.lastDrawnTrack = -1; } updateIcons(); if (state.trackElapsedSec != state.lastDrawnSec) { char eStr[10]; sprintf(eStr, "%02d:%02d", state.trackElapsedSec / 60, state.trackElapsedSec % 60); tft.fillRect(50, 95, 60, 16, COLOR_BG); tft.setTextColor(COLOR_DIM, COLOR_BG); tft.drawString(eStr, 80 - (tft.textWidth(eStr, 1)/2), 97, 1); state.lastDrawnSec = state.trackElapsedSec; } updatePlaybackProgress(); }
void updatePlaybackProgress() { int barWidth = 140; int barFill = (state.trackElapsedSec * barWidth) / 210; if (barFill > barWidth) barFill = barWidth; if (barFill != state.lastBarFill) { tft.drawRect(8, 113, barWidth + 4, 8, COLOR_BAR_OUT); tft.fillRect(10, 115, barWidth, 4, COLOR_BG); if (barFill > 0) { tft.fillRect(10, 115, barFill, 4, COLOR_BAR_FILL); } state.lastBarFill = barFill; } }
void updateIcons() { if (state.isPlaying != state.lastPlayState) { tft.fillRect(140, 2, 16, 16, COLOR_BG); if (state.isPlaying) { tft.fillRect(143, 4, 3, 10, COLOR_ACCENT); tft.fillRect(150, 4, 3, 10, COLOR_ACCENT); } else { tft.fillTriangle(143, 4, 143, 14, 153, 9, TFT_GREEN); } state.lastPlayState = state.isPlaying; } if (state.loopEnabled != state.lastLoopState) { tft.fillRect(115, 2, 16, 16, COLOR_BG); if (state.loopEnabled) { tft.drawRoundRect(117, 4, 12, 9, 3, TFT_CYAN); tft.fillRect(119, 6, 8, 5, COLOR_BG); tft.fillTriangle(126, 2, 129, 6, 123, 6, TFT_CYAN); } state.lastLoopState = state.loopEnabled; } }

void drawGamesMenu() { 
  tft.fillScreen(TFT_BLACK); 
  tft.setTextColor(TFT_MAGENTA); 
  tft.drawString("APPS & GAMES", 20, 5, 2); 
  tft.drawLine(0, 22, 160, 22, TFT_DARKGREY); 
  for(int i = 0; i < 8; i++) { 
    int idx = menuScroll + i; 
    if (idx >= MENU_ITEMS) break; 
    int y = 28 + (i * 12); 
    tft.setTextColor((idx == menuSelection) ? TFT_CYAN : TFT_WHITE); 
    String prefix = (idx == menuSelection) ? "> " : "  ";
    tft.drawString(prefix + menuNames[idx], 10, y, 1); 
  } 
  lastMenuSelection = menuSelection; 
}
void openGamesMenu() { currentMode = MODE_GAMES_MENU; menuSelection = 0; menuScroll = 0; lastMenuSelection = -1; drawGamesMenu(); }
void returnToGamesMenu() { currentMode = MODE_GAMES_MENU; lastMenuSelection = -1; drawGamesMenu(); }
void updateGamesMenuSelection() { 
  if (menuSelection != lastMenuSelection) { 
    if (menuSelection < menuScroll) { menuScroll = menuSelection; drawGamesMenu(); return; } 
    if (menuSelection >= menuScroll + 8) { menuScroll = menuSelection - 7; drawGamesMenu(); return; } 
    int oldI = lastMenuSelection - menuScroll; int oldY = 28 + (oldI * 12); 
    tft.fillRect(10, oldY, 140, 10, TFT_BLACK); 
    tft.setTextColor(TFT_WHITE); tft.drawString("  " + menuNames[lastMenuSelection], 10, oldY, 1); 
    int newI = menuSelection - menuScroll; int newY = 28 + (newI * 12); 
    tft.fillRect(10, newY, 140, 10, TFT_BLACK); 
    tft.setTextColor(TFT_CYAN); tft.drawString("> " + menuNames[menuSelection], 10, newY, 1); 
    lastMenuSelection = menuSelection; 
  } 
}
void handleMenuButtons() { 
  if (digitalRead(BTN_NEXT_PIN) == LOW) { menuSelection++; if(menuSelection>=MENU_ITEMS) menuSelection=0; delay(200); } 
  if (digitalRead(BTN_PREV_PIN) == LOW) { menuSelection--; if(menuSelection<0) menuSelection=MENU_ITEMS-1; delay(200); } 
  if (digitalRead(BTN_PLAY_PIN) == LOW) { 
    if (menuSelection == 0) { initSnake(); currentMode = MODE_SNAKE; } 
    else if (menuSelection == 1) { initBlackjack(); currentMode = MODE_BLACKJACK; } 
    else if (menuSelection == 2) { initPong(); currentMode = MODE_PONG; } 
    else if (menuSelection == 3) { initFlappy(); currentMode = MODE_FLAPPY; } 
    else if (menuSelection == 4) { initTetris(); currentMode = MODE_TETRIS; } 
    else if (menuSelection == 5) { initDoom(); currentMode = MODE_DOOM; } 
    else if (menuSelection == 6) { currentMode = MODE_STOPWATCH; tft.fillScreen(TFT_BLACK); util.needsFullRedraw = true; } 
    else if (menuSelection == 7) { currentMode = MODE_TIMER; tft.fillScreen(TFT_BLACK); util.needsFullRedraw = true; } 
    else if (menuSelection == 8) { initGalaga(); currentMode = MODE_GALAGA; } 
    else if (menuSelection == 9) { initDino(); currentMode = MODE_DINO; } 
    else if (menuSelection == 10) { initSimon(); currentMode = MODE_SIMON; } 
    else if (menuSelection == 11) { initTTT(); currentMode = MODE_TTT; drawTTTMenu(); } 
    else if (menuSelection == 12) { currentMode = MODE_PACMAN; }
    else if (menuSelection == 13) { currentMode = MODE_ROADFIGHTER; }
    else if (menuSelection == 14) { initWeather(); currentMode = MODE_WEATHER; }
    else if (menuSelection == 15) { 
       currentMode = MODE_RESET_SCORES;
       resetScoresMenu.cursor = 0;
       resetScoresMenu.needsRedraw = true;
    } 
    else if (menuSelection == 16) { 
        int tracks = 0;
        
        if (state.totalTracks == 0) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_YELLOW);
            tft.drawString("Checking SD...", 20, 50, 2);
            
            if (dfPlayer.begin(dfSerial, true, true)) {
                delay(500);
                tracks = dfPlayer.readFileCounts();
                dfPlayer.volume(state.volume); 
                dfPlayer.EQ(DFPLAYER_EQ_NORMAL);
            }
        } else {
            tracks = dfPlayer.readFileCounts();
        }
        
        if (tracks == 0 || tracks == 65535) {
            state.totalTracks = 0;
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(TFT_RED);
            tft.drawString("SD card not inserted!", 10, 50, 2);
            delay(1500);
            drawGamesMenu();
        } else {
            state.totalTracks = tracks;
            if (state.currentTrack > state.totalTracks || state.currentTrack == 0) state.currentTrack = 1;
            currentMode = MODE_PLAYER; 
            forcePlayerRedraw(); 
            if(!state.isPlaying) playCurrentTrack();
        }
    } 
    delay(300); 
  } 
}

void handleStopwatchButtons() { if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } if (digitalRead(BTN_PLAY_PIN) == LOW) { if (util.swRunning) { util.swAccumMs += (millis() - util.swStartMs); util.swRunning = false; } else { util.swStartMs = millis(); util.swRunning = true; } delay(200); } if (digitalRead(BTN_PREV_PIN) == LOW || digitalRead(BTN_NEXT_PIN) == LOW) { util.swRunning = false; util.swAccumMs = 0; delay(200); } }
void updateStopwatch() { uint32_t ms = util.swAccumMs; if (util.swRunning) ms += (millis() - util.swStartMs); uint32_t secs = ms / 1000; uint32_t mins = secs / 60; uint32_t hrs = mins / 60; secs %= 60; mins %= 60; uint32_t tenths = (ms % 1000) / 100; char buf[20]; sprintf(buf, "%02d:%02d:%02d.%1d", hrs, mins, secs, tenths); if (util.needsFullRedraw) { tft.drawRect(5, 5, 150, 118, TFT_CYAN); tft.drawRect(7, 7, 146, 114, TFT_DARKGREY); tft.fillRect(15, 15, 130, 20, 0x03EF); tft.setTextColor(TFT_WHITE, 0x03EF); tft.drawString(" CHRONOMETER ", 25, 17, 2); tft.drawLine(15, 45, 145, 45, TFT_CYAN); tft.drawLine(15, 85, 145, 85, TFT_CYAN); tft.setTextColor(COLOR_DIM, TFT_BLACK); tft.drawString("PL:Run PR:Rst EQ:Ext", 20, 106, 1); util.needsFullRedraw = false; } tft.setTextColor(TFT_GREEN, TFT_BLACK); tft.drawString(buf, 20, 55, 4); tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString(util.swRunning ? "[ RUNNING ]" : (util.swAccumMs > 0 ? "[ PAUSED ] " : "[ READY ]  "), 25, 93, 1); }

void handleTimerButtons() { if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } if (digitalRead(BTN_PLAY_PIN) == LOW) { if (util.tmrRunning) { util.tmrRunning = false; util.tmrSetSecs = (util.tmrEndMs - millis()) / 1000; } else { if (util.tmrSetSecs > 0) { util.tmrEndMs = millis() + (util.tmrSetSecs * 1000); util.tmrTotalSecs = util.tmrSetSecs; util.tmrRunning = true; } } delay(200); } if (!util.tmrRunning) { if (digitalRead(BTN_NEXT_PIN) == LOW) { util.tmrSetSecs += 10; delay(120); } if (digitalRead(BTN_PREV_PIN) == LOW) { if (util.tmrSetSecs >= 10) util.tmrSetSecs -= 10; else util.tmrSetSecs = 0; delay(120); } } }
void updateTimer() { 
  if (util.tmrAlert) { tft.fillScreen(TFT_RED); tft.setTextColor(TFT_WHITE); tft.drawString(" TIME'S UP! ", 20, 50, 4); delay(500); tft.fillScreen(TFT_BLACK); delay(500); return; }
  uint32_t left = util.tmrSetSecs; 
  if (util.tmrRunning) { 
      if (millis() > util.tmrEndMs) { util.tmrRunning = false; util.tmrAlert = true; util.tmrSetSecs = 0; left = 0; } 
      else left = (util.tmrEndMs - millis()) / 1000; 
  } 
  uint32_t mins = left / 60; uint32_t secs = left % 60; char buf[10]; sprintf(buf, "%02d:%02d", mins, secs); 
  static int lastFill = -1; 
  if (util.needsFullRedraw) { 
      tft.drawRect(5, 5, 150, 118, TFT_ORANGE); tft.drawRect(7, 7, 146, 114, TFT_DARKGREY); 
      tft.fillRect(15, 15, 130, 20, 0x9000); tft.setTextColor(TFT_WHITE, 0x9000); 
      tft.drawString(" UPLINK TIMER ", 30, 17, 2); tft.drawRect(15, 40, 130, 8, TFT_DARKGREY); 
      tft.setTextColor(COLOR_DIM, TFT_BLACK); tft.drawString("PL:Go PR/NX:Set EQ:Ext", 14, 106, 1); 
      util.needsFullRedraw = false; lastFill = -1; 
  } 
  int fill = 0; 
  if (util.tmrRunning && util.tmrTotalSecs > 0) fill = (left * 126) / util.tmrTotalSecs; 
  if (fill != lastFill) { 
      if (fill > 0) tft.fillRect(17, 42, fill, 4, TFT_ORANGE); 
      if (126 - fill > 0) tft.fillRect(17 + fill, 42, 126 - fill, 4, TFT_BLACK); lastFill = fill; 
  } 
  tft.setTextColor(util.tmrRunning ? TFT_ORANGE : TFT_CYAN, TFT_BLACK); 
  tft.drawString(buf, 80 - (tft.textWidth(buf, 4)/2), 55, 4); 
  tft.setTextColor(TFT_YELLOW, TFT_BLACK); 
  String timerStateStr = "[ SET TIME: +/- 10s ]";
  if (util.tmrRunning) { timerStateStr = "[ COUNTDOWN ACTIVE ]"; } 
  else if (util.tmrSetSecs > 0) { timerStateStr = "[ COUNTDOWN PAUSED ]"; }
  tft.drawString(timerStateStr, 80 - (tft.textWidth(timerStateStr, 1)/2), 88, 1); 
}

// WEATHER APP LOGIC
void drawWeatherIcon(int type, int x, int y) {
  gameSprite.fillRect(x, y, 36, 36, TFT_BLACK);

  if (type == 0) { 
    uint16_t COL = TFT_YELLOW; 
    gameSprite.drawCircle(x+18, y+18, 6, COL); 
    gameSprite.drawCircle(x+18, y+18, 5, COL); 
    for(int i=0; i<8; i++) {
       float angle = i * 45 * 3.14159 / 180.0;
       gameSprite.drawLine(x+18 + cos(angle)*9, y+18 + sin(angle)*9, x+18 + cos(angle)*15, y+18 + sin(angle)*15, COL);
    }
  } else if (type == 1) { 
    uint16_t COL = TFT_LIGHTGREY; 
    gameSprite.drawFastHLine(x+8, y+24, 20, COL);
    gameSprite.drawFastHLine(x+8, y+23, 20, COL);
    gameSprite.drawCircle(x+14, y+19, 5, COL);
    gameSprite.drawCircle(x+14, y+18, 5, COL);
    gameSprite.drawCircle(x+22, y+17, 7, COL);
    gameSprite.drawCircle(x+22, y+16, 7, COL);
    gameSprite.fillRect(x+14, y+16, 8, 8, TFT_BLACK);
  } else if (type == 2) { 
    uint16_t COL = TFT_LIGHTGREY; 
    uint16_t COL_R = TFT_WHITE;
    gameSprite.drawFastHLine(x+8, y+18, 20, COL);
    gameSprite.drawFastHLine(x+8, y+17, 20, COL);
    gameSprite.drawCircle(x+14, y+13, 5, COL);
    gameSprite.drawCircle(x+14, y+12, 5, COL);
    gameSprite.drawCircle(x+22, y+11, 7, COL);
    gameSprite.drawCircle(x+22, y+10, 7, COL);
    gameSprite.fillRect(x+14, y+10, 8, 8, TFT_BLACK);
    for(int i=0; i<3; i++) {
       gameSprite.drawLine(x + 11 + i*7, y + 21, x + 8 + i*7, y + 30, COL_R);
    }
  } else { 
    uint16_t COL = TFT_LIGHTGREY; 
    uint16_t COL_S = TFT_WHITE;
    gameSprite.drawFastHLine(x+8, y+18, 20, COL);
    gameSprite.drawFastHLine(x+8, y+17, 20, COL);
    gameSprite.drawCircle(x+14, y+13, 5, COL);
    gameSprite.drawCircle(x+14, y+12, 5, COL);
    gameSprite.drawCircle(x+22, y+11, 7, COL);
    gameSprite.drawCircle(x+22, y+10, 7, COL);
    gameSprite.fillRect(x+14, y+10, 8, 8, TFT_BLACK);
    gameSprite.drawLine(x+13, y+23, x+13, y+29, COL_S); gameSprite.drawLine(x+10, y+26, x+16, y+26, COL_S);
    gameSprite.drawLine(x+23, y+25, x+23, y+31, COL_S); gameSprite.drawLine(x+20, y+28, x+26, y+28, COL_S);
  }
}

void initWeather() {
  weatherApp.fetching = true;
  weatherApp.success = false;
  weatherApp.location = "SYNCING...";
  
  gameSprite.fillSprite(TFT_BLACK);
  uint16_t ACCENT = TFT_ORANGE; 
  gameSprite.drawRect(2, 2, 156, 124, ACCENT);
  gameSprite.setTextColor(ACCENT);
  gameSprite.drawString("UPLINK COMM...", 15, 50, 1);
  gameSprite.pushSprite(0, 0);
  
  String targetSSID = ""; String targetPass = "";
  for(int i=0; i<3; i++) {
      if(String(wifi_ssids[i]) != "" && String(wifi_ssids[i]) != "YOUR_WIFI_1") {
          targetSSID = wifi_ssids[i];
          targetPass = wifi_passwords[i];
          break;
      }
  }
  
  if (WiFi.status() != WL_CONNECTED && targetSSID != "") {
      WiFi.mode(WIFI_STA);
      WiFi.begin(targetSSID.c_str(), targetPass.c_str());
      int retries = 0;
      while(WiFi.status() != WL_CONNECTED && retries < 15) { delay(200); retries++; }
  }
  
  if(WiFi.status() != WL_CONNECTED) {
     gameSprite.fillSprite(TFT_BLACK);
     gameSprite.drawRect(2, 2, 156, 124, TFT_RED);
     gameSprite.setTextColor(TFT_RED);
     gameSprite.drawString("NETWORK ERR", 15, 45, 2);
     gameSprite.drawString("No Signal.", 15, 70, 1);
     gameSprite.pushSprite(0, 0);
     weatherApp.fetching = false;
     return;
  }
  
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  int ntpRetries = 0;
  while(!getLocalTime(&timeinfo) && ntpRetries < 8) { delay(100); ntpRetries++; }
  if (ntpRetries < 8) {
      weatherApp.hour = timeinfo.tm_hour;
      weatherApp.minute = timeinfo.tm_min;
      weatherApp.second = timeinfo.tm_sec;
      weatherApp.lastTick = millis();
      char dateBuf[12];
      sprintf(dateBuf, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
      weatherApp.dateStr = String(dateBuf);
  }

  gameSprite.fillSprite(TFT_BLACK);
  gameSprite.drawRect(2, 2, 156, 124, TFT_ORANGE);
  gameSprite.setTextColor(TFT_ORANGE);
  gameSprite.drawString("DOWNLOADING...", 15, 50, 1);
  gameSprite.pushSprite(0, 0);
  
  HTTPClient http;
  
  static String cached_lat = "";
  static String cached_lon = "";
  static String cached_city = "";

  if (cached_city == "") {
      http.begin("http://ip-api.com/json/");
      int httpCode = http.GET();
      if(httpCode == 200) {
         String payload = http.getString();
         #if ARDUINOJSON_VERSION_MAJOR >= 7
           JsonDocument doc;
         #else
           DynamicJsonDocument doc(1024);
         #endif
         deserializeJson(doc, payload);
         if(doc["lat"].is<float>() && doc["lon"].is<float>()) {
             cached_lat = doc["lat"].as<String>(); cached_lon = doc["lon"].as<String>();
             cached_city = doc["city"].as<String>();
         } else { cached_city = "LOCAL"; cached_lat="22.57"; cached_lon="88.36"; }
      } else { cached_city = "LOCAL"; cached_lat="22.57"; cached_lon="88.36"; }
      http.end();
  }
  weatherApp.location = cached_city;

  String url = "http://api.open-meteo.com/v1/forecast?latitude=" + cached_lat + "&longitude=" + cached_lon + "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m";
  http.begin(url);
  int httpCode = http.GET();
  if(httpCode == 200) {
     String payload = http.getString();
     #if ARDUINOJSON_VERSION_MAJOR >= 7
       JsonDocument doc;
     #else
       DynamicJsonDocument doc(1024);
     #endif
     deserializeJson(doc, payload);
     
     weatherApp.tempStr = doc["current"]["temperature_2m"].as<String>();
     
     String desc = "";
     int wcode = doc["current"]["weather_code"].as<int>();
     if (wcode == 0) { weatherApp.iconType = 0; desc = "CLR"; }
     else if (wcode < 40) { weatherApp.iconType = 1; desc = "CLD"; }
     else if (wcode < 70) { weatherApp.iconType = 2; desc = "RAN"; }
     else { weatherApp.iconType = 3; desc = "SNO"; }
     
     weatherApp.condStr = desc + " | W:" + doc["current"]["wind_speed_10m"].as<String>() + " | H:" + doc["current"]["relative_humidity_2m"].as<String>() + "%";
     weatherApp.success = true;
  }
  http.end();
  
  weatherApp.fetching = false;
}

void updateWeather() {
  if (weatherApp.fetching) return;
  if (weatherApp.success) {
     uint32_t msPassed = millis() - weatherApp.lastTick;
     bool timeChanged = false;
     
     if (msPassed >= 1000) {
         weatherApp.lastTick = millis();
         weatherApp.second++;
         if (weatherApp.second >= 60) {
             weatherApp.second = 0;
             weatherApp.minute++;
             if (weatherApp.minute >= 60) {
                 weatherApp.minute = 0;
                 weatherApp.hour++;
                 if (weatherApp.hour >= 24) weatherApp.hour = 0;
             }
         }
         timeChanged = true;
     }

     static uint32_t lastCallMs = 0;
     bool forceDraw = false;
     if (millis() - lastCallMs > 500) forceDraw = true; 
     lastCallMs = millis();

     if (!timeChanged && !forceDraw) return;
     
     int hr = weatherApp.hour;
     String ampm = (hr >= 12) ? "PM" : "AM";
     if (hr == 0) hr = 12;
     else if (hr > 12) hr -= 12;
     char minStr[3], secStr[3];
     sprintf(minStr, "%02d", weatherApp.minute);
     sprintf(secStr, "%02d", weatherApp.second);
     String timeStr = String(hr) + ":" + String(minStr) + ":" + String(secStr);

     gameSprite.fillSprite(TFT_BLACK);
     
     uint16_t ACCENT = TFT_ORANGE; 
     uint16_t DARK_ACCENT = 0x8200; 

     gameSprite.drawRect(2, 2, 156, 124, ACCENT);
     gameSprite.drawFastHLine(30, 42, 100, DARK_ACCENT);
     
     String topStr = weatherApp.location;
     String botStr = weatherApp.dateStr;
     gameSprite.setTextColor(TFT_WHITE, TFT_BLACK);
     gameSprite.drawString(topStr, 80 - (gameSprite.textWidth(topStr, 2)/2), 46, 2);
     gameSprite.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
     gameSprite.drawString(botStr, 80 - (gameSprite.textWidth(botStr, 1)/2), 63, 1);

     String tStr = weatherApp.tempStr;
     gameSprite.setTextColor(TFT_WHITE, TFT_BLACK);
     gameSprite.drawString(tStr, 18, 80, 4);
     int twTemp = gameSprite.textWidth(tStr, 4);
     gameSprite.drawCircle(18 + twTemp + 6, 83, 3, TFT_WHITE);
     gameSprite.drawCircle(18 + twTemp + 6, 83, 1, TFT_WHITE);
     gameSprite.setTextColor(TFT_WHITE, TFT_BLACK); 
     gameSprite.drawString("C", 18 + twTemp + 13, 80, 4);

     drawWeatherIcon(weatherApp.iconType, 105, 74);

     String cond = weatherApp.condStr;
     if (cond.startsWith("STS: ")) cond = cond.substring(5);
     gameSprite.setTextColor(TFT_GREEN, TFT_BLACK);
     gameSprite.drawString(cond, 80 - (gameSprite.textWidth(cond, 1)/2), 114, 1);
     
     int twTime = gameSprite.textWidth(timeStr, 4);
     int twAm = gameSprite.textWidth(ampm, 4); 
     int startX = 80 - ((twTime + 4 + twAm) / 2);
     
     gameSprite.setTextColor(ACCENT, TFT_BLACK);
     gameSprite.drawString(timeStr, startX, 10, 4);
     gameSprite.drawString(ampm, startX + twTime + 4, 10, 4);
     
     gameSprite.pushSprite(0, 0);
  }
}
void handleWeatherButtons() {
  if (!weatherApp.fetching && digitalRead(BTN_EQ_PIN) == LOW) {
     returnToGamesMenu();
     delay(200);
  }
}

void initSnake() { snake.inMenu = true; snake.menuCursor = 0; snake.mode = preferences.getInt("sn_md", 0); snake.speedLevel = preferences.getInt("sn_sp", 4); snake.gameOver = false; snake.pAct = false; }
void startSnakeGame() { snake.inMenu = false; snake.len = 4; snake.dir = 1; snake.nextDir = 1; snake.score = 0; snake.lastScore = -1; snake.gameOver = false; snake.x[0] = 10; snake.y[0] = 8; snake.x[1] = 9; snake.y[1] = 8; snake.x[2] = 8; snake.y[2] = 8; snake.x[3] = 7; snake.y[3] = 8; snake.appleX = random(2, 18); snake.appleY = random(3, 14); snake.pAct = false; snake.pTime = 0; snake.eqPressTime = 0; }
void drawSnakeMenu() { gameSprite.fillRect(0, 0, 160, 128, TFT_BLACK); gameSprite.setTextColor(TFT_GREEN); gameSprite.drawString("SNAKE XENZIA", 20, 10, 2); gameSprite.setTextColor(snake.menuCursor == 0 ? TFT_YELLOW : TFT_WHITE); String ms = snake.mode == 0 ? "WALL" : (snake.mode == 1 ? "FREE" : "APT"); gameSprite.drawString("MODE: < " + ms + " >", 15, 45, 1); gameSprite.setTextColor(snake.menuCursor == 1 ? TFT_YELLOW : TFT_WHITE); gameSprite.drawString("SPEED: < " + String(snake.speedLevel) + " >", 15, 65, 1); gameSprite.setTextColor(snake.menuCursor == 2 ? TFT_YELLOW : TFT_WHITE); gameSprite.drawString("START GAME", 45, 95, 2); gameSprite.pushSprite(0, 0); }
void handleSnakeMenuButtons() { if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } if (digitalRead(BTN_NEXT_PIN) == LOW) { if (snake.menuCursor == 0) { snake.mode = (snake.mode + 1) % 3; preferences.putInt("sn_md", snake.mode); } else if (snake.menuCursor == 1) { snake.speedLevel++; if(snake.speedLevel>7) snake.speedLevel=1; preferences.putInt("sn_sp", snake.speedLevel); } else { snake.menuCursor = 0; } delay(150); } if (digitalRead(BTN_PREV_PIN) == LOW) { if (snake.menuCursor == 0) { snake.mode--; if(snake.mode<0) snake.mode=2; preferences.putInt("sn_md", snake.mode); } else if (snake.menuCursor == 1) { snake.speedLevel--; if(snake.speedLevel<1) snake.speedLevel=7; preferences.putInt("sn_sp", snake.speedLevel); } else { snake.menuCursor = 1; } delay(150); } if (digitalRead(BTN_PLAY_PIN) == LOW) { if (snake.menuCursor == 2) { startSnakeGame(); } else { snake.menuCursor++; } delay(200); } }
void handleSnakeButtons() { if (snake.inMenu) { handleSnakeMenuButtons(); return; } if (snake.gameOver) { if (digitalRead(BTN_PLAY_PIN) == LOW) { initSnake(); delay(200); } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } return; } if (digitalRead(BTN_EQ_PIN) == LOW) { if (snake.eqPressTime == 0) { snake.eqPressTime = millis(); } else if (millis() - snake.eqPressTime > 1200) { returnToGamesMenu(); delay(200); return; } } else { if (snake.eqPressTime > 0 && millis() - snake.eqPressTime < 400) { if (snake.dir != 0) snake.nextDir = 2; } snake.eqPressTime = 0; } if (digitalRead(BTN_PREV_PIN) == LOW) { if (snake.dir != 1) snake.nextDir = 3; delay(30); } if (digitalRead(BTN_NEXT_PIN) == LOW) { if (snake.dir != 3) snake.nextDir = 1; delay(30); } if (digitalRead(BTN_PLAY_PIN) == LOW) { if (snake.dir != 2) snake.nextDir = 0; delay(30); } }
void updateSnake() { if (snake.inMenu) { drawSnakeMenu(); return; } if (snake.gameOver) { gameSprite.fillRect(30, 40, 100, 48, TFT_BLACK); gameSprite.setTextColor(TFT_RED); gameSprite.drawString("GAME OVER", 35, 50, 2); gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("Play:Re EQ:Ext", 35, 75, 1); gameSprite.pushSprite(0, 0); return; } int speedDelay = 200 - (snake.speedLevel * 23); if (millis() - snake.lastMove < speedDelay) return; snake.lastMove = millis(); snake.dir = snake.nextDir; for (int i = snake.len; i > 0; i--) { snake.x[i] = snake.x[i - 1]; snake.y[i] = snake.y[i - 1]; } if (snake.dir == 0) snake.y[0]--; else if (snake.dir == 1) snake.x[0]++; else if (snake.dir == 2) snake.y[0]++; else if (snake.dir == 3) snake.x[0]--; if (snake.mode == 0 || snake.mode == 2) { if (snake.x[0] < 0 || snake.x[0] >= 20 || snake.y[0] < 2 || snake.y[0] >= 16) snake.gameOver = true; } else if (snake.mode == 1) { if (snake.x[0] < 0) snake.x[0] = 19; if (snake.x[0] >= 20) snake.x[0] = 0; if (snake.y[0] < 2) snake.y[0] = 15; if (snake.y[0] >= 16) snake.y[0] = 2; } if (snake.mode == 2) { if ((snake.y[0] == 8 && snake.x[0] >= 5 && snake.x[0] <= 14) || (snake.x[0] == 10 && snake.y[0] >= 4 && snake.y[0] <= 12)) snake.gameOver = true; } for (int i = 1; i < snake.len; i++) { if (snake.x[0] == snake.x[i] && snake.y[0] == snake.y[i]) snake.gameOver = true; } if (snake.x[0] == snake.appleX && snake.y[0] == snake.appleY) { snake.len++; snake.score += 10; snake.appleX = random(1, 19); snake.appleY = random(3, 15); while(snake.mode == 2 && ((snake.appleY == 8 && snake.appleX >= 5 && snake.appleX <= 14) || (snake.appleX == 10 && snake.appleY >= 4 && snake.appleY <= 12))) { snake.appleX = random(1, 19); snake.appleY = random(3, 15); } if(snake.score > snakeHigh) { snakeHigh = snake.score; preferences.putInt("snake_hi", snakeHigh); } if (!snake.pAct && random(0, 5) == 0) { snake.pAct = true; snake.pTime = millis(); snake.px = random(2, 18); snake.py = random(4, 14); } } if (snake.pAct && snake.x[0] == snake.px && snake.y[0] == snake.py) { snake.score += 50; snake.pAct = false; } if (snake.pAct && millis() - snake.pTime > 5000) { snake.pAct = false; } gameSprite.fillRect(0, 0, 160, 128, TFT_BLACK); gameSprite.fillRect(0, 0, 160, 16, 0x2104); gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("S:" + String(snake.score) + " H:" + String(snakeHigh), 2, 4, 1); if (snake.pAct) { int left = 5 - ((millis() - snake.pTime) / 1000); gameSprite.setTextColor(TFT_MAGENTA); gameSprite.drawString("BONUS:" + String(left) + "s", 85, 4, 1); } if (snake.mode == 0 || snake.mode == 2) { gameSprite.drawRect(0, 16, 160, 112, 0x18E3); } if (snake.mode == 2) { gameSprite.fillRect(5*8, 8*8, 10*8, 8, 0x18E3); gameSprite.fillRect(10*8, 4*8, 8, 9*8, 0x18E3); } gameSprite.fillCircle(snake.appleX * 8 + 4, snake.appleY * 8 + 4, 4, TFT_RED); if (snake.pAct) { gameSprite.fillRoundRect(snake.px * 8, snake.py * 8, 8, 8, 2, TFT_MAGENTA); if ((millis()/200)%2==0) gameSprite.drawRoundRect(snake.px*8-1, snake.py*8-1, 10, 10, 2, TFT_WHITE); } for (int i = 0; i < snake.len; i++) { uint16_t c = (i==0) ? TFT_GREEN : 0x07E0; if (i % 2 != 0) c = 0x05E0; int rx = snake.x[i] * 8; int ry = snake.y[i] * 8; if (i == 0) { gameSprite.fillRoundRect(rx, ry, 8, 8, 2, c); if(snake.dir == 0) { gameSprite.drawPixel(rx+2,ry+2,TFT_BLACK); gameSprite.drawPixel(rx+5,ry+2,TFT_BLACK); } else if(snake.dir == 1) { gameSprite.drawPixel(rx+5,ry+2,TFT_BLACK); gameSprite.drawPixel(rx+5,ry+5,TFT_BLACK); } else if(snake.dir == 2) { gameSprite.drawPixel(rx+2,ry+5,TFT_BLACK); gameSprite.drawPixel(rx+5,ry+5,TFT_BLACK); } else if(snake.dir == 3) { gameSprite.drawPixel(rx+2,ry+2,TFT_BLACK); gameSprite.drawPixel(rx+2,ry+5,TFT_BLACK); } } else { gameSprite.fillRect(rx+1, ry+1, 6, 6, c); } } gameSprite.pushSprite(0, 0); }
int drawCard() { int c = random(1, 14); return (c > 10) ? 10 : c; } String getCardName(int val) { if (val == 1) return "A"; if(val==11) return "J"; if(val==12) return "Q"; if(val==13) return "K"; return String(val); }
int calcHand(int cards[], int count) { int total = 0, aces = 0; for (int i=0; i<count; i++) { if(cards[i] == 1) { aces++; total+=11; } else total+=cards[i]; } while(total > 21 && aces > 0) { total -= 10; aces--; } return total; }
void initBlackjack() { tft.fillScreen(0x0270); bj.pCount = 0; bj.dCount = 0; bj.state = 1; bj.resultMsg = ""; bj.needsRedraw = true; bj.pCards[bj.pCount++] = drawCard(); bj.pCards[bj.pCount++] = drawCard(); bj.dCards[bj.dCount++] = drawCard(); bj.dCards[bj.dCount++] = drawCard(); if (calcHand(bj.pCards, bj.pCount) == 21) { bj.state = 3; bj.resultMsg = "BLACKJACK! You Win!"; bjWins++; preferences.putInt("bj_hi", bjWins); } }
void handleBlackjackButtons() { if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } if (bj.state == 1) { if (digitalRead(BTN_PREV_PIN) == LOW) { bj.pCards[bj.pCount++] = drawCard(); if(calcHand(bj.pCards, bj.pCount) > 21) { bj.state = 3; bj.resultMsg = "BUSTED! Dealer Wins"; } bj.needsRedraw = true; delay(300); } if (digitalRead(BTN_NEXT_PIN) == LOW) { bj.state = 2; bj.needsRedraw = true; delay(300); } } else if (bj.state == 3) { if (digitalRead(BTN_PLAY_PIN) == LOW) { initBlackjack(); delay(300); } } }
void updateBlackjack() { if (bj.state == 2) { while(calcHand(bj.dCards, bj.dCount) < 17) { bj.dCards[bj.dCount++] = drawCard(); bj.needsRedraw = true; } int pTotal = calcHand(bj.pCards, bj.pCount); int dTotal = calcHand(bj.dCards, bj.dCount); if(dTotal > 21 || pTotal >= dTotal) { bj.resultMsg = "You Win!"; bjWins++; preferences.putInt("bj_hi", bjWins); } else { bj.resultMsg = "Dealer Wins."; } bj.state = 3; bj.needsRedraw = true; } if (!bj.needsRedraw) return; bj.needsRedraw = false; tft.fillScreen(0x0270); tft.setTextColor(TFT_WHITE); tft.drawString("Streak: " + String(bjWins), 5, 5, 1); tft.drawString("Dealer:", 5, 20, 2); for(int i=0; i<bj.dCount; i++) { tft.fillRect(10 + (i*25), 40, 20, 30, TFT_WHITE); tft.setTextColor(TFT_BLACK); if(bj.state == 1 && i == 1) { tft.fillRect(10+(i*25)+2, 42, 16, 26, TFT_RED); } else { tft.drawString(getCardName(bj.dCards[i]), 13 + (i*25), 45, 2); } } tft.setTextColor(TFT_WHITE); tft.drawString("Player (" + String(calcHand(bj.pCards, bj.pCount)) + "):", 5, 75, 2); for(int i=0; i<bj.pCount; i++) { tft.fillRect(10 + (i*25), 95, 20, 30, TFT_WHITE); tft.setTextColor(TFT_BLACK); tft.drawString(getCardName(bj.pCards[i]), 13 + (i*25), 100, 2); } if (bj.state == 1) { tft.setTextColor(TFT_YELLOW); tft.drawString("< HIT", 120, 90, 1); tft.drawString("STAND >", 110, 110, 1); } else if (bj.state == 3) { tft.fillRect(10, 50, 140, 30, TFT_BLACK); tft.setTextColor(TFT_YELLOW); tft.drawString(bj.resultMsg, 20, 58, 2); tft.setTextColor(TFT_WHITE); tft.drawString("Play=Replay EQ=Quit", 10, 118, 1); } }
void initPong() { tft.fillScreen(TFT_BLACK); tft.drawLine(80, 0, 80, 128, TFT_DARKGREY); pong.ballX = 80; pong.ballY = 60; pong.ballDX = 2.5; pong.ballDY = 2.0; pong.pY = 50; pong.aiY = 50; pong.score = 0; pong.lastScore = -1; pong.gameOver = false; pong.lastFrame = 0; }
void handlePongButtons() { if (pong.gameOver) { if (digitalRead(BTN_PLAY_PIN) == LOW) { initPong(); delay(200); } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } return; } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } }
void updatePong() { if (pong.gameOver || millis() - pong.lastFrame < 25) return; pong.lastFrame = millis(); tft.fillRect(5, pong.pY, 4, 28, TFT_BLACK); tft.fillRect(151, pong.aiY, 4, 28, TFT_BLACK); tft.fillRect((int)pong.ballX, (int)pong.ballY, 4, 4, TFT_BLACK); if (digitalRead(BTN_PREV_PIN) == LOW) pong.pY -= 4; if (digitalRead(BTN_NEXT_PIN) == LOW) pong.pY += 4; if (pong.pY < 0) pong.pY = 0; if (pong.pY > 100) pong.pY = 100; pong.ballX += pong.ballDX; pong.ballY += pong.ballDY; if (pong.ballY <= 0 || pong.ballY >= 124) pong.ballDY = -pong.ballDY; if (pong.ballX <= 9 && pong.ballX >= 5 && pong.ballY + 4 >= pong.pY && pong.ballY <= pong.pY + 28) { pong.ballDX = -pong.ballDX + 0.2; pong.ballX = 10; pong.score++; if(pong.score > pongHigh) { pongHigh = pong.score; preferences.putInt("pong_hi", pongHigh); } } if (pong.aiY + 14 < pong.ballY) pong.aiY += 2; if (pong.aiY + 14 > pong.ballY) pong.aiY -= 2; if (pong.aiY < 0) pong.aiY = 0; if (pong.aiY > 100) pong.aiY = 100; if (pong.ballX >= 147 && pong.ballX <= 151 && pong.ballY + 4 >= pong.aiY && pong.ballY <= pong.aiY + 28) { pong.ballDX = -pong.ballDX - 0.2; pong.ballX = 146; } if (pong.ballX < 0) pong.gameOver = true; if (pong.ballX > 160) { pong.ballDX = -pong.ballDX; } tft.drawLine(80, 0, 80, 128, TFT_DARKGREY); if (pong.score != pong.lastScore) { tft.fillRect(20, 2, 40, 12, TFT_BLACK); tft.setTextColor(TFT_WHITE); tft.drawString("S:" + String(pong.score), 20, 2, 1); tft.fillRect(100, 2, 40, 12, TFT_BLACK); tft.drawString("H:" + String(pongHigh), 100, 2, 1); pong.lastScore = pong.score; } tft.fillRect(5, pong.pY, 4, 28, TFT_CYAN); tft.fillRect(151, pong.aiY, 4, 28, TFT_MAGENTA); tft.fillRect((int)pong.ballX, (int)pong.ballY, 4, 4, TFT_WHITE); if (pong.gameOver) { tft.setTextColor(TFT_RED); tft.drawString("GAME OVER", 40, 50, 2); tft.setTextColor(TFT_WHITE); tft.drawString("Play:Retry EQ:Exit", 15, 75, 1); } }

void handleFlappyButtons() {
  if (flappy.gameOver) {
    if (digitalRead(BTN_PLAY_PIN) == LOW) { initFlappy(); delay(200); }
    if (digitalRead(BTN_EQ_PIN) == LOW || digitalRead(BTN_PREV_PIN) == LOW) { returnToGamesMenu(); delay(200); }
    return;
  }
  
  if (digitalRead(BTN_EQ_PIN) == LOW || digitalRead(BTN_PREV_PIN) == LOW) { returnToGamesMenu(); delay(200); return; }
  
  if (digitalRead(BTN_NEXT_PIN) == LOW) { flappy.isPaused = !flappy.isPaused; delay(200); return; }
  if (flappy.isPaused) return;

  // Make the bird flap!
  if (digitalRead(BTN_PLAY_PIN) == LOW) { 
    flappy.dy = -2.0; 
  }
}

void updateFlappy() { 
  if (flappy.gameOver || millis() - flappy.lastFrame < 25) return; 
  if (flappy.isPaused) {
      gameSprite.fillRect(141, 6, 4, 12, 0x4208);
      gameSprite.fillRect(149, 6, 4, 12, 0x4208);
      gameSprite.fillRect(140, 5, 4, 12, TFT_YELLOW);
      gameSprite.fillRect(148, 5, 4, 12, TFT_YELLOW);
      gameSprite.pushSprite(0, 0);
      return;
  }
  flappy.lastFrame = millis(); flappy.frameCount++;
  flappy.y += flappy.dy; flappy.dy += 0.12; flappy.pipeX -= 3; 
  
  if (flappy.pipeX < -24) { 
    flappy.pipeX = 160; 
    flappy.pipeGapY = random(15, 50); 
    flappy.score++; 
    if (flappy.score > flappyHigh) { flappyHigh = flappy.score; preferences.putInt("flap_hi", flappyHigh); } 
  } 
  
  if (flappy.y > 115 || flappy.y < 0) flappy.gameOver = true; 
  if (flappy.pipeX < 45 && flappy.pipeX + 20 > 30) { 
    if (flappy.y < flappy.pipeGapY || flappy.y + 10 > flappy.pipeGapY + 50) { flappy.gameOver = true; } 
  } 
  
  gameSprite.fillSprite(0x3CF3); 
  
  // --- 1. REALISTIC FLUFFY CLOUDS WITH SHADOWS (3 Types) ---
  for(int i=0; i<5; i++) { 
      int cx = 160 - ((flappy.frameCount / 3 + i * 55) % 240); 
      uint16_t cShadow = 0x9D55; // Soft grayish-blue for shadow
      
      // Draw shadow layer first (offset by +2 Y), then the bright white layer
      for (int layer = 0; layer < 2; layer++) {
          uint16_t color = (layer == 0) ? cShadow : TFT_WHITE;
          int yOff = (layer == 0) ? 2 : 0;
          
          if (i % 3 == 0) { // Type 1: Large fluffy
              gameSprite.fillCircle(cx + 15, 15 + yOff, 8, color);
              gameSprite.fillCircle(cx + 25, 17 + yOff, 6, color);
              gameSprite.fillCircle(cx + 5, 18 + yOff, 5, color);
              gameSprite.fillRect(cx + 5, 15 + yOff, 22, 9, color);
          } else if (i % 3 == 1) { // Type 2: Wide & flat
              gameSprite.fillCircle(cx + 12, 25 + yOff, 7, color);
              gameSprite.fillCircle(cx + 24, 24 + yOff, 8, color);
              gameSprite.fillCircle(cx + 34, 27 + yOff, 5, color);
              gameSprite.fillRect(cx + 10, 24 + yOff, 26, 9, color);
          } else { // Type 3: Smooth multi-puff cloud
              gameSprite.fillCircle(cx + 10, 14 + yOff, 5, color);
              gameSprite.fillCircle(cx + 16, 12 + yOff, 6, color);
              gameSprite.fillCircle(cx + 22, 14 + yOff, 5, color);
              gameSprite.fillRoundRect(cx + 8, 14 + yOff, 17, 6, 3, color);
          }
      }
  }
  
  // --- 2. CITY SKYLINE WITH SPARSE, SIGNIFICANT WINDOWS ---
  for(int i=0; i<6; i++) { 
      int bx = 160 - ((flappy.frameCount / 2 + i * 35) % 200); 
      int bHeight = 35 + (i % 3) * 15; 
      uint16_t bColor = (i % 2 == 0) ? 0x4208 : 0x2104; 
      
      gameSprite.fillRect(bx, 120 - bHeight, 26, bHeight, bColor); 
      
      // Draw spaced-out grid of windows (fewer, cleaner look)
      int row = 0;
      for (int wy = 120 - bHeight + 6; wy < 115; wy += 11) {
          int col = 0;
          // Step by 12 to push the second column right, matching the 5px left/right margins
          for (int wx = bx + 5; wx < bx + 22; wx += 12) {
              if ((col + row * 2 + i) % 3 != 0) {
                  gameSprite.fillRect(wx, wy, 4, 4, TFT_YELLOW); 
              } else {
                  gameSprite.fillRect(wx, wy, 4, 4, 0x10A2); 
              }
              col++;
          }
          row++;
      }
  }

  // --- 3. AUTHENTIC TEXTURED PIPES ---
  uint16_t pBase = 0x5504, pHi = 0x9FD3, pSh = 0x2280; 
  
  // Top Pipe Body
  gameSprite.fillRect(flappy.pipeX, 0, 20, flappy.pipeGapY - 8, pBase);
  gameSprite.fillRect(flappy.pipeX + 2, 0, 4, flappy.pipeGapY - 8, pHi);
  gameSprite.fillRect(flappy.pipeX + 16, 0, 4, flappy.pipeGapY - 8, pSh);
  gameSprite.drawRect(flappy.pipeX, -1, 20, flappy.pipeGapY - 7, TFT_BLACK);
  
  // Top Pipe Cap
  gameSprite.fillRect(flappy.pipeX - 2, flappy.pipeGapY - 8, 24, 8, pBase);
  gameSprite.fillRect(flappy.pipeX, flappy.pipeGapY - 8, 4, 8, pHi);
  gameSprite.fillRect(flappy.pipeX + 18, flappy.pipeGapY - 8, 4, 8, pSh);
  gameSprite.drawRect(flappy.pipeX - 2, flappy.pipeGapY - 8, 24, 8, TFT_BLACK);

  // Bottom Pipe Cap
  gameSprite.fillRect(flappy.pipeX - 2, flappy.pipeGapY + 50, 24, 8, pBase);
  gameSprite.fillRect(flappy.pipeX, flappy.pipeGapY + 50, 4, 8, pHi);
  gameSprite.fillRect(flappy.pipeX + 18, flappy.pipeGapY + 50, 4, 8, pSh);
  gameSprite.drawRect(flappy.pipeX - 2, flappy.pipeGapY + 50, 24, 8, TFT_BLACK);
  
  // Bottom Pipe Body
  int botH = 128 - (flappy.pipeGapY + 58); 
  if (botH > 0) {
    gameSprite.fillRect(flappy.pipeX, flappy.pipeGapY + 58, 20, botH, pBase);
    gameSprite.fillRect(flappy.pipeX + 2, flappy.pipeGapY + 58, 4, botH, pHi);
    gameSprite.fillRect(flappy.pipeX + 16, flappy.pipeGapY + 58, 4, botH, pSh);
    gameSprite.drawRect(flappy.pipeX, flappy.pipeGapY + 58, 20, botH + 1, TFT_BLACK);
  }

  // --- 4. FOREGROUND & LAYERED BUSHES ---
  gameSprite.fillRect(0, 120, 160, 8, 0x05E0); 
  gameSprite.drawLine(0, 120, 160, 120, TFT_BLACK); 
  
  for(int i=0; i<6; i++) { 
      int bushX = 160 - ((flappy.frameCount * 2 + i * 45) % 250); 
      if (i % 2 == 0) {
          gameSprite.fillCircle(bushX + 12, 118, 11, 0x0280); 
          gameSprite.fillCircle(bushX + 26, 121, 8, 0x0280);
          gameSprite.fillCircle(bushX, 122, 7, 0x0280);
          gameSprite.fillCircle(bushX + 10, 115, 7, 0x04A0); 
          gameSprite.fillCircle(bushX + 24, 119, 5, 0x04A0);
          gameSprite.fillCircle(bushX + 2, 119, 4, 0x04A0);
      } else {
          gameSprite.fillCircle(bushX + 10, 122, 8, 0x0280); 
          gameSprite.fillCircle(bushX + 20, 124, 5, 0x0280);
          gameSprite.fillCircle(bushX + 8, 120, 5, 0x04A0); 
          gameSprite.fillCircle(bushX + 18, 122, 3, 0x04A0);
      }
  }
  
  // Bird
  int by = flappy.y; 
  const char* tex = (flappy.dy < 0) ? texFlappy2 : texFlappy1;
  drawIndexedSprite(tex, 30, by, 16, 12, flappyPal, &gameSprite);
  
  // Score UI
  gameSprite.setTextColor(TFT_BLACK); gameSprite.drawString("Sc:" + String(flappy.score) + " Hi:" + String(flappyHigh), 5, 5, 1); 
  if (flappy.gameOver) { 
      gameSprite.setTextColor(TFT_RED); gameSprite.drawString("GAME OVER", 40, 50, 2); 
      gameSprite.setTextColor(TFT_BLACK); gameSprite.drawString("Play:Retry EQ:Exit", 20, 75, 1); 
  } 
  gameSprite.pushSprite(0, 0); 
}
bool tetrisCheck(int nx, int ny, int nt, int nr) { uint16_t shape = TETROMINO[nt][nr]; for(int i=0; i<16; i++) { if(shape & (1 << (15-i))) { int bx = nx + (i % 4); int by = ny + (i / 4); if(bx < 0 || bx >= 10 || by >= 20) return false; if(by >= 0 && tetris.board[by][bx]) return false; } } return true; }
void tetrisClearLines() { int lines = 0; for(int i=19; i>=0; i--) { bool full = true; for(int j=0; j<10; j++) if(!tetris.board[i][j]) full = false; if(full) { lines++; for(int k=i; k>0; k--) { for(int j=0; j<10; j++) tetris.board[k][j] = tetris.board[k-1][j]; } for(int j=0; j<10; j++) tetris.board[0][j] = 0; i++; } } if(lines == 1) tetris.score += 40; else if(lines == 2) tetris.score += 100; else if(lines == 3) tetris.score += 300; else if(lines == 4) tetris.score += 1200; if (tetris.score > tetrisHigh) { tetrisHigh = tetris.score; preferences.putInt("tet_hi", tetrisHigh); } }
void initTetris() { for(int i=0; i<20; i++) for(int j=0; j<10; j++) tetris.board[i][j] = 0; tetris.type = random(0, 7); tetris.rot = 0; tetris.x = 3; tetris.y = -2; tetris.score = 0; tetris.gameOver = false; tetris.dropSpeed = 500; tetris.lastDrop = millis(); tetris.eqPressTime = 0; }
void handleTetrisButtons() { if (tetris.gameOver) { if (digitalRead(BTN_PLAY_PIN) == LOW) { initTetris(); delay(200); } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } return; } if (digitalRead(BTN_EQ_PIN) == LOW) { if (tetris.eqPressTime == 0) tetris.eqPressTime = millis(); else if (millis() - tetris.eqPressTime > 2000) { returnToGamesMenu(); delay(200); return; } } else { if (tetris.eqPressTime > 0 && millis() - tetris.eqPressTime < 400) { if(tetrisCheck(tetris.x, tetris.y + 1, tetris.type, tetris.rot)) { tetris.y++; tetris.lastDrop = millis(); } } tetris.eqPressTime = 0; } if (digitalRead(BTN_PREV_PIN) == LOW) { if(tetrisCheck(tetris.x - 1, tetris.y, tetris.type, tetris.rot)) tetris.x--; delay(120); } if (digitalRead(BTN_NEXT_PIN) == LOW) { if(tetrisCheck(tetris.x + 1, tetris.y, tetris.type, tetris.rot)) tetris.x++; delay(120); } if (digitalRead(BTN_PLAY_PIN) == LOW) { int nextRot = (tetris.rot + 1) % 4; if(tetrisCheck(tetris.x, tetris.y, tetris.type, nextRot)) tetris.rot = nextRot; delay(180); } }
void updateTetris() { if (tetris.gameOver) return; if (millis() - tetris.lastDrop > (uint32_t)tetris.dropSpeed) { tetris.lastDrop = millis(); if(tetrisCheck(tetris.x, tetris.y + 1, tetris.type, tetris.rot)) { tetris.y++; } else { uint16_t shape = TETROMINO[tetris.type][tetris.rot]; for(int i=0; i<16; i++) { if(shape & (1 << (15-i))) { int bx = tetris.x + (i % 4); int by = tetris.y + (i / 4); if(by >= 0 && by < 20 && bx >= 0 && bx < 10) tetris.board[by][bx] = tetris.type + 1; } } tetrisClearLines(); tetris.type = random(0, 7); tetris.rot = 0; tetris.x = 3; tetris.y = -2; tetris.dropSpeed = max(100, 500 - (tetris.score / 2)); if(!tetrisCheck(tetris.x, tetris.y, tetris.type, tetris.rot)) { tetris.gameOver = true; } } } gameSprite.fillSprite(TFT_BLACK); gameSprite.drawRect(28, 4, 62, 122, TFT_DARKGREY); for(int i=0; i<20; i++) { for(int j=0; j<10; j++) { if(tetris.board[i][j]) { gameSprite.fillRect(30 + j*6, 5 + i*6, 5, 5, TETRIS_COLORS[tetris.board[i][j]]); } } } uint16_t shape = TETROMINO[tetris.type][tetris.rot]; for(int i=0; i<16; i++) { if(shape & (1 << (15-i))) { int bx = tetris.x + (i % 4); int by = tetris.y + (i / 4); if (by >= 0) { gameSprite.fillRect(30 + bx*6, 5 + by*6, 5, 5, TETRIS_COLORS[tetris.type + 1]); } } } gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("TETRIS", 100, 10, 1); gameSprite.setTextColor(TFT_CYAN); gameSprite.drawString("Sc:" + String(tetris.score), 100, 30, 1); gameSprite.setTextColor(TFT_MAGENTA); gameSprite.drawString("Hi:" + String(tetrisHigh), 100, 50, 1); if (tetris.gameOver) { gameSprite.setTextColor(TFT_RED); gameSprite.drawString("GAME OVER", 40, 50, 2); gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("Play:Retry EQ:Exit", 20, 75, 1); } gameSprite.pushSprite(0, 0); }

// --------------------------------------------------------
// GALAGA, DINO, SIMON, TIC-TAC-TOE
// --------------------------------------------------------
void initGalaga() { galaga.pX = 75; galaga.score = 0; galaga.level = 1; galaga.lastScore = -1; galaga.gameOver = false; for(int i=0; i<5; i++) galaga.bAct[i] = false; for(int i=0; i<10; i++) { galaga.eAct[i] = true; galaga.ex[i] = 20 + (i%5)*24; galaga.ey[i] = 16 + (i/5)*16; galaga.eType[i] = 0; } galaga.eDir = 1; galaga.eSpeed = 0.5; galaga.frameCount = 0; galaga.lastFrame = 0; }
void handleGalagaButtons() { if (galaga.gameOver) { if (digitalRead(BTN_PLAY_PIN) == LOW) { initGalaga(); delay(200); } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } return; } if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } if (digitalRead(BTN_PLAY_PIN) == LOW) { for(int i=0; i<5; i++) { if(!galaga.bAct[i]) { galaga.bAct[i] = true; galaga.bx[i] = galaga.pX + 4; galaga.by[i] = 108; break; } } delay(100); } }
void updateGalaga() { 
  if (galaga.gameOver || millis() - galaga.lastFrame < 25) return; 
  galaga.lastFrame = millis(); galaga.frameCount++; 
  gameSprite.fillSprite(TFT_BLACK); 
  for(int i=0; i<15; i++) { int sx = (i*37) % 160; int sy = (galaga.frameCount + i*13) % 128; gameSprite.drawPixel(sx, sy, (i%3==0) ? TFT_DARKGREY : TFT_WHITE); } 
  if (digitalRead(BTN_PREV_PIN) == LOW) galaga.pX -= 3.5; 
  if (digitalRead(BTN_NEXT_PIN) == LOW) galaga.pX += 3.5; 
  if(galaga.pX < 0) galaga.pX = 0; if(galaga.pX > 150) galaga.pX = 150; 
  for(int i=0; i<5; i++) { 
    if(galaga.bAct[i]) { 
      galaga.by[i] -= 5; 
      if(galaga.by[i] < 0) galaga.bAct[i] = false; 
      else { 
        gameSprite.fillRect(galaga.bx[i], galaga.by[i], 2, 6, TFT_YELLOW); 
        for(int j=0; j<10; j++) { 
          if(galaga.eAct[j] && galaga.bx[i] >= galaga.ex[j]-2 && galaga.bx[i] <= galaga.ex[j]+12 && galaga.by[i] <= galaga.ey[j]+10 && galaga.by[i] >= galaga.ey[j]) { 
            galaga.eAct[j] = false; galaga.bAct[i] = false; galaga.score += (galaga.eType[j]+1)*10; 
            if (galaga.score > galagaHigh) { galagaHigh = galaga.score; preferences.putInt("galaga_hi", galagaHigh); } 
            break; 
          } 
        } 
      } 
    } 
  } 
  bool hitEdge = false; int activeE = 0; 
  for(int i=0; i<10; i++) { 
    if(galaga.eAct[i]) { 
      activeE++; 
      galaga.ex[i] += galaga.eDir * galaga.eSpeed; 
      if(galaga.ex[i] < 2 || galaga.ex[i] > 148) hitEdge = true; 
      if(galaga.level > 1 && galaga.frameCount % 80 == 0 && random(0, 100) < (galaga.level * 15)) galaga.ey[i] += 1.5; 
      const char* t; uint16_t col;
      if (galaga.eType[i] == 0) {
          t = ((galaga.frameCount/10)%2 == 0) ? texGalagaEnemy1 : texGalagaEnemy2; col = TFT_RED;
      } else if (galaga.eType[i] == 1) {
          t = ((galaga.frameCount/10)%2 == 0) ? texGalagaEnemy3 : texGalagaEnemy4; col = TFT_GREEN;
      } else {
          t = ((galaga.frameCount/10)%2 == 0) ? texGalagaEnemy5 : texGalagaEnemy6; col = TFT_MAGENTA;
      }
      drawSpriteMask(t, galaga.ex[i], galaga.ey[i], 10, 10, col, &gameSprite); 
      if (galaga.ey[i]+10 > 110 && abs(galaga.ex[i] - galaga.pX) < 8) galaga.gameOver = true; 
    } 
  } 
  if(hitEdge) { 
    galaga.eDir *= -1; 
    for(int i=0; i<10; i++) if(galaga.eAct[i]) { galaga.ey[i] += 8; if(galaga.ey[i] > 105) galaga.gameOver = true; } 
  } 
  if (activeE == 0) { 
    galaga.level++; galaga.eSpeed += 0.2; 
    for(int i=0; i<10; i++) { 
        galaga.eAct[i] = true; galaga.ex[i] = 20 + (i%5)*24; galaga.ey[i] = 16 + (i/5)*16; 
        if (galaga.level < 5) galaga.eType[i] = 0;
        else if (galaga.level < 10) galaga.eType[i] = random(0, 2);
        else galaga.eType[i] = random(0, 3);
    } 
  } 
  drawSpriteMask(texGalagaShip, galaga.pX, 110, 10, 10, TFT_CYAN, &gameSprite); 
  gameSprite.fillRect(0, 0, 160, 14, 0x10A2); gameSprite.drawLine(0, 14, 160, 14, TFT_WHITE); 
  gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("LV " + String(galaga.level), 5, 3, 1); 
  gameSprite.setTextColor(TFT_YELLOW); gameSprite.drawString(String(galaga.score), 40, 3, 1); 
  gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString(" HI ", 90, 3, 1); 
  gameSprite.setTextColor(TFT_CYAN); gameSprite.drawString(String(galagaHigh), 115, 3, 1); 
  if (galaga.gameOver) { 
    gameSprite.fillRect(20, 45, 120, 38, TFT_BLACK); gameSprite.drawRect(20, 45, 120, 38, TFT_RED); 
    gameSprite.setTextColor(TFT_RED); gameSprite.drawString("GAME OVER", 40, 50, 2); 
    gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("Play:Re EQ:Ext", 35, 70, 1); 
  } 
  gameSprite.pushSprite(0, 0); 
}

void startDinoGame() { 
  dino.inMenu = false; 
  dino.y = 84; dino.dy = 0; dino.score = 0; dino.lastScore = -1; 
  dino.gameOver = false; dino.speed = 2.0; dino.lastFrame = 0; 
  dino.frameCount = 0; for(int i=0; i<3; i++) dino.cAct[i] = false; 
}

void initDino() { 
  dino.inMenu = true; 
  dino.menuCursor = 0; 
  dino.menuNeedsRedraw = true; 
}

void handleDinoButtons() { 
  if (dino.inMenu) { 
    if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } 
    if (digitalRead(BTN_PREV_PIN) == LOW) { dino.menuCursor--; if(dino.menuCursor < 0) dino.menuCursor = 2; dino.menuNeedsRedraw = true; delay(150); } 
    if (digitalRead(BTN_NEXT_PIN) == LOW) { dino.menuCursor++; if(dino.menuCursor > 2) dino.menuCursor = 0; dino.menuNeedsRedraw = true; delay(150); } 
    if (digitalRead(BTN_PLAY_PIN) == LOW) { dino.mode = dino.menuCursor; startDinoGame(); delay(200); } 
    return; 
  } 
  if (dino.gameOver) { 
    if (digitalRead(BTN_PLAY_PIN) == LOW) { startDinoGame(); delay(200); } 
    if (digitalRead(BTN_PREV_PIN) == LOW) { returnToGamesMenu(); delay(200); } 
    return; 
  } 
  if (digitalRead(BTN_PREV_PIN) == LOW) { returnToGamesMenu(); delay(200); return; } 
  if (digitalRead(BTN_NEXT_PIN) == LOW) { dino.isPaused = !dino.isPaused; delay(200); return; }
  if (dino.isPaused) return;
  if (digitalRead(BTN_PLAY_PIN) == LOW && dino.y >= 84) { dino.dy = -7.5; } 
  if (digitalRead(BTN_EQ_PIN) == LOW) { 
    if (dino.y < 84) dino.dy += 1.5; 
    dino.isDucking = true; 
  } else { 
    dino.isDucking = false; 
  } 
}

void updateDino() { 
  if (dino.inMenu) { 
    if (dino.menuNeedsRedraw) { 
      dino.menuNeedsRedraw = false; 
      gameSprite.fillSprite(TFT_BLACK); 
      gameSprite.setTextColor(TFT_CYAN); gameSprite.drawString("DINO RUN", 45, 15, 2); 
      gameSprite.setTextColor(dino.menuCursor == 0 ? TFT_YELLOW : TFT_WHITE); gameSprite.drawString("Day/Night Cycle", 25, 45, 2); 
      gameSprite.setTextColor(dino.menuCursor == 1 ? TFT_YELLOW : TFT_WHITE); gameSprite.drawString("Always Day", 25, 65, 2); 
      gameSprite.setTextColor(dino.menuCursor == 2 ? TFT_YELLOW : TFT_WHITE); gameSprite.drawString("Always Night", 25, 85, 2); 
      gameSprite.setTextColor(TFT_DARKGREY); gameSprite.drawString("Play: Sel   EQ: Exit", 25, 110, 1); 
      gameSprite.pushSprite(0, 0); 
    } 
    return; 
  } 

  if (dino.gameOver || millis() - dino.lastFrame < 16) return; 
  
  bool isNight = false;
  if (dino.mode == 0) isNight = ((dino.score / 400) % 2 == 1);
  else if (dino.mode == 2) isNight = true;
  
  uint16_t bgColor = isNight ? TFT_BLACK : TFT_WHITE; 
  uint16_t fgColor = isNight ? TFT_WHITE : TFT_BLACK; 
  
  // Smooth Day/Night Transition (Only happens in Cycle Mode)
  if (dino.mode == 0) {
    int prog = dino.score % 400;
    if (dino.score >= 400 && prog < 30) {
        int fade = (prog * 255) / 30; 
        int bgVal = isNight ? (255 - fade) : fade;
        int fgVal = isNight ? fade : (255 - fade);
        bgColor = ((bgVal >> 3) << 11) | ((bgVal >> 2) << 5) | (bgVal >> 3);
        fgColor = ((fgVal >> 3) << 11) | ((fgVal >> 2) << 5) | (fgVal >> 3);
    }
  }

  if (dino.isPaused) {
      gameSprite.fillRect(140, 5, 4, 12, fgColor);
      gameSprite.fillRect(148, 5, 4, 12, fgColor);
      gameSprite.pushSprite(0, 0);
      return;
  }
  
  dino.lastFrame = millis(); 
  dino.frameCount++; 
  
  gameSprite.fillSprite(bgColor); 
  gameSprite.drawLine(0, 100, 160, 100, fgColor); 
  
  for(int i=0; i<15; i++) { 
    int gx = (i*15 - (dino.frameCount*int(dino.speed))%15 + 160) % 160; 
    gameSprite.drawPixel(gx, 102 + (i%3), TFT_DARKGREY); 
  } 
  
  dino.y += dino.dy; 
  dino.dy += 0.55; 
  if(dino.y >= 84) { dino.y = 84; dino.dy = 0; } 
  
  if (random(0, 100) < 2 + (dino.speed * 0.4)) { 
    for(int i=0; i<3; i++) { 
      if(!dino.cAct[i]) { 
        bool tooClose = false; 
        float minGap = 80 + (dino.speed * 12); 
        for(int j=0; j<3; j++) if(dino.cAct[j] && dino.cx[j] > (160 - minGap)) tooClose = true; 
        if(!tooClose) { 
          dino.cAct[i] = true; dino.cx[i] = 160; 
          if (dino.score > 200) dino.type[i] = random(1, 5); else dino.type[i] = random(1, 3); 
          break; 
        } 
      } 
    } 
  } 
  
  for(int i=0; i<3; i++) { 
    if(dino.cAct[i]) { 
      if (dino.type[i] >= 3) dino.cx[i] -= (dino.speed + 1.0); else dino.cx[i] -= dino.speed; 
      if(dino.cx[i] < -24) dino.cAct[i] = false; 
      else { 
        int oX = dino.cx[i]; int oY = 84; int oW = 16, oH = 16; 
        if (dino.type[i] == 1) { drawSpriteMask(texCactus1, oX, oY, 16, 16, fgColor, &gameSprite); oW = 12; } 
        else if (dino.type[i] == 2) { drawSpriteMask(texCactus1, oX, oY, 16, 16, fgColor, &gameSprite); drawSpriteMask(texCactus1, oX+8, oY, 16, 16, fgColor, &gameSprite); oW = 20; } 
        else if (dino.type[i] == 3) { oY = 76; const char* birdTex = ((dino.frameCount/6)%2 == 0) ? texBird1 : texBird2; drawSpriteMask(birdTex, oX, oY, 16, 12, fgColor, &gameSprite); oW = 16; oH = 9; oY += 2; } 
        else if (dino.type[i] == 4) { oY = 84; const char* birdTex = ((dino.frameCount/6)%2 == 0) ? texBird1 : texBird2; drawSpriteMask(birdTex, oX, oY, 16, 12, fgColor, &gameSprite); oW = 14; oH = 9; oY += 2; } 
        
        int dLeft = 20 + 3; int dRight = 20 + 13; int dTop = dino.y + (dino.isDucking ? 5 : 2); int dBot = dino.y + 15; 
        if (dino.isDucking) dRight = 20 + 22; 
        int oLeft = oX + 2; int oRight = oX + oW - 2; int oTop = oY; int oBot = oY + oH; 
        if (dRight > oLeft && dLeft < oRight && dBot > oTop && dTop < oBot) { dino.gameOver = true; } 
      } 
    } 
  } 
  
  if (dino.y < 84) drawSpriteMask(texDinoRun1, 20, dino.y, 16, 16, fgColor, &gameSprite); 
  else if (dino.isDucking) { const char* dt = ((dino.frameCount/4)%2 == 0) ? texDinoDuck1 : texDinoDuck2; drawSpriteMask(dt, 20, dino.y, 24, 16, fgColor, &gameSprite); } 
  else { const char* dt = ((dino.frameCount/4)%2 == 0) ? texDinoRun1 : texDinoRun2; drawSpriteMask(dt, 20, dino.y, 16, 16, fgColor, &gameSprite); } 
  
  if (dino.frameCount % 3 == 0) {
    dino.score++; 
    if (dino.score % 150 == 0 && dino.speed < 4.5) {
      dino.speed += 0.15;
    }
  }
  
  if(dino.score > dinoHigh) { dinoHigh = dino.score; preferences.putInt("dino_hi", dinoHigh); } 
  gameSprite.setTextColor(fgColor); gameSprite.drawString("HI " + String(dinoHigh) + "  " + String(dino.score), 50, 5, 1); 
  
  if (dino.gameOver) { 
    gameSprite.fillRect(20, 40, 120, 48, bgColor); gameSprite.drawRect(20, 40, 120, 48, fgColor); 
    gameSprite.setTextColor(fgColor); gameSprite.drawString("GAME OVER", 40, 50, 2); gameSprite.drawString("Play:Re Prv:Ext", 35, 75, 1); 
  } 
  gameSprite.pushSprite(0, 0); 
}

void initSimon() { simon.seqLen = 1; simon.seq[0] = random(0, 4); simon.step = 0; simon.score = 0; simon.gameOver = false; simon.showing = true; simon.flashIdx = 0; simon.flashTimer = millis() + 500; drawSimonBoard(-1); }
void drawSimonBoard(int highlight) { gameSprite.fillRect(0, 0, 160, 128, TFT_BLACK); gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("SCORE: " + String(simon.score), 5, 2, 1); gameSprite.drawString("HIGH: " + String(simonHigh), 105, 2, 1); uint16_t c0 = highlight==0 ? TFT_WHITE : 0x03E0; uint16_t c1 = highlight==1 ? TFT_WHITE : 0x7800; uint16_t c2 = highlight==2 ? TFT_WHITE : 0x000F; uint16_t c3 = highlight==3 ? TFT_WHITE : 0x7BE0; gameSprite.fillRoundRect(65, 20, 30, 30, 4, highlight==0 ? c0 : TFT_BLACK); gameSprite.drawRoundRect(65, 20, 30, 30, 4, c0); gameSprite.drawRoundRect(64, 19, 32, 32, 4, c0); gameSprite.fillRoundRect(25, 55, 30, 30, 4, highlight==1 ? c1 : TFT_BLACK); gameSprite.drawRoundRect(25, 55, 30, 30, 4, c1); gameSprite.drawRoundRect(24, 54, 32, 32, 4, c1); gameSprite.fillRoundRect(105, 55, 30, 30, 4, highlight==2 ? c2 : TFT_BLACK); gameSprite.drawRoundRect(105, 55, 30, 30, 4, c2); gameSprite.drawRoundRect(104, 54, 32, 32, 4, c2); gameSprite.fillRoundRect(65, 90, 30, 30, 4, highlight==3 ? c3 : TFT_BLACK); gameSprite.drawRoundRect(65, 90, 30, 30, 4, c3); gameSprite.drawRoundRect(64, 89, 32, 32, 4, c3); gameSprite.setTextColor(highlight==0 ? TFT_BLACK : c0); gameSprite.drawString("PL", 73, 29, 2); gameSprite.setTextColor(highlight==1 ? TFT_BLACK : c1); gameSprite.drawString("PR", 33, 64, 2); gameSprite.setTextColor(highlight==2 ? TFT_BLACK : c2); gameSprite.drawString("NX", 113, 64, 2); gameSprite.setTextColor(highlight==3 ? TFT_BLACK : c3); gameSprite.drawString("EQ", 73, 99, 2); gameSprite.pushSprite(0, 0); }
void handleSimonButtons() { if(simon.gameOver) { if(digitalRead(BTN_PLAY_PIN)==LOW) { initSimon(); delay(200); } if(digitalRead(BTN_EQ_PIN)==LOW) { returnToGamesMenu(); delay(200); } return; } if(digitalRead(BTN_EQ_PIN)==LOW && !simon.showing) { uint32_t t = millis(); while(digitalRead(BTN_EQ_PIN)==LOW) { if(millis()-t > 600) { returnToGamesMenu(); delay(200); return; } } int press = 3; drawSimonBoard(press); delay(200); drawSimonBoard(-1); if(simon.seq[simon.step] == press) { simon.step++; if(simon.step >= simon.seqLen) { simon.score++; if(simon.score > simonHigh) { simonHigh = simon.score; preferences.putInt("simon_hi", simonHigh); } simon.seqLen++; simon.seq[simon.seqLen-1] = random(0, 4); simon.showing = true; simon.flashIdx = 0; simon.flashTimer = millis() + 500; simon.step = 0; } } else { simon.gameOver = true; } return; } if(!simon.showing) { int press = -1; if(digitalRead(BTN_PLAY_PIN)==LOW) press = 0; else if(digitalRead(BTN_PREV_PIN)==LOW) press = 1; else if(digitalRead(BTN_NEXT_PIN)==LOW) press = 2; if(press != -1) { drawSimonBoard(press); delay(200); drawSimonBoard(-1); if(simon.seq[simon.step] == press) { simon.step++; if(simon.step >= simon.seqLen) { simon.score++; if(simon.score > simonHigh) { simonHigh = simon.score; preferences.putInt("simon_hi", simonHigh); } simon.seqLen++; simon.seq[simon.seqLen-1] = random(0, 4); simon.showing = true; simon.flashIdx = 0; simon.flashTimer = millis() + 500; simon.step = 0; } } else { simon.gameOver = true; } } } }
void updateSimon() { if (simon.gameOver) { gameSprite.fillRect(30, 40, 100, 48, TFT_BLACK); gameSprite.setTextColor(TFT_RED); gameSprite.drawString("GAME OVER", 35, 50, 2); gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("Play:Re EQ:Ext", 35, 75, 1); gameSprite.pushSprite(0, 0); return; } if (simon.showing) { if(millis() > simon.flashTimer) { if (simon.flashIdx < simon.seqLen * 2) { if (simon.flashIdx % 2 == 0) { drawSimonBoard(simon.seq[simon.flashIdx/2]); simon.flashTimer = millis() + 400; } else { drawSimonBoard(-1); simon.flashTimer = millis() + 200; } simon.flashIdx++; } else { simon.showing = false; drawSimonBoard(-1); } } } }

void drawTTTMenu() { tft.fillScreen(TFT_BLACK); tft.setTextColor(TFT_CYAN); tft.drawString("TIC-TAC-TOE", 25, 20, 2); tft.setTextColor(ttt.cursor == 0 ? TFT_YELLOW : TFT_WHITE); tft.drawString("1 Player (Vs AI)", 25, 60, 2); tft.setTextColor(ttt.cursor == 1 ? TFT_YELLOW : TFT_WHITE); tft.drawString("2 Player (Manual)", 25, 80, 2); }
void initTTT() { ttt.mode = 0; ttt.cursor = 0; ttt.needsRedraw = true; }
void startTTTGame(int aiMode) { ttt.mode = aiMode; ttt.cursor = 4; ttt.turn = 1; ttt.winner = 0; for(int i=0; i<9; i++) ttt.board[i] = 0; tft.fillScreen(TFT_BLACK); ttt.needsRedraw = true; }
void drawTTTBoard() { if(!ttt.needsRedraw) return; ttt.needsRedraw = false; tft.drawLine(65, 20, 65, 110, TFT_WHITE); tft.drawLine(95, 20, 95, 110, TFT_WHITE); tft.drawLine(35, 50, 125, 50, TFT_WHITE); tft.drawLine(35, 80, 125, 80, TFT_WHITE); for(int i=0; i<9; i++) { int x = 40 + (i%3)*30; int y = 25 + (i/3)*30; if (ttt.cursor == i && ttt.winner == 0) tft.drawRect(x-4, y-4, 26, 26, TFT_YELLOW); else tft.drawRect(x-4, y-4, 26, 26, TFT_BLACK); if(ttt.board[i] == 1) { tft.setTextColor(TFT_CYAN); tft.drawString("X", x, y, 4); } else if(ttt.board[i] == 2) { tft.setTextColor(TFT_RED); tft.drawString("O", x, y, 4); } } if (ttt.winner != 0) { tft.fillRect(10, 50, 140, 30, TFT_BLACK); tft.setTextColor(TFT_YELLOW); if(ttt.winner == 3) tft.drawString(" DRAW! ", 50, 55, 2); else if(ttt.winner == 1) tft.drawString(" X WINS! ", 45, 55, 2); else tft.drawString(" O WINS! ", 45, 55, 2); tft.setTextColor(TFT_WHITE); tft.drawString("Play:Re EQ:Ext", 35, 115, 1); } }
int checkTTTWinner() { int wins[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}}; for(int i=0; i<8; i++) { if(ttt.board[wins[i][0]] != 0 && ttt.board[wins[i][0]] == ttt.board[wins[i][1]] && ttt.board[wins[i][1]] == ttt.board[wins[i][2]]) return ttt.board[wins[i][0]]; } bool full = true; for(int i=0; i<9; i++) if(ttt.board[i]==0) full=false; if(full) return 3; return 0; }
void doTTTAI() { 
  for(int i=0; i<9; i++) { if(ttt.board[i]==0) { ttt.board[i] = 2; if(checkTTTWinner()==2) goto ai_done; ttt.board[i] = 0; } } 
  for(int i=0; i<9; i++) { if(ttt.board[i]==0) { ttt.board[i] = 1; if(checkTTTWinner()==1) { ttt.board[i] = 2; goto ai_done; } ttt.board[i] = 0; } } 
  if(ttt.board[4]==0) { ttt.board[4] = 2; goto ai_done; } 
  { int corners[4] = {0,2,6,8}; for(int c=0; c<4; c++) { if(ttt.board[corners[c]]==0) { ttt.board[corners[c]] = 2; goto ai_done; } } } 
  { int empty[9], count=0; for(int i=0; i<9; i++) if(ttt.board[i]==0) empty[count++] = i; if(count>0) ttt.board[empty[random(0, count)]] = 2; }
ai_done:
  ttt.winner = checkTTTWinner(); ttt.turn = 1; ttt.needsRedraw = true;
}
void handleTTTButtons() { if(digitalRead(BTN_EQ_PIN)==LOW) { returnToGamesMenu(); delay(200); return; } if(ttt.mode == 0) { if(digitalRead(BTN_NEXT_PIN)==LOW || digitalRead(BTN_PREV_PIN)==LOW) { ttt.cursor = 1 - ttt.cursor; drawTTTMenu(); delay(200); } if(digitalRead(BTN_PLAY_PIN)==LOW) { startTTTGame(ttt.cursor == 0 ? 1 : 2); delay(200); } } else { if (ttt.winner != 0) { if(digitalRead(BTN_PLAY_PIN)==LOW) { startTTTGame(ttt.mode); delay(200); } return; } if(digitalRead(BTN_NEXT_PIN)==LOW) { do { ttt.cursor = (ttt.cursor + 1) % 9; } while(ttt.board[ttt.cursor]!=0); ttt.needsRedraw = true; delay(150); } if(digitalRead(BTN_PREV_PIN)==LOW) { do { ttt.cursor = (ttt.cursor - 1 + 9) % 9; } while(ttt.board[ttt.cursor]!=0); ttt.needsRedraw = true; delay(150); } if(digitalRead(BTN_PLAY_PIN)==LOW) { if(ttt.board[ttt.cursor] == 0) { ttt.board[ttt.cursor] = ttt.turn; ttt.winner = checkTTTWinner(); if(ttt.winner == 0) { if(ttt.mode == 1) { ttt.turn = 2; doTTTAI(); } else { ttt.turn = (ttt.turn == 1) ? 2 : 1; } } if(ttt.winner == 0) { for(int i=0; i<9; i++) if(ttt.board[i]==0) { ttt.cursor = i; break; } } ttt.needsRedraw = true; delay(200); } } } }
void updateTTT() { if (ttt.mode == 0) return; drawTTTBoard(); }

// --------------------------------------------------------
// DOOM 3D ENGINE
// --------------------------------------------------------

void initDoors() {
  doorCount = 0;
  for(int i=0; i<16; i++) {
    for(int j=0; j<16; j++) {
      if(doomMap[i][j] == 2 && doorCount < 10) { 
        doors[doorCount].x = i; 
        doors[doorCount].y = j; 
        doors[doorCount].offset = 0.0; 
        doors[doorCount].opening = false; 
        doorCount++; 
      }
    }
  }
}

void spawnEntity(int type, int &eCount) {
  if (eCount >= 20) return;
  for(int i=0; i<50; i++) {
    int rx = random(1, 15), ry = random(1, 15);
    if(doomMap[rx][ry] == 0) {
      bool occupied = false;
      for(int j=0; j<eCount; j++) { if(int(doom.entities[j].x)==rx && int(doom.entities[j].y)==ry) occupied = true; }
      if(!occupied && (abs(rx - (int)doom.posX) > 3 || abs(ry - (int)doom.posY) > 3)) {
        doom.entities[eCount].x = (float)rx + 0.5;
        doom.entities[eCount].y = (float)ry + 0.5;
        doom.entities[eCount].active = true;
        doom.entities[eCount].type = type;
        doom.entities[eCount].dist = 0;
        doom.entities[eCount].lastAttack = millis();
        eCount++;
        return;
      }
    }
  }
}

void initDoomLevel() {
  // Fill entire 32x32 with walls, then copy 16x16 base map
  for(int i=0; i<32; i++) for(int j=0; j<32; j++) doomMap[i][j] = 1; 
  for(int y=0; y<16; y++) for(int x=0; x<16; x++) doomMap[x][y] = E1M1[y][x]; 
  
  // Player starts in top-left room
  doom.posX = 2.5; doom.posY = 2.5; 
  doom.dirX = 1.0; doom.dirY = 0.0; 
  doom.planeX = 0.0; doom.planeY = 0.66;
  
  initDoors();
  for(int i=0; i<20; i++) doom.entities[i].active = false;
  int eCount = 0;
  
  // ALWAYS mix all 3 enemy types from level 1
  int numStationary = max(1, 4 - doom.level / 2);  // 4 at lv1, scales down
  int numRed = min(4, 1 + doom.level);              // 2 at lv1, scales up
  int numBlue = min(3, doom.level);                  // 1 at lv1, scales up
  
  // Cap total entities to 14 (+ 5 pickups + 1 exit = 20 max)
  while (eCount + numStationary + numRed + numBlue + 5 > 20) {
     if (numStationary > 1) numStationary--;
     else if (numRed > 1) numRed--;
     else if (numBlue > 1) numBlue--;
     else break;
  }

  for(int i=0; i<numStationary; i++) spawnEntity(1, eCount);  // stationary demons
  for(int i=0; i<numRed; i++) spawnEntity(5, eCount);         // walking red demons
  for(int i=0; i<numBlue; i++) spawnEntity(6, eCount);        // walking blue demons (fast)
  
  // Pickups: ammo, health, armor
  spawnEntity(2, eCount); // ammo
  spawnEntity(2, eCount); // ammo
  spawnEntity(3, eCount); // health
  spawnEntity(3, eCount); // health
  spawnEntity(7, eCount); // armor
}

void initDoom() {
  doom.level = 1; doom.health = 100; doom.armor = 0; doom.ammo = 15;
  doom.magAmmo = 6; doom.isReloading = false; doom.reloadTimer = 0;
  doom.kills = 0; 
  doom.gameOver = false; doom.isShooting = false; doom.eqPressTime = 0;
  doom.message = "LEVEL 1: BASE"; doom.messageTimer = millis() + 3000;
  doom.lastMoveTime = millis();
  initDoomLevel();
}

bool hasLineOfSight(float x1, float y1, float x2, float y2) {
  float dx = x2 - x1; float dy = y2 - y1; float dist = sqrt(dx*dx + dy*dy);
  if (dist == 0) return true;
  float rayDirX = dx/dist; float rayDirY = dy/dist;
  float deltaDistX = (rayDirX == 0) ? 1e30 : abs(1.0f / rayDirX); 
  float deltaDistY = (rayDirY == 0) ? 1e30 : abs(1.0f / rayDirY);
  int mapX = int(x1), mapY = int(y1); float sideDistX, sideDistY; int stepX, stepY;
  
  if (rayDirX < 0) { stepX = -1; sideDistX = (x1 - mapX) * deltaDistX; } else { stepX = 1; sideDistX = (mapX + 1.0f - x1) * deltaDistX; }
  if (rayDirY < 0) { stepY = -1; sideDistY = (y1 - mapY) * deltaDistY; } else { stepY = 1; sideDistY = (mapY + 1.0f - y1) * deltaDistY; }
  
  for(int s=0; s<30; s++) {
    if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; } else { sideDistY += deltaDistY; mapY += stepY; }
    if (mapX < 0 || mapX >= 32 || mapY < 0 || mapY >= 32) return false;
    if (doomMap[mapX][mapY] == 1 || doomMap[mapX][mapY] == 2) return false; 
    float traveled = (sideDistX < sideDistY ? sideDistX - deltaDistX : sideDistY - deltaDistY);
    if (traveled > dist) return true; 
  }
  return true;
}

void handleDoomButtons() {
  if (doom.gameOver) {
    if (digitalRead(BTN_PLAY_PIN) == LOW) { initDoom(); delay(200); }
    if (digitalRead(BTN_EQ_PIN) == LOW) { returnToGamesMenu(); delay(200); } return;
  }
  
  if (digitalRead(BTN_EQ_PIN) == LOW) {
    if (doom.eqPressTime == 0) doom.eqPressTime = millis(); else if (millis() - doom.eqPressTime > 2000) { returnToGamesMenu(); delay(200); return; }
  } else {
    if (doom.eqPressTime > 0 && millis() - doom.eqPressTime < 400) { doom.isShooting = true; } doom.eqPressTime = 0;
  }

  if (millis() - doom.lastMoveTime < 30) return; 
  doom.lastMoveTime = millis();

  float moveSpeed = 0.20; float rotSpeed = 0.15;

  if (digitalRead(BTN_PLAY_PIN) == LOW) { 
    float nextX = doom.posX + doom.dirX * moveSpeed; float nextY = doom.posY + doom.dirY * moveSpeed;
    
    if (doomMap[int(nextX)][int(doom.posY)] == 2) { for(int d=0; d<doorCount; d++) if(doors[d].x == int(nextX) && doors[d].y == int(doom.posY)) doors[d].opening = true; }
    if (doomMap[int(doom.posX)][int(nextY)] == 2) { for(int d=0; d<doorCount; d++) if(doors[d].x == int(doom.posX) && doors[d].y == int(nextY)) doors[d].opening = true; }
    
    if(doomMap[int(nextX)][int(doom.posY)] == 0) doom.posX = nextX;
    if(doomMap[int(doom.posX)][int(nextY)] == 0) doom.posY = nextY;
    
    for(int i=0; i<20; i++) { 
      if(doom.entities[i].active) {
        float dx = doom.entities[i].x - doom.posX; float dy = doom.entities[i].y - doom.posY;
        if (sqrt(dx*dx + dy*dy) < 0.6) {
           if (doom.entities[i].type == 2) { doom.ammo += 10; doom.entities[i].active = false; doom.message = "Ammo!"; doom.messageTimer = millis() + 1500; } 
           else if (doom.entities[i].type == 3) { doom.health += 25; if(doom.health>100) doom.health=100; doom.entities[i].active = false; doom.message = "Health!"; doom.messageTimer = millis() + 1500; } 
           else if (doom.entities[i].type == 7) { doom.armor += 50; if(doom.armor>100) doom.armor=100; doom.entities[i].active = false; doom.message = "Armor!"; doom.messageTimer = millis() + 1500; } 
           else if (doom.entities[i].type == 4) { 
               doom.level++; 
               if (doom.level > doomHigh) { doomHigh = doom.level; preferences.putInt("doom_hi", doomHigh); }
               doom.message = "LEVEL " + String(doom.level); doom.messageTimer = millis() + 2000;
               initDoomLevel();
               return; 
           }
        }
      }
    }
  }
  if (digitalRead(BTN_PREV_PIN) == LOW) {
    float oldDirX = doom.dirX; doom.dirX = doom.dirX * cos(rotSpeed) - doom.dirY * sin(rotSpeed); doom.dirY = oldDirX * sin(rotSpeed) + doom.dirY * cos(rotSpeed);
    float oldPlaneX = doom.planeX; doom.planeX = doom.planeX * cos(rotSpeed) - doom.planeY * sin(rotSpeed); doom.planeY = oldPlaneX * sin(rotSpeed) + doom.planeY * cos(rotSpeed);
  }
  if (digitalRead(BTN_NEXT_PIN) == LOW) {
    float oldDirX = doom.dirX; doom.dirX = doom.dirX * cos(-rotSpeed) - doom.dirY * sin(-rotSpeed); doom.dirY = oldDirX * sin(-rotSpeed) + doom.dirY * cos(-rotSpeed);
    float oldPlaneX = doom.planeX; doom.planeX = doom.planeX * cos(-rotSpeed) - doom.planeY * sin(-rotSpeed); doom.planeY = oldPlaneX * sin(-rotSpeed) + doom.planeY * cos(-rotSpeed);
  }
}

void sortEnemies() {
  for(int i=0; i<20; i++) {
    if (doom.entities[i].active) doom.entities[i].dist = (doom.posX - doom.entities[i].x)*(doom.posX - doom.entities[i].x) + (doom.posY - doom.entities[i].y)*(doom.posY - doom.entities[i].y);
    else doom.entities[i].dist = -1;
  }
  for(int i=0; i<19; i++) { for(int j=0; j<19-i; j++) { if(doom.entities[j].dist < doom.entities[j+1].dist) { DoomSprite temp = doom.entities[j]; doom.entities[j] = doom.entities[j+1]; doom.entities[j+1] = temp; } } }
}

uint16_t applyFog(uint16_t color, float dist) {
  if (dist < 1.0) return color;
  float factor = 1.0 / dist; if (factor > 1.0) factor = 1.0; if (factor < 0.45) factor = 0.45;
  uint8_t r = (color >> 11) & 0x1F; uint8_t g = (color >> 5) & 0x3F; uint8_t b = color & 0x1F;
  r = r * factor; g = g * factor; b = b * factor; return (r << 11) | (g << 5) | b;
}

void updateDoom() {
  if (doom.gameOver) return;

  // AI enemy movement
  static uint32_t lastAiMove = 0;
  if (millis() - lastAiMove > 100) {
    lastAiMove = millis();
    for(int i=0; i<20; i++) {
      if (doom.entities[i].active && (doom.entities[i].type == 5 || doom.entities[i].type == 6)) {
        float dist = sqrt( (doom.entities[i].x - doom.posX)*(doom.entities[i].x - doom.posX) + (doom.entities[i].y - doom.posY)*(doom.entities[i].y - doom.posY) );
        if (dist > 1.2 && dist < 12.0) { 
          if (hasLineOfSight(doom.entities[i].x, doom.entities[i].y, doom.posX, doom.posY)) {
            float speed = (doom.entities[i].type == 6) ? 0.08 : 0.04;
            float dx = (doom.posX - doom.entities[i].x) / dist; float dy = (doom.posY - doom.entities[i].y) / dist;
            float nextX = doom.entities[i].x + dx * speed; float nextY = doom.entities[i].y + dy * speed;
            if (doomMap[int(nextX)][int(doom.entities[i].y)] == 0) doom.entities[i].x = nextX;
            if (doomMap[int(doom.entities[i].x)][int(nextY)] == 0) doom.entities[i].y = nextY;
          }
        }
      }
    }
  }

  // Door animation
  for(int d=0; d<doorCount; d++) {
    if(doors[d].opening) {
      doors[d].offset += 0.05;
      if(doors[d].offset >= 1.0) { doors[d].offset = 1.0; doomMap[doors[d].x][doors[d].y] = 0; doors[d].opening = false; }
    }
  }

  // Draw ceiling (black) and floor (gradient)
  gameSprite.fillRect(0, 0, 160, 57, TFT_BLACK); 
  for (int y = 1; y < 56; y++) {
     float dist = 56.0 / y; 
     uint16_t fColor = applyFog(0x4208, dist); 
     gameSprite.drawLine(0, 56 + y, 160, 56 + y, fColor);
  }
  
  float zBuffer[160];

  // Enemy attack logic
  for(int i=0; i<20; i++) {
    if (doom.entities[i].active && (doom.entities[i].type == 1 || doom.entities[i].type == 5 || doom.entities[i].type == 6)) {
      float dx = doom.posX - doom.entities[i].x; float dy = doom.posY - doom.entities[i].y;
      if (sqrt(dx*dx + dy*dy) < 5.0 && millis() - doom.entities[i].lastAttack > 1500) {
         if (hasLineOfSight(doom.entities[i].x, doom.entities[i].y, doom.posX, doom.posY)) {
           int dmg = (doom.entities[i].type == 6) ? random(5, 10) : random(3, 8);
           if (doom.armor > 0) {
              int armorDmg = dmg / 2;
              if (doom.armor >= armorDmg) { doom.armor -= armorDmg; dmg -= armorDmg; }
              else { dmg -= doom.armor; doom.armor = 0; }
           }
           doom.health -= dmg;
           doom.entities[i].lastAttack = millis();
           gameSprite.fillRect(0,0,160,112, TFT_RED); if (doom.health <= 0) { doom.gameOver = true; }
         }
      }
    }
  }

  // Shooting logic
  if (doom.isReloading) {
     if (millis() - doom.reloadTimer > 800) {
        doom.isReloading = false;
        doom.magAmmo = (doom.level >= 3) ? 2 : 6;
     }
  } else if (doom.magAmmo == 0 && doom.ammo > 0) {
     doom.isReloading = true;
     doom.reloadTimer = millis();
     doom.message = "RELOADING...";
     doom.messageTimer = millis() + 800;
  }

  if (doom.isShooting) {
    if (!doom.isReloading) {
      if (doom.magAmmo > 0 && doom.ammo > 0) {
        doom.ammo--;
        doom.magAmmo--;
        doom.lastShotTime = millis();
        sortEnemies(); 
        
        for(int i=19; i>=0; i--) { 
           if (doom.entities[i].active && (doom.entities[i].type == 1 || doom.entities[i].type == 5 || doom.entities[i].type == 6)) {
             float dx = doom.entities[i].x - doom.posX; 
             float dy = doom.entities[i].y - doom.posY;
             float angle = atan2(dy, dx) - atan2(doom.dirY, doom.dirX);
             while (angle > PI) angle -= 2*PI; 
             while (angle < -PI) angle += 2*PI;
             
             // TIGHTER HITBOX: reduced from 0.6 to 0.15 for more precision!
             if (abs(angle) < 0.15 && sqrt(dx*dx + dy*dy) < 20.0) {
                if (sqrt(dx*dx + dy*dy) < 4.0 || hasLineOfSight(doom.posX, doom.posY, doom.entities[i].x, doom.entities[i].y)) {
                   
                   doom.entities[i].active = false; 
                   doom.kills++; 
                   if (doom.kills > doomHigh) { doomHigh = doom.kills; preferences.putInt("doom_hi", doomHigh); }
                   
                   bool enemiesLeft = false;
                   for(int j=0; j<20; j++) {
                      if (doom.entities[j].active && (doom.entities[j].type == 1 || doom.entities[j].type == 5 || doom.entities[j].type == 6)) {
                         enemiesLeft = true; break;
                      }
                   }
                   
                   if (!enemiesLeft) {
                      doom.entities[i].type = 4;
                      doom.entities[i].active = true;
                      doom.message = "PORTAL OPENED!";
                      doom.messageTimer = millis() + 2500;
                   }
                   break; 
                }
             }
           }
        }
      } else if (doom.ammo <= 0) { 
        doom.message = "OUT OF AMMO!"; 
        doom.messageTimer = millis() + 1000; 
      }
    }
    doom.isShooting = false;
  }

  // Raycasting walls
  for(int x = 0; x < 160; x++) {
    float cameraX = 2 * x / 160.0 - 1;
    float rayDirX = doom.dirX + doom.planeX * cameraX;
    float rayDirY = doom.dirY + doom.planeY * cameraX;
    int mapX = int(doom.posX), mapY = int(doom.posY);
    float sideDistX, sideDistY;
    float deltaDistX = (rayDirX == 0) ? 1e30 : abs(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30 : abs(1.0f / rayDirY);
    float perpWallDist; int stepX, stepY, hit = 0, side;

    if (rayDirX < 0) { stepX = -1; sideDistX = (doom.posX - mapX) * deltaDistX; } else { stepX = 1; sideDistX = (mapX + 1.0f - doom.posX) * deltaDistX; }
    if (rayDirY < 0) { stepY = -1; sideDistY = (doom.posY - mapY) * deltaDistY; } else { stepY = 1; sideDistY = (mapY + 1.0f - doom.posY) * deltaDistY; }

    float doorOff = 0.0;
    for(int s=0; s<30 && hit == 0; s++) {
      if (sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; } else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
      if (mapX < 0 || mapX >= 32 || mapY < 0 || mapY >= 32) { hit = 1; break; }
      if (doomMap[mapX][mapY] == 1) hit = 1;
      else if (doomMap[mapX][mapY] == 2) {
        float tempPerp = (side == 0) ? (sideDistX - deltaDistX) : (sideDistY - deltaDistY);
        float tempWallX = (side == 0) ? doom.posY + tempPerp * rayDirY : doom.posX + tempPerp * rayDirX;
        tempWallX -= floor(tempWallX);
        float offset = 0; for(int d=0; d<doorCount; d++) { if(doors[d].x == mapX && doors[d].y == mapY) { offset = doors[d].offset; break; } }
        
        bool isSplitDoor = ((mapX + mapY) % 2 == 0);
        if (isSplitDoor) {
           if (tempWallX <= 0.5 - (offset / 2.0) || tempWallX >= 0.5 + (offset / 2.0)) { hit = 2; doorOff = 0; }
        } else {
           if (tempWallX > offset) { hit = 2; doorOff = offset; }
        }
      }
    }

    if (side == 0) perpWallDist = (sideDistX - deltaDistX); else perpWallDist = (sideDistY - deltaDistY);
    if (perpWallDist < 0.01) perpWallDist = 0.01;
    zBuffer[x] = perpWallDist;

    int lineHeight = (int)(112 / perpWallDist);
    int drawStart = -lineHeight / 2 + 56; if(drawStart < 0) drawStart = 0;
    int drawEnd = lineHeight / 2 + 56; if(drawEnd >= 112) drawEnd = 111;
    
    float wallX; if (side == 0) wallX = doom.posY + perpWallDist * rayDirY; else wallX = doom.posX + perpWallDist * rayDirX;
    wallX -= floor(wallX);
    
    float step = 32.0 / lineHeight;
    float texPos = (drawStart - 56.0 + lineHeight / 2.0) * step; 
    
    for(int y = drawStart; y < drawEnd; y++) {
      int texY = ((int)texPos) & 31;
      texPos += step;
      uint16_t color;
      if (hit == 2) {
         int texX = int((wallX - doorOff) * 32.0); if (texX<0) texX=0; if (texX>31) texX=31;
         if(side == 0 && rayDirX > 0) texX = 32 - texX - 1; if(side == 1 && rayDirY < 0) texX = 32 - texX - 1;
         color = texDoor[texY * 32 + texX];
      } else {
         int texX = int(wallX * 32.0);
         if(side == 0 && rayDirX > 0) texX = 32 - texX - 1; if(side == 1 && rayDirY < 0) texX = 32 - texX - 1;
         color = texWall[texY * 32 + texX];
      }
      if(side == 1) color = applyFog(color, perpWallDist + 0.8); 
      color = applyFog(color, perpWallDist); 
      gameSprite.drawPixel(x, y, color);
    }
  }

  // Draw sprites
  sortEnemies();
  for(int i=0; i<20; i++) {
    if(!doom.entities[i].active || doom.entities[i].dist < 0.1) continue;
    float spriteX = doom.entities[i].x - doom.posX; float spriteY = doom.entities[i].y - doom.posY;
    float invDet = 1.0 / (doom.planeX * doom.dirY - doom.dirX * doom.planeY);
    float transformX = invDet * (doom.dirY * spriteX - doom.dirX * spriteY); float transformY = invDet * (-doom.planeY * spriteX + doom.planeX * spriteY);
    if (transformY <= 0.1) continue;
    int spriteScreenX = int((160 / 2) * (1 + transformX / transformY));
    int spriteHeight = abs(int(112 / transformY)); int drawStartY = -spriteHeight / 2 + 56; if (drawStartY < 0) drawStartY = 0; int drawEndY = spriteHeight / 2 + 56; if (drawEndY >= 112) drawEndY = 111;
    int spriteWidth = abs(int(112 / transformY)); int drawStartX = -spriteWidth / 2 + spriteScreenX; if (drawStartX < 0) drawStartX = 0; int drawEndX = spriteWidth / 2 + spriteScreenX; if (drawEndX >= 160) drawEndX = 159;

    // Pick animation frame based on entity type
    const char* tM = texMonster1;
    int cycle = (millis() / 150) % 4;
    if (doom.entities[i].type == 1) {
       tM = texMonster1; // stationary - no animation
    } else if (doom.entities[i].type == 5) {
       tM = texMonster1;
       if (cycle == 1) tM = texMonster2; else if (cycle == 3) tM = texMonster3;
    } else if (doom.entities[i].type == 6) {
       tM = texMonsterBlue1;
       if (cycle == 1) tM = texMonsterBlue2; else if (cycle == 3) tM = texMonsterBlue3;
    }

    for (int stripe = drawStartX; stripe < drawEndX; stripe++) {
      int texX = int(256 * (stripe - (-spriteWidth / 2.0 + spriteScreenX)) * 16.0 / spriteWidth) / 256;
      if (texX < 0) texX = 0; if (texX > 15) texX = 15;
      if (transformY > 0 && stripe > 0 && stripe < 160 && transformY < zBuffer[stripe]) {
        for (int y = drawStartY; y < drawEndY; y++) {
          int texY = (int)((y - (-spriteHeight / 2.0 + 56.0)) * 16.0 / spriteHeight);
          if (texY < 0) texY = 0; else if (texY > 15) texY = 15;
          int colorIdx = 0;
          
          if (doom.entities[i].type == 1 || doom.entities[i].type == 5 || doom.entities[i].type == 6) colorIdx = tM[texY * 16 + texX] - '0';
          else if (doom.entities[i].type == 2) colorIdx = texAmmo[texY * 16 + texX] - '0';
          else if (doom.entities[i].type == 3) colorIdx = texHealth[texY * 16 + texX] - '0';
          else if (doom.entities[i].type == 4) colorIdx = texExit[texY * 16 + texX] - '0';
          else if (doom.entities[i].type == 7) colorIdx = texArmor[texY * 16 + texX] - '0';
          
          if (colorIdx != 0) { gameSprite.drawPixel(stripe, y, applyFog(doomPal[colorIdx], transformY)); }
        }
      }
    }
  }

// Beautiful Minimap
  gameSprite.fillRect(122, 2, 36, 36, TFT_BLACK);     // Black background
  gameSprite.drawRect(122, 2, 36, 36, TFT_DARKGREY);  // Outer border
  gameSprite.drawRect(123, 3, 34, 34, 0x2104);        // Inner accent border

  for(int y=0; y<16; y++) {
    for(int x=0; x<16; x++) {
      if(doomMap[x][y] == 1) gameSprite.fillRect(124 + x*2, 4 + y*2, 2, 2, 0x8410); // Walls (grey)
      else if(doomMap[x][y] == 2) gameSprite.fillRect(124 + x*2, 4 + y*2, 2, 2, TFT_YELLOW); // Doors
    }
  }
  
  // Draw player
  gameSprite.fillRect(124 + (int)doom.posX*2, 4 + (int)doom.posY*2, 2, 2, TFT_GREEN);
  
  // Draw enemies & items
  for(int i=0; i<20; i++) { 
    if (doom.entities[i].active && (int)doom.entities[i].x < 16 && (int)doom.entities[i].y < 16) {
       uint16_t ec = TFT_RED; 
       if (doom.entities[i].type == 6) ec = TFT_CYAN;
       else if (doom.entities[i].type == 2) ec = TFT_ORANGE; 
       else if (doom.entities[i].type == 3) ec = TFT_BLUE; 
       else if (doom.entities[i].type == 4) ec = TFT_MAGENTA;
       else if (doom.entities[i].type == 7) ec = TFT_LIGHTGREY;
       
       gameSprite.fillRect(124 + (int)doom.entities[i].x*2, 4 + (int)doom.entities[i].y*2, 2, 2, ec);
    }
  }
  // Gun
  int gunScale = 3;
  int gunX = 80 - (16 * gunScale) / 2; 
  int gunY = 112 - (16 * gunScale);    
  
  if (millis() - doom.lastMoveTime < 100 && (digitalRead(BTN_PLAY_PIN) == LOW)) {
    gunX += sin(millis() / 150.0) * 4;
    gunY += abs(cos(millis() / 150.0)) * 4;
  }
  
  if (doom.isReloading) {
     float rp = (millis() - doom.reloadTimer) / 800.0;
     if (rp > 1.0) rp = 1.0;
     int frame = int(rp * 5); // 5 choppy retro frames
     if (doom.level >= 3) {
        // Shotgun pump action
        if (frame == 0) { gunY += 10; }
        else if (frame == 1) { gunY += 20; gunX -= 5; } // Pull back
        else if (frame == 2) { gunY += 15; gunX -= 10; } // Pump
        else if (frame == 3) { gunY += 20; gunX += 5; }  // Push forward
        else if (frame == 4) { gunY += 5; }
     } else {
        // Pistol mag swap
        if (frame == 0) { gunY += 15; gunX += 5; }
        else if (frame == 1) { gunY += 35; gunX += 10; }
        else if (frame == 2) { gunY += 45; gunX += 15; } // Dropped off screen
        else if (frame == 3) { gunY += 25; gunX += 5; }  // Bring back up
        else if (frame == 4) { gunY += 10; }
     }
  } else if (millis() - doom.lastShotTime < 80) { 
     // Chunky recoil
     gunY += 8;
  }
  
  if (!doom.isReloading && millis() - doom.lastShotTime < 60) { 
     int tx = gunX + 8 * gunScale;
     int ty = gunY; 
     // Retro blocky muzzle flash - FINE TUNED HORIZONTAL SPREAD
     // Stretches horizontally by exactly 4 true pixels compared to the original tight version
     gameSprite.fillRect(tx - 11, ty - 9, 22, 15, TFT_ORANGE); 
     gameSprite.fillRect(tx - 6, ty - 12, 12, 21, TFT_ORANGE); 
     
     gameSprite.fillRect(tx - 8, ty - 6, 16, 9, TFT_YELLOW);   
     gameSprite.fillRect(tx - 3, ty - 9, 6, 15, TFT_YELLOW);   
     
     gameSprite.fillRect(tx - 3, ty - 3, 6, 6, TFT_WHITE);     
  }

  for(int y=0; y<16; y++) {
    for(int x=0; x<16; x++) {
      int c = (doom.level >= 3) ? (texShotgun[y*16+x] - '0') : (texPistol[y*16+x] - '0');
      if (c != 0) gameSprite.fillRect(gunX+x*gunScale, gunY+y*gunScale, gunScale, gunScale, doomPal[c]);
    }
  }

  // Crosshair
  gameSprite.drawLine(78, 56, 82, 56, TFT_GREEN);
  gameSprite.drawLine(80, 54, 80, 58, TFT_GREEN);

  // HUD bar
  gameSprite.fillRect(0, 112, 160, 16, 0x2104);
  gameSprite.drawRect(0, 112, 160, 16, 0x4208);
  
  gameSprite.setTextColor(TFT_WHITE); 
  // Perfectly mathematical side-by-side fit with a clean 4-pixel gap
  gameSprite.drawString("HP:" + String(doom.health), 0, 114, 1);
  gameSprite.drawString("AR:" + String(doom.armor), 40, 114, 1);
  
  // Right side of face
  gameSprite.drawString("AM:" + String(doom.magAmmo) + "/" + String(doom.ammo), 96, 114, 1);
  
  // Top-left Kill counter to avoid bottom overlap
  gameSprite.setTextColor(TFT_YELLOW); 
  gameSprite.drawString("KILLS:" + String(doom.kills), 2, 2, 1);
  
  // Face (Shifted exactly 4 pixels right to X=76 to balance the 4-pixel gap on the left)
  for(int y=0; y<16; y++) {
    for(int x=0; x<16; x++) {
      int c = (doom.health > 50) ? (texFace[y*16+x] - '0') : (texFaceHurt[y*16+x] - '0');
      if (c != 0) gameSprite.drawPixel(76+x, 112+y, doomPal[c]);
    }
  }

  // Message
  if (millis() < doom.messageTimer) { 
    gameSprite.setTextColor(TFT_YELLOW); 
    gameSprite.drawString(doom.message, 80 - (gameSprite.textWidth(doom.message, 1)/2), 80, 1); 
  }

  // Game over
  if (doom.gameOver) {
    gameSprite.fillRect(0, 35, 160, 60, TFT_BLACK); 
    gameSprite.setTextColor(TFT_RED); gameSprite.drawString("YOU DIED", 40, 45, 2);
    gameSprite.setTextColor(TFT_WHITE); gameSprite.drawString("Play:Re  EQ:Exit", 30, 65, 1);
    gameSprite.setTextColor(TFT_YELLOW); gameSprite.drawString("KILLS: " + String(doom.kills) + "  BEST: " + String(doomHigh), 25, 80, 1);
  }
  gameSprite.pushSprite(0, 0); 
}


const char* resetOpts[] = {"All Games", "Snake", "Blackjack", "Pong", "Flappy", "Tetris", "DOOM", "Galaga", "Dino", "Simon"};

void handleResetButtons() {
    if (digitalRead(BTN_EQ_PIN) == LOW) {
        returnToGamesMenu();
        delay(200);
        return;
    }
    if (digitalRead(BTN_NEXT_PIN) == LOW) {
        resetScoresMenu.cursor++;
        if (resetScoresMenu.cursor > 9) resetScoresMenu.cursor = 0;
        resetScoresMenu.needsRedraw = true;
        delay(150);
    }
    if (digitalRead(BTN_PREV_PIN) == LOW) {
        resetScoresMenu.cursor--;
        if (resetScoresMenu.cursor < 0) resetScoresMenu.cursor = 9;
        resetScoresMenu.needsRedraw = true;
        delay(150);
    }
    if (digitalRead(BTN_PLAY_PIN) == LOW) {
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 1) { snakeHigh = 0; preferences.putInt("snake_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 2) { bjWins = 0; preferences.putInt("bj_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 3) { pongHigh = 0; preferences.putInt("pong_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 4) { flappyHigh = 0; preferences.putInt("flap_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 5) { tetrisHigh = 0; preferences.putInt("tet_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 6) { doomHigh = 0; preferences.putInt("doom_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 7) { galagaHigh = 0; preferences.putInt("galaga_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 8) { dinoHigh = 0; preferences.putInt("dino_hi", 0); }
        if (resetScoresMenu.cursor == 0 || resetScoresMenu.cursor == 9) { simonHigh = 0; preferences.putInt("simon_hi", 0); }
        
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN);
        tft.drawString("Score(s) Reset!", 20, 50, 2);
        delay(1000);
        returnToGamesMenu();
    }
}

void updateResetMenu() {
    if (!resetScoresMenu.needsRedraw) return;
    resetScoresMenu.needsRedraw = false;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_MAGENTA);
    tft.drawString("RESET SCORES", 20, 5, 2);
    tft.drawLine(0, 22, 160, 22, TFT_DARKGREY);
    
    int startIdx = resetScoresMenu.cursor - 2;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > 5) startIdx = 5;
    
    for(int i = 0; i < 5; i++) {
        int idx = startIdx + i;
        if (idx > 9) break;
        int y = 30 + (i * 15);
        if (idx == resetScoresMenu.cursor) {
            tft.setTextColor(TFT_CYAN);
            tft.drawString("> " + String(resetOpts[idx]), 10, y, 1);
        } else {
            tft.setTextColor(TFT_WHITE);
            tft.drawString("  " + String(resetOpts[idx]), 10, y, 1);
        }
    }
}