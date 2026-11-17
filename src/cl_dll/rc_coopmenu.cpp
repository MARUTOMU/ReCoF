// ReCoF story co-op (B393): the main menu's "Story Co-op" entry and its panel. The entry's picture (Cry of Fear's
// picture menu, rc_menu_classic 1) is ReCoF's own, made in the look of the menu's other labels (280 x 38, white
// text on a faint streak, red under the cursor) and kept here as an alpha mask, so no file is added to the game.
// B406: the panel is the group's lobby (a Steam lobby, rc_steamnet.cpp RcLobby_*), shown before anyone enters the
// game: the players in it, the difficulty and the game the host picks (a new game or the last co-op save), Invite
// friends, Leave and Start. When the host starts, the guests' games join by themselves.

#include "hud.h"
#include "cl_util.h"
#include "vgui_TeamFortressViewport.h"
#include "GameMenu_guess.h"
#include "..\game_shared\vgui_loadtga.h"
#include "rc_vgui1.h"
#include "rc_ui.h"	// B406
#include "rc_steamnet.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

void IrrKlangStop_guess( float flFadeTime );

// the "Story Co-op" label: 280 x 38 alpha values, top row first, run-length coded as (count, value) byte pairs
static const unsigned char s_ucRcCoopLabel[] =
{
	255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 248, 0, 1, 1, 1, 2, 1, 4,
	1, 5, 1, 7, 1, 8, 10, 9, 13, 10, 1, 58, 1, 153, 1, 216, 1, 242, 1, 247, 1, 228, 1, 183,
	1, 109, 8, 10, 1, 131, 1, 55, 13, 10, 1, 9, 1, 8, 1, 7, 1, 6, 1, 4, 1, 3, 1, 2,
	1, 3, 1, 4, 1, 6, 1, 7, 1, 8, 5, 9, 18, 8, 1, 15, 1, 111, 1, 188, 1, 233, 1, 251,
	1, 241, 1, 202, 1, 126, 1, 21, 23, 8, 1, 7, 1, 6, 1, 5, 1, 3, 1, 2, 147, 0, 1, 2,
	1, 3, 1, 5, 1, 6, 1, 7, 1, 9, 1, 10, 1, 12, 7, 13, 1, 14, 13, 16, 1, 151, 8, 255,
	1, 231, 1, 55, 5, 16, 1, 204, 1, 255, 1, 60, 12, 16, 1, 15, 1, 14, 1, 13, 1, 11, 1, 10,
	6, 9, 1, 11, 1, 12, 1, 13, 2, 15, 1, 14, 1, 13, 17, 12, 1, 80, 1, 238, 1, 255, 1, 254,
	1, 231, 1, 200, 1, 214, 1, 253, 1, 255, 1, 237, 1, 76, 21, 12, 1, 11, 1, 9, 1, 8, 1, 6,
	1, 5, 1, 4, 1, 3, 1, 1, 144, 0, 1, 2, 1, 4, 1, 5, 4, 6, 1, 7, 1, 8, 1, 10,
	6, 12, 1, 14, 1, 16, 11, 17, 1, 120, 1, 255, 1, 253, 1, 136, 1, 45, 2, 17, 1, 27, 1, 93,
	1, 223, 1, 255, 1, 240, 1, 24, 4, 17, 1, 244, 1, 255, 1, 60, 11, 17, 1, 16, 1, 14, 1, 12,
	1, 11, 1, 10, 1, 11, 1, 13, 2, 15, 1, 14, 1, 12, 3, 10, 1, 12, 1, 13, 1, 15, 1, 14,
	17, 12, 1, 119, 2, 255, 1, 155, 1, 34, 3, 12, 1, 30, 1, 177, 1, 255, 1, 251, 1, 70, 18, 12,
	1, 11, 1, 9, 1, 7, 1, 6, 5, 5, 1, 4, 1, 2, 142, 0, 1, 1, 1, 2, 8, 3, 1, 4,
	1, 5, 5, 6, 1, 7, 12, 9, 1, 237, 1, 255, 1, 123, 6, 9, 1, 32, 1, 236, 1, 255, 1, 137,
	4, 9, 1, 244, 1, 255, 1, 60, 11, 9, 1, 8, 1, 7, 3, 6, 1, 8, 4, 9, 1, 8, 1, 7,
	3, 6, 1, 7, 2, 8, 16, 7, 1, 61, 1, 252, 1, 255, 1, 110, 7, 7, 1, 186, 1, 255, 1, 231,
	1, 10, 16, 7, 1, 6, 1, 5, 1, 4, 8, 3, 1, 2, 1, 1, 141, 0, 1, 2, 1, 3, 8, 4,
	1, 5, 1, 6, 5, 8, 1, 9, 1, 11, 10, 12, 1, 34, 2, 255, 1, 39, 7, 12, 1, 150, 1, 240,
	1, 167, 4, 12, 1, 244, 1, 255, 1, 60, 10, 12, 1, 11, 1, 10, 1, 8, 2, 7, 1, 9, 1, 11,
	4, 12, 1, 11, 1, 9, 4, 8, 2, 10, 16, 9, 1, 214, 1, 255, 1, 182, 8, 9, 1, 38, 1, 253,
	1, 255, 1, 108, 16, 9, 1, 7, 1, 5, 10, 4, 1, 2, 141, 0, 1, 1, 1, 3, 14, 4, 1, 6,
	1, 8, 1, 9, 1, 11, 5, 13, 1, 14, 1, 15, 1, 16, 1, 18, 1, 24, 2, 255, 1, 88, 4, 21,
	1, 22, 1, 24, 2, 25, 1, 26, 1, 28, 1, 29, 1, 30, 1, 144, 5, 255, 1, 116, 1, 33, 1, 34,
	1, 35, 1, 37, 1, 144, 1, 216, 1, 247, 1, 243, 1, 205, 1, 122, 1, 34, 2, 33, 1, 35, 1, 37,
	1, 96, 1, 255, 1, 152, 1, 70, 1, 213, 1, 247, 1, 212, 1, 153, 1, 255, 1, 236, 1, 36, 1, 35,
	1, 34, 1, 33, 1, 31, 2, 30, 1, 253, 1, 252, 4, 33, 1, 32, 1, 31, 2, 30, 1, 61, 2, 255,
	1, 63, 1, 30, 1, 28, 2, 27, 1, 25, 1, 24, 1, 22, 1, 21, 1, 20, 1, 181, 1, 192, 1, 97,
	1, 22, 1, 24, 1, 25, 1, 26, 1, 31, 1, 144, 1, 216, 1, 247, 1, 243, 1, 205, 1, 122, 2, 26,
	1, 27, 1, 28, 2, 29, 10, 28, 1, 31, 1, 144, 1, 216, 1, 247, 1, 243, 1, 205, 1, 122, 2, 25,
	1, 24, 1, 23, 1, 21, 1, 92, 1, 255, 1, 168, 1, 17, 1, 149, 1, 228, 1, 247, 1, 218, 1, 140,
	1, 24, 10, 17, 1, 16, 1, 15, 1, 13, 1, 12, 1, 10, 7, 9, 1, 7, 2, 6, 1, 5, 1, 4,
	1, 2, 1, 1, 92, 0, 1, 1, 1, 2, 1, 3, 13, 4, 1, 5, 1, 7, 1, 8, 1, 9, 1, 10,
	4, 11, 1, 12, 1, 14, 1, 15, 1, 16, 1, 17, 1, 201, 1, 255, 1, 240, 1, 114, 1, 22, 3, 21,
	1, 23, 1, 24, 1, 25, 1, 26, 1, 27, 2, 29, 1, 108, 1, 192, 1, 253, 1, 255, 1, 207, 1, 192,
	1, 87, 1, 32, 1, 33, 1, 76, 1, 243, 1, 255, 1, 248, 1, 202, 1, 209, 1, 253, 1, 255, 1, 220,
	1, 38, 1, 31, 1, 33, 1, 34, 1, 96, 1, 255, 1, 204, 1, 249, 1, 234, 1, 203, 1, 247, 1, 72,
	1, 247, 1, 255, 1, 78, 1, 31, 1, 30, 1, 29, 1, 28, 1, 27, 1, 121, 1, 255, 1, 181, 2, 30,
	1, 29, 3, 30, 1, 29, 1, 27, 1, 129, 1, 255, 1, 244, 2, 27, 1, 25, 1, 24, 1, 23, 1, 22,
	1, 21, 2, 19, 2, 18, 1, 19, 1, 20, 1, 21, 1, 22, 1, 24, 1, 76, 1, 243, 1, 255, 1, 248,
	1, 202, 1, 209, 1, 253, 1, 255, 1, 220, 1, 38, 1, 29, 4, 30, 1, 31, 7, 32, 1, 76, 1, 243,
	1, 255, 1, 248, 1, 202, 1, 209, 1, 253, 1, 255, 1, 220, 1, 38, 1, 29, 1, 27, 1, 26, 1, 92,
	1, 255, 1, 185, 1, 215, 1, 255, 1, 221, 1, 201, 1, 249, 1, 255, 1, 230, 1, 46, 8, 21, 1, 20,
	1, 18, 1, 17, 1, 16, 1, 15, 1, 14, 1, 12, 5, 11, 1, 10, 1, 9, 1, 8, 1, 7, 1, 6,
	1, 5, 1, 4, 1, 2, 1, 1, 92, 0, 1, 2, 1, 3, 1, 5, 1, 7, 1, 8, 1, 9, 10, 10,
	1, 12, 6, 14, 1, 16, 1, 18, 4, 19, 1, 46, 1, 239, 2, 255, 1, 253, 1, 205, 1, 143, 1, 82,
	1, 27, 1, 28, 5, 30, 1, 31, 1, 33, 1, 244, 1, 255, 1, 60, 3, 34, 1, 39, 1, 247, 1, 255,
	1, 166, 4, 38, 1, 201, 1, 255, 1, 218, 3, 33, 1, 96, 2, 255, 1, 185, 4, 32, 1, 166, 1, 255,
	1, 170, 1, 30, 4, 28, 1, 214, 1, 255, 1, 84, 1, 29, 3, 28, 1, 29, 2, 31, 1, 29, 1, 169,
	1, 255, 1, 203, 1, 28, 1, 27, 1, 25, 1, 24, 5, 22, 6, 23, 1, 39, 1, 247, 1, 255, 1, 166,
	3, 24, 1, 34, 1, 201, 1, 255, 1, 218, 2, 31, 1, 29, 2, 28, 1, 30, 6, 31, 1, 39, 1, 247,
	1, 255, 1, 166, 3, 31, 1, 34, 1, 201, 1, 255, 1, 218, 3, 26, 1, 92, 2, 255, 1, 246, 1, 82,
	2, 21, 1, 24, 1, 192, 1, 255, 1, 214, 7, 21, 1, 19, 1, 17, 5, 16, 1, 14, 1, 12, 4, 11,
	1, 10, 1, 8, 1, 7, 1, 5, 4, 4, 1, 3, 1, 1, 92, 0, 1, 1, 1, 2, 1, 3, 1, 4,
	1, 6, 1, 7, 1, 8, 1, 9, 7, 10, 1, 11, 1, 12, 4, 13, 1, 14, 1, 15, 6, 16, 1, 31,
	1, 174, 1, 252, 4, 255, 1, 254, 1, 197, 1, 92, 4, 22, 1, 23, 1, 24, 1, 244, 1, 255, 1, 60,
	2, 25, 1, 26, 1, 164, 1, 255, 1, 200, 6, 28, 1, 235, 1, 255, 1, 112, 1, 27, 1, 26, 1, 96,
	2, 255, 1, 49, 4, 21, 1, 69, 1, 255, 1, 247, 1, 20, 3, 18, 1, 52, 1, 255, 1, 237, 1, 19,
	5, 18, 1, 19, 1, 21, 1, 20, 1, 187, 1, 255, 1, 183, 2, 18, 1, 16, 5, 15, 1, 16, 1, 17,
	1, 16, 4, 15, 1, 164, 1, 255, 1, 200, 4, 15, 1, 16, 1, 19, 1, 235, 1, 255, 1, 112, 1, 19,
	1, 18, 1, 17, 1, 16, 1, 17, 1, 18, 5, 19, 1, 164, 1, 255, 1, 200, 6, 19, 1, 235, 1, 255,
	1, 112, 2, 16, 1, 92, 2, 255, 1, 99, 4, 13, 1, 18, 1, 237, 1, 255, 1, 92, 5, 13, 1, 12,
	1, 10, 7, 9, 1, 8, 1, 7, 4, 6, 1, 4, 6, 3, 1, 2, 1, 1, 95, 0, 1, 1, 1, 3,
	1, 5, 1, 6, 1, 8, 1, 10, 6, 11, 1, 13, 5, 15, 1, 17, 1, 18, 7, 19, 1, 20, 1, 23,
	1, 98, 1, 165, 1, 227, 4, 255, 1, 194, 4, 27, 1, 28, 1, 244, 1, 255, 1, 60, 2, 31, 1, 32,
	1, 238, 1, 255, 1, 92, 6, 34, 1, 141, 1, 255, 1, 191, 2, 37, 1, 96, 1, 255, 1, 244, 1, 30,
	1, 28, 4, 27, 1, 226, 1, 255, 1, 98, 1, 24, 2, 23, 1, 146, 1, 255, 1, 146, 1, 24, 5, 23,
	1, 24, 2, 26, 1, 176, 1, 255, 1, 188, 1, 23, 1, 22, 1, 21, 5, 19, 1, 21, 2, 22, 1, 20,
	3, 19, 1, 238, 1, 255, 1, 92, 1, 18, 3, 19, 1, 20, 1, 22, 1, 141, 1, 255, 1, 191, 1, 23,
	1, 22, 1, 20, 1, 19, 1, 20, 1, 22, 5, 23, 1, 238, 1, 255, 1, 92, 5, 23, 1, 21, 1, 141,
	1, 255, 1, 191, 2, 19, 1, 92, 1, 255, 1, 236, 1, 18, 1, 16, 4, 15, 1, 151, 1, 255, 1, 171,
	5, 15, 1, 14, 1, 12, 7, 11, 1, 10, 1, 9, 4, 7, 1, 5, 7, 3, 1, 2, 1, 1, 98, 0,
	1, 1, 1, 3, 1, 4, 5, 5, 1, 6, 5, 8, 1, 9, 9, 11, 1, 13, 3, 14, 1, 35, 1, 105,
	1, 207, 2, 255, 1, 179, 3, 17, 1, 18, 1, 244, 1, 255, 1, 60, 2, 20, 1, 25, 2, 255, 1, 41,
	6, 23, 1, 89, 1, 255, 1, 231, 1, 25, 1, 26, 1, 96, 1, 255, 1, 220, 1, 24, 1, 23, 4, 21,
	1, 131, 1, 255, 1, 190, 3, 18, 1, 234, 1, 255, 1, 49, 7, 18, 1, 19, 1, 20, 1, 152, 1, 255,
	1, 208, 3, 21, 1, 22, 1, 23, 3, 24, 1, 25, 2, 26, 1, 25, 2, 24, 1, 25, 2, 255, 1, 41,
	1, 23, 4, 24, 1, 26, 1, 89, 1, 255, 1, 231, 1, 27, 1, 52, 6, 255, 1, 140, 2, 26, 2, 255,
	1, 41, 5, 26, 1, 25, 1, 89, 1, 255, 1, 231, 2, 23, 1, 92, 1, 255, 1, 185, 1, 23, 1, 22,
	3, 20, 1, 19, 1, 104, 1, 255, 1, 209, 1, 13, 1, 12, 3, 11, 1, 10, 9, 8, 1, 6, 4, 5,
	1, 3, 8, 2, 1, 1, 100, 0, 1, 2, 5, 3, 1, 4, 5, 6, 1, 8, 8, 9, 1, 10, 1, 11,
	3, 12, 1, 13, 1, 14, 1, 15, 1, 143, 2, 255, 1, 40, 3, 15, 1, 244, 1, 255, 1, 60, 1, 18,
	1, 19, 1, 39, 2, 255, 1, 27, 6, 21, 1, 76, 1, 255, 1, 241, 1, 23, 1, 24, 1, 96, 1, 255,
	1, 212, 2, 24, 1, 22, 3, 21, 1, 35, 1, 254, 1, 253, 1, 28, 1, 18, 1, 77, 1, 255, 1, 208,
	1, 19, 7, 18, 2, 20, 1, 112, 1, 255, 1, 245, 1, 21, 3, 22, 1, 23, 3, 24, 1, 26, 2, 27,
	1, 26, 2, 24, 1, 39, 2, 255, 1, 27, 1, 23, 3, 24, 1, 25, 1, 26, 1, 76, 1, 255, 1, 241,
	1, 27, 1, 52, 6, 255, 1, 140, 1, 27, 1, 39, 2, 255, 5, 27, 1, 26, 1, 25, 1, 76, 1, 255,
	1, 241, 2, 24, 1, 92, 1, 255, 1, 167, 1, 23, 1, 22, 2, 21, 1, 20, 1, 19, 1, 92, 1, 255,
	1, 225, 1, 14, 1, 12, 3, 11, 1, 10, 9, 8, 1, 7, 4, 5, 8, 3, 1, 2, 1, 1, 88, 0,
	1, 1, 1, 2, 10, 3, 1, 5, 1, 6, 4, 7, 1, 9, 1, 10, 4, 11, 1, 13, 1, 14, 4, 15,
	1, 176, 1, 237, 1, 118, 1, 15, 1, 17, 3, 18, 1, 19, 1, 21, 2, 22, 1, 249, 1, 255, 1, 83,
	3, 22, 1, 244, 1, 255, 1, 60, 1, 26, 1, 27, 1, 29, 2, 255, 1, 41, 4, 30, 1, 28, 1, 27,
	1, 92, 1, 255, 1, 221, 1, 28, 1, 30, 1, 96, 1, 255, 1, 212, 2, 30, 1, 28, 1, 27, 3, 26,
	1, 193, 1, 255, 1, 116, 1, 25, 1, 169, 1, 255, 1, 111, 1, 27, 1, 26, 5, 25, 1, 26, 1, 28,
	1, 29, 1, 35, 2, 255, 1, 64, 1, 25, 1, 23, 5, 22, 1, 23, 1, 25, 1, 112, 1, 203, 1, 134,
	1, 22, 1, 26, 2, 255, 1, 41, 1, 20, 4, 22, 1, 24, 1, 92, 1, 255, 1, 221, 2, 25, 1, 23,
	2, 22, 1, 24, 4, 25, 1, 26, 2, 255, 1, 41, 1, 24, 1, 23, 3, 22, 1, 20, 1, 92, 1, 255,
	1, 221, 2, 18, 1, 92, 1, 255, 1, 177, 1, 18, 1, 16, 4, 14, 1, 106, 1, 255, 1, 203, 5, 14,
	1, 13, 8, 11, 1, 10, 1, 8, 3, 7, 1, 6, 1, 4, 8, 3, 1, 1, 88, 0, 1, 1, 1, 3,
	10, 5, 1, 7, 5, 9, 1, 11, 5, 14, 1, 16, 1, 18, 4, 19, 1, 167, 1, 255, 1, 199, 1, 20,
	1, 22, 3, 24, 1, 25, 1, 27, 2, 29, 1, 248, 1, 255, 1, 67, 3, 29, 1, 244, 1, 255, 1, 60,
	1, 33, 1, 35, 1, 37, 1, 239, 1, 255, 1, 93, 4, 37, 1, 36, 1, 34, 1, 144, 1, 255, 1, 189,
	1, 34, 1, 36, 1, 96, 1, 255, 1, 212, 2, 38, 1, 35, 4, 33, 1, 96, 1, 255, 1, 198, 1, 34,
	1, 242, 1, 249, 1, 38, 1, 37, 6, 34, 1, 35, 2, 37, 1, 35, 1, 205, 1, 255, 1, 181, 1, 33,
	1, 31, 5, 29, 1, 31, 1, 33, 1, 214, 1, 255, 1, 189, 2, 29, 1, 239, 1, 255, 1, 93, 1, 28,
	3, 30, 1, 31, 1, 33, 1, 144, 1, 255, 1, 189, 2, 35, 1, 33, 2, 31, 1, 33, 5, 34, 1, 239,
	1, 255, 1, 93, 1, 32, 1, 30, 3, 29, 1, 27, 1, 144, 1, 255, 1, 189, 2, 24, 1, 92, 1, 255,
	1, 222, 1, 22, 1, 20, 4, 19, 1, 155, 1, 255, 1, 160, 5, 19, 1, 17, 1, 15, 7, 14, 1, 13,
	1, 11, 3, 9, 1, 8, 1, 6, 7, 4, 1, 3, 1, 1, 89, 0, 1, 1, 9, 3, 1, 4, 1, 5,
	5, 7, 1, 8, 1, 9, 4, 10, 1, 11, 1, 12, 4, 13, 1, 70, 2, 255, 1, 116, 1, 16, 4, 17,
	1, 18, 1, 20, 1, 117, 1, 255, 1, 238, 4, 20, 1, 242, 1, 255, 1, 61, 1, 23, 1, 24, 1, 25,
	1, 165, 1, 255, 1, 200, 5, 25, 1, 23, 1, 236, 1, 255, 1, 109, 1, 22, 1, 23, 1, 96, 1, 255,
	1, 212, 2, 25, 1, 23, 3, 22, 1, 23, 1, 24, 1, 243, 1, 254, 1, 96, 1, 255, 1, 172, 2, 25,
	1, 24, 5, 23, 1, 24, 2, 25, 1, 23, 1, 75, 2, 255, 1, 104, 1, 21, 6, 20, 1, 130, 2, 255,
	1, 66, 2, 20, 1, 165, 1, 255, 1, 200, 1, 20, 4, 21, 1, 23, 1, 236, 1, 255, 1, 109, 3, 24,
	3, 23, 5, 24, 1, 165, 1, 255, 1, 200, 1, 22, 4, 20, 1, 19, 1, 236, 1, 255, 1, 109, 2, 17,
	1, 92, 2, 255, 1, 65, 1, 14, 3, 13, 1, 21, 1, 239, 1, 255, 1, 75, 5, 13, 1, 12, 1, 11,
	7, 10, 1, 8, 1, 7, 4, 6, 1, 4, 7, 3, 1, 1, 90, 0, 1, 1, 1, 2, 1, 3, 5, 5,
	1, 6, 1, 7, 1, 8, 6, 10, 1, 12, 1, 13, 5, 14, 1, 15, 1, 17, 4, 18, 1, 171, 2, 255,
	1, 188, 1, 85, 1, 26, 1, 22, 1, 23, 1, 63, 1, 161, 1, 254, 1, 255, 1, 108, 4, 25, 1, 230,
	1, 255, 1, 92, 1, 29, 2, 31, 1, 42, 1, 246, 1, 255, 1, 166, 3, 31, 1, 33, 1, 200, 1, 255,
	1, 221, 3, 26, 1, 96, 1, 255, 1, 212, 1, 29, 1, 27, 1, 26, 1, 25, 2, 26, 1, 28, 1, 29,
	1, 158, 1, 255, 1, 234, 1, 255, 1, 75, 1, 29, 1, 30, 1, 29, 1, 28, 3, 27, 1, 28, 2, 30,
	1, 28, 2, 27, 1, 146, 2, 255, 1, 152, 1, 34, 3, 24, 1, 28, 1, 155, 2, 255, 1, 143, 1, 27,
	1, 25, 1, 24, 1, 42, 1, 246, 1, 255, 1, 166, 3, 26, 1, 33, 1, 200, 1, 255, 1, 221, 7, 32,
	5, 31, 1, 42, 1, 246, 1, 255, 1, 166, 3, 26, 1, 33, 1, 200, 1, 255, 1, 221, 3, 21, 1, 92,
	2, 255, 1, 229, 1, 54, 2, 17, 1, 29, 1, 198, 1, 255, 1, 193, 7, 17, 1, 15, 1, 14, 5, 13,
	1, 12, 1, 10, 4, 9, 1, 8, 1, 7, 1, 5, 1, 4, 4, 3, 1, 2, 1, 1, 91, 0, 1, 1,
	1, 3, 1, 5, 1, 6, 1, 8, 1, 10, 1, 12, 1, 14, 1, 16, 1, 18, 6, 19, 1, 20, 1, 22,
	1, 24, 1, 25, 3, 26, 1, 27, 1, 29, 1, 30, 1, 32, 1, 34, 1, 35, 1, 37, 1, 159, 1, 254,
	7, 255, 1, 249, 1, 122, 5, 45, 1, 180, 1, 255, 1, 246, 1, 198, 1, 99, 2, 51, 1, 78, 1, 244,
	1, 255, 1, 247, 1, 202, 1, 208, 1, 253, 1, 255, 1, 220, 1, 48, 1, 46, 1, 45, 1, 43, 1, 96,
	1, 255, 1, 212, 1, 42, 1, 41, 1, 42, 1, 44, 1, 46, 1, 47, 1, 49, 1, 50, 1, 61, 2, 255,
	1, 231, 1, 43, 1, 44, 1, 45, 2, 47, 3, 46, 2, 47, 1, 45, 4, 43, 1, 132, 1, 252, 1, 255,
	1, 254, 1, 224, 1, 197, 1, 217, 1, 254, 1, 255, 1, 251, 1, 130, 1, 43, 1, 45, 3, 44, 1, 78,
	1, 244, 1, 255, 1, 247, 1, 202, 1, 208, 1, 253, 1, 255, 1, 220, 1, 54, 1, 56, 1, 57, 1, 59,
	1, 61, 2, 63, 1, 62, 1, 60, 1, 58, 1, 56, 1, 55, 1, 53, 1, 51, 1, 78, 1, 244, 1, 255,
	1, 247, 1, 202, 1, 208, 1, 253, 1, 255, 1, 220, 1, 44, 1, 42, 1, 40, 1, 39, 1, 92, 1, 255,
	1, 236, 1, 232, 1, 255, 1, 212, 1, 203, 1, 251, 1, 255, 1, 209, 9, 31, 1, 29, 1, 27, 1, 25,
	1, 24, 1, 22, 1, 20, 1, 18, 1, 17, 5, 16, 1, 14, 1, 12, 1, 10, 1, 8, 1, 7, 1, 5,
	1, 3, 1, 1, 94, 0, 1, 1, 1, 3, 1, 5, 1, 7, 1, 9, 1, 11, 1, 12, 8, 13, 1, 15,
	1, 17, 1, 19, 4, 20, 1, 21, 1, 22, 1, 24, 1, 26, 1, 28, 1, 30, 1, 31, 1, 53, 1, 141,
	1, 205, 1, 232, 1, 250, 1, 238, 1, 208, 1, 137, 1, 38, 7, 36, 1, 179, 1, 242, 1, 255, 1, 173,
	4, 41, 1, 147, 1, 218, 1, 247, 1, 239, 1, 202, 1, 116, 2, 41, 1, 40, 1, 38, 1, 36, 1, 96,
	1, 255, 1, 212, 1, 32, 1, 34, 1, 36, 1, 38, 1, 40, 4, 41, 1, 219, 1, 255, 1, 137, 3, 34,
	1, 35, 1, 37, 3, 38, 1, 37, 1, 35, 6, 34, 1, 39, 1, 140, 1, 203, 1, 239, 1, 252, 1, 243,
	1, 207, 1, 141, 1, 39, 1, 29, 1, 30, 1, 32, 1, 33, 1, 32, 1, 33, 2, 34, 1, 147, 1, 218,
	1, 247, 1, 239, 1, 202, 1, 116, 2, 39, 1, 41, 1, 43, 1, 45, 1, 47, 3, 48, 1, 46, 1, 45,
	1, 42, 1, 40, 1, 38, 1, 36, 2, 35, 1, 147, 1, 218, 1, 247, 1, 239, 1, 202, 1, 116, 1, 35,
	1, 34, 1, 33, 1, 31, 1, 29, 1, 92, 1, 255, 1, 216, 1, 28, 1, 160, 1, 234, 1, 245, 1, 208,
	1, 117, 11, 24, 1, 22, 1, 20, 1, 18, 1, 16, 1, 14, 6, 13, 1, 12, 1, 10, 2, 8, 1, 7,
	1, 5, 1, 2, 1, 1, 141, 0, 1, 1, 29, 3, 1, 221, 1, 255, 1, 41, 28, 3, 1, 2, 9, 0,
	1, 2, 28, 4, 1, 92, 1, 255, 1, 216, 28, 4, 1, 3, 1, 1, 147, 0, 1, 2, 28, 3, 1, 68,
	1, 255, 1, 195, 28, 3, 1, 2, 1, 1, 9, 0, 1, 2, 28, 3, 1, 92, 1, 255, 1, 216, 29, 3,
	1, 1, 146, 0, 1, 2, 1, 4, 25, 5, 1, 23, 1, 7, 1, 46, 1, 218, 1, 255, 1, 81, 27, 5,
	1, 4, 1, 2, 10, 0, 1, 3, 28, 5, 1, 92, 1, 255, 1, 216, 28, 5, 1, 4, 1, 1, 144, 0,
	1, 2, 1, 3, 1, 4, 26, 5, 1, 155, 3, 255, 1, 183, 26, 5, 1, 4, 1, 3, 1, 2, 11, 0,
	1, 1, 28, 2, 1, 92, 1, 255, 1, 216, 29, 2, 1, 1, 144, 0, 1, 2, 1, 4, 27, 6, 1, 100,
	1, 235, 1, 238, 1, 156, 1, 12, 26, 6, 1, 5, 1, 3, 1, 1, 11, 0, 1, 2, 28, 4, 1, 92,
	1, 255, 1, 216, 28, 4, 1, 3, 1, 1, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0,
	228, 0,
};

