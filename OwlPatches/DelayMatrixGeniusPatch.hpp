#include "DelayMatrixPatch.hpp"

class DelayMatrixGeniusPatch : public DelayMatrixPatch<4>
{
  static constexpr const char* CLOCK_RATIOS[] = {
    "1/4",
    "1/2",
    "3/4",
    "x1",
    "x1.5",
    "x2",
    "x4"
  };

  static constexpr const char* SPREAD_RATIOS[] = {
    "1/4",
    "1/2",
    "3/4",
    "x1",
    "x2",
    "x3",
    "x4"
  };

public:
  void processScreen(MonochromeScreenBuffer& screen) override 
  {
    screen.clear();

    const int matrixTop = 17;
    const int rowSpacing = 12;
    const int knobRadius = 4;
    int x = 0;
    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      const int rowY = matrixTop + rowSpacing * i;
      const int knobY = rowY - knobRadius - 1;
      x = 0;
      screen.setCursor(x, rowY);
      screen.print(ftoa(data.time / getSampleRate(), 10));
      x += 36;
      drawKnob(data.input, screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 4;
      drawKnob((data.cutoff - MIN_CUTOFF) / (MAX_CUTOFF - MIN_CUTOFF), screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 6;

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        float fbk = getParameterValue(delayParamIds[f].feedback[i]);
        drawKnob(fbk, screen, x, knobY, knobRadius);
        x += knobRadius * 2 + 4;
      }
    }

    x -= 3;
    int barY = matrixTop + rowSpacing * (DELAY_LINE_COUNT - 1) - 3;
    int barW = 6;
    drawFeedback<true>(screen, x, barY, barW, feedback);

    x += 9;
    //drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    drawSkew(screen, x, barY, barW, skew);

    x += 9;
    //drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    drawSkew(screen, x, barY, barW, skew);
  }
private:

  void drawKnob(float value, MonochromeScreenBuffer& screen, int x, int y, int radius)
  {
    float angle = Interpolator::linear(-3*M_PI_4, 3*M_PI_4, value);
    float dirX = sinf(angle);
    float dirY = -cosf(angle);
    screen.drawCircle(x, y, radius+1, WHITE);
    screen.drawLine(x, y, x + dirX * radius, y + dirY * radius, WHITE);
  }

  template<bool pointLeft>
  void drawFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    const int iconY = y;
    screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
    screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
    screen.drawLine(x + iconDim, iconY - iconDim, x + iconDim, iconY, WHITE);
    if (pointLeft)
    {
      screen.drawLine(x + iconDim, iconY, x + 2, iconY, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY - 2, WHITE);
      screen.drawLine(x + 2, iconY, x + 4, iconY + 2, WHITE);
    }
    else
    {
      screen.drawLine(x, iconY, x + iconDim - 2, iconY, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY - 2, WHITE);
      screen.drawLine(x + iconDim - 2, iconY, x + iconDim - 4, iconY + 2, WHITE);
    }

    const int barHeight = 45;
    screen.drawRectangle(x, iconY - iconDim - barHeight - 1, iconDim + 1, barHeight, WHITE);
    screen.fillRectangle(x, iconY - iconDim - barHeight * amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }

  void drawSkew(MonochromeScreenBuffer& screen, const int x, const int y, const int iconDim, const float amt)
  {
    int tx = x;
    int tw = iconDim;
    int ty = y - iconDim;
    screen.drawLine(tx, ty, tx + tw, ty, WHITE);
    screen.drawLine(tx + tw, ty, tx + tw / 2, y, WHITE);
    screen.drawLine(tx + tw / 2, y, tx, ty, WHITE);

    const int barHeight = 45;
    screen.drawRectangle(x, y - iconDim - barHeight - 1, iconDim + 1, barHeight, WHITE);
    screen.fillRectangle(x, y - iconDim - barHeight * amt - 1, iconDim + 1, barHeight*amt, WHITE);
  }

  // copied from message.cpp and modified to give 3 decimal points
  static constexpr char hexnumerals[] = "0123456789abcdef";
  char* ftoa(float val, int base)
  {
    static char buf[16] = { 0 };
    int i = 14;
    // print 3 decimal points
    unsigned int part = abs((int)((val - int(val)) * 1000));
    do {
      buf[i--] = hexnumerals[part % base];
      part /= base;
    } while (i > 11);
    buf[i--] = '.';
    part = abs(int(val));
    do {
      buf[i--] = hexnumerals[part % base];
      part /= base;
    } while (part && i);
    if (val < 0.0f)
      buf[i--] = '-';
    return &buf[i + 1];
  }
};
