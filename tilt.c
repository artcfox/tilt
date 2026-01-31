/*

  tilt.c

  Copyright 2026 Matthew T. Pandina. All rights reserved.

  This file is part of Tilt Puzzle.

  Tilt Puzzle is free software: you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  Tilt Puzzle is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Tilt Puzzle.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <uzebox.h>
#include <avr/interrupt.h>

#include "data/titlescreen.inc"
#include "data/tileset.inc"
#include "data/patches.inc"
//#include "data/midisong.h"

typedef struct {
  uint16_t held;
  uint16_t prev;
  uint16_t pressed;
  uint16_t released;
} __attribute__ ((packed)) BUTTON_INFO;

#define GAME_USER_RAM_TILES_COUNT 0

#define TILE_BACKGROUND 0
#define TILE_FOREGROUND 1

#define BOARD_HEIGHT 5
#define BOARD_WIDTH 5
#define LEVEL_SIZE (BOARD_WIDTH * BOARD_HEIGHT)
#define BOARD_OFFSET_IN_LEVEL 0

uint8_t currentLevel;
bool youWin;
bool youLose;
bool playedYouLoseSound;

// How many pieces hit their end stops this frame (affect whether the sfx plays and its volume)
uint8_t numSlidersHitEndStops;
bool playFellDownHoleSound;

// The configuration of the playing board
uint8_t board[BOARD_HEIGHT][BOARD_WIDTH] = {
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0 },
};

#include "levels.h"

#define ENTIRE_GAMEBOARD_LEFT ((SCREEN_TILES_H - MAP_BOARD_WIDTH) / 2)
#define ENTIRE_GAMEBOARD_TOP ((SCREEN_TILES_V - MAP_BOARD_HEIGHT) / 2)
#define GAMEBOARD_ACTIVE_AREA_LEFT (ENTIRE_GAMEBOARD_LEFT + 2)
#define GAMEBOARD_ACTIVE_AREA_TOP (ENTIRE_GAMEBOARD_TOP + 2)
#define GAMEPIECE_WIDTH 2
#define GAMEPIECE_HEIGHT 2

// Each piece has a different tile map if it partially overlaps with the hole in the center of the board
const VRAM_PTR_TYPE* MapPieceToTileMapForBoardPosition(uint8_t piece, uint8_t x, uint8_t y) {
  const VRAM_PTR_TYPE* map_piece = map_stopper;
  switch (piece) {
  case S:
    if (x == 2 && y == 1)
      map_piece = map_stopper_t;
    else if (x == 1 && y == 2)
      map_piece = map_stopper_l;
    else if (x == 3 && y == 2)
      map_piece = map_stopper_r;
    else if (x == 2 && y == 3)
      map_piece = map_stopper_b;
    else
      map_piece = map_stopper;
    break;
  case G:
    if (x == 2 && y == 1)
      map_piece = map_green_t;
    else if (x == 1 && y == 2)
      map_piece = map_green_l;
    else if (x == 3 && y == 2)
      map_piece = map_green_r;
    else if (x == 2 && y == 3)
      map_piece = map_green_b;
    else if (x == 2 && y == 2)
      map_piece = map_green_h;
    else
      map_piece = map_green;
    break;
  case B:
    if (x == 2 && y == 1)
      map_piece = map_blue_t;
    else if (x == 1 && y == 2)
      map_piece = map_blue_l;
    else if (x == 3 && y == 2)
      map_piece = map_blue_r;
    else if (x == 2 && y == 3)
      map_piece = map_blue_b;
    else if (x == 2 && y == 2)
      map_piece = map_blue_h;
    else
      map_piece = map_blue;
    break;
  }
  return map_piece;
}

const VRAM_PTR_TYPE* MapBoardPositionToGridTileMap(uint8_t x, uint8_t y) {
  const VRAM_PTR_TYPE* map_piece = map_grid;
  if (x == 2 && y == 1)
    map_piece = map_grid_t;
  else if (x == 1 && y == 2)
    map_piece = map_grid_l;
  else if (x == 3 && y == 2)
    map_piece = map_grid_r;
  else if (x == 2 && y == 3)
    map_piece = map_grid_b;
  return map_piece;
}

/*
 * BCD_addConstant
 *
 * Adds a constant (binary number) to a BCD number
 *
 * num [in, out]
 *   The BCD number
 *
 * digits [in]
 *   The number of digits in the BCD number, num
 *
 * x [in]
 *   The binary value to be added to the BCD number
 *
 *   Note: The largest value that can be safely added to a BCD number
 *         is BCD_ADD_CONSTANT_MAX. If the result would overflow num,
 *         then num will be clamped to its maximum value (all 9's).
 *
 * Returns:
 *   A boolean that is true if num has been clamped to its maximum
 *   value (all 9's), or false otherwise.
 */
#define BCD_ADD_CONSTANT_MAX 244
static bool BCD_addConstant(uint8_t* const num, const uint8_t digits, uint8_t x)
{
  for (uint8_t i = 0; i < digits; ++i) {
    uint8_t val = num[i] + x;
    if (val < 10) { // speed up the common cases
      num[i] = val;
      x = 0;
      break;
    } else if (val < 20) {
      num[i] = val - 10;
      x = 1;
    } else if (val < 30) {
      num[i] = val - 20;
      x = 2;
    } else if (val < 40) {
      num[i] = val - 30;
      x = 3;
    } else { // handle the rest of the cases (up to 255 - 9) with a loop
      for (uint8_t j = 5; j < 26; ++j) {
        if (val < (j * 10)) {
          num[i] = val - ((j - 1) * 10);
          x = (j - 1);
          break;
        }
      }
    }
  }

  if (x > 0) {
    for (uint8_t i = 0; i < digits; ++i)
      num[i] = 9;
    return true;
  }

  return false;
}

