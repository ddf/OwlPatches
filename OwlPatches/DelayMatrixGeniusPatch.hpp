#include "DelayMatrixPatch.hpp"
#include "Noise.hpp"

class DelayMatrixGeniusPatch : public DelayMatrixPatch<4>
{
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
    if (clocked)
    {
      screen.print("Q=");
      screen.print((int)tapTempo.getBeatsPerMinute());
    }
    else
    {
      screen.print("TIME");
    }
    x += 39;
    screen.setCursor(x, headingY);
    screen.print("IN");
    x += 14;
    screen.setCursor(x, headingY);
    screen.print("LP");
    x += 14;

    DelayLineData& lastData = delayData[DELAY_LINE_COUNT - 1];
    const float lastMaxFreezePosition = min(lastData.time * 8 - lastData.time - lastData.skew, (float)lastData.delayLength - lastData.time - lastData.skew);
    const float maxFreezeSize = lastMaxFreezePosition + lastData.time + lastData.skew;
    if (freezeState == FreezeOn)
    {
      screen.setCursor(x, headingY);
      screen.print("/");
      screen.print(ftoa(maxFreezeSize / getSampleRate(), 10));
      screen.print("s\\");
    }

    for (int i = 0; i < DELAY_LINE_COUNT; ++i)
    {
      DelayLineData& data = delayData[i];
      const int rowY = matrixTop + rowSpacing * i;
      const int knobY = rowY - knobRadius - 1;
      x = 1;
      if (clocked)
      {
        int clockMult = CLOCK_MULT[clockMultIndex];
        int spreadDivMult = SPREAD_DIVMULT[spreadDivMultIndex];
        int tapFirst = Quarter / clockMult;
        int spreadInc = spreadDivMult < 0 ? tapFirst / -spreadDivMult : tapFirst * spreadDivMult;
        TapDelayLength tap = TapDelayLength(tapFirst + spreadInc*i);
        screen.setCursor(x, rowY);
        switch (tap)
        {
          // for easy debug
          case 0: screen.print(ftoa(data.time / getSampleRate(), 10)); break;
#define QUAV ""
#define DOT2 "."
#define DOT4 ","
#define DOT8 ";"
          case Whole: screen.print("W"); break;
          case Half: screen.print("H"); break;
          case Quarter: screen.print("Q"); break;
          case One8: screen.print(QUAV "8"); break;
          case One16: screen.print(QUAV "16"); break;
          case One32: screen.print(QUAV "32"); break;
          case One64: screen.print(QUAV "64"); break;
          case One128: screen.print(QUAV "128"); break;
          case One256: screen.print(QUAV "256"); break;
          case One512: screen.print(QUAV "512"); break;

          case WholeT: screen.print("WT"); break;
          case HalfT: screen.print("HT"); break;
          case QuarterT: screen.print("QT"); break;
          case One8T: screen.print(QUAV "8T"); break;
          case One16T: screen.print(QUAV "16T"); break;
          case One32T: screen.print(QUAV "32T"); break;
          case One64T: screen.print(QUAV "64T"); break;
          case One128T: screen.print(QUAV "128T"); break;
          case One256T: screen.print(QUAV "256T"); break;
          case One512T: screen.print(QUAV "512T"); break;
          case One1028T: screen.print(QUAV "1028T"); break;

          case WholeTT: screen.print("WTT"); break;
          case HalfTT: screen.print("HTT"); break;
          case QuarterTT: screen.print("QTT"); break;
          case One8TT: screen.print(QUAV "8TT"); break;
          case One16TT: screen.print(QUAV "16TT"); break;
          case One32TT: screen.print(QUAV "32TT"); break;
          case One64TT: screen.print(QUAV "64TT"); break;
          case One128TT: screen.print(QUAV "128TT"); break;
          case One256TT: screen.print(QUAV "256TT"); break;
          case One512TT: screen.print(QUAV "512TT"); break;
          case One1028TT: screen.print(QUAV "1028TT"); break;

          case Whole + One8: screen.print("W" DOT8); break;
          case Half + One16: screen.print("H" DOT8); break;
          case Quarter + One32: screen.print("Q" DOT8); break;
          case One8 + One64: screen.print(QUAV "8" DOT8); break;
          case One16 + One128: screen.print(QUAV "16" DOT8); break;
          case One32 + One256: screen.print(QUAV "32" DOT8); break;
          case One64 + One512: screen.print(QUAV "64" DOT8); break;
          case One128 + One1028: screen.print(QUAV "128" DOT8); break;

          case Whole + Quarter: screen.print("W" DOT4); break;
          case Half + One8: screen.print("H" DOT4); break;
          case Quarter + One16: screen.print("Q" DOT4); break;
          case One8 + One32: screen.print(QUAV "8" DOT4); break;
          case One16 + One64: screen.print(QUAV "16" DOT4); break;
          case One32 + One128: screen.print(QUAV "32" DOT4); break;
          case One64 + One256: screen.print(QUAV "64" DOT4); break;
          case One128 + One512: screen.print(QUAV "128" DOT4); break;

          case Whole + Quarter + One16: screen.print("W" DOT4 DOT4); break;
          case Half + One8 + One32: screen.print("H" DOT4 DOT4); break;
          case Quarter + One16 + One64: screen.print("Q" DOT4 DOT4); break;
          case One8 + One32 + One128: screen.print(QUAV "8" DOT4 DOT4); break;
          case One16 + One64 + One256: screen.print(QUAV "16" DOT4 DOT4); break;
          case One32 + One128 + One512: screen.print(QUAV "32" DOT4 DOT4); break;
          case One64 + One256 + One1028: screen.print(QUAV "64" DOT4 DOT4); break;

          case WholeT + QuarterT: screen.print("WT" DOT4); break;
          case HalfT + One8T: screen.print("HT" DOT4); break;
          case QuarterT + One16T: screen.print("QT" DOT4); break;
          case One8T + One32T: screen.print(QUAV "8T" DOT4); break;
          case One16T + One64T: screen.print(QUAV "16T" DOT4); break;
          case One32T + One128T: screen.print(QUAV "32T" DOT4); break;
          case One64T + One256T: screen.print(QUAV "64T" DOT4); break;
          case One128T + One512T: screen.print(QUAV "128T" DOT4); break;

          case WholeTT + QuarterTT: screen.print("WTT" DOT4); break;
          case HalfTT + One8TT: screen.print("HTT" DOT4); break;
          case QuarterTT + One16TT: screen.print("QTT" DOT4); break;
          case One8TT + One32TT: screen.print(QUAV "8TT" DOT4); break;
          case One16TT + One64TT: screen.print(QUAV "16TT" DOT4); break;
          case One32TT + One128TT: screen.print(QUAV "32TT" DOT4); break;
          case One64TT + One256TT: screen.print(QUAV "64TT" DOT4); break;
          case One128TT + One512TT: screen.print(QUAV "128TT" DOT4); break;

          case Whole+Half: screen.print("W" DOT2); break;
          case Half+Quarter: screen.print("H" DOT2); break;
          case Quarter + One8: screen.print("Q" DOT2); break;
          case One8+One16: screen.print(QUAV "8" DOT2); break;
          case One16+One32: screen.print(QUAV "16" DOT2); break;
          case One32+One64: screen.print(QUAV "32" DOT2); break;
          case One64+One128: screen.print(QUAV "64" DOT2); break;
          case One128+One256: screen.print(QUAV "128" DOT2); break;
          case One256+One512: screen.print(QUAV "256" DOT2); break;

          case Whole + Half + Quarter: screen.print("W" DOT2 DOT2); break;
          case Half + Quarter + One8: screen.print("H" DOT2 DOT2); break;
          case Quarter + One8 + One16: screen.print("Q" DOT2 DOT2); break;
          case One8 + One16 + One32: screen.print(QUAV "8" DOT2 DOT2); break;
          case One16 + One32 + One64: screen.print(QUAV "16" DOT2 DOT2); break;
          case One32 + One64 + One128: screen.print(QUAV "32" DOT2 DOT2); break;
          case One64 + One128 + One256: screen.print(QUAV "64" DOT2 DOT2); break;
          case One128 + One256 + One512: screen.print(QUAV "128" DOT2 DOT2); break;

          case WholeT + HalfT + QuarterT: screen.print("WT" DOT2 DOT2); break;
          case QuarterT + One8T + One16T: screen.print("QT" DOT2 DOT2); break;
          case HalfT + QuarterT + One8T: screen.print("HT" DOT2 DOT2); break;
          case One8T + One16T + One32T: screen.print(QUAV "8T"  DOT2 DOT2); break;
          case One16T + One32T + One64T: screen.print(QUAV "16T" DOT2 DOT2); break;
          case One32T + One64T + One128T: screen.print(QUAV "32T" DOT2 DOT2); break;
          case One64T + One128T + One256T: screen.print(QUAV "64T" DOT2 DOT2); break;
          case One128T + One256T + One512T: screen.print(QUAV "128T" DOT2 DOT2); break;

          case WholeTT + HalfTT + QuarterTT: screen.print("WTT" DOT2 DOT2); break;
          case QuarterTT + One8TT + One16TT: screen.print("QTT" DOT2 DOT2); break;
          case HalfTT + QuarterTT + One8TT: screen.print("HTT" DOT2 DOT2); break;
          case One8TT + One16TT + One32TT: screen.print(QUAV "8TT"  DOT2 DOT2); break;
          case One16TT + One32TT + One64TT: screen.print(QUAV "16TT" DOT2 DOT2); break;
          case One32TT + One64TT + One128TT: screen.print(QUAV "32TT" DOT2 DOT2); break;
          case One64TT + One128TT + One256TT: screen.print(QUAV "64TT" DOT2 DOT2); break;
          case One128TT + One256TT + One512TT: screen.print(QUAV "128TT" DOT2 DOT2); break;

          case Whole + Half + One8: screen.print("W" DOT2 DOT4); break;
          case Half + Quarter + One16: screen.print("H" DOT2 DOT4); break;
          case Quarter + One8 + One32: screen.print("Q" DOT2 DOT4); break;
          case One8 + One16 + One64: screen.print(QUAV "8" DOT2 DOT4); break;
          case One16 + One32 + One128: screen.print(QUAV "16" DOT2 DOT4); break;
          case One32 + One64 + One256: screen.print(QUAV "32" DOT2 DOT4); break;
          case One64 + One128 + One512: screen.print(QUAV "64" DOT2 DOT4); break;
          case One128 + One256 + One1028: screen.print(QUAV "128" DOT2 DOT4); break;

          case WholeT + HalfT + One8T: screen.print("WT" DOT2 DOT4); break;
          case HalfT + QuarterT + One16T: screen.print("HT" DOT2 DOT4); break;
          case QuarterT + One8T + One32T: screen.print("QT" DOT2 DOT4); break;
          case One8T + One16T + One64T: screen.print(QUAV "8T" DOT2 DOT4); break;
          case One16T + One32T + One128T: screen.print(QUAV "16T" DOT2 DOT4); break;
          case One32T + One64T + One256T: screen.print(QUAV "32T" DOT2 DOT4); break;
          case One64T + One128T + One512T: screen.print(QUAV "64T" DOT2 DOT4); break;
          case One128T + One256T + One1028T: screen.print(QUAV "128T" DOT2 DOT4); break;

          default: screen.print(tap); break;
        }

        //screen.print((int)TapTempo::samplePeriodToBpm(data.time, getSampleRate()));
        //screen.print((int)data.time);
      }
      else
      {
        screen.setCursor(x, rowY);
        screen.print(ftoa(data.time / getSampleRate(), 10));
        screen.print("s");
      }
      x += 44;
      drawKnob(data.input, screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 4;
      drawKnob((data.cutoff - MIN_CUTOFF) / (MAX_CUTOFF - MIN_CUTOFF), screen, x, knobY, knobRadius);
      x += knobRadius * 2 + 6;

      //screen.setCursor(x, rowY);
      //screen.print(ftoa(data.delayLength / getSampleRate(), 10));

      if (freezeState == FreezeOn)
      {
        const float windowStart = 1.0f - ((delays[i]->getPosition() + data.time) / maxFreezeSize);
        const float windowSize = min(data.time / maxFreezeSize, 1.0f);
        const int freezeX = x - knobRadius;
        const int freezeY = knobY - knobRadius;
        const float freezeW = (knobRadius * 2 + 4)*DELAY_LINE_COUNT - 1;
        //screen.setCursor(freezeX, rowY);
        //screen.print(delays[i]->getPosition()/getSampleRate());
        screen.drawRectangle(freezeX-1, freezeY, freezeW+1, 8, WHITE);
        screen.fillRectangle(freezeX + (freezeW)*windowStart, freezeY, max(freezeW * windowSize, 1.f), 8, WHITE);
      }
      else
      {
        for (int f = 0; f < DELAY_LINE_COUNT; ++f)
        {
          drawFeedLabel(screen, x - knobRadius, headingY, f + 1);
          float fbk = getParameterValue(delayParamIds[f].feedback[i]);
          drawKnob(fbk, screen, x+1, knobY, knobRadius);
          x += knobRadius * 2 + 4;
        }
      }
    }

    const int horizBarHeight = 8;
    const int barY = screen.getHeight() - 1;

    x = 0;
    drawMod(screen, x, barY, 37, horizBarHeight, modAmount);

    x += 40;
    drawSkew(screen, x, barY, 22, horizBarHeight, skew);

    x += 26;
    drawFeedback<true>(screen, x, barY, 48, horizBarHeight, feedback);

    x += 52;
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

    if (freezeState == FreezeOn)
    {
      screen.drawLine(x, iconY, x, iconY - iconDim, WHITE);
      screen.drawLine(x, iconY - iconDim, x + iconDim, iconY - iconDim, WHITE);
      screen.drawLine(x, iconY - iconDim + 2, x + 2, iconY - iconDim + 2, WHITE);
      screen.drawRectangle(x + iconDim - 3, iconY - 3, 3, 3, WHITE);
      screen.drawLine(x + iconDim - 3, iconY, x + iconDim - 3, iconY - 3, WHITE);
    }
    else
    {
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
