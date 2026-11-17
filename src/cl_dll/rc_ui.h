// ReCoF (B405): the modern look of ReCoF's own VGUI1 screens (the main menu, the story co-op lobby): flat dark panels
// with a title band, plain text buttons, one red accent, Inter (registered for the process by GameUI.dll), every size
// a fraction of the screen's height. The colours are GameUI.dll's theme (src/gameui/RcTheme.cpp), so the VGUI1 and
// the VGUI2 windows look alike.
#ifndef RC_UI_H
#define RC_UI_H

#include "VGUI_Panel.h"
#include "VGUI_Font.h"

class CommandButton;

// the fonts, by role
enum
{
	RCF_ITEM,		// main menu entries
	RCF_WORD,		// the wordmark
	RCF_WORDSUB,	// the line under it
	RCF_TITLE,		// a panel's title band
	RCF_BODY,		// rows, values
	RCF_LABEL,		// captions, hints
	RCF_BUTTON,		// buttons
	RCF_COUNT
};

// a colour with its opacity (0 clear, 255 solid; VGUI1's own alpha is the other way round)
struct RcUiColor
{
	int r, g, b, a;
};

extern const RcUiColor RCC_PANEL, RCC_BAND, RCC_BORDER, RCC_TITLE, RCC_TEXT, RCC_DIM, RCC_HI, RCC_ACCENT, RCC_FILL,
	RCC_SELECT, RCC_HOVER, RCC_PRESS, RCC_DISABLED, RCC_SEPARATOR, RCC_INPUT, RCC_SCRIM;

float RcUiScale( void );			// the screen's height over 768, times 0.88 and Menu size (rc_menu_scale)
int RcUiS( float value );			// a length in 768-line units at the screen's scale
vgui::Font *RcUiFont( int role );	// made again when the scale changes
bool RcUiModern( void );			// the main menu in the modern look (rc_menu_text 1, B410)

// a text button: an entry of the main menu (text only), a row of a panel, a framed button or the panel's main
// button (red); a selected row has the red fill and red text
class CRcUiButton : public CommandButton
{
public:
	enum { STYLE_ITEM, STYLE_ROW, STYLE_BOX, STYLE_PRIMARY };

	CRcUiButton( const char *text, int x, int y, int wide, int tall, int style, int font );
	void setRcText( const char *text );
	const char *getRcText( void ) { return m_szRcText; }
	void setRcSelected( bool bSelected ) { m_bRcSelected = bSelected; repaint(); }
	void setRcDisabled( bool bDisabled ) { m_bRcDisabled = bDisabled; repaint(); }
	bool isRcDisabled( void ) { return m_bRcDisabled; }
	void setRcCentred( bool bCentred ) { m_bRcCentred = bCentred; }
	int textWide( void );	// the text's width in pixels at its font

	virtual void paint();
	virtual void paintBackground();

private:
	char m_szRcText[96];
	int m_iRcStyle, m_iRcFont;
	bool m_bRcSelected, m_bRcDisabled, m_bRcCentred;
};

// a line of text
class CRcUiText : public vgui::Panel
{
public:
	CRcUiText( const char *text, int x, int y, int wide, int tall, int font, const RcUiColor &color );
	void setRcText( const char *text );
	void setRcColor( const RcUiColor &color ) { m_Color = color; repaint(); }
	void setRcAlign( int iAlign ) { m_iAlign = iAlign; }	// 0 left, 1 centre, 2 right
	virtual void paintBackground() {}
	virtual void paint();

private:
	char m_szText[128];
	int m_iFont, m_iAlign;
	RcUiColor m_Color;
};

// a panel: the body, a title band with its title in capitals, a hairline round it
class CRcUiFrame : public vgui::Panel
{
public:
	CRcUiFrame( const char *title, int x, int y, int wide, int tall );
	void setRcTitle( const char *title );
	int bandTall( void ) { return RcUiS( 34 ); }
	virtual void paintBackground();
	virtual void paint();

private:
	char m_szTitle[64];
};

// the main menu's wordmark: CRY OF FEAR spaced out, ReCoF under it
class CRcUiWordmark : public vgui::Panel
{
public:
	CRcUiWordmark( int x, int y, int wide, int tall );
	int blockTall( void );
	virtual void paintBackground() {}
	virtual void paint();
};

#endif