#define TILE_GREEN 2
#define TILE_YELLOW 3
#define TILE_BLUE 4
#define TILE_RED 5

uint8_t GetLevelColor(uint8_t level)
{
  if ((level >= 1) && (level <= 10))
    return TILE_GREEN;
  else if ((level >= 11) && (level <= 20))
    return TILE_YELLOW;
  else if ((level >= 21) && (level <= 30))
    return TILE_BLUE;
  else if ((level >= 31) && (level <= 40))
    return TILE_RED;
  return 0;
}

#define TILE_NUM_START_DIGITS 6

static void LoadLevel(const uint8_t level)
{
  youWin = false;
  youLose = false;
  playedYouLoseSound = false;

  // Draw LEVEL ##
  DrawMap((SCREEN_TILES_H - 8) / 2, ENTIRE_GAMEBOARD_TOP - 3, map_level);
  uint8_t digits[2] = {0};
  BCD_addConstant(digits, 2, level);
  SetTile((SCREEN_TILES_H - 8) / 2 + 6, ENTIRE_GAMEBOARD_TOP - 3, TILE_NUM_START_DIGITS + digits[1]);
  SetTile((SCREEN_TILES_H - 8) / 2 + 7, ENTIRE_GAMEBOARD_TOP - 3, TILE_NUM_START_DIGITS + digits[0]);

  uint8_t levelColor = GetLevelColor(level);
  SetTile((SCREEN_TILES_H - 8) / 2 - 3, ENTIRE_GAMEBOARD_TOP - 3, levelColor);
  SetTile((SCREEN_TILES_H - 8) / 2 - 2, ENTIRE_GAMEBOARD_TOP - 3, levelColor);
  SetTile((SCREEN_TILES_H - 8) / 2 + 9, ENTIRE_GAMEBOARD_TOP - 3, levelColor);
  SetTile((SCREEN_TILES_H - 8) / 2 + 10, ENTIRE_GAMEBOARD_TOP - 3, levelColor);

  DrawMap(ENTIRE_GAMEBOARD_LEFT, ENTIRE_GAMEBOARD_TOP, map_board);

  const uint16_t levelOffset = (level - 1) * LEVEL_SIZE;
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + BOARD_OFFSET_IN_LEVEL) + y * BOARD_WIDTH + x]);
      board[y][x] = piece;

      if (piece == S || piece == G || piece == B)
        DrawMap(GAMEBOARD_ACTIVE_AREA_LEFT + GAMEPIECE_WIDTH * x,
                GAMEBOARD_ACTIVE_AREA_TOP + GAMEPIECE_HEIGHT * y,
                MapPieceToTileMapForBoardPosition(piece, x, y));
    }
}

struct MOVE_INFO;
typedef struct MOVE_INFO MOVE_INFO;

struct MOVE_INFO {
  uint8_t piece; // G or B
  uint8_t xStart;
  uint8_t yStart;
  uint8_t xEnd;
  uint8_t yEnd;
  bool fellDownHole;

  // Animation stuff
  bool doneMoving;

  // Should be able to reduce this to just position and velocity
  int16_t x;
  int16_t y;
  int16_t dx;
  int16_t dy;
} __attribute__ ((packed));

#define MAX_MOVABLE_PIECES 5

MOVE_INFO moveInfo[MAX_MOVABLE_PIECES];

static void TiltBoardLeft() {
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  // Start from the leftmost column, and work our way right, looping over each row
  for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
    for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].yEnd = y;

        // Decrement xEnd until we hit a stopper or the edge, adding the number of G's and B's along the way
        uint8_t numGreenBlueSeen = 0;
        uint8_t xEnd = x;

        while (xEnd > 0) {
          uint8_t nextPiece = board[y][xEnd - 1];

          if (nextPiece == S)
            break;

          if (xEnd - 1 == 2 && y == 2) {
            moveInfo[currentIndex].fellDownHole = true;
            if (moveInfo[currentIndex].piece == B)
              youLose = true;
            break;
          }

          if (nextPiece == G || nextPiece == B)
            ++numGreenBlueSeen;

          --xEnd;
        }

        if (moveInfo[currentIndex].fellDownHole)
          moveInfo[currentIndex].xEnd = 2;
        else
          moveInfo[currentIndex].xEnd = xEnd + numGreenBlueSeen;

        if (currentIndex < MAX_MOVABLE_PIECES - 1)
          ++currentIndex;
      }
}

static void TiltBoardUp() {
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  // Start from the topmost row, and work our way down, looping over each column
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].xEnd = x;
        // Decrement yEnd until we hit a stopper or the edge, adding the number of G's and B's along the way
        uint8_t numGreenBlueSeen = 0;
        uint8_t yEnd = y;

        while (yEnd > 0) {
          uint8_t nextPiece = board[yEnd - 1][x];

          if (nextPiece == S)
            break;

          if (yEnd - 1 == 2 && x == 2) {
            moveInfo[currentIndex].fellDownHole = true;
            if (moveInfo[currentIndex].piece == B)
              youLose = true;
            break;
          }

          if (nextPiece == G || nextPiece == B)
            ++numGreenBlueSeen;

          --yEnd;
        }

        if (moveInfo[currentIndex].fellDownHole)
          moveInfo[currentIndex].yEnd = 2;
        else
          moveInfo[currentIndex].yEnd = yEnd + numGreenBlueSeen;

        if (currentIndex < MAX_MOVABLE_PIECES - 1)
          ++currentIndex;
      }
}

