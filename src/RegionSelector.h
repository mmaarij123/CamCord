#pragma once
#include "CaptureSources.h"
// Returns false on Esc, right-click, display change, shutdown, or an invalid drag.
bool ChooseCaptureRegion(HWND owner, const CaptureTarget& display, CaptureTarget& selected);
bool CaptureRegionFromDrag(const CaptureTarget& display, int x1, int y1, int x2, int y2, CaptureTarget& selected);