#define RC_LABEL_W	280
#define RC_LABEL_H	38

// the label as a 32-bit TGA in memory (bottom row first, as the menu's own files), in one colour
static BitmapTGA *RcCoopLabel( int r, int g, int b )
{
	const int size = 18 + RC_LABEL_W * RC_LABEL_H * 4;
	unsigned char *buf = new unsigned char[size];
	memset( buf, 0, size );
	buf[2] = 2;	// uncompressed true colour
	buf[12] = RC_LABEL_W & 255;
	buf[13] = RC_LABEL_W >> 8;
	buf[14] = RC_LABEL_H & 255;
	buf[15] = RC_LABEL_H >> 8;
	buf[16] = 32;
	buf[17] = 8;	// 8 alpha bits
	int p = 0;
	for ( int k = 0; k + 1 < (int)sizeof( s_ucRcCoopLabel ); k += 2 )
	{
		for ( int n = 0; n < s_ucRcCoopLabel[k] && p < RC_LABEL_W * RC_LABEL_H; n++, p++ )
		{
			int x = p % RC_LABEL_W, y = p / RC_LABEL_W;
			unsigned char *px = buf + 18 + ( ( RC_LABEL_H - 1 - y ) * RC_LABEL_W + x ) * 4;
			px[0] = b;
			px[1] = g;
			px[2] = r;
			px[3] = s_ucRcCoopLabel[k + 1];
		}
	}
	BitmapTGA *pImage = vgui_LoadTGAFromMemory( buf, size );
	delete [] buf;
	if ( pImage )
		pImage->setColor( Color( 255, 255, 255, 0 ) );
	return pImage;
}

