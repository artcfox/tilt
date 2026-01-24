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

#include "data/tileset.inc"
//#include "data/spriteset.inc"
//#include "data/patches.inc"
//#include "data/midisong.h"

#define NELEMS(x) (sizeof(x)/sizeof(x[0]))

typedef struct {
  uint16_t held;
  uint16_t prev;
  uint16_t pressed;
  uint16_t released;
} __attribute__ ((packed)) BUTTON_INFO;

#define TILE_BACKGROUND 0
#define TILE_FOREGROUND 1

#define BOARD_HEIGHT 5
#define BOARD_WIDTH 5
#define LEVEL_SIZE (BOARD_WIDTH * BOARD_HEIGHT)
#define BOARD_OFFSET_IN_LEVEL 0

uint8_t currentLevel;

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
    else
      map_piece = map_blue;
    break;
  }
  return map_piece;
}

static void LoadLevel(const uint8_t level)
{
  DrawMap(ENTIRE_GAMEBOARD_LEFT, ENTIRE_GAMEBOARD_TOP, map_board);

  const uint16_t levelOffset = (level - 1) * LEVEL_SIZE;
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = (uint8_t)pgm_read_byte(&levelData[(levelOffset + BOARD_OFFSET_IN_LEVEL) + y * BOARD_WIDTH + x]);
      board[y][x] = piece;

      if (piece == S || piece == G || piece == B)
        DrawMap(ENTIRE_GAMEBOARD_LEFT + 2 + GAMEPIECE_WIDTH * x,
                ENTIRE_GAMEBOARD_TOP + 2 + GAMEPIECE_HEIGHT * y,
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
} __attribute__ ((packed));

#define MAX_MOVABLE_PIECES 5

MOVE_INFO moveInfo[MAX_MOVABLE_PIECES];

static void TiltBoardLeft() {
  // Start from the leftmost column, loop over the rows until you find a G or B.
  // Then loop from its (x,y) left, counting the number of G's and B's until you reach a S or the edge.
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
    for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].yEnd = y;
        // Decrement x until you hit position 0, or a stopper, counting the number of G's and B's along the way, adding them at the end
        uint8_t numGreenBlueSeen = 0;
        uint8_t xEnd = x;

        while (xEnd > 0) {
          uint8_t nextPiece = board[y][xEnd - 1];

          if (nextPiece == S)
            break;

          if (xEnd - 1 == 2 && y == 2) {
            // Fell down hole
            moveInfo[currentIndex].fellDownHole = true;
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
  // Start from the top row, and loop over the columns until you find a G or B.
  // Then loop from its (x,y) up, counting the number of G's and B's until you reach a S or the edge.
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].xEnd = x;
        // Decrement y until you hit position 4, or a stopper, counting the number of G's and B's along the way, and adding them at the end
        uint8_t numGreenBlueSeen = 0;
        uint8_t yEnd = y;

        while (yEnd > 0) {
          uint8_t nextPiece = board[yEnd - 1][x];

          if (nextPiece == S)
            break;

          if (yEnd - 1 == 2 && x == 2) {
            // Fell down hole
            moveInfo[currentIndex].fellDownHole = true;
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
  // Start from the rightmost column, loop over the rows until you find a G or B.
  // Then loop from its (x,y) right, counting the number of G's and B's until you reach a S or the edge
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  for (uint8_t x = BOARD_WIDTH - 1; x != 255; --x)
    for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].yEnd = y;
        // Increment x until you hit position 4, or a stopper, counting the number of G's and B's along the way, and subtracting them at the end
        uint8_t numGreenBlueSeen = 0;
        uint8_t xEnd = x;

        while (xEnd < BOARD_WIDTH - 1) {
          uint8_t nextPiece = board[y][xEnd + 1];

          if (nextPiece == S)
            break;

          if (xEnd + 1 == 2 && y == 2) {
            // Fell down hole
            moveInfo[currentIndex].fellDownHole = true;
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
  // Start from the bottom row, and loop over the columns until you find a G or B.
  // Then loop from its (x,y) down, counting the number of G's and B's until you reach a S or the edge.
  memset(moveInfo, 0, MAX_MOVABLE_PIECES * sizeof(MOVE_INFO));
  uint8_t currentIndex = 0;

  for (uint8_t y = BOARD_HEIGHT - 1; y != 255; --y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x)
      if (board[y][x] == G || board[y][x] == B) {
        moveInfo[currentIndex].piece = board[y][x];
        moveInfo[currentIndex].xStart = x;
        moveInfo[currentIndex].yStart = y;
        moveInfo[currentIndex].xEnd = x;
        // Increment y until you hit position 4, or a stopper, counting the number of G's and B's along the way, and subtracting them at the end
        uint8_t numGreenBlueSeen = 0;
        uint8_t yEnd = y;

        while (yEnd < BOARD_HEIGHT - 1) {
          uint8_t nextPiece = board[yEnd + 1][x];

          if (nextPiece == S)
            break;

          if (yEnd + 1 == 2 && x == 2) {
            // Fell down hole
            moveInfo[currentIndex].fellDownHole = true;
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
  for (uint8_t move = 0; move < MAX_MOVABLE_PIECES; ++move) {
    if (moveInfo[move].piece == 0)
      break;
    if (!moveInfo[move].fellDownHole)
      board[moveInfo[move].yEnd][moveInfo[move].xEnd] = moveInfo[move].piece;
  }
}

static void RedrawBoard()
{
  DrawMap(ENTIRE_GAMEBOARD_LEFT, ENTIRE_GAMEBOARD_TOP, map_board);
  for (uint8_t y = 0; y < BOARD_HEIGHT; ++y)
    for (uint8_t x = 0; x < BOARD_WIDTH; ++x) {
      uint8_t piece = board[y][x];
      if (piece == S || piece == G || piece == B)
        DrawMap(ENTIRE_GAMEBOARD_LEFT + 2 + GAMEPIECE_WIDTH * x,
                ENTIRE_GAMEBOARD_TOP + 2 + GAMEPIECE_HEIGHT * y,
                MapPieceToTileMapForBoardPosition(piece, x, y));
    }
}

int main()
{
  ClearVram();
  SetTileTable(tileset);

  BUTTON_INFO buttons;
  memset(&buttons, 0, sizeof(BUTTON_INFO));

  //InitMusicPlayer(patches);

  //StartSong(midisong);
  //StopSong();

  goto start_game;

 start_game:
  ClearVram();
  SetTileTable(tileset);
  //SetSpritesTileBank(0, mysprites);
  SetSpritesTileBank(1, tileset);

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

    if (buttons.pressed == BTN_LEFT) {
      TiltBoardLeft();
      UpdateBoardAfterMove();
      RedrawBoard();
    } else if (buttons.pressed == BTN_UP) {
      TiltBoardUp();
      UpdateBoardAfterMove();
      RedrawBoard();
    } else if (buttons.pressed == BTN_RIGHT) {
      TiltBoardRight();
      UpdateBoardAfterMove();
      RedrawBoard();
    } else if (buttons.pressed == BTN_DOWN) {
      TiltBoardDown();
      UpdateBoardAfterMove();
      RedrawBoard();
    }
  }
}