static void TiltBoardRight() {
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  // Start from the rightmost column, and work our way left, looping over each row
  for (uint8_t x = BOARD_WIDTH - 1; x != 255; --x)
    for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].yEnd = y;

        // Increment xEnd until we hit a stopper or the edge, subtracting the number of G's and B's along the way
        uint8_t numGreenBlueSeen = 0;
        uint8_t xEnd = x;

        while (xEnd < BOARD_WIDTH - 1) {
          uint8_t nextPiece = board[y][xEnd + 1];

          if (nextPiece == S)
            break;

          if (xEnd + 1 == 2 && y == 2) {
            moveInfo[currentIndex].fellDownHole = true;
            if (moveInfo[currentIndex].piece == B)
              youLose = true;
            break;
          }

          if (nextPiece == G || nextPiece == B)
            ++numGreenBlueSeen;

          ++xEnd;
        }

        if (moveInfo[currentIndex].fellDownHole)
          moveInfo[currentIndex].xEnd = 2;
        else
          moveInfo[currentIndex].xEnd = xEnd - numGreenBlueSeen;

        if (currentIndex < MAX_MOVABLE_PIECES - 1)
          ++currentIndex;
      }
}

static void TiltBoardDown() {
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  // Start from the bottommost row, and work our way up, looping over each column
  for (uint8_t y = BOARD_HEIGHT - 1; y != 255; --y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].xEnd = x;

        // Increment yEnd until we hit a stopper or the edge, subtracting the number of G's and B's along the way
        uint8_t numGreenBlueSeen = 0;
        uint8_t yEnd = y;

        while (yEnd < BOARD_HEIGHT - 1) {
          uint8_t nextPiece = board[yEnd + 1][x];

          if (nextPiece == S)
            break;

          if (yEnd + 1 == 2 && x == 2) {
            moveInfo[currentIndex].fellDownHole = true;
            if (moveInfo[currentIndex].piece == B)
              youLose = true;
            break;
          }

          if (nextPiece == G || nextPiece == B)
            ++numGreenBlueSeen;

          ++yEnd;
        }

        if (moveInfo[currentIndex].fellDownHole)
          moveInfo[currentIndex].yEnd = 2;
        else
          moveInfo[currentIndex].yEnd = yEnd - numGreenBlueSeen;

        if (currentIndex < MAX_MOVABLE_PIECES - 1)
          ++currentIndex;
      }
}

static void UpdateBoardAfterMove()
{
  // Remove start pieces from the board
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    board[moveInfo[move].yStart][moveInfo[move].xStart] = 0;
  }
  // Put pieces back on the board
  uint8_t greenCount = 0;
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    if (!moveInfo[move].fellDownHole) {
      board[moveInfo[move].yEnd][moveInfo[move].xEnd] = moveInfo[move].piece;
      if (moveInfo[move].piece == G)
        ++greenCount;
    }
  }
  if (!youLose && greenCount == 0)
    youWin = true;
}

#if 0
static void SameTimeAnimation()
{
  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x2_8 = (xStart + x4_8) / 2;
    uint16_t y2_8 = (yStart + y4_8) / 2;
    uint16_t x1_8 = (xStart + x2_8) / 2;
    uint16_t y1_8 = (yStart + y2_8) / 2;
    MoveSprite(move * 4, x1_8, y1_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x2_8 = (xStart + x4_8) / 2;
    uint16_t y2_8 = (yStart + y4_8) / 2;
    MoveSprite(move * 4, x2_8, y2_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x2_8 = (xStart + x4_8) / 2;
    uint16_t y2_8 = (yStart + y4_8) / 2;
    uint16_t x3_8 = (x2_8 + x4_8) / 2;
    uint16_t y3_8 = (y2_8 + y4_8) / 2;
    MoveSprite(move * 4, x3_8, y3_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    MoveSprite(move * 4, x4_8, y4_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x6_8 = (x4_8 + xEnd) / 2;
    uint16_t y6_8 = (y4_8 + yEnd) / 2;
    uint16_t x5_8 = (x4_8 + x6_8) / 2;
    uint16_t y5_8 = (y4_8 + y6_8) / 2;
    MoveSprite(move * 4, x5_8, y5_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x6_8 = (x4_8 + xEnd) / 2;
    uint16_t y6_8 = (y4_8 + yEnd) / 2;
    MoveSprite(move * 4, x6_8, y6_8, 2, 2);
  }

  WaitVsync(1);
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    uint16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    uint16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);
    uint16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);
    uint16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);
    uint16_t x4_8 = (xStart + xEnd) / 2;
    uint16_t y4_8 = (yStart + yEnd) / 2;
    uint16_t x6_8 = (x4_8 + xEnd) / 2;
    uint16_t y6_8 = (y4_8 + yEnd) / 2;
    uint16_t x7_8 = (x6_8 + xEnd) / 2;
    uint16_t y7_8 = (y6_8 + yEnd) / 2;
    MoveSprite(move * 4, x7_8, y7_8, 2, 2);
  }

  WaitVsync(1);
}
#endif

#define NEAREST_SCREEN_PIXEL(p)  (((p) + (1 << (FP_SHIFT - 1))) >> FP_SHIFT)

#define FP_SHIFT                    (2)
#define WORLD_FPS                   (24)
#define WORLD_METER                 (10 << FP_SHIFT)
#define WORLD_GRAVITY               (615)
#define WORLD_MAX_VELOCITY          (WORLD_METER * 16)

static void UpdatePhysicsLeft()
{
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    int16_t ddx = 0;
    ddx -= WORLD_GRAVITY;

    // Integrate the X forces to calculate the new position (x,y) and the new velocity (dx)
    moveInfo[move].x += (moveInfo[move].dx / WORLD_FPS);
    moveInfo[move].dx += (ddx / WORLD_FPS);
    if (moveInfo[move].dx < -WORLD_MAX_VELOCITY)
      moveInfo[move].dx = -WORLD_MAX_VELOCITY;

    int16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);

    if (moveInfo[move].x <= (xEnd << FP_SHIFT)) {
      moveInfo[move].x = xEnd << FP_SHIFT;
      moveInfo[move].dx = 0;
      if (!moveInfo[move].doneMoving && moveInfo[move].xStart != moveInfo[move].xEnd) {
        if (!moveInfo[move].fellDownHole)
          ++numSlidersHitEndStops;
        else
          playFellDownHoleSound = true;
      }
      moveInfo[move].doneMoving = true;
      if (moveInfo[move].fellDownHole) {
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
        /*if (moveInfo[move].piece == B && !playedYouLoseSound) {
          playedYouLoseSound = true;
          TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
          }*/
      }
    }
  }
}

