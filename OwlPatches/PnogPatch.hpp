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
    When the ball reflects off of a wall, a trigger is emitted at Gate Out 1.
    When the ball reflects off of a paddle, a trigger is emitted at Gate Out 2.
    Reflections off of paddles adds some extra velocity to the ball based on how fast a paddle is moving on contact.
    Reflections off of walls dampen added velocity to slow the ball down.
    A small amount of drag is applied to the added velocity when the ball moves through empty space.
    The left audio output is the normalized x coordinate of the ball.
    The right audio output is the normalized Y coordinate of the ball.
    (0,0) is the center of the screen with positive coordinates to the right and above, negative to the left and below.

    Input Gates & Buttons trigger paddles to move forward towards the center and enlarge.
    Paddles will hold there for as long as the gate is high or button is pressed.
    When the gate goes off they move back to the original position and size.
    The speed at which they moved forward and back is based on the speed of their vertical movement.
*/

#pragma once

#include "AdsrEnvelope.h"
#include "MonochromeScreenPatch.h"
#include "PatchParameter.h"
#include "basicmaths.h"
#include "Easing.h"
#include "PatchParameterDescription.h"

typedef uint32_t count_t;
typedef uint16_t coord_t;

class Paddle
{
  coord_t hw, hh;
  float   cx, cy;
  float   d, s, xo;
public:
  Paddle(const coord_t cx, const coord_t cy, const coord_t hw, const coord_t hh, const float d) : hw(hw), hh(hh), cx(cx), cy(cy), d(d), s(0), xo(0) {}
  void draw(MonochromeScreenBuffer& screen) const;
  void moveTo(coord_t y);
  void tick(const float dt);
  bool pointInside(const coord_t x, const coord_t y) const;
  float getPositionNormalized() const;
  void  setSpeed(const float v) { s = v;}
  float getSpeed() const {return s;}
  float getX() const { return cx; }
  float getY() const { return cy; }
  float getD() const { return d; }
  void setXOff(const float offset) { xo = offset; }
  void setHalfHeight(const coord_t value) { hh = value; }
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
  void moveTo(const float x, const float y) { cx = x; cy = y; }
  void setDirection(const float dxn, const float dyn) { dx = 2*dxn; dy = dyn; }
  void addVelocity(const float avx, const float avy) { vx += avx; vy += avy; }
  void clearVelocity() { vx = vy = 0.0f; }
  float getDX() const { return dx*0.5f; }
  float getDY() const { return dy; }
};

// hard-coding screen size until I can get this implemented in MonochromeScreenPatch
static constexpr coord_t SCREEN_W = 128;
static constexpr coord_t SCREEN_H = 64;
static constexpr coord_t PAD_HW = 1;
static constexpr int PAD_HH_MIN = 2;
static constexpr int PAD_HH_DEF = 4;
static constexpr int PAD_HH_MAX = 12;
static constexpr float PAD_SPEED_MIN = 10.0f;
static constexpr float PAD_SPEED_MAX = 440.0f - PAD_SPEED_MIN;
static constexpr float PAD_ENV_MIN = 0.15f; // in seconds
static constexpr float PAD_ENV_MAX = 1.5f; // in seconds
static constexpr coord_t PAD_MAX_X_OFFSET = SCREEN_W / 4;
static constexpr coord_t BALL_R = 1;
static constexpr float   BALL_DRAG = 0.0001f;
static constexpr float   BALL_SPEED_PARAM_MAX = 55 * SCREEN_H;
static constexpr float   BALL_SPEED_MIN = 6.4f;
static constexpr float   BALL_SPEED_MAX = 24000 * SCREEN_H;
static constexpr float   BALL_KICK_SPEED = BALL_SPEED_MIN*12;

class PnogPatch final : public MonochromeScreenPatch
{
  FloatParameter pinPadLeftSpeed;
  FloatParameter pinPadRightSpeed;
  FloatParameter pinPadLeftXOffset;
  FloatParameter pinPadRightXOffset;
  IntParameter pinPadLeftHalfHeight;
  IntParameter pinPadRightHalfHeight;
  
  OutputParameter poutPadLeft;
  OutputParameter poutPadRight;
  
  Paddle padLeft{ PAD_HW*8,           SCREEN_H/2, PAD_HW, PAD_HH_DEF, 1 };
  Paddle padRight{ SCREEN_W-PAD_HW*8, SCREEN_H/2, PAD_HW, PAD_HH_DEF, -1 };
  Ball ball{ SCREEN_W/2, SCREEN_H/2, BALL_R };