class CRcCoopPanel;

class CRcCoopHandler : public ActionSignal
{
public:
	CRcCoopHandler( int cmd ) { m_cmd = cmd; }
	virtual void actionPerformed( Panel *panel );
private:
	int m_cmd;
};

static CGameMenu *s_pRcMenu;
static CRcImagePanel *s_pRcItem;	// B401: drawn at the menus' scale
static ColorButton *s_pRcItemButton;
static BitmapTGA *s_pRcLabel, *s_pRcLabelArmed;
static bool s_bRcArmed;
static int s_iRcArmedX, s_iRcArmedY;
static CRcCoopPanel *s_pRcPanel;
// B396: the game's own difficulties are the co-op presets (hl.dll rc_coop_preset): each brings its co-op rules
static const char *s_pszRcPreset[5] = { "", "easy", "normal", "hard", "nightmare" };
static const char *s_pszRcSkillName[5] = { "", "Easy", "Normal", "Hard", "Nightmare" };

// the newest co-op save (coop_auto, coop_tape) in either save folder next to cof.exe, its name in pszName; false for none
static bool RcCoopLastSave( char *pszName, int len )
{
	char dir[MAX_PATH];
	GetModuleFileNameA( NULL, dir, sizeof( dir ) );
	char *slash = strrchr( dir, '\\' );
	if ( !slash )
		return false;
	slash[1] = 0;
	static const char *s_pszDirs[2] = { "SAVE\\", "cryoffear\\SAVE\\" };
	static const char *s_pszSaves[2] = { "coop_auto", "coop_tape" };
	FILETIME best;
	bool bFound = false;
	for ( int d = 0; d < 2; d++ )
	{
		for ( int s = 0; s < 2; s++ )
		{
			char path[MAX_PATH];
			_snprintf( path, sizeof( path ) - 1, "%s%s%s.sav", dir, s_pszDirs[d], s_pszSaves[s] );
			path[sizeof( path ) - 1] = 0;
			WIN32_FILE_ATTRIBUTE_DATA fad;
			if ( !GetFileAttributesExA( path, GetFileExInfoStandard, &fad ) )
				continue;
			if ( !bFound || CompareFileTime( &fad.ftLastWriteTime, &best ) > 0 )
			{
				best = fad.ftLastWriteTime;
				strncpy( pszName, s_pszSaves[s], len - 1 );
				pszName[len - 1] = 0;
				bFound = true;
			}
		}
	}
	return bFound;
}