static void UpdatePhysicsUp()
{
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    int16_t ddy = 0;
    ddy -= WORLD_GRAVITY;

    // Integrate the Y forces to calculate the new position (x,y) and the new velocity (dy)
    moveInfo[move].y += (moveInfo[move].dy / WORLD_FPS);
    moveInfo[move].dy += (ddy / WORLD_FPS);
    if (moveInfo[move].dy < -WORLD_MAX_VELOCITY)
      moveInfo[move].dy = -WORLD_MAX_VELOCITY;

    int16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);

    if (moveInfo[move].y <= (yEnd << FP_SHIFT)) {
      moveInfo[move].y = yEnd << FP_SHIFT;
      moveInfo[move].dy = 0;
      if (!moveInfo[move].doneMoving && moveInfo[move].yStart != moveInfo[move].yEnd) {
        if (!moveInfo[move].fellDownHole)
          ++numSlidersHitEndStops;
        else
          playFellDownHoleSound = true;
      }
      moveInfo[move].doneMoving = true;
      if (moveInfo[move].fellDownHole) {
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
        /*if (moveInfo[move].piece == B && !playedYouLoseSound) {
          playedYouLoseSound = true;
          TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
          }*/
      }
    }
  }
}

static void UpdatePhysicsRight()
{
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    int16_t ddx = 0;
    ddx += WORLD_GRAVITY;

    // Integrate the X forces to calculate the new position (x,y) and the new velocity (dx)
    moveInfo[move].x += (moveInfo[move].dx / WORLD_FPS);
    moveInfo[move].dx += (ddx / WORLD_FPS);
    if (moveInfo[move].dx > WORLD_MAX_VELOCITY)
      moveInfo[move].dx = WORLD_MAX_VELOCITY;

    int16_t xEnd = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2);

    if (moveInfo[move].x >= (xEnd << FP_SHIFT)) {
      moveInfo[move].x = xEnd << FP_SHIFT;
      moveInfo[move].dx = 0;
      if (!moveInfo[move].doneMoving && moveInfo[move].xStart != moveInfo[move].xEnd) {
        if (!moveInfo[move].fellDownHole)
          ++numSlidersHitEndStops;
        else
          playFellDownHoleSound = true;
      }
      moveInfo[move].doneMoving = true;
      if (moveInfo[move].fellDownHole) {
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
        /*if (moveInfo[move].piece == B && !playedYouLoseSound) {
          playedYouLoseSound = true;
          TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
          }*/
      }
    }
  }
}

static void UpdatePhysicsDown()
{
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    int16_t ddy = 0;
    ddy += WORLD_GRAVITY;

    // Integrate the Y forces to calculate the new position (x,y) and the new velocity (dy)
    moveInfo[move].y += (moveInfo[move].dy / WORLD_FPS);
    moveInfo[move].dy += (ddy / WORLD_FPS);
    if (moveInfo[move].dy > WORLD_MAX_VELOCITY)
      moveInfo[move].dy = WORLD_MAX_VELOCITY;

    int16_t yEnd = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2);

    if (moveInfo[move].y >= (yEnd << FP_SHIFT)) {
      moveInfo[move].y = yEnd << FP_SHIFT;
      moveInfo[move].dy = 0;
      if (!moveInfo[move].doneMoving && moveInfo[move].yStart != moveInfo[move].yEnd) {
        if (!moveInfo[move].fellDownHole)
          ++numSlidersHitEndStops;
        else
          playFellDownHoleSound = true;
      }
      moveInfo[move].doneMoving = true;
      if (moveInfo[move].fellDownHole) {
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
        /*if (moveInfo[move].piece == B && !playedYouLoseSound) {
          playedYouLoseSound = true;
          TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
          }*/
      }
    }
  }
}

static void UpdatePhysics(uint8_t direction)
{
  if (direction == BTN_LEFT)
    UpdatePhysicsLeft();
  else if (direction == BTN_UP)
    UpdatePhysicsUp();
  else if (direction == BTN_RIGHT)
    UpdatePhysicsRight();
  else if (direction == BTN_DOWN)
    UpdatePhysicsDown();
}

static void GravityAnimation(uint8_t direction)
{
  // Initialize the starting positions and velocities with sub-pixel precision
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    int16_t xStart = TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2);
    int16_t yStart = TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2);

    moveInfo[move].x = xStart << FP_SHIFT;
    moveInfo[move].y = yStart << FP_SHIFT;
    moveInfo[move].dx = moveInfo[move].dy = 0;

    // The following code block would give each piece an initial velocity
    /*
    if (direction == BTN_LEFT)
      moveInfo[move].dx = -WORLD_METER * 1;
    else if (direction == BTN_UP)
      moveInfo[move].dy = -WORLD_METER * 1;
    else if (direction == BTN_RIGHT)
      moveInfo[move].dx = WORLD_METER * 1;
    else if (direction == BTN_DOWN)
      moveInfo[move].dy = WORLD_METER * 1;
    */
    moveInfo[move].doneMoving = false;
  }

  bool allDoneMoving;
  do {
    allDoneMoving = true;
    numSlidersHitEndStops = 0;
    playFellDownHoleSound = false;

    UpdatePhysics(direction);
    if (numSlidersHitEndStops > 0)
      TriggerNote(SFX_CHANNEL, SFX_SLIDER_STOP, SFX_SPEED_SLIDER_STOP, SFX_VOL_SLIDER_STOP + 24 * numSlidersHitEndStops);
      //TriggerFx(FX_THUD, 80, true);

    if (playFellDownHoleSound)
      TriggerNote(SFX_CHANNEL, SFX_SLIDER_HOLE, SFX_SPEED_SLIDER_HOLE, SFX_VOL_SLIDER_HOLE + 24 * 5);


    for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
      if (moveInfo[move].piece == 0)
        break;

        MoveSprite(move * 4,
                   NEAREST_SCREEN_PIXEL(moveInfo[move].x),
                   NEAREST_SCREEN_PIXEL(moveInfo[move].y),
                   2, 2);

        allDoneMoving &= moveInfo[move].doneMoving;
    }

    WaitVsync(1);
  } while (!allDoneMoving);
}