  ExponentialAdsrEnvelope padLeftEnvelope;
  ExponentialAdsrEnvelope padRightEnvelope;
  
public:
  PnogPatch()
    : poutPadLeft(this, { "PL Y", PARAMETER_AA }) // if these don't start here setting the gate outputs interferes with setting these.
    , poutPadRight(this, { "PR Y", PARAMETER_AB }), padLeftEnvelope(getSampleRate()), padRightEnvelope(getSampleRate())
  {
    pinPadLeftSpeed = getFloatParameter("PL Spd", 0, 1, 0.f, 0.95f, 0);
    pinPadRightSpeed = getFloatParameter("PR Spd", 0, 1, 0.f, 0.95f, 0);
    pinPadLeftXOffset = getFloatParameter("PL X Off", 0, PAD_MAX_X_OFFSET, 0, 0.95f, 0);
    pinPadRightXOffset = getFloatParameter("PR X Off", 0, PAD_MAX_X_OFFSET, 0, 0.95f, 0);
    pinPadLeftHalfHeight = getIntParameter("PL HH", PAD_HH_MIN, PAD_HH_MAX, PAD_HH_DEF, 0, 0);
    pinPadRightHalfHeight = getIntParameter("PR HH", PAD_HH_MIN, PAD_HH_MAX, PAD_HH_DEF, 0, 0);

    // HACK: getIntParameter doesn't set the default value, so we do it here.
    constexpr float phhDefault = static_cast<float>(PAD_HH_DEF - PAD_HH_MIN) / (PAD_HH_MAX - PAD_HH_MIN);
    setParameterValue(static_cast<PatchParameterId>(pinPadLeftHalfHeight.id()), phhDefault);
    setParameterValue(static_cast<PatchParameterId>(pinPadRightHalfHeight.id()), phhDefault);
  }

  void processAudio(AudioBuffer& audio) override
  {
    const count_t size = audio.getSize();
    const float dt = 1.0f / getSampleRate();
    const float padLeftSpeed  = PAD_SPEED_MIN + PAD_SPEED_MAX*pinPadLeftSpeed.getValue();
    const float padLeftEnvTime = Easing::interp(PAD_ENV_MAX, PAD_ENV_MIN, pinPadLeftSpeed.getValue(), Easing::expoIn);
    const float padRightSpeed = PAD_SPEED_MIN + PAD_SPEED_MAX*pinPadRightSpeed.getValue();
    const float padRightEnvTime = Easing::interp(PAD_ENV_MAX, PAD_ENV_MIN, pinPadRightSpeed.getValue(), Easing::expoIn);

    padLeft.setSpeed(padLeftSpeed);
    //padLeft.setXOff(pinPadLeftXOffset.getValue());
    //padLeft.setHalfHeight(static_cast<coord_t>(pinPadLeftHalfHeight.getValue()));

    padLeftEnvelope.setAttack(padLeftEnvTime);
    padLeftEnvelope.setRelease(padLeftEnvTime);
    
    padRight.setSpeed(padRightSpeed);
    padRight.setXOff(-pinPadRightXOffset.getValue());
    //padRight.setHalfHeight(static_cast<coord_t>(pinPadRightHalfHeight.getValue()));

    padRightEnvelope.setAttack(padRightEnvTime);
    padRightEnvelope.setRelease(padRightEnvTime);

    FloatArray inputLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray inputRight = audio.getSamples(RIGHT_CHANNEL);

    FloatArray outputLeft = audio.getSamples(LEFT_CHANNEL);
    FloatArray outputRight = audio.getSamples(RIGHT_CHANNEL);

    count_t padCollideSample = size;
    count_t wallCollideSample = size;
    for (count_t i = 0; i < size; i++)
    {
      const float padLeftEnv = padLeftEnvelope.generate();
      const float padRightEnv = padRightEnvelope.generate();
      
      padLeft.setXOff(padLeftEnv*PAD_MAX_X_OFFSET);
      padLeft.setHalfHeight(static_cast<coord_t>(Easing::interp(PAD_HH_DEF, PAD_HH_MAX, padLeftEnv)));
      padLeft.tick(dt);

      padRight.setXOff(-padRightEnv*PAD_MAX_X_OFFSET);
      padRight.setHalfHeight(static_cast<coord_t>(Easing::interp(PAD_HH_DEF, PAD_HH_MAX, padRightEnv)));
      padRight.tick(dt);
      
      // pad move may have caused overlap with the ball
      bool padCollide = ball.collideWith(padLeft, dt);
      padCollide |= ball.collideWith(padRight, dt);
      
      const float sl = 1.0f - Easing::expoOut(inputLeft[i]*0.5f + 0.5f);
      const float sr = 1.0f - Easing::expoOut(inputRight[i]*0.5f + 0.5f);

      // setting speed directly has a nice "creepy" feel to it when speed fluctuates wildly between very fast and very slow
      //const bool wallCollide = ball.tick(BALL_SPEED_PARAM_MAX*sl, BALL_SPEED_PARAM_MAX*sr, dt);

      // but adding velocity is must nicer looking...
      ball.addVelocity((BALL_SPEED_MIN+BALL_SPEED_PARAM_MAX*sl)*dt, (BALL_SPEED_MIN+BALL_SPEED_PARAM_MAX*sr)*dt);
      const bool wallCollide = ball.tick(0, 0, dt);

      // ball move may have caused overlap with a pad
      padCollide |= ball.collideWith(padLeft, dt);
      padCollide |= ball.collideWith(padRight, dt);

      outputLeft[i] = Easing::interp(-1.f, 1.f, ball.getX()/SCREEN_W);
      outputRight[i] = Easing::interp(-1.f, 1.f, ball.getY()/SCREEN_H);

      if (padCollide && padCollideSample == size)
      {
        padCollideSample = i;
      }

      if (wallCollide && wallCollideSample == size)
      {
        wallCollideSample = i;
      }
    }
    
    poutPadLeft.setValue(padLeft.getPositionNormalized());
    poutPadRight.setValue(padRight.getPositionNormalized());

    setButton(BUTTON_1, wallCollideSample < size, static_cast<uint16_t>(wallCollideSample));
    setButton(BUTTON_2, padCollideSample < size, static_cast<uint16_t>(padCollideSample));
  }
  
