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
    A Trigger and CV generator based on Pong.

    Parameters A and B control the speed at which the left and right paddles move.
    The paddles switch directions automatically when they reach the edge of the screen.
    CV Out A tracks the vertical position of the left paddle.
    CV Out B tracks the vertical position of the right paddle.
    The left audio input controls the speed of the ball's motion along the x-axis.
    The right audio input controls the speed of the ball's motion along the y-axis.
    Negative signals slow the ball down along that axis, positive speed it up with an exponential response.
    The ball will reflect off of all four sides of the screen (walls) as well as the paddles.
    Reflections give the ball a small burst of speed that decays over time (i.e. walls and paddles are "bouncy").
    When the ball reflects off of a wall, a trigger is emitted at Gate Out 1.
    When the ball reflects off of a paddle, a trigger is emitted at Gate Out 2.
    The left audio output is the normalized x coordinate of the ball.
    The right audio output is the normalized Y coordinate of the ball.
    (0,0) is the center of the screen with positive coordinates to the right and above, negative to the left and below.
*/

#pragma once

#include "MonochromeScreenPatch.h"
#include "PatchParameter.h"
#include "basicmaths.h"
#include "Easing.h"
#include "PatchParameterDescription.h"

typedef uint32_t count_t;
typedef uint16_t coord_t;

class Paddle
{
  coord_t cx, hw, hh;
  float   cy, d, s;
public:
  Paddle(const coord_t cx, const coord_t cy, const coord_t hw, const coord_t hh, const float d) : cx(cx), hw(hw), hh(hh), cy(cy), d(d), s(0) {}
  void draw(MonochromeScreenBuffer& screen) const;
  void moveTo(coord_t y);
  void tick(const float dt);
  bool pointInside(const coord_t x, const coord_t y) const;
  float getPositionNormalized() const;
  void  setSpeed(const float v) { s = v;}
  float getSpeed() const {return s;}
};

class Ball
{
  float cx, cy;
  float dx, dy;
  float vx, vy;
  coord_t r;
public:
  Ball(const coord_t cx, const coord_t cy, const coord_t r): cx(cx), cy(cy), dx(2), dy(1), vx(0), vy(0), r(r) {}
  void draw(MonochromeScreenBuffer& screen) const;
  bool tick(float sx, float sy, float dt);
  bool collideWith(const Paddle& paddle, const float dt);
  float getX() const { return cx; }
  float getY() const { return cy; }
};

// hard-coding until I can get this implemented in MonochromeScreenPatch
static constexpr coord_t SCREEN_W = 128;
static constexpr coord_t SCREEN_H = 64;
static constexpr coord_t PAD_HW = 1;
static constexpr coord_t PAD_HH = 8;
static constexpr float   PAD_MAX_SPEED = 220.0f;
static constexpr coord_t BALL_R = 1;
static constexpr float   BALL_DRAG = 0.00001f;
static constexpr float   BALL_MAX_SPEED = SCREEN_H*440.0f;

class PlingPatch final : public MonochromeScreenPatch
{
  FloatParameter pinPadLeft;
  FloatParameter pinPadRight;
  OutputParameter poutPadLeft;
  OutputParameter poutPadRight;
  