// This function expects moveInfo to be populated before calling
// Maybe make it so if you press a direction while it's animating, it skips directly to the end?
static void AnimateBoard(uint8_t direction)
{
  // Hide all sprites
  for (uint8_t i = 0; i < MAX_SPRITES; ++i)
    sprites[i].y = SCREEN_TILES_V * TILE_HEIGHT; // OFF_SCREEN;

  // Turn all G and B tile pieces into sprites, and draw a blank grid where they were
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;

    MapSprite2(move * 4, moveInfo[move].piece == G ? map_green : map_blue, 0);
    MoveSprite(move * 4,
               TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2),
               TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2),
               2, 2);

    DrawMap(GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xStart * 2,
            GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yStart * 2,
            MapBoardPositionToGridTileMap(moveInfo[move].xStart, moveInfo[move].yStart));
  }

  // Animate them
  //SameTimeAnimation();
  GravityAnimation(direction);

  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    MoveSprite(move * 4,
               TILE_WIDTH * (GAMEBOARD_ACTIVE_AREA_LEFT + moveInfo[move].xEnd * 2),
               TILE_HEIGHT * (GAMEBOARD_ACTIVE_AREA_TOP + moveInfo[move].yEnd * 2),
               2, 2);
  }

  // Turn all G and B sprites back into tile pieces in their end locations, and hide all the sprites
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    if (moveInfo[move].piece == G) // Draw green sliders first in case both fell in the exit hole at the same time
      DrawMap(GAMEBOARD_ACTIVE_AREA_LEFT + GAMEPIECE_WIDTH * moveInfo[move].xEnd,
              GAMEBOARD_ACTIVE_AREA_TOP + GAMEPIECE_HEIGHT * moveInfo[move].yEnd,
              MapPieceToTileMapForBoardPosition(moveInfo[move].piece, moveInfo[move].xEnd, moveInfo[move].yEnd));
  }

  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    if (moveInfo[move].piece == B) // Ensure blue sliders that fell in the hole will always be drawn on top
      DrawMap(GAMEBOARD_ACTIVE_AREA_LEFT + GAMEPIECE_WIDTH * moveInfo[move].xEnd,
              GAMEBOARD_ACTIVE_AREA_TOP + GAMEPIECE_HEIGHT * moveInfo[move].yEnd,
              MapPieceToTileMapForBoardPosition(moveInfo[move].piece, moveInfo[move].xEnd, moveInfo[move].yEnd));
  }

  for (uint8_t i = 0; i < MAX_SPRITES; ++i)
    sprites[i].y = SCREEN_TILES_V * TILE_HEIGHT; // OFF_SCREEN;
}

const uint8_t rf_title[] PROGMEM = {
  0x30, 0x78, 0xec, 0xe4, 0xfe, 0xc2, 0xc2, 0x00, // A
  0x3e, 0x62, 0x32, 0x7e, 0xe2, 0xf2, 0x7e, 0x00, // B
  0x7c, 0xc6, 0x02, 0x02, 0xc6, 0xfe, 0x7c, 0x00, // C
  0x3c, 0x62, 0xc2, 0xc2, 0xe2, 0xfe, 0x7e, 0x00, // D
  0x7c, 0xc6, 0x02, 0x7e, 0x02, 0xfe, 0xfc, 0x00, // E
  0x7c, 0xc6, 0x02, 0x7e, 0x06, 0x06, 0x06, 0x00, // F
  0x7c, 0xc6, 0x02, 0x02, 0xf2, 0xe6, 0xbc, 0x00, // G
  0x42, 0xc2, 0xc2, 0xfe, 0xc2, 0xc6, 0xc6, 0x00, // H
  0x10, 0x30, 0x30, 0x30, 0x38, 0x38, 0x38, 0x00, // I
  0x60, 0xc0, 0xc0, 0xc0, 0xe2, 0xfe, 0x7c, 0x00, // J
  0x64, 0x36, 0x16, 0x3e, 0x76, 0xe6, 0xe6, 0x00, // K
  0x04, 0x06, 0x02, 0x02, 0x82, 0xfe, 0x7c, 0x00, // L
  0x62, 0xf6, 0xde, 0xca, 0xc2, 0xc6, 0x46, 0x00, // M
  0x46, 0xce, 0xda, 0xf2, 0xe2, 0xc6, 0x46, 0x00, // N
  0x70, 0xcc, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00, // O
  0x7c, 0xc6, 0xe2, 0x7e, 0x06, 0x06, 0x04, 0x00, // P
  0x7c, 0xe2, 0xc2, 0xc2, 0x7a, 0xe6, 0xdc, 0x00, // Q
  0x7c, 0xc6, 0xc2, 0x7e, 0x1a, 0xf2, 0xe2, 0x00, // R
  0x3c, 0x62, 0x02, 0x7c, 0xc0, 0xe6, 0x7c, 0x00, // S
  0x7c, 0xfe, 0x12, 0x10, 0x18, 0x18, 0x18, 0x00, // T
  0x40, 0xc2, 0xc2, 0xc2, 0xe6, 0x7e, 0x3c, 0x00, // U
  0x40, 0xc2, 0xc2, 0xc4, 0x64, 0x38, 0x18, 0x00, // V
  0x40, 0xc2, 0xd2, 0xda, 0xda, 0xfe, 0x6c, 0x00, // W
  0x80, 0xc6, 0x6e, 0x38, 0x38, 0xec, 0xc6, 0x00, // X
  0x80, 0x86, 0xcc, 0x78, 0x30, 0x1c, 0x0c, 0x00, // Y
  0x7c, 0xc0, 0x60, 0x10, 0x0c, 0xfe, 0x7c, 0x00, // Z
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x0c, // ,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, // .
  0x3c, 0x42, 0x99, 0x85, 0x99, 0x42, 0x3c, 0x00, // (c)
  0x7c, 0xc2, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00, // 0
  0x7c, 0xe6, 0xc4, 0x60, 0x18, 0xfc, 0x7e, 0x00, // 2
  0x3c, 0x62, 0x02, 0x7e, 0xc2, 0xfe, 0x7c, 0x00, // 6
};

