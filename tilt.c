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
#include "levels.h"

typedef struct {
  uint16_t held;
  uint16_t prev;
  uint16_t pressed;
  uint16_t released;
} __attribute__ ((packed)) BUTTON_INFO;

#define GAME_USER_RAM_TILES_COUNT 0

#define TILE_NUM_BACKGROUND 0
#define TILE_NUM_GREEN 1
#define TILE_NUM_YELLOW 2
#define TILE_NUM_BLUE 3
#define TILE_NUM_RED 4
#define TILE_NUM_DPAD_LEFT 5
#define TILE_NUM_DPAD_RIGHT 6
#define TILE_NUM_START_DIGITS 7

#define BOARD_HEIGHT 5
#define BOARD_WIDTH 5
#define LEVEL_SIZE (BOARD_WIDTH * BOARD_HEIGHT)
#define BOARD_OFFSET_IN_LEVEL 0

#define ENTIRE_GAMEBOARD_LEFT ((SCREEN_TILES_H - MAP_BOARD_WIDTH) / 2)
#define ENTIRE_GAMEBOARD_TOP ((SCREEN_TILES_V - MAP_BOARD_HEIGHT) / 2)
#define GAMEBOARD_ACTIVE_AREA_LEFT (ENTIRE_GAMEBOARD_LEFT + 2)
#define GAMEBOARD_ACTIVE_AREA_TOP (ENTIRE_GAMEBOARD_TOP + 2)
#define GAMEPIECE_WIDTH 2
#define GAMEPIECE_HEIGHT 2

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

uint8_t GetDifficultyTileForLevel(uint8_t level)
{
  if ((level >= 1) && (level <= 10))
    return TILE_NUM_GREEN;
  else if ((level >= 11) && (level <= 20))
    return TILE_NUM_YELLOW;
  else if ((level >= 21) && (level <= 30))
    return TILE_NUM_BLUE;
  else if ((level >= 31) && (level <= 40))
    return TILE_NUM_RED;
  return 0;
}

static void LoadLevel(const uint8_t level)
{
  youWin = false;
  youLose = false;
  playedYouLoseSound = false;

  // Draw PUZZLE ##
  DrawMap(ENTIRE_GAMEBOARD_LEFT, ENTIRE_GAMEBOARD_TOP - 3, map_tilt_puzzle);
  uint8_t digits[2] = {0};
  BCD_addConstant(digits, 2, level);
  SetTile(ENTIRE_GAMEBOARD_LEFT + MAP_TILT_PUZZLE_WIDTH + 1, ENTIRE_GAMEBOARD_TOP - 3, TILE_NUM_START_DIGITS + digits[1]);
  SetTile(ENTIRE_GAMEBOARD_LEFT + MAP_TILT_PUZZLE_WIDTH + 2, ENTIRE_GAMEBOARD_TOP - 3, TILE_NUM_START_DIGITS + digits[0]);

  uint8_t difficultyStripe = GetDifficultyTileForLevel(level);
  for (uint8_t i = ENTIRE_GAMEBOARD_LEFT; i < ENTIRE_GAMEBOARD_LEFT + MAP_BOARD_WIDTH; ++i)
    SetTile(i, ENTIRE_GAMEBOARD_TOP - 2, difficultyStripe);

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
      if (moveInfo[move].fellDownHole)
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
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
      if (moveInfo[move].fellDownHole)
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
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
      if (moveInfo[move].fellDownHole)
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
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
      if (moveInfo[move].fellDownHole)
        MapSprite2(move * 4, moveInfo[move].piece == G ? map_green_h : map_blue_h, 0);
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
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x0c, // ,    (use '[')
  0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, // .    (use '\\')
  0x3c, 0x42, 0x99, 0x85, 0x99, 0x42, 0x3c, 0x00, // (c)  (use ']')
  0x7c, 0xc2, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00, // 0    (use '^')
  0x7c, 0xe6, 0xc4, 0x60, 0x18, 0xfc, 0x7e, 0x00, // 2    (use '_')
  0x3c, 0x62, 0x02, 0x7e, 0xc2, 0xfe, 0x7c, 0x00, // 6    (use '`')
                                                  // TILE_NUM_BACKGROUND (use 'a' - ASCII corresponding to RAM_TILES_COUNT + 'A')
};

