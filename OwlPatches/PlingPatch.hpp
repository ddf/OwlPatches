/**

AUTHOR:
    (c) 2025 Damien Quartz

LICENSE:
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.


DESCRIPTION:

*/

#pragma once

#include "MonochromeScreenPatch.h"
#include "PatchParameter.h"
#include "basicmaths.h"

typedef uint32_t count_t;
typedef uint16_t coord_t;

template<>
IntParameter::PatchParameter();

template<>
IntParameter& IntParameter::operator=(const IntParameter& other);

class Paddle
{
  coord_t cx, cy;
  coord_t hw, hh;
public:
  Paddle(const coord_t cx, const coord_t cy, const coord_t hw, const coord_t hh): cx(cx), cy(cy), hw(hw), hh(hh) {}
  void draw(MonochromeScreenBuffer& screen) const;
  void moveTo(coord_t y);
  bool pointInside(const coord_t x, const coord_t y) const;
};

class Ball
{
  float cx, cy;
  float dx, dy;
  coord_t r;
public:
  Ball(const coord_t cx, const coord_t cy, const float dx, const float dy, const coord_t r): cx(cx), cy(cy), dx(dx), dy(dy), r(r) {}
  void draw(MonochromeScreenBuffer& screen) const;
  void moveBy(float sx, float sy);
  void collideWith(const Paddle& paddle, const float dt);
};

static constexpr coord_t PAD_HW = 1;
static constexpr coord_t PAD_HH = 8;
static constexpr coord_t BALL_R = 1;
// hard-coding until I can get this implemented in the patch class
static constexpr coord_t SCREEN_W = 128;
static constexpr coord_t SCREEN_H = 64;

class PlingPatch final : public MonochromeScreenPatch
{
  IntParameter pinPadLeft;
  IntParameter pinPadRight;
  
  Paddle padLeft{ PAD_HW*8, 0, PAD_HW, PAD_HH };
  Paddle padRight{ SCREEN_W-PAD_HW*8, 0, PAD_HW, PAD_HH };
  Ball ballLeft{ BALL_R, SCREEN_H/2, 100, 100, BALL_R };
  Ball ballRight{ SCREEN_W-BALL_R, SCREEN_H/2, -100, -150, BALL_R };
  
public:
  PlingPatch()
  {
    pinPadLeft = getIntParameter("Pad Left", PAD_HH, SCREEN_H-PAD_HH);
    pinPadRight = getIntParameter("Pad Right", PAD_HH, SCREEN_H-PAD_HH);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const count_t size = audio.getSize();
    const float dt = 1.0f / getSampleRate();

    padLeft.moveTo(static_cast<coord_t>(pinPadLeft.getValue()));
    padRight.moveTo(static_cast<coord_t>(pinPadRight.getValue()));

    FloatArray inputLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray inputRight = audio.getSamples(RIGHT_CHANNEL);
    
    for (count_t i = 0; i < size; i++)
    {
      ballLeft.moveBy(dt*abs(inputLeft[i]), dt*abs(inputRight[i]));
      ballLeft.collideWith(padLeft, dt);
      ballLeft.collideWith(padRight, dt);
      
      // ballRight.moveBy(dt*abs(inputRight[i]));
      // ballRight.collideWith(padLeft, dt);
      // ballRight.collideWith(padRight, dt);
    }
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.clear();
    
    padLeft.draw(screen);
    padRight.draw(screen);
    ballLeft.draw(screen);
    // ballRight.draw(screen);
  }
};

inline void Paddle::draw(MonochromeScreenBuffer& screen) const
{
  const int x = cx;
  const int y = screen.getHeight() - cy;
  screen.fillRectangle(x-hw, y-hh, hw*2, hh*2, WHITE);
}

inline void Paddle::moveTo(const coord_t y)
{
  cy = y;
}
inline bool Paddle::pointInside(const coord_t x, const coord_t y) const
{
  return !(x < cx-hw || x > cx+hw || y < cy-hh || y > cy+hh); 
}

inline void Ball::draw(MonochromeScreenBuffer& screen) const
{
  const int x = static_cast<int>(cx);
  const int y = screen.getHeight() - static_cast<int>(cy);
  screen.fillRectangle(x-r, y-r, r*2, r*2, WHITE);
}

inline void Ball::moveBy(const float sx, const float sy)
{
  cx += dx*sx;
  if (cx < 0)
  {
    cx = -cx;
    dx *= -1;
  }
  else if (cx > SCREEN_W)
  {
    cx = SCREEN_W - (cx - SCREEN_W);
    dx *= -1;
  }
  
  cy += dy*sy;
  if (cy < 0)
  {
    cy = -cy;
    dy *= -1;
  }
  else if (cy > SCREEN_H)
  {
    cy = SCREEN_H - (cy - SCREEN_H);
    dy *= -1;
  }
}

inline void Ball::collideWith(const Paddle& paddle, const float dt)
{
  coord_t lx = static_cast<coord_t>(cx) - r;
  coord_t rx = lx+r+r;
  coord_t by = static_cast<coord_t>(cy) - r;
  coord_t ty = by+r+r;
  const float step = dt*10;
  if (dx < 0)
  {
    if (paddle.pointInside(lx, ty) || paddle.pointInside(lx, by))
    {
      dx *= -1;
      moveBy(step, step);
    }
  }
  else
  {
    if (paddle.pointInside(rx, ty) || paddle.pointInside(rx, by))
    {
      dx *= -1;
      moveBy(step, step);
    }
  }
}