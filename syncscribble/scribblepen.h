#pragma once

#include "basics.h"
#include "ulib/painter.h"

class ScribblePen
{
public:
  Color color;
  Dim width;
  Dim wRatio;  // (maxwidth - minwidth)/maxwidth
  Dim prParam;
  Dim spdMax;
  Dim dirAngle;
  Dim dash, gap;

  unsigned int flags;
  // existing numerical values must NOT be changed - flags is serialized to config file when saving pen
  static constexpr unsigned int DRAW_UNDER = 0x1, SNAP_TO_GRID = 0x2, LINE_DRAWING = 0x4, EPHEMERAL = 0x8,
      WIDTH_PR = 0x10, WIDTH_SPEED = 0x20, WIDTH_DIR = 0x40, WIDTH_MASK = 0xF0, //WIDTH_LINPR = 0x80,
      TIP_FLAT = 0x100, TIP_ROUND = 0x200, TIP_CHISEL = 0x400, TIP_MASK = 0xF00;

  ScribblePen(Color c, Dim w, unsigned int _flags = 0, Dim wr = 0, Dim pr = 0, Dim spd = 0, Dim angle = 0, Dim _dash = 0, Dim _gap = 0)
      : color(c), width(w), wRatio(wr), prParam(pr), spdMax(spd), dirAngle(angle), dash(_dash), gap(_gap), flags(_flags) {}

  bool operator==(const ScribblePen& b) const { return memcmp(this, &b, sizeof(ScribblePen)) == 0; }

  bool hasFlag(unsigned int flag) const { return (flags & flag) == flag; }
  void setFlag(unsigned int flag, bool value) { if(value) flags |= flag; else flags &= ~flag; }
  void clearAndSet(unsigned int clearflag, unsigned int setflag) { flags = (flags & ~clearflag) | setflag; }
  bool hasVarWidth() const { return flags & WIDTH_MASK; }
  bool usesPressure() const { return hasFlag(WIDTH_PR); }

  Rect getBBox() const
  {
    Dim hw = 0.75*0.5*width;  //pressureparam > 0 ? 0.5*width * (1 - pow(1 - 0.66, pressureparam)) : 0.5*width;
    return Rect::ltrb(-hw, -hw, hw, hw);
  }
};