// B395: Steam's launch options for Cry of Fear that start ReCoF instead ("<this cof.exe>" %command%): with them a
// friend's invite or Join Game starts ReCoF while the game is closed, as Steam then runs this cof.exe with its own
// command and the host's connect value (+rc_coop_join) after it. Copied to the clipboard; false if it could not be
static bool RcCoopLaunchOptions( char *text, int len )
{
	char exe[MAX_PATH];
	if ( !GetModuleFileNameA( NULL, exe, sizeof( exe ) ) )
		return false;
	_snprintf( text, len - 1, "\"%s\" %%command%%", exe );
	text[len - 1] = 0;
	return true;
}

static bool RcCoopCopyLaunchOptions( void )
{
	char text[MAX_PATH + 32];
	if ( !RcCoopLaunchOptions( text, sizeof( text ) ) )
		return false;
	if ( !OpenClipboard( NULL ) )
		return false;
	EmptyClipboard();
	size_t n = strlen( text ) + 1;
	HGLOBAL h = GlobalAlloc( GMEM_MOVEABLE, n );
	bool bOk = false;
	if ( h )
	{
		void *p = GlobalLock( h );
		if ( p )
		{
			memcpy( p, text, n );
			GlobalUnlock( h );
			bOk = SetClipboardData( CF_TEXT, h ) != NULL;
		}
		if ( !bOk )
			GlobalFree( h );
	}
	CloseClipboard();
	return bOk;
}