// If RAM_TILES_COUNT is changed, the 'a' in the strings below need to change (see comment above)
const char pgm_TITLE[] PROGMEM = "TILTaPUZZLE";
const char pgm_UZEBOX_GAME[] PROGMEM = "UZEBOXaGAMEa]_^_`aMATTaPANDINA";
const char pgm_INVENTED_BY1[] PROGMEM = "INVENTEDaBYaVESAaTIMONEN[";
const char pgm_INVENTED_BY2[] PROGMEM = "TIMOaJOKITALO";
const char pgm_START_GAME[] PROGMEM = "STARTaGAME";
const char pgm_HOW_TO_PLAY[] PROGMEM = "HOWaTOaPLAY";
const char pgm_FAIL[] PROGMEM = "FAIL";
const char pgm_PASS[] PROGMEM = "PASS";

// Generated using ~/uzebox/bin/bin2hex data/HELP.TXT (and terminating with a 0x00)
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

// This allows the use of PROGMEM char* strings, rather than a uint8_t array of bytes
void RamFont_Print_Minus_A(uint8_t x, uint8_t y, const char* message, uint8_t len)
{
  for (uint8_t i = 0; i < len; ++i) {
    int8_t tileno = (int8_t)pgm_read_byte(&message[i]);
    if (tileno >= 0)
      SetRamTile(x + i, y, tileno - 'A');
  }
}

// Compressed ram font data for other characters: *RETUNSOKPZL
// run ramfont/main ramfont-popup.png to generate
const uint8_t rf_popup[] PROGMEM = {
  0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00,
  0x3e, 0x63, 0x61, 0x3f, 0x0d, 0x79, 0x71, 0x00,
  0x3e, 0x63, 0x01, 0x3f, 0x01, 0x7f, 0x7e, 0x00,
  0x3e, 0x7f, 0x09, 0x08, 0x0c, 0x0c, 0x0c, 0x00,
  0x20, 0x61, 0x61, 0x61, 0x73, 0x3f, 0x1e, 0x00,
  0x23, 0x67, 0x6d, 0x79, 0x71, 0x63, 0x23, 0x00,
  0x1e, 0x31, 0x01, 0x3e, 0x60, 0x73, 0x3e, 0x00,
  0x38, 0x66, 0x61, 0x61, 0x71, 0x7f, 0x3e, 0x00,
  0x32, 0x1b, 0x0b, 0x1f, 0x3b, 0x73, 0x73, 0x00,
  0x3e, 0x63, 0x71, 0x3f, 0x03, 0x03, 0x02, 0x00,
  0x3e, 0x60, 0x30, 0x08, 0x06, 0x7f, 0x3e, 0x00,
  0x02, 0x03, 0x01, 0x01, 0x41, 0x7f, 0x3e, 0x00,
};

// Compressed ram font data for popup border
// run ramfont/main ramfont-popup-border.png to generate
const uint8_t rf_popup_border[] PROGMEM = {
  0xff, 0xff, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xff, 0x7f, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
  0x03, 0x03, 0x03, 0x03, 0x03, 0xff, 0xff, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00,
  0x60, 0x60, 0x60, 0x60, 0x60, 0x7f, 0x7f, 0x00,
};

// Compressed ram font data for digits 0-9
// run ramfont/main ramfont-digits.png to generate
const uint8_t rf_digits[] PROGMEM = {
  0x7c, 0xc2, 0xc2, 0xc2, 0xe2, 0xfe, 0x7c, 0x00,
  0x10, 0x38, 0x38, 0x38, 0x38, 0x38, 0x10, 0x00,
  0x7c, 0xe6, 0xc4, 0x60, 0x18, 0xfc, 0x7e, 0x00,
  0x7c, 0x60, 0x30, 0xfc, 0xc0, 0xfe, 0x7c, 0x00,
  0x60, 0x70, 0x68, 0x64, 0xfe, 0xfc, 0x60, 0x00,
  0x7e, 0x06, 0x7c, 0xc0, 0xc0, 0xfe, 0x7c, 0x00,
  0x3c, 0x62, 0x02, 0x7e, 0xc2, 0xfe, 0x7c, 0x00,
  0x7c, 0xc2, 0xc0, 0x70, 0x18, 0x1c, 0x1c, 0x00,
  0x7c, 0x66, 0x3c, 0x7c, 0xc6, 0xfe, 0x7c, 0x00,
  0x7c, 0xe2, 0xc2, 0xfc, 0xc0, 0xc2, 0x7c, 0x00,
};