  Paddle padLeft{ PAD_HW*8, SCREEN_H/2, PAD_HW, PAD_HH, 1 };
  Paddle padRight{ SCREEN_W-PAD_HW*8, SCREEN_H/2, PAD_HW, PAD_HH, -1 };
  Ball ballLeft{ BALL_R, SCREEN_H/2, BALL_R };
  Ball ballRight{ SCREEN_W-BALL_R, SCREEN_H/2, BALL_R };
  
public:
  PlingPatch()
  : poutPadLeft(this, { "Pad Left", PARAMETER_F })
  , poutPadRight(this, {"Pad Right", PARAMETER_G})
  {
    pinPadLeft = getFloatParameter("Pad Left", 0, 1, 0.25f, 0.95f, 0, Patch::LIN);
    pinPadRight = getFloatParameter("Pad Right", 0, 1, 0.25f, 0.95f, 0, Patch::LIN);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const count_t size = audio.getSize();
    const float dt = 1.0f / getSampleRate();
    const float padLeftSpeed  = PAD_MAX_SPEED*pinPadLeft.getValue();
    const float padRightSpeed = PAD_MAX_SPEED*pinPadRight.getValue();

    padLeft.setSpeed(padLeftSpeed);
    padRight.setSpeed(padRightSpeed);

    FloatArray inputLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray inputRight = audio.getSamples(RIGHT_CHANNEL);

    FloatArray outputLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray outputRight = audio.getSamples(RIGHT_CHANNEL);

    count_t padCollideSample = size;
    count_t wallCollideSample = size;
    for (count_t i = 0; i < size; i++)
    {
      padLeft.tick(dt);
      padRight.tick(dt);
      
      // pad move may have caused overlap with the ball
      bool padCollide = ballLeft.collideWith(padLeft, dt);
      padCollide |= ballLeft.collideWith(padRight, dt);
      
      const float sl = 1.0f - Easing::expoOut(inputLeft[i]*0.5f + 0.5f);
      const float sr = 1.0f - Easing::expoOut(inputRight[i]*0.5f + 0.5f);
      const bool wallCollide = ballLeft.tick(BALL_MAX_SPEED*sl, BALL_MAX_SPEED*sr, dt);

      // ball move may have caused overlap with a pad
      padCollide |= ballLeft.collideWith(padLeft, dt);
      padCollide |= ballLeft.collideWith(padRight, dt);

      outputLeft[i] = Easing::interp(-1.f, 1.f, ballLeft.getX()/SCREEN_W);
      outputRight[i] = Easing::interp(-1.f, 1.f, ballLeft.getY()/SCREEN_H);

      if (padCollide && padCollideSample == size)
      {
        padCollideSample = i;
      }

      if (wallCollide && wallCollideSample == size)
      {
        wallCollideSample = i;
      }
      
      // ballRight.moveBy(dt*abs(inputRight[i]));
      // ballRight.collideWith(padLeft, dt);
      // ballRight.collideWith(padRight, dt);
    }

    setButton(BUTTON_1, wallCollideSample < size, static_cast<uint16_t>(wallCollideSample));
    setButton(BUTTON_2, padCollideSample < size, static_cast<uint16_t>(padCollideSample));
    poutPadLeft.setValue(padLeft.getPositionNormalized());
    poutPadRight.setValue(padRight.getPositionNormalized());
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
  const int y = screen.getHeight() - static_cast<int>(cy);
  screen.fillRectangle(x-hw, y-hh, hw*2, hh*2, WHITE);
  
  // screen.setCursor(x, 10);
  // screen.print(cy);
}

inline void Paddle::moveTo(const coord_t y)
{
  cy = y;
}

inline void Paddle::tick(const float dt)
{
  cy += d*s*dt;
  const float hhf = hh;
  
  const float top = cy - hhf;
  if (top < 0)
  {
    cy = hhf;
    d  *= -1;
  }

  const float btm = cy + hhf;
  if (btm > SCREEN_H)
  {
    cy = SCREEN_H - hhf;
    d  *= -1;
  }
}

inline bool Paddle::pointInside(const coord_t x, const coord_t y) const
{
  const coord_t cyc = static_cast<coord_t>(cy);
  return !(x < cx-hw || x > cx+hw || y < cyc-hh || y > cyc+hh); 
}

inline float Paddle::getPositionNormalized() const
{
  return (cy - static_cast<float>(hh)) / static_cast<float>(SCREEN_H - hh - hh);
}

inline void Ball::draw(MonochromeScreenBuffer& screen) const
{
  const int x = static_cast<int>(cx);
  const int y = screen.getHeight() - static_cast<int>(cy);
  screen.fillRectangle(x-r, y-r, r*2, r*2, WHITE);
}

inline bool Ball::tick(const float sx, const float sy, const float dt)
{
  bool collidedX = false;
  bool collidedY = false;
  
  cx += dx*clamp(sx+vx, 0, BALL_MAX_SPEED*50)*dt;
  if (cx < 0)
  {
    cx = -cx;
    dx *= -1;
    collidedX = true;
  }
  else if (cx > SCREEN_W)
  {
    cx = SCREEN_W - (cx - SCREEN_W);
    dx *= -1;
    collidedX = true;
  }
  
  cy += dy*clamp(sy+vy, 0, BALL_MAX_SPEED*50)*dt;
  if (cy < 0)
  {
    cy = -cy;
    dy *= -1;
    collidedY = true;
  }
  else if (cy > SCREEN_H)
  {
    cy = SCREEN_H - (cy - SCREEN_H);
    dy *= -1;
    collidedY = true;
  }

  // wall collisions reduce velocity a little bit
  if (collidedX || collidedY)
  {
    vx *= 0.99f;
    vy *= 0.99f;
  }
  else
  {
    // drag
    vx = Easing::interp(vx, 0.0f, BALL_DRAG);
    vy = Easing::interp(vy, 0.0f, BALL_DRAG);
  }
  
  return collidedX || collidedY;
}

inline bool Ball::collideWith(const Paddle& paddle, const float dt)
{
  coord_t lx = static_cast<coord_t>(cx) - r;
  coord_t rx = lx+r+r;
  coord_t by = static_cast<coord_t>(cy) - r;
  coord_t ty = by+r+r;
  const float step = 10.0f;
  bool collided = false;
  if (dx < 0)
  {
    collided = paddle.pointInside(lx, ty) || paddle.pointInside(lx, by);
  }
  else
  {
    collided = paddle.pointInside(rx, ty) || paddle.pointInside(rx, by);
  }

  if (collided)
  {
    dx *= -1;
    vx += paddle.getSpeed()*0.25f;
    vy += paddle.getSpeed()*0.25f;
    tick(step, step, dt);
    return true;
  }

  return collided;
}