// the panel's commands
enum
{
	RC_LOBBY_OPEN = 1,	// the menu's entry
	RC_LOBBY_CLOSE = 10,
	RC_LOBBY_LEAVE,
	RC_LOBBY_INVITE,
	RC_LOBBY_START,
	RC_LOBBY_NEW,
	RC_LOBBY_CONTINUE,
	RC_LOBBY_LAUNCH,
	RC_LOBBY_SKILL,		// + 0 to 3
};

// B406: the lobby, a frame in the middle of the screen over a dark veil
class CRcCoopPanel : public Panel
{
public:
	CRcCoopPanel() : Panel( 0, 0, ScreenWidth, ScreenHeight )
	{
		setBgColor( 0, 0, 0, 255 );
		m_pFrame = new CRcUiFrame( "Story Co-op", 0, 0, 10, 10 );
		m_pFrame->setParent( this );
		m_pClose = AddButton( "X", CRcUiButton::STYLE_ROW, RCF_TITLE, RC_LOBBY_CLOSE );
		m_pClose->setRcCentred( true );
		m_pPlayersCap = AddText( "PLAYERS", RCF_LABEL, RCC_DIM );
		for ( int i = 0; i < 4; i++ )
		{
			m_pName[i] = AddText( "", RCF_BODY, RCC_TEXT );
			m_pTag[i] = AddText( "", RCF_LABEL, RCC_DIM );
			m_pTag[i]->setRcAlign( 2 );
		}
		m_pSkillCap = AddText( "DIFFICULTY", RCF_LABEL, RCC_DIM );
		for ( int i = 0; i < 4; i++ )
		{
			m_pSkill[i] = AddButton( s_pszRcSkillName[i + 1], CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_SKILL + i );
			m_pSkill[i]->setRcCentred( true );
		}
		m_pGameCap = AddText( "GAME", RCF_LABEL, RCC_DIM );
		m_pNew = AddButton( "New game", CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_NEW );
		m_pNew->setRcCentred( true );
		m_pContinue = AddButton( "Continue", CRcUiButton::STYLE_ROW, RCF_BODY, RC_LOBBY_CONTINUE );
		m_pContinue->setRcCentred( true );
		m_pStatus = AddText( "", RCF_LABEL, RCC_DIM );
		m_pStatus->setRcAlign( 1 );
		m_pInvite = AddButton( "Invite friends", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_INVITE );
		m_pLeave = AddButton( "Leave", CRcUiButton::STYLE_BOX, RCF_BUTTON, RC_LOBBY_LEAVE );
		m_pStart = AddButton( "Start", CRcUiButton::STYLE_PRIMARY, RCF_BUTTON, RC_LOBBY_START );
		// under the frame: the launch options a friend's Steam needs to start ReCoF from an invite (B395)
		m_pLaunch = new CRcUiButton( "Copy Steam launch options", 0, 0, 10, 10, CRcUiButton::STYLE_ITEM, RCF_LABEL );
		m_pLaunch->setRcCentred( true );
		m_pLaunch->setParent( this );
		m_pLaunch->addActionSignal( new CRcCoopHandler( RC_LOBBY_LAUNCH ) );
		m_iGame = 0;
		m_bSave = false;
		m_flScale = 0;
		setVisible( false );
	}

