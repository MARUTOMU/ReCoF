// ====================================
// PoC-2 Japanese subtitle renderer
//		UTF-8 + stb_truetype rendering for the Paranoia subtitle panels
// ====================================
//
// The panel-side entry points are member functions of CSubtitleTextPanel
// (declared in vgui_subtitles.h, implemented in jp_subtitle_render.cpp):
//   SetJpText()    - keep a copy of the display string, validate it
//   JpMeasureText()- wrapped measurement used instead of getTextSizeWrapped
//   JpPaintText()  - two-pass (shadow + body) textured-glyph paint
//
// This header only carries the one free helper shared with
// vgui_subtitles.cpp logic.

#ifndef JP_SUBTITLE_RENDER_H
#define JP_SUBTITLE_RENDER_H

// True if pText is well-formed UTF-8 and contains at least one non-ASCII
// codepoint. C0 control bytes (other than tab) make it return false.
// A '*' byte terminates the check, mirroring the server's use of '*' as
// the subtitle string terminator / color marker.
bool JpTextIsUtf8Japanese(const char *pText);

#endif // JP_SUBTITLE_RENDER_H