const char HELP_TXT[] PROGMEM = {
  0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x4f, 0x42, 0x4a, 0x45, 0x43, 0x54, 0x49, 0x56, 0x45, 0x0a, 0x0a, 0x20,
  0x47, 0x45, 0x54, 0x20, 0x41, 0x4c, 0x4c, 0x20, 0x47, 0x52, 0x45, 0x45,
  0x4e, 0x20, 0x53, 0x4c, 0x49, 0x44, 0x45, 0x52, 0x53, 0x20, 0x54, 0x4f,
  0x20, 0x46, 0x41, 0x4c, 0x4c, 0x0a, 0x20, 0x54, 0x48, 0x52, 0x4f, 0x55,
  0x47, 0x48, 0x20, 0x54, 0x48, 0x45, 0x20, 0x45, 0x58, 0x49, 0x54, 0x20,
  0x48, 0x4f, 0x4c, 0x45, 0x20, 0x49, 0x4e, 0x20, 0x54, 0x48, 0x45, 0x0a,
  0x20, 0x43, 0x45, 0x4e, 0x54, 0x45, 0x52, 0x2c, 0x20, 0x57, 0x49, 0x54,
  0x48, 0x4f, 0x55, 0x54, 0x20, 0x4c, 0x45, 0x54, 0x54, 0x49, 0x4e, 0x47,
  0x20, 0x41, 0x4e, 0x59, 0x0a, 0x20, 0x42, 0x4c, 0x55, 0x45, 0x20, 0x53,
  0x4c, 0x49, 0x44, 0x45, 0x52, 0x53, 0x20, 0x46, 0x41, 0x4c, 0x4c, 0x20,
  0x54, 0x48, 0x52, 0x4f, 0x55, 0x47, 0x48, 0x2e, 0x0a, 0x0a, 0x0a, 0x20,
  0x49, 0x46, 0x20, 0x59, 0x4f, 0x55, 0x20, 0x47, 0x45, 0x54, 0x20, 0x54,
  0x52, 0x41, 0x50, 0x50, 0x45, 0x44, 0x2c, 0x20, 0x55, 0x53, 0x45, 0x20,
  0x54, 0x48, 0x45, 0x0a, 0x20, 0x53, 0x54, 0x41, 0x52, 0x54, 0x20, 0x4d,
  0x45, 0x4e, 0x55, 0x20, 0x54, 0x4f, 0x20, 0x52, 0x45, 0x53, 0x45, 0x54,
  0x20, 0x54, 0x4f, 0x4b, 0x45, 0x4e, 0x53, 0x2e, 0x00
};

const char pgm_TITLE[] PROGMEM = "TILTaPUZZLE";
const char pgm_UZEBOX_GAME[] PROGMEM = "UZEBOXaGAMEa]_^_`aMATTaPANDINA";
const char pgm_INVENTED_BY1[] PROGMEM = "INVENTEDaBYaVESAaTIMONEN[";
const char pgm_INVENTED_BY2[] PROGMEM = "TIMOaJOKITALO";
const char pgm_START_GAME[] PROGMEM = "STARTaGAME";
const char pgm_HOW_TO_PLAY[] PROGMEM = "HOWaTOaPLAY";
const char pgm_YOU_LOSE[] PROGMEM = "YOUaLOSE";
const char pgm_YOU_WIN[] PROGMEM = "YOUaWIN";

// Loads 'len' compressed 'ramfont' tiles into user ram tiles starting at 'user_ram_tile_start' using 'fg_color' and 'bg_color'
void RamFont_Load(const uint8_t* ramfont, uint8_t user_ram_tile_start, uint8_t len, uint8_t fg_color, uint8_t bg_color)
{
  //SetUserRamTilesCount(len); // commented out to avoid flickering of the current level, call manually before this function is called
  if (fg_color == bg_color) { // This saves 10's of thousands of clock cycles when the condition is met
    uint8_t* ramTile = GetUserRamTile(user_ram_tile_start);
    memset(ramTile, fg_color, len * 64);
    return;
  }
  for (uint8_t tile = 0; tile < len; ++tile) {
    uint8_t* ramTile = GetUserRamTile(user_ram_tile_start + tile);
    for (uint8_t row = 0; row < 8; ++row) {
      uint8_t rowstart = row * 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[tile * 8 + row]);
      uint8_t bit = 0;
      for (uint8_t bitmask = 1; bitmask != 0; bitmask <<= 1) {
        if (data & bitmask)
          ramTile[rowstart + bit] = fg_color;
        else
          ramTile[rowstart + bit] = bg_color;
        ++bit;
      }
    }
  }
}