// Defines for the ram fonts used in the popup menu // *RETUNSOKPZL
#define RF_ASTERISK (GAME_USER_RAM_TILES_COUNT)
#define RF_R (GAME_USER_RAM_TILES_COUNT + 1)
#define RF_E (GAME_USER_RAM_TILES_COUNT + 2)
#define RF_T (GAME_USER_RAM_TILES_COUNT + 3)
#define RF_U (GAME_USER_RAM_TILES_COUNT + 4)
#define RF_N (GAME_USER_RAM_TILES_COUNT + 5)
#define RF_S (GAME_USER_RAM_TILES_COUNT + 6)
#define RF_O (GAME_USER_RAM_TILES_COUNT + 7)
#define RF_K (GAME_USER_RAM_TILES_COUNT + 8)
#define RF_P (GAME_USER_RAM_TILES_COUNT + 9)
#define RF_Z (GAME_USER_RAM_TILES_COUNT + 10)
#define RF_L (GAME_USER_RAM_TILES_COUNT + 11)

#define RF_B_TL (GAME_USER_RAM_TILES_COUNT + 12)
#define RF_B_T (GAME_USER_RAM_TILES_COUNT + 13)
#define RF_B_TR (GAME_USER_RAM_TILES_COUNT + 14)
#define RF_B_L (GAME_USER_RAM_TILES_COUNT + 15)
#define RF_B_R (GAME_USER_RAM_TILES_COUNT + 16)
#define RF_B_BL (GAME_USER_RAM_TILES_COUNT + 17)
#define RF_B_B (GAME_USER_RAM_TILES_COUNT + 18)
#define RF_B_BR (GAME_USER_RAM_TILES_COUNT + 19)

#define RF_OnesPlace (GAME_USER_RAM_TILES_COUNT + 20)
#define RF_TensPlace (GAME_USER_RAM_TILES_COUNT + 21)

const uint8_t pgm_P_RETURN[] PROGMEM         = { RF_R, RF_E, RF_T, RF_U, RF_R, RF_N };
const uint8_t pgm_P_RESET_TOKENS[] PROGMEM   = { RF_R, RF_E, RF_S, RF_E, RF_T, RAM_TILES_COUNT, RF_T, RF_O, RF_K, RF_E, RF_N, RF_S };
const uint8_t pgm_P_PUZZLE[] PROGMEM        = { RF_P, RF_U, RF_Z, RF_Z, RF_L, RF_E };

void RamFont_Print(uint8_t x, uint8_t y, const uint8_t* message, uint8_t len)
{
  for (uint8_t i = 0; i < len; ++i) {
    int8_t tileno = (int8_t)pgm_read_byte(&message[i]);
    if (tileno >= 0)
      SetRamTile(x + i, y, tileno);
  }
}

uint8_t RamFont_GetLevelColor(uint8_t level)
{
  if ((level >= 1) && (level <= 10))
    return 0x20; // TILE_NUM_GREEN
  else if ((level >= 11) && (level <= 20))
    return 0x2F; // TILE_NUM_YELLOW
  else if ((level >= 21) && (level <= 30))
    return 0xD0; // TILE_NUM_BLUE
  else if ((level >= 31) && (level <= 40))
    return 0x0E; // TILE_NUM_RED
  return 0xFF;
}