  void processScreen(MonochromeScreenBuffer& screen) override
  {
    screen.clear();

    // screen.setCursor(0, 20);
    // screen.print(pinPadLeftHalfHeight.getValue());
    // screen.print("   ");
    // screen.print(pinPadRightHalfHeight.getValue());
    
    padLeft.draw(screen);
    padRight.draw(screen);
    ball.draw(screen);
  }
  
  void buttonChanged(PatchButtonId bid, uint16_t value, uint16_t samples) override
  {
    // "kicking" the ball directly
    // if (bid == BUTTON_1 && value == ON)
    // {
    //   // ball.moveTo(padLeft.getX()+PAD_HW*2, padLeft.getY());
    //   // ball.setDirection(1.0, padLeft.getD());
    //   // ball.clearVelocity();
    //
    //   ball.setDirection(1, ball.getDY());
    //   ball.addVelocity(BALL_KICK_SPEED, BALL_KICK_SPEED);
    // }
    // else if (bid == BUTTON_2 && value == ON)
    // {
    //   // ball.moveTo(padRight.getX()-BALL_R*2, padRight.getY());
    //   // ball.setDirection(-1.0, padRight.getD());
    //   // ball.clearVelocity();
    //
    //   ball.setDirection(-1, ball.getDY());
    //   
    //   ball.addVelocity(BALL_KICK_SPEED, BALL_KICK_SPEED);
    // }

    // triggering a pad move, which might hit the ball
    if (bid == BUTTON_1)
    {
      padLeftEnvelope.gate(value == ON, samples);
    }
    else if (bid == BUTTON_2)
    {
      padRightEnvelope.gate(value == ON, samples);
    }
  }
  
  void processMidi(MidiMessage msg) override
  {
    if (msg.isNoteOn())
    {
      if (msg.getNote() == 60)
      {
        padLeftEnvelope.gate(true);
      }
      else if (msg.getNote() == 62)
      {
        padRightEnvelope.gate(true);
      }
    }
    else if (msg.isNoteOff())
    {
      if (msg.getNote() == 60)
      {
        padLeftEnvelope.gate(false);
      }
      else if (msg.getNote() == 62)
      {
        padRightEnvelope.gate(false);
      }
    }
  }
};

inline void Paddle::draw(MonochromeScreenBuffer& screen) const
{
  const int x = static_cast<int>(cx + xo);
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
  const coord_t cxc = static_cast<coord_t>(cx+xo);
  return !(x < cxc-hw || x > cxc+hw || y < cyc-hh || y > cyc+hh); 
}

inline float Paddle::getPositionNormalized() const
{
  return (cy - static_cast<float>(hh)) / static_cast<float>(SCREEN_H - hh - hh);
}

inline void Ball::draw(MonochromeScreenBuffer& screen) const
{
  const int x = static_cast<int>(cx);
  const int y = screen.getHeight() - static_cast<int>(cy);
  screen.fillRectangle(x-r, y-r, r*2 + 1, r*2 + 1, WHITE);
}

inline bool Ball::tick(const float sx, const float sy, const float dt)
{
  bool collidedX = false;
  bool collidedY = false;
  const float rf = r;
  
  cx += dx*clamp(sx+vx, 0, BALL_SPEED_MAX)*dt;
  if (cx < rf)
  {
    cx = rf;
    dx *= -1;
    collidedX = true;
  }
  else if (cx > SCREEN_W - rf)
  {
    cx = SCREEN_W - rf;
    dx *= -1;
    collidedX = true;
  }
  
  cy += dy*clamp(sy+vy, 0, BALL_SPEED_MAX)*dt;
  if (cy < rf)
  {
    cy = rf;
    dy *= -1;
    collidedY = true;
  }
  else if (cy > SCREEN_H - rf)
  {
    cy = SCREEN_H - rf;
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
  const coord_t lx = static_cast<coord_t>(cx) - r;
  const coord_t rx = static_cast<coord_t>(cx) + r;
  const coord_t by = static_cast<coord_t>(cy) - r;
  const coord_t ty = static_cast<coord_t>(cy) + r;

  const bool collided = dx < 0 ? paddle.pointInside(lx, ty) || paddle.pointInside(lx, by)
                               : paddle.pointInside(rx, ty) || paddle.pointInside(rx, by);
  if (collided)
  {
    dx *= -1;
    vx += paddle.getSpeed();
    vy += paddle.getSpeed();
    tick(BALL_SPEED_MIN, BALL_SPEED_MIN, dt);
  }

  return collided;
}