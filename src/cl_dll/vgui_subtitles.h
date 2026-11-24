// ======================================
// Paranoia subtitle system header file
//		written by BUzer
// ======================================

// JP-POC: modified for the PoC-2 Japanese subtitle renderer.
// Changes vs upstream ReCoF main are marked with "JP-POC".
// New members are plain (non-virtual) so the class vtable is unchanged.

#ifndef _SUBTITLES_H
#define _SUBTITLES_H
using namespace vgui;

struct JpGlyph;	// JP-POC: defined in jp_subtitle_render.cpp

class CSubtitleTextPanel : public TextPanel
{
public:
	CSubtitleTextPanel(const char* text,int x,int y,int wide,int tall) : TextPanel(text, x, y, wide, tall)
	{
		m_fBirthTime = 0;
		m_fHoldTime = 0;
		msgAfterDeath = NULL;
//		bkalpha = 0;
		// JP-POC: Japanese renderer state
		m_szJpText[0] = 0;
		m_bJpText = false;
		m_bJpActive = false;
		m_iJpLineCount = 0;
		m_iJpLineTall = 0;
	}

	virtual void paintBackground();
	virtual void paint();

	float m_fBirthTime;
	float m_fHoldTime;
//	int bkalpha;
	client_textmessage_t *msgAfterDeath;

	// JP-POC: Japanese renderer (implemented in jp_subtitle_render.cpp)
	void SetJpText(const char *pText);		// copy the display string, validate UTF-8
	bool JpMeasureText(int &tw, int &th);	// wrapped measurement, fills the line table
	bool JpPaintText();						// returns false -> caller uses the retail path
	bool JpEnsureGlyph(int cp, const JpGlyph **ppGlyph);	// rasterize+upload on first use (paint only)

	char m_szJpText[512];		// display string copy ($/@ prefixes already stripped)
	bool m_bJpText;				// m_szJpText is valid UTF-8 and has non-ASCII content
	bool m_bJpActive;			// decided in AddMessage: draw through the JP path
	int m_iJpLineStart[32];		// per-line byte offset into m_szJpText
	int m_iJpLineLen[32];		// per-line byte length
	int m_iJpLineCount;
	int m_iJpLineTall;			// pixel height the layout was measured with
};


class CSubtitle : public Panel
{
public:
    CSubtitle();
	void AddMessage( client_textmessage_t *tempMessage );
	void Initialize();

protected:
	virtual void paintBackground();

//protected:
public: // hacks..
	Panel *m_pLayer;
	CSubtitleTextPanel *m_pCur;
	CSubtitleTextPanel *m_pWait;

	float layerpos; // float version of layer's y-coordinate
	float lasttime;
	float m_fCurStartTime;
};

void SubtitleMessageAdd( client_textmessage_t *tempMessage );
void SubtitleInit();

// JP-POC: gate cvar, registered in SubtitleInit() (vgui_subtitles.cpp)
extern cvar_t *jp_poc;

#endif // _SUBTITLES_H