void RamFont_Load2Digits(const uint8_t* ramfont, uint8_t ramfont_index, uint8_t number, uint8_t fg_color, uint8_t bg_color)
{
  uint8_t digits[2] = {0};
  BCD_addConstant(digits, 2, number);

  for (uint8_t tile = 0; tile < 2; ++tile) {
    uint8_t* ramTile = GetUserRamTile(tile + ramfont_index);
    for (uint8_t row = 0; row < 8; ++row) {
      uint8_t rowstart = row * 8;
      uint8_t data = (uint8_t)pgm_read_byte(&ramfont[digits[tile] * 8 + row]);
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

void TileToRam(uint16_t toff, uint16_t roff, uint16_t len, const char* tiles, uint8_t* ramTile)
{   // copy len number of tiles from tiles to ram tiles, use Abs below to give an absolute offset.
   toff = toff << 6; // multiply by 64 to convert from ram tile index to actual address for pixel 0
   roff = roff << 6;
   len  = len << 6;
   while (len--)
     ramTile[roff++] = pgm_read_byte(tiles + toff++);
}

int main()
{
  ClearVram();
  SetTileTable(titlescreen);

  BUTTON_INFO buttons;
  memset(&buttons, 0, sizeof(BUTTON_INFO));

  InitMusicPlayer(patches);

 title_screen:
  ClearVram();
  SetTileTable(titlescreen);

  // Load the entire alphabet + extras
  SetUserRamTilesCount(RAM_TILES_COUNT);
  RamFont_Load(rf_title, 0, sizeof(rf_title) / 8, 0xFF, 0x00);

  WaitVsync(1); // This avoids tearing during VSYNC when drawing the logo after loading RAM fonts

  DrawMap(4, 2, map_logo);
  RamFont_Print_Minus_A(11, 14, pgm_START_GAME, sizeof(pgm_START_GAME) - 1);
  RamFont_Print_Minus_A(11, 16, pgm_HOW_TO_PLAY, sizeof(pgm_HOW_TO_PLAY) - 1);
  RamFont_Print_Minus_A(1, 22, pgm_UZEBOX_GAME, sizeof(pgm_UZEBOX_GAME) - 1);
  RamFont_Print_Minus_A(3, 24, pgm_INVENTED_BY1, sizeof(pgm_INVENTED_BY1) - 1);
  RamFont_Print_Minus_A(15, 25, pgm_INVENTED_BY2, sizeof(pgm_INVENTED_BY2) - 1);

  /* BEGIN TITLE SCREEN SCOPE */ {
    int8_t prev_selection;
    int8_t selection = 0;

#define TITLE_T_BACKGROUND 0
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

      if ((buttons.pressed & BTN_START && buttons.held == BTN_START) ||
          (buttons.pressed & BTN_A && buttons.held == BTN_A))
        break;

      if (buttons.pressed & BTN_UP) {
        if (selection > 0) {
          selection--;
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
          SetTile(9, 14 + 2 * prev_selection, TITLE_T_BACKGROUND);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      } else if (buttons.pressed & BTN_DOWN) {
        if (selection < 1) {
          selection++;
          TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
          SetTile(9, 14 + 2 * prev_selection, TITLE_T_BACKGROUND);
          SetTile(9, 14 + 2 * selection, TILE_T_SELECTION);
          prev_selection = selection;
        }
      }

      WaitVsync(1);
    }

    TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);

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

    RamFont_SparkleLoad(rf_title, 0, sizeof(rf_title) / 8, 0xFF);

    for (;;) {
      // Read the current state of the player's controller
      buttons.prev = buttons.held;
      buttons.held = ReadJoypad(0);
      buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
      buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

      if ((buttons.pressed & BTN_START && buttons.held == BTN_START) ||
          (buttons.pressed & BTN_A && buttons.held == BTN_A)) {
        TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
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

  currentLevel = 1;
  LoadLevel(currentLevel);

  for (;;) {
    WaitVsync(1);

    // Read the current state of the player's controller
    buttons.prev = buttons.held;
    buttons.held = ReadJoypad(0);
    buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
    buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

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
      SetUserRamTilesCount(RAM_TILES_COUNT);
      RamFont_Load(rf_title, 0, sizeof(rf_title) / 8, youWin ? 0x20 : 0x0E, 0x00);

      if (youLose)
        RamFont_Print_Minus_A(14, 23, pgm_FAIL, sizeof(pgm_FAIL) - 1);
      else if (youWin)
        RamFont_Print_Minus_A(14, 23, pgm_PASS, sizeof(pgm_PASS) - 1);

      for (;;) {
        WaitVsync(1);

        // Read the current state of the player's controller
        buttons.prev = buttons.held;
        buttons.held = ReadJoypad(0);
        buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
        buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

        if ((buttons.pressed & BTN_START && buttons.held == BTN_START) ||
            (buttons.pressed & BTN_A && buttons.held == BTN_A)) {

          // Erase PASS/FAIL message
          for (uint8_t i = 0; i < sizeof(pgm_FAIL) - 1; ++i)
            SetTile(14 + i, 23, 0);

          SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

          if (youLose) {
            LoadLevel(currentLevel);
            break;
          } else if (youWin) {
            currentLevel = (currentLevel + 1) % 40;
            LoadLevel(currentLevel);
            break;
          }
        }
      }
    } else {
      // -------------------- PROCESS POPUP MENU --------------------
      // If we pressed the START button with no other buttons held down
      if ((buttons.pressed & BTN_START && buttons.held == BTN_START) ||
          (buttons.pressed & BTN_A && buttons.held == BTN_A)) {
#define MENU_WIDTH 18
#define MENU_HEIGHT 5
#define MENU_START_X 7
#define MENU_START_Y 12
#define TILE_MENU_BG TILE_NUM_BACKGROUND

        // Play a sound effect that indicates the popup menu, unfortunately if music is playing, a TriggerFx won't work
        TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);

        // Save what is behind the popup menu
        uint8_t backing[MENU_HEIGHT][MENU_WIDTH];
        for (uint8_t y = 0; y < MENU_HEIGHT; ++y)
          for (uint8_t x = 0; x < MENU_WIDTH; ++x)
            backing[y][x] = GetTile(MENU_START_X + x, MENU_START_Y + y);

        // Put all the stuff we'll need to display for the menu into user ram tiles

        WaitVsync(1); // Ensures any sprites have a chance to hide before we reuse their ram tiles, avoiding glitches
        SetUserRamTilesCount(RAM_TILES_COUNT);

        // Load the popup menu into ram tiles starting at 0
        uint8_t rf_popup_len = sizeof(rf_popup) / 8;
        RamFont_Load(rf_popup, GAME_USER_RAM_TILES_COUNT, rf_popup_len, 0xFF, 0x00);

        // Load the popup menu border after that (starting at the user ram tile: rf_popup_len) with a different fg color
        uint8_t rf_popup_border_len = sizeof(rf_popup_border) / 8;
        RamFont_Load(rf_popup_border, GAME_USER_RAM_TILES_COUNT + rf_popup_len, rf_popup_border_len, 0xA4, 0x00);

        // Make the top right and bottom left pixels of the border "transparent"
        uint8_t bgTile;
        char bgTilePixel;
        uint8_t* ramTile;

        bgTile = GetTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y);
        bgTilePixel = pgm_read_byte(tileset + bgTile * 64 + 7); // 7 is top right pixel
        ramTile = GetUserRamTile(RF_B_TR); // top right corner in rf_popup
        ramTile[7] = bgTilePixel; // top right pixel of ramTile

        bgTile = GetTile(MENU_START_X, MENU_START_Y + MENU_HEIGHT - 1);
        bgTilePixel = pgm_read_byte(tileset + bgTile * 64 + 56); // 56 is bottom left pixel
        ramTile = GetUserRamTile(RF_B_BL); // bottom left corner in rf_popup
        ramTile[56] = bgTilePixel; // bottom left pixel of ramTile

        // Draw the current level number in the color corresponding to its difficulty
        RamFont_Load2Digits(rf_digits,
                            GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len,
                            currentLevel, RamFont_GetLevelColor(currentLevel), 0x00);

        // Draw the menu background
        Fill(MENU_START_X + 1, MENU_START_Y + 1, MENU_WIDTH - 2, MENU_HEIGHT - 2, TILE_MENU_BG);
        SetRamTile(MENU_START_X, MENU_START_Y, RF_B_TL);
        for (uint8_t i = MENU_START_X + 1; i < MENU_START_X + MENU_WIDTH - 1; ++i)
          SetRamTile(i, MENU_START_Y, RF_B_T);
        SetRamTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y, RF_B_TR);
        for (uint8_t i = MENU_START_Y + 1; i < MENU_START_Y + MENU_HEIGHT - 1; ++i) {
          SetRamTile(MENU_START_X, i, RF_B_L);
          SetRamTile(MENU_START_X + MENU_WIDTH - 1, i, RF_B_R);
        }
        SetRamTile(MENU_START_X, MENU_START_Y + MENU_HEIGHT - 1, RF_B_BL);
        for (uint8_t i = MENU_START_X + 1; i < MENU_START_X + MENU_WIDTH - 1; ++i)
          SetRamTile(i, MENU_START_Y + MENU_HEIGHT - 1, RF_B_B);
        SetRamTile(MENU_START_X + MENU_WIDTH - 1, MENU_START_Y + MENU_HEIGHT - 1, RF_B_BR);

        RamFont_Print(MENU_START_X + 5, MENU_START_Y + 1, pgm_P_RETURN, sizeof(pgm_P_RETURN));
        RamFont_Print(MENU_START_X + 5, MENU_START_Y + 2, pgm_P_RESET_TOKENS, sizeof(pgm_P_RESET_TOKENS));
        RamFont_Print(MENU_START_X + 5, MENU_START_Y + 3, pgm_P_PUZZLE, sizeof(pgm_P_PUZZLE));

        SetRamTile(MENU_START_X + 5 + 8, MENU_START_Y + 3, RF_OnesPlace);
        SetRamTile(MENU_START_X + 5 + 7, MENU_START_Y + 3, RF_TensPlace);

        int8_t prev_selection;
        int8_t selection = 0;
        bool confirmed = false;
        uint8_t selectedLevel = currentLevel;

        // The popup menu has its own run loop
        for (;;) {
          SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
          prev_selection = selection;

          // Read the current state of the player's controller
          buttons.prev = buttons.held;
          buttons.held = ReadJoypad(0);
          buttons.pressed = buttons.held & (buttons.held ^ buttons.prev);
          buttons.released = buttons.prev & (buttons.held ^ buttons.prev);

          if ((buttons.pressed & BTN_START && buttons.held == BTN_START) ||
              (buttons.pressed & BTN_A && buttons.held == BTN_A)) {
            confirmed = true;
            break;
          }

          if (buttons.pressed & BTN_UP) {
            if (selection > 0) {
              selection--;
              TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
              for (uint8_t x = MENU_START_X + 1; x <= MENU_START_X + 3; ++x)
                SetTile(x, MENU_START_Y + 1 + prev_selection, TILE_MENU_BG);
              SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
              prev_selection = selection;
            }
          } else if (buttons.pressed & BTN_DOWN) {
            if (selection < 2) {
              selection++;
              TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
              for (uint8_t x = MENU_START_X + 1; x <= MENU_START_X + 3; ++x)
                SetTile(x, MENU_START_Y + 1 + prev_selection, TILE_MENU_BG);
              SetRamTile(MENU_START_X + 2, MENU_START_Y + 1 + selection, RF_ASTERISK);
              prev_selection = selection;
            }
          }
          if (selection == 2) {
            SetTile(MENU_START_X + 1, MENU_START_Y + 1 + selection, TILE_NUM_DPAD_LEFT);
            SetTile(MENU_START_X + 3, MENU_START_Y + 1 + selection, TILE_NUM_DPAD_RIGHT);
          }

          if ((selection == 2) && ((buttons.pressed & BTN_LEFT) || (buttons.pressed & BTN_RIGHT))) {
            if (buttons.pressed & BTN_LEFT) {
              if (selectedLevel > 1)
                selectedLevel--;
              else
                selectedLevel = 40;

              RamFont_Load2Digits(rf_digits,
                                  GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len,
                                  selectedLevel, RamFont_GetLevelColor(selectedLevel), 0x00);

              SetTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, TILE_NUM_BACKGROUND); // Not strictly necessary, a vestage
              TriggerNote(SFX_CHANNEL, SFX_MOUSE_DOWN, SFX_SPEED_MOUSE_DOWN, SFX_VOL_MOUSE_DOWN);
            } else if (buttons.pressed & BTN_RIGHT) {
              if (selectedLevel < 40)
                selectedLevel++;
              else
                selectedLevel = 1;

              RamFont_Load2Digits(rf_digits,
                                  GAME_USER_RAM_TILES_COUNT + rf_popup_len + rf_popup_border_len,
                                  selectedLevel, RamFont_GetLevelColor(selectedLevel), 0x00);

              SetTile(MENU_START_X + 5 + 11, MENU_START_Y + 3, TILE_NUM_BACKGROUND); // Not strictly necessary, a vestage
              TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);
            }
          }

          WaitVsync(1);
        }

        SetUserRamTilesCount(GAME_USER_RAM_TILES_COUNT);

        // Restore what was behind the popup menu
        for (uint8_t y = 0; y < MENU_HEIGHT; ++y)
          for (uint8_t x = 0; x < MENU_WIDTH; ++x)
            SetTile(MENU_START_X + x, MENU_START_Y + y, backing[y][x]);

        TriggerNote(SFX_CHANNEL, SFX_MOUSE_UP, SFX_SPEED_MOUSE_UP, SFX_VOL_MOUSE_UP);

        if (confirmed && selection == 1)
          LoadLevel(currentLevel);
        else if (confirmed && selectedLevel != currentLevel) {
          currentLevel = selectedLevel;
          LoadLevel(currentLevel);
        }
      }
      // -------------------- END PROCESS POPUP MENU --------------------
    }

  }
}
