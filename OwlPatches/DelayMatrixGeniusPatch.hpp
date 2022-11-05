#include "DelayMatrixPatch.hpp"
#include "Noise.hpp"

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

  float dryWetAnim = 0;

public:
  void processScreen(MonochromeScreenBuffer& screen) override 
  {
    screen.clear();

    const int matrixTop = 17;
    const int rowSpacing = 12;
    const int knobRadius = 4;
    int x = 0;

    const int headingY = matrixTop - knobRadius * 2 - 1;
    screen.setCursor(x, headingY);
    screen.print("TIME");
    x += 39;
    screen.setCursor(x, headingY);
    screen.print("IN");
    x += 14;
    screen.setCursor(x, headingY);
    screen.print("LP");

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      const int rowY = matrixTop + rowSpacing * i;
      const int knobY = rowY - knobRadius - 1;
      x = 1;
      screen.setCursor(x, rowY);
      screen.print(ftoa(data.time / getSampleRate(), 10));
      screen.print("s");
      x += 44;
      drawKnob(data.input, screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 4;
      drawKnob((data.cutoff - MIN_CUTOFF) / (MAX_CUTOFF - MIN_CUTOFF), screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 6;

      for (int f = 0; f < DELAY_LINE_COUNT; ++f)
      {
        drawFeedLabel(screen, x - knobRadius, headingY, f+1);
        float fbk = getParameterValue(delayParamIds[f].feedback[i]);
        drawKnob(fbk, screen, x, knobY, knobRadius);
        x += knobRadius * 2 + 4;
      }
    }

    const int horizBarHeight = 8;
    const int barY = screen.getHeight() - 1;

    x = 0;
    drawMod(screen, x, barY, 37, horizBarHeight, modAmount);

    x += 40;
    drawSkew(screen, x, barY, 22, horizBarHeight, skew);

    x += 26;
    drawFeedback<true>(screen, x, barY, 47, horizBarHeight, feedback);

    x += 51;
    drawDryWet(screen, x, barY, horizBarHeight, barY - matrixTop + 8, dryWet);

    //x += 9;
    ////drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    //drawSkew(screen, x, barY, barW, skew);

    //x += 9;
    ////drawKnob(dryWet, screen, x, matrixTop + rowSpacing - knobRadius - 1, knobRadius);
    //drawSkew(screen, x, barY, barW, skew);
  }
private:

  void drawFeedLabel(MonochromeScreenBuffer& screen, const int x, const int y, int num)
  {
    const int ac = y - 5;
    screen.drawLine(x, ac, x + 3, ac, WHITE);
    screen.drawLine(x + 2, ac - 2, x + 4, ac, WHITE);
    screen.drawLine(x + 2, ac + 2, x + 4, ac, WHITE);
    screen.setCursor(x + 5, y);
    screen.print(num);
    //switch (num)
    //{
    //  case 4: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 3: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 2: screen.fillRectangle(x + 6, y - 3 * num--, 2, 2, WHITE);
    //  case 1: screen.fillRectangle(x + 6, y - 3 * num, 2, 2, WHITE); break;
    //  default: break;
    //}
  }

  void drawKnob(float value, MonochromeScreenBuffer& screen, int x, int y, int radius)
  {
    float angle = Interpolator::linear(-3.1*M_PI_4, 3.1*M_PI_4, value);
    float dirX = sinf(angle);
    float dirY = -cosf(angle);
    screen.drawCircle(x, y, radius+1, WHITE);
    screen.drawLine(x, y, x + dirX * radius, y + dirY * radius, WHITE);

    // hack to fix "pointy" circle sides
    screen.setPixel(x - radius -1, y, BLACK);
    screen.setPixel(x - radius, y, WHITE);
    screen.setPixel(x + radius + 1, y, BLACK);
    screen.setPixel(x + radius, y, WHITE);
    screen.setPixel(x, y + radius + 1, BLACK);
    screen.setPixel(x, y + radius, WHITE);
    screen.setPixel(x, y - radius - 1, BLACK);
    screen.setPixel(x, y - radius, WHITE);
  }


  void drawMod(MonochromeScreenBuffer& screen, int x, int y, int w, int h, float amt)
  {
    screen.drawRectangle(x, y - h, w, h, WHITE);
    int fw = w * amt;
    int c = w / 2;
    screen.drawLine(x + c + fw, y - h, x + c + fw, y -1, WHITE);
    screen.drawLine(x + c, y - h, x + c, y - h + 1, WHITE);
    screen.drawLine(x + c, y - 1, x + c, y - 2, WHITE);
  }


  template<bool pointLeft>
  void drawFeedback(MonochromeScreenBuffer& screen, const int x, const int y, const int w, const int h, const float amt)
  {
    const int iconY = y-2;
    const int iconDim = h-2;
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

    const int barWidth = w - iconDim - 2;
    screen.drawRectangle(x+iconDim+2, y - h, barWidth, h, WHITE);
    screen.fillRectangle(x+iconDim+2, y - h, barWidth*amt, h, WHITE);
  }

  void drawSkew(MonochromeScreenBuffer& screen, const int x, const int y, const int w, const int h, const float amt)
  {
    int tx = x;
    int tw = h;
    int ty = y - h;
    screen.drawLine(tx, ty, tx + tw, ty, WHITE);
    screen.drawLine(tx + tw, ty, tx + tw / 2, y, WHITE);
    screen.drawLine(tx + tw / 2, y, tx, ty, WHITE);

    const int barWidth = w - tw - 1;
    screen.drawRectangle(x+tw+2, y - h, barWidth, h, WHITE);
    screen.fillRectangle(x+tw+2, y - h, barWidth*amt, h, WHITE);
  }

  void drawDryWet(MonochromeScreenBuffer& screen, int x, const int y, const int w, const int h, float amt)
  {
    dryWetAnim += amt;
    if (dryWetAnim >= 256)
    {
      dryWetAnim -= 256;
    }
    for (int ix = 0; ix < w; ++ix)
    {
      for (int iy = 0; iy < w; ++iy)
      {
        screen.setPixel(x + ix, y - iy, noise2(ix, iy+dryWetAnim) > 224);
      }
    }

    const int barHeight = h - w - 2;
    const int barFill = barHeight * amt;
    screen.drawRectangle(x, y - h, w, barHeight, WHITE);
    screen.fillRectangle(x, y - w - 2 - barFill, w, barFill, WHITE);
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