	// from the menu (bHost: a lobby of this player's own when there is none), or Steam brought the player into one
	void Open( bool bHost )
	{
		m_bSave = RcCoopLastSave( m_szSave, sizeof( m_szSave ) );
		if ( !m_bSave )
			m_iGame = 0;
		if ( bHost && RcLobby_State() == RCLOBBY_NONE )
			RcLobby_Create();
		m_pLaunch->setRcText( "Copy Steam launch options" );
		Layout();
		Refresh();
		setVisible( true );
		if ( s_pRcMenu )
			s_pRcMenu->Panel::setVisible( false );	// Panel's: CGameMenu's own would start its music again
		gViewPort->UpdateCursorState();
	}

	void Close( void )
	{
		setVisible( false );
		if ( s_pRcMenu && strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
			s_pRcMenu->Panel::setVisible( true );
		gViewPort->UpdateCursorState();
	}

	int RcSkill( void )
	{
		int iSkill = (int)gEngfuncs.pfnGetCvarFloat( "difficulty" );
		if ( iSkill < 1 || iSkill > 4 || ( iSkill == 4 && !( gHUD.m_iUnlocks_guess & 32 ) ) )
			iSkill = 2;
		return iSkill;
	}

	// this player decides: the lobby's owner, or a player alone (no lobby: Steam is not there)
	bool Hosting( void )
	{
		return RcLobby_State() != RCLOBBY_IN || RcLobby_Owner();
	}

	void Command( int cmd )
	{
		char szCmd[128];
		bool bHost = Hosting();
		if ( cmd >= RC_LOBBY_SKILL && cmd < RC_LOBBY_SKILL + 4 )
		{
			int iSkill = cmd - RC_LOBBY_SKILL + 1;
			if ( !bHost || ( iSkill == 4 && !( gHUD.m_iUnlocks_guess & 32 ) ) )
				return;
			_snprintf( szCmd, sizeof( szCmd ) - 1, "difficulty %d\n", iSkill );
			szCmd[sizeof( szCmd ) - 1] = 0;
			gEngfuncs.pfnClientCmd( szCmd );
			gEngfuncs.Cvar_SetValue( "difficulty", iSkill );	// shown at once, before the command runs
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			return;
		}
		switch ( cmd )
		{
		case RC_LOBBY_CLOSE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Close();
			break;
		case RC_LOBBY_LEAVE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			RcLobby_Leave();
			Close();
			break;
		case RC_LOBBY_INVITE:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			if ( !RcLobby_Invite() )
				m_pStatus->setRcText( "Steam's overlay is not available" );
			break;
		case RC_LOBBY_NEW:
		case RC_LOBBY_CONTINUE:
			if ( !bHost || ( cmd == RC_LOBBY_CONTINUE && !m_bSave ) )
				return;
			m_iGame = cmd == RC_LOBBY_CONTINUE ? 1 : 0;
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			Refresh();
			break;
		case RC_LOBBY_LAUNCH:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			m_pLaunch->setRcText( RcCoopCopyLaunchOptions() ? "Copied: paste it in Cry of Fear's launch options on Steam" : "Could not copy them" );
			break;
		case RC_LOBBY_START:
			gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
			if ( !bHost )
			{
				RcLobby_JoinGame();	// the host's game is up: join it
				return;
			}
			if ( m_iGame == 1 && !m_bSave )
				return;
			Close();
			if ( s_pRcMenu )
				s_pRcMenu->setVisible( false );
			gViewPort->UpdateCursorState();
			if ( strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
				IrrKlangStop_guess( 1 );
			if ( m_iGame == 0 )
				_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_coop_start c_intro %s\n", s_pszRcPreset[RcSkill()] );
			else
				_snprintf( szCmd, sizeof( szCmd ) - 1, "rc_coop_continue %s\n", m_szSave );
			szCmd[sizeof( szCmd ) - 1] = 0;
			gEngfuncs.pfnClientCmd( szCmd );
			break;
		}
	}

	// every frame while shown: the lobby's players, the host's choices, what happens next
	void Refresh( void )
	{
		int iState = RcLobby_State();
		bool bHost = Hosting();
		char szNames[4][48];
		bool bIsHost[4], bIsSelf[4];
		int n = iState == RCLOBBY_IN ? RcLobby_Members( szNames, bIsHost, bIsSelf, 4 ) : 0;
		if ( n == 0 )
		{
			const char *pszMe = RcSteamNet_PersonaName();
			strncpy( szNames[0], pszMe && pszMe[0] ? pszMe : "You", 47 );
			szNames[0][47] = 0;
			bIsHost[0] = bIsSelf[0] = true;
			n = 1;
		}
		for ( int i = 0; i < 4; i++ )
		{
			if ( i < n )
			{
				m_pName[i]->setRcText( szNames[i] );
				m_pName[i]->setRcColor( RCC_TEXT );
				m_pTag[i]->setRcText( bIsHost[i] && bIsSelf[i] ? "Host, you" : bIsHost[i] ? "Host" : bIsSelf[i] ? "You" : "" );
			}
			else
			{
				m_pName[i]->setRcText( "Open" );
				m_pName[i]->setRcColor( RCC_DISABLED );
				m_pTag[i]->setRcText( "" );
			}
		}

		// the host's choices: his own, or the lobby's for a guest
		if ( bHost )
		{
			char sz[8];
			_snprintf( sz, sizeof( sz ) - 1, "%d", RcSkill() );
			sz[sizeof( sz ) - 1] = 0;
			if ( strcmp( RcLobby_Data( "difficulty" ), sz ) )
				RcLobby_SetData( "difficulty", sz );
			if ( strcmp( RcLobby_Data( "game" ), m_iGame ? "continue" : "new" ) )
				RcLobby_SetData( "game", m_iGame ? "continue" : "new" );
		}
		int iSkill = bHost ? RcSkill() : atoi( RcLobby_Data( "difficulty" ) );
		bool bContinue = bHost ? m_iGame == 1 : !strcmp( RcLobby_Data( "game" ), "continue" );
		for ( int i = 0; i < 4; i++ )
		{
			m_pSkill[i]->setRcSelected( i + 1 == iSkill );
			m_pSkill[i]->setRcDisabled( !bHost || ( i == 3 && !( gHUD.m_iUnlocks_guess & 32 ) ) );
		}
		m_pNew->setRcSelected( !bContinue );
		m_pNew->setRcDisabled( !bHost );
		m_pContinue->setRcSelected( bContinue );
		m_pContinue->setRcDisabled( !bHost || !m_bSave );
		char szCont[48];
		if ( bHost && m_bSave )
			_snprintf( szCont, sizeof( szCont ) - 1, "Continue (%s)", m_szSave );
		else
			strcpy( szCont, "Continue" );
		szCont[sizeof( szCont ) - 1] = 0;
		m_pContinue->setRcText( szCont );

		// a host back on the menu after a game: the lobby's guests no longer have a game to join
		const char *pszConnect = RcLobby_Data( "connect" );
		if ( iState == RCLOBBY_IN && RcLobby_Owner() && pszConnect[0] && strstr( gEngfuncs.pfnGetLevelName(), "game_menu" ) )
		{
			RcLobby_SetData( "connect", "" );
			pszConnect = "";
		}

		const char *pszStatus = "";
		if ( iState == RCLOBBY_ASKED )
			pszStatus = "Opening the lobby...";
		else if ( iState == RCLOBBY_NONE )
			pszStatus = RcLobby_Note()[0] ? RcLobby_Note() : "";
		else if ( bHost )
			pszStatus = n < 2 ? "Invite your friends, then start" : "";
		else
			pszStatus = pszConnect[0] ? "The host's game is on" : "Waiting for the host to start";
		m_pStatus->setRcText( pszStatus );
		m_pInvite->setVisible( iState == RCLOBBY_IN );
		m_pStart->setRcText( bHost ? "Start" : "Join game" );
		m_pStart->setVisible( bHost || pszConnect[0] );
	}

	void Layout( void )
	{
		m_flScale = RcUiScale();
		int W = RcUiS( 620 ), H = RcUiS( 470 );
		if ( W > ScreenWidth - 32 )
			W = ScreenWidth - 32;
		int x0 = ( ScreenWidth - W ) / 2, y0 = ( ScreenHeight - H ) / 2 - RcUiS( 16 );
		if ( y0 < 0 )
			y0 = 0;
		m_pFrame->setBounds( x0, y0, W, H );
		int band = m_pFrame->bandTall(), pad = RcUiS( 24 ), gap = RcUiS( 8 );
		m_pClose->setBounds( W - band, 0, band, band );
		int y = band + RcUiS( 18 );
		m_pPlayersCap->setBounds( pad, y, W - 2 * pad, RcUiS( 20 ) );
		y += RcUiS( 24 );
		for ( int i = 0; i < 4; i++ )
		{
			m_pName[i]->setBounds( pad, y + i * RcUiS( 30 ), W - 2 * pad, RcUiS( 28 ) );
			m_pTag[i]->setBounds( pad, y + i * RcUiS( 30 ), W - 2 * pad, RcUiS( 28 ) );
		}
		y += 4 * RcUiS( 30 ) + RcUiS( 14 );
		m_pSkillCap->setBounds( pad, y, W - 2 * pad, RcUiS( 20 ) );
		y += RcUiS( 24 );
		int bw = ( W - 2 * pad - 3 * gap ) / 4;
		for ( int i = 0; i < 4; i++ )
			m_pSkill[i]->setBounds( pad + i * ( bw + gap ), y, bw, RcUiS( 32 ) );
		y += RcUiS( 32 ) + RcUiS( 16 );
		m_pGameCap->setBounds( pad, y, W - 2 * pad, RcUiS( 20 ) );
		y += RcUiS( 24 );
		int gw = ( W - 2 * pad - gap ) / 2;
		m_pNew->setBounds( pad, y, gw, RcUiS( 32 ) );
		m_pContinue->setBounds( pad + gw + gap, y, gw, RcUiS( 32 ) );
		int by = H - RcUiS( 20 ) - RcUiS( 34 );
		m_pStatus->setBounds( pad, by - RcUiS( 32 ), W - 2 * pad, RcUiS( 22 ) );
		m_pInvite->setBounds( pad, by, RcUiS( 170 ), RcUiS( 34 ) );
		m_pStart->setBounds( W - pad - RcUiS( 150 ), by, RcUiS( 150 ), RcUiS( 34 ) );
		m_pLeave->setBounds( W - pad - RcUiS( 150 ) - RcUiS( 12 ) - RcUiS( 120 ), by, RcUiS( 120 ), RcUiS( 34 ) );
		m_pLaunch->setBounds( x0, y0 + H + RcUiS( 10 ), W, RcUiS( 28 ) );
	}

	virtual void solve()
	{
		Panel::solve();
		// the guest's game joined the host's: the lobby goes with the menu
		const char *pszLevel = gEngfuncs.pfnGetLevelName();
		if ( pszLevel && pszLevel[0] && !strstr( pszLevel, "game_menu" ) )
		{
			setVisible( false );
			gViewPort->UpdateCursorState();
			return;
		}
		if ( RcUiScale() != m_flScale )
			Layout();
		Refresh();
	}

	// a dark veil over the menu's scene
	virtual void paintBackground()
	{
		int wide, tall;
		getPaintSize( wide, tall );
		drawSetColor( RCC_SCRIM.r, RCC_SCRIM.g, RCC_SCRIM.b, 255 - RCC_SCRIM.a );
		drawFilledRect( 0, 0, wide, tall );
	}

private:
	CRcUiText *AddText( const char *pszText, int font, const RcUiColor &color )
	{
		CRcUiText *p = new CRcUiText( pszText, 0, 0, 10, 10, font, color );
		p->setParent( m_pFrame );
		return p;
	}

	CRcUiButton *AddButton( const char *pszText, int style, int font, int cmd )
	{
		CRcUiButton *p = new CRcUiButton( pszText, 0, 0, 10, 10, style, font );
		p->setParent( m_pFrame );
		p->addActionSignal( new CRcCoopHandler( cmd ) );
		return p;
	}

	CRcUiFrame *m_pFrame;
	CRcUiButton *m_pClose, *m_pSkill[4], *m_pNew, *m_pContinue, *m_pInvite, *m_pLeave, *m_pStart, *m_pLaunch;
	CRcUiText *m_pPlayersCap, *m_pName[4], *m_pTag[4], *m_pSkillCap, *m_pGameCap, *m_pStatus;
	int m_iGame;	// 0 a new game, 1 the last co-op save
	bool m_bSave;
	char m_szSave[32];
	float m_flScale;
};

void CRcCoopHandler::actionPerformed( Panel *panel )
{
	if ( !s_pRcPanel )
	{
		s_pRcPanel = new CRcCoopPanel();
		s_pRcPanel->setParent( gViewPort );
	}
	if ( m_cmd == RC_LOBBY_OPEN )
	{
		// the menu's entry: the lobby in place of the menu's entries (the viewport keeps the cursor for it,
		// RcCoopMenu_Visible)
		gEngfuncs.pfnPlaySoundByName( "ui/3d_click.wav", 0.5 );
		s_pRcPanel->Open( true );
		return;
	}
	s_pRcPanel->Command( m_cmd );
}

// TeamFortressViewport::UpdateCursorState: the panel needs the mouse cursor
bool RcCoopMenu_Visible( void )
{
	return s_pRcPanel && s_pRcPanel->isVisible();
}

// rc_coop_launchoptions: prints the Steam launch options the panel copies (B395)
void RcCoopMenu_LaunchOptions( void )
{
	char text[MAX_PATH + 32];
	if ( RcCoopLaunchOptions( text, sizeof( text ) ) )
		gEngfuncs.Con_Printf( "ReCoF co-op: Steam, Cry of Fear, Properties, Launch options: %s\n", text );
}

// rc_coop_menu: the Story Co-op panel, as the menu's entry opens it
void RcCoopMenu_Open( void )
{
	CRcCoopHandler open( 1 );
	open.actionPerformed( NULL );
}

// CGameMenu::CGameMenu: the entry (picture and button) at x, y
void RcCoopMenu_Create( CGameMenu *pMenu, int x, int y, int wide, int tall )
{
	s_pRcMenu = pMenu;
	s_pRcLabel = RcCoopLabel( 255, 255, 255 );
	s_pRcLabelArmed = RcCoopLabel( 217, 0, 0 );

	s_pRcItem = new CRcImagePanel( s_pRcLabel );
	s_pRcItem->setParent( pMenu );
	s_pRcItem->setPos( x, y );
	s_pRcItem->setVisible( true );

	s_pRcItemButton = new ColorButton( "", x, y, wide, tall, false, true );
	s_pRcItemButton->setParent( pMenu );
	s_pRcItemButton->addActionSignal( new CRcCoopHandler( 1 ) );
}

// CGameMenu::ResetButtonPositions_guess
void RcCoopMenu_SetPos( int x, int y )
{
	if ( s_pRcItem )
		s_pRcItem->setPos( x, y );
}

// the menu shows or hides its entries (the custom campaign list hides them)
void RcCoopMenu_Show( bool bShow )
{
	if ( s_pRcItem )
		s_pRcItem->setVisible( bShow );
	if ( s_pRcItemButton )
		s_pRcItemButton->setVisible( bShow );
}

// B408: rc_coop_lobby, the game menu's Story co-op: the lobby, on the main menu (its map c_game_menu1, which the
// game menu's 3D menu loads; from a game or the game menu's own screen it is loaded first)
static bool s_bRcLobbyOnMenu;

void RcCoopMenu_Lobby( void )
{
	const char *pszLevel = gEngfuncs.pfnGetLevelName();
	if ( s_pRcMenu && pszLevel && strstr( pszLevel, "game_menu" ) )
	{
		RcCoopMenu_Open();
		return;
	}
	s_bRcLobbyOnMenu = true;
	gEngfuncs.pfnClientCmd( "disconnect;maxplayers 1;deathmatch 0;map c_game_menu1\n" );
}

// CGameMenu::solve, each frame: the red picture and the shake under the cursor, as the menu's other entries
void RcCoopMenu_Solve( void )
{
	if ( s_bRcLobbyOnMenu )
	{
		s_bRcLobbyOnMenu = false;
		RcCoopMenu_Open();
	}
	// B406: Steam brought this player into a lobby (an invite, Join Game, +connect_lobby): the menu shows it
	if ( RcLobby_WantPanel() )
	{
		if ( !s_pRcPanel )
		{
			s_pRcPanel = new CRcCoopPanel();
			s_pRcPanel->setParent( gViewPort );
		}
		s_pRcPanel->Open( false );
	}
	if ( !s_pRcItemButton || !s_pRcItem )
		return;
	if ( s_pRcItemButton->isArmed() )
	{
		if ( !s_bRcArmed )
		{
			s_pRcItem->setImage( s_pRcLabelArmed );
			gEngfuncs.pfnPlaySoundByName( "ui/3d_rollover.wav", 0.5 );
			s_bRcArmed = true;
			s_pRcItem->getPos( s_iRcArmedX, s_iRcArmedY );
		}
		else
			s_pRcItem->setPos( s_iRcArmedX + gEngfuncs.pfnRandomLong( -3, 3 ), s_iRcArmedY );
	}
	else if ( s_bRcArmed )
	{
		s_pRcItem->setImage( s_pRcLabel );
		s_bRcArmed = false;
		if ( s_pRcMenu )
			s_pRcMenu->ResetButtonPositions_guess();
	}
}

// MsgGameMenu_guess: the menu comes back with every picture unlit
void RcCoopMenu_Reset( void )
{
	if ( s_pRcItem && s_pRcLabel )
		s_pRcItem->setImage( s_pRcLabel );
	s_bRcArmed = false;
}