// Ensure that 4 adjacent letters will pixel fade in differently
const uint8_t sparkle_effect[][64] PROGMEM =
{
 { 6, 33, 27, 42, 39, 47, 5, 22, 35, 36, 17, 23, 20, 11, 63, 10, 8, 14, 12, 60, 61, 9, 38, 43, 15, 0, 1, 50, 19, 37, 52, 51,
   54, 24, 16, 30, 59, 53, 58, 34, 2, 40, 4, 25, 31, 57, 7, 41, 28, 3, 18, 21, 29, 56, 48, 26, 13, 44, 32, 49, 45, 46, 62, 55, },
 { 26, 35, 44, 21, 60, 22, 52, 18, 53, 54, 58, 36, 20, 55, 25, 10, 42, 1, 2, 28, 37, 31, 0, 8, 51, 41, 5, 30, 59, 14, 39, 38,
   47, 24, 17, 27, 56, 32, 23, 13, 40, 49, 50, 15, 61, 43, 19, 3, 34, 4, 48, 33, 7, 63, 29, 11, 62, 45, 57, 9, 6, 46, 16, 12, },
 { 40, 57, 39, 22, 14, 43, 42, 3, 60, 52, 24, 46, 53, 6, 13, 54, 51, 55, 16, 33, 63, 21, 31, 28, 18, 25, 32, 9, 11, 36, 38,
   15, 7, 61, 49, 17, 45, 20, 0, 50, 34, 10, 47, 41, 23, 19, 5, 59, 44, 2, 35, 62, 26, 29, 58, 37, 30, 27, 4, 48, 1, 12, 8, 56, },
 { 24, 4, 37, 59, 20, 61, 42, 17, 6, 12, 9, 32, 5, 15, 33, 21, 57, 60, 31, 29, 2, 16, 62, 7, 45, 1, 3, 43, 27, 63, 53, 11, 36,
   41, 39, 40, 19, 58, 8, 56, 25, 48, 55, 28, 0, 50, 14, 44, 26, 18, 38, 52, 54, 49, 51, 46, 13, 22, 35, 23, 30, 47, 34, 10, },
};

// Instead of uncompressing all pixels at once for the RAM font, unveil it randomly pixel-by-pixel until it is fully displayed
void RamFont_SparkleLoad(const uint8_t* ramfont, const uint8_t user_ram_tile_start, const uint8_t len, const uint8_t fg_color)
{
  uint8_t shift[8];
  uint8_t bit = 0;
  for (uint8_t bitmask = 1; bitmask != 0; bitmask <<= 1)
    shift[bit++] = bitmask;

  // Loop over all the tiles
  for (uint8_t pixel = 0; pixel < 64; ++pixel) {
    for (uint8_t tile = 0; tile < len; ++tile) {
      uint8_t* ramTile = GetUserRamTile(user_ram_tile_start + tile);
      uint8_t target_pixel = (uint8_t)pgm_read_byte(&sparkle_effect[tile % 4][pixel]);
      uint8_t row = target_pixel / 8;
      uint8_t offset = target_pixel % 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[tile * 8 + row]);
      if (data & shift[offset])
        ramTile[target_pixel] = fg_color;
    }
    if (pixel % 2) // speed it up
      WaitVsync(1);
  }
}

void RamFont_Print_Minus_A(uint8_t x, uint8_t y, const char* message, uint8_t len)
{
  for (uint8_t i = 0; i < len; ++i) {
    int8_t tileno = (int8_t)pgm_read_byte(&message[i]);
    if (tileno >= 0)
      SetRamTile(x + i, y, tileno - 'A');
  }
}

int main()
{
  ClearVram();
  SetTileTable(titlescreen);

  BUTTON_INFO buttons;
  memset(&buttons, 0, sizeof(BUTTON_INFO));

  InitMusicPlayer(patches);
  //StartSong(midisong);
  //StopSong();

 title_screen:
  ClearVram();
  SetTileTable(titlescreen);

  // Load the entire alphabet + extras
  SetUserRamTilesCount(RAM_TILES_COUNT);
  RamFont_Load(rf_title, 0, sizeof(rf_title) / 8, 0xFF, 0x00);

  DrawMap(4, 2, map_logo);
  //RamFont_Print_Minus_A(10, 5, pgm_TITLE, sizeof(pgm_TITLE) - 1);
  RamFont_Print_Minus_A(11, 14, pgm_START_GAME, sizeof(pgm_START_GAME) - 1);
  RamFont_Print_Minus_A(11, 16, pgm_HOW_TO_PLAY, sizeof(pgm_HOW_TO_PLAY) - 1);
  RamFont_Print_Minus_A(1, 22, pgm_UZEBOX_GAME, sizeof(pgm_UZEBOX_GAME) - 1);
  RamFont_Print_Minus_A(3, 24, pgm_INVENTED_BY1, sizeof(pgm_INVENTED_BY1) - 1);
  RamFont_Print_Minus_A(15, 25, pgm_INVENTED_BY2, sizeof(pgm_INVENTED_BY2) - 1);

  /* BEGIN TITLE SCREEN SCOPE */ {
    int8_t prev_selection;
    int8_t selection = 0;

#define TILE_T_BG 0
#define TILE_T_SELECTION 1

    for (;;) {
      // Draw the menu selection indicator
      SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
      prev_selection = selection;

      // Read the current state of the player's controller
      buttons.prev = buttons.held;
      buttons.held = ReadJoypad(0);
      buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
      buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

      if (buttons.pressed & BTN_START)
        break;

      if (buttons.pressed & BTN_UP) {
        if (selection > 0) {
          selection--;
          TriggerNote(SFX_CHANNEL, SFX_SLIDER_STOP, SFX_SPEED_SLIDER_STOP, SFX_VOL_SLIDER_STOP);
          SetTile(9, 14 + 2 * prev_selection, TILE_T_BG);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      } else if (buttons.pressed & BTN_DOWN) {
        if (selection < 1) {
          selection++;
          TriggerNote(SFX_CHANNEL, SFX_SLIDER_STOP, SFX_SPEED_SLIDER_STOP, SFX_VOL_SLIDER_STOP);
          SetTile(9, 14 + 2 * prev_selection, TILE_T_BG);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      }

      WaitVsync(1);
    }

    //TriggerNote(SFX_CHANNEL, SFX_SLIDER_HOLE, SFX_SPEED_SLIDER_HOLE, SFX_VOL_SLIDER_HOLE);

    if (selection == 0)
      goto start_game;
  } /* END TITLE SCREEN SCOPE */

  /* BEGIN HOW TO PLAY SCOPE */ {
    ClearVram();
    SetTileTable(tileset);

    // Load the entire alphabet + extras
    SetUserRamTilesCount(RAM_TILES_COUNT);
    RamFont_Load(rf_title, 0, sizeof(rf_title) / 8, 0x00, 0x00); // Unused 4 tiles at the end

    int16_t in = 0;
    uint8_t letter = 0;
    uint8_t prev_letter = 0;
    for (uint16_t out = 0; out < SCREEN_TILES_H * SCREEN_TILES_V; ++out, ++in) {
      uint8_t x = out % SCREEN_TILES_H;
      prev_letter = letter;
      letter = pgm_read_byte(&HELP_TXT[in]);
      uint8_t output;
      switch (letter) {
      case 0x00:
        out = SCREEN_TILES_H * SCREEN_TILES_V;
        continue;
        break;
      case 0x0A:
        out += SCREEN_TILES_H + SCREEN_TILES_H - 1 - x;
        if (prev_letter == 0x0A)
          out -= SCREEN_TILES_H;
        continue;
        break;
      case ' ':
        output = RAM_TILES_COUNT;
        break;
      case ',':
        output = '[' - 'A';
        break;
      case '.':
        output = '\\' - 'A';
        break;
      default:
        output = letter - 'A';
      }
      vram[out + SCREEN_TILES_H * 2] = output;
    }

    //TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
    RamFont_SparkleLoad(rf_title, 0, sizeof(rf_title) / 8, 0xFF);

    for (;;) {
      // Read the current state of the player's controller
      buttons.prev = buttons.held;
      buttons.held = ReadJoypad(0);
      buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
      buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

      if (buttons.pressed & BTN_START) {
        //TriggerNote(SFX_CHANNEL, SFX_ZAP, SFX_SPEED_ZAP, SFX_VOL_ZAP);
        RamFont_SparkleLoad(rf_title, 0, sizeof(rf_title) / 8, 0x00);
        goto title_screen;
      }

      WaitVsync(1);
    }
  } /* END HOW TO PLAY SCOPE */

 start_game:
  ClearVram();
  SetTileTable(tileset);
  SetSpritesTileBank(0, tileset);
  SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

  // Idea: Maybe create a looping MIDI file that is just the noise channel
  //       so it can be started when pieces begin to slide, and be stopped
  //       when all the pieces stop sliding?
  //ResumeSong();

  currentLevel = 1;
  LoadLevel(currentLevel);

  for (;;) {
    WaitVsync(1);

    // Read the current state of the player's controller
    buttons.prev = buttons.held;
    buttons.held = ReadJoypad(0);
    buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
    buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

    // Crude level select for now
    if (buttons.pressed == BTN_SR) {
      currentLevel++;
      if (currentLevel > 40)
        currentLevel = 1;
      LoadLevel(currentLevel);
    } else if (buttons.pressed == BTN_SL) {
      currentLevel--;
      if (currentLevel < 1)
        currentLevel = 40;
      LoadLevel(currentLevel);
    }

    // Beat Level 31
    if (buttons.pressed == BTN_LEFT) {
      TiltBoardLeft();
      UpdateBoardAfterMove();
      AnimateBoard(BTN_LEFT);
    } else if (buttons.pressed == BTN_UP) {
      TiltBoardUp();
      UpdateBoardAfterMove();
      AnimateBoard(BTN_UP);
    } else if (buttons.pressed == BTN_RIGHT) {
      TiltBoardRight();
      UpdateBoardAfterMove();
      AnimateBoard(BTN_RIGHT);
    } else if (buttons.pressed == BTN_DOWN) {
      TiltBoardDown();
      UpdateBoardAfterMove();
      AnimateBoard(BTN_DOWN);
    }

    if (youLose || youWin) {
      // Load the entire alphabet + extras
      SetUserRamTilesCount(RAM_TILES_COUNT);
      RamFont_Load(rf_title, 0, sizeof(rf_title) / 8, 0xFF, 0x00);

      if (youLose) {
        RamFont_Print_Minus_A(12, 23, pgm_YOU_LOSE, sizeof(pgm_YOU_LOSE) - 1);
      } else if (youWin) {
        //TriggerNote(SFX_CHANNEL, SFX_WIN, SFX_SPEED_WIN, SFX_VOL_WIN);
        RamFont_Print_Minus_A(12, 23, pgm_YOU_WIN, sizeof(pgm_YOU_WIN) - 1);
      }

      for (;;) {
        WaitVsync(1);

        // Read the current state of the player's controller
        buttons.prev = buttons.held;
        buttons.held = ReadJoypad(0);
        buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
        buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

        if (buttons.pressed) {
          // Erase Win/Lose message
          for (uint8_t i = 0; i < sizeof(pgm_YOU_LOSE) - 1; ++i)
            SetTile(12 + i, 23, 0);

          SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

          if (youLose) {
            LoadLevel(currentLevel);
            break;
          } else if (youWin){
            currentLevel = (currentLevel + 1) % 40;
            LoadLevel(currentLevel);
            break;
          }
        }
      }
    }
  }
}
