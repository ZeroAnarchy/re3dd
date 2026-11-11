#pragma once
// Controls round 2026-11-09 (menu agent): the 3DD menu itself - strings EN/RU, pages, cursor, key/button capture, and its
// drawing with the D3D9 device it is given (GDI text -> textures, crisp at any render size). No game memory here: the
// game side (mod_menu.h) implements Host; the offline test (qa/ctl-2026-11-09/menu/tools/menu_shots.cpp) has its own.
// Non-ASCII text is written as \u escapes (the build has no /utf-8).
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "mod_input.h"
#pragma comment(lib, "gdi32.lib")

namespace re3menu {

// ================================================================== strings
struct Text2 { const wchar_t* en; const wchar_t* ru; };
enum Str {
    S_Menu, S_Paused, S_PageControls, S_PageOptions, S_MainControls, S_MainOptions, S_MainNative, S_MainNativeNote, S_ResetAll,
    S_Close, S_Back, S_SavedNote, S_ColAction, S_ColKeyboard, S_ColGamepad, S_GroupGame, S_GroupMod, S_ResetControls,
    S_ResetOptions, S_CaptureKey, S_CapturePad, S_Cancelled, S_Added, S_Removed, S_MovedFrom, S_ReservedMenu, S_ReservedNav,
    S_Cleared, S_NoPad, S_Max16, S_PressAgain, S_DefaultsControls, S_DefaultsOptions, S_DefaultsAll, S_MenuNeedsKey, S_On, S_Off,
    S_AfterRestart, S_NeedsModels, S_NotMerce, S_HintMain, S_HintControls, S_HintOptions, S_HintCapture, S_GroupGraphics,
    S_GroupEffects, S_GroupSound, S_GroupMovies, S_GroupPlay, S_GroupMenu, S_Unavailable, S_PadNone, S_Saved, S_NextMovie,
    S_ReservedGame, S_HintCaptureMod, S_GroupScreen, S_GroupCompat, S_GroupTest, S_Already, S_DescControls, S_DescOptions,
    S_DescNative, S_DescResetAll, S_DescClose, S_DescBindGame, S_DescBindMod, S_DescResetControls, S_DescResetOptions, S_DescBack,
    // round 7 (exit rows): the two descriptions continue the S_Desc* range (S_DescControls .. S_DescExitDesktop)
    S_DescExitTitle, S_DescExitDesktop, S_ExitTitle, S_ExitDesktop, S_PressAgainExit, S_PressAgainQuit, S_ExitNoGame, S_ExitSaveScreen,
    S_ExitNotNow,
    // fix 2026-11-11: why a row is greyed by another row (Host::OptBlocked), the LIGHTING group, the heading of the scheme row
    S_NeedsHd, S_NeedsEnemies, S_NeedsGpu, S_NeedsFollow, S_NeedsWide, S_NeedsBlood, S_NeedsOwnMovies, S_GroupLight, S_GroupMove,
    // voice packs 2026-11-11: the voice-over row without any pack in 3DD-Voice
    S_NoVoicePacks,
    // fix 2026-11-11d: the two shadow rows under "Dynamic shadows"
    S_NeedsShadows, S_Count
};
static const Text2 kStr[S_Count] = {
    {L"3DD MENU", L"\u041c\u0415\u041d\u042e 3DD"},
    {L"game paused", L"\u0438\u0433\u0440\u0430 \u043d\u0430 \u043f\u0430\u0443\u0437\u0435"},
    {L"Controls", L"\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435"},
    {L"3DD options", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 3DD"},
    {L"Controls: scheme, keyboard and gamepad", L"\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435: \u0441\u0445\u0435\u043c\u0430, \u043a\u043b\u0430\u0432\u0438\u0430\u0442\u0443\u0440\u0430 \u0438 \u0433\u0435\u0439\u043c\u043f\u0430\u0434"},
    {L"3DD options (screen, HD, lighting, effects)", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 3DD (\u044d\u043a\u0440\u0430\u043d, HD, \u043e\u0441\u0432\u0435\u0449\u0435\u043d\u0438\u0435, \u044d\u0444\u0444\u0435\u043a\u0442\u044b)"},
    {L"Game settings window (video, sound)", L"\u041e\u043a\u043d\u043e \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a \u0438\u0433\u0440\u044b (\u0432\u0438\u0434\u0435\u043e, \u0437\u0432\u0443\u043a)"},
    {L"opens the game's own Windows dialog", L"\u043e\u0442\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u0440\u043e\u0434\u043d\u043e\u0435 \u043e\u043a\u043d\u043e Windows"},
    {L"Reset everything to defaults", L"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0432\u0441\u0451 \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"Close", L"\u0417\u0430\u043a\u0440\u044b\u0442\u044c"},
    {L"Back", L"\u041d\u0430\u0437\u0430\u0434"},
    {L"Changes are saved at once.", L"\u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442\u0441\u044f \u0441\u0440\u0430\u0437\u0443."},
    {L"Action", L"\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435"},
    {L"Keyboard", L"\u041a\u043b\u0430\u0432\u0438\u0430\u0442\u0443\u0440\u0430"},
    {L"Gamepad", L"\u0413\u0435\u0439\u043c\u043f\u0430\u0434"},
    {L"GAME", L"\u0418\u0413\u0420\u0410"},
    {L"3DD MOD", L"\u041c\u041e\u0414 3DD"},
    {L"Reset controls to defaults", L"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"Reset options to defaults", L"\u0421\u0431\u0440\u043e\u0441\u0438\u0442\u044c \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"Press a key...   Esc: cancel", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0443...   Esc - \u043e\u0442\u043c\u0435\u043d\u0430"},
    {L"Press a button or move a stick...", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u043a\u043d\u043e\u043f\u043a\u0443 \u0438\u043b\u0438 \u043e\u0442\u043a\u043b\u043e\u043d\u0438\u0442\u0435 \u0441\u0442\u0438\u043a..."},
    {L"Cancelled", L"\u041e\u0442\u043c\u0435\u043d\u0435\u043d\u043e"},
    {L"Added: %s", L"\u0414\u043e\u0431\u0430\u0432\u043b\u0435\u043d\u043e: %s"},
    {L"Removed: %s", L"\u0423\u0431\u0440\u0430\u043d\u043e: %s"},
    {L"%s moved here from \u201c%s\u201d", L"%s \u043f\u0435\u0440\u0435\u043d\u0435\u0441\u0435\u043d\u043e \u0441\u044e\u0434\u0430 \u0438\u0437 \u00ab%s\u00bb"},
    {L"%s opens this menu", L"%s \u043e\u0442\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u044d\u0442\u043e \u043c\u0435\u043d\u044e"},
    {L"%s is needed to control the menu", L"%s \u043d\u0443\u0436\u043d\u0430 \u0434\u043b\u044f \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f \u043c\u0435\u043d\u044e"},
    {L"Cleared", L"\u041e\u0447\u0438\u0449\u0435\u043d\u043e"},
    {L"No gamepad found", L"\u0413\u0435\u0439\u043c\u043f\u0430\u0434 \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d"},
    {L"Up to 16 per action", L"\u041d\u0435 \u0431\u043e\u043b\u044c\u0448\u0435 16 \u043d\u0430 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435"},
    {L"Press again to reset", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u0435\u0449\u0451 \u0440\u0430\u0437 \u0434\u043b\u044f \u0441\u0431\u0440\u043e\u0441\u0430"},
    {L"Controls reset to defaults", L"\u0423\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0441\u0431\u0440\u043e\u0448\u0435\u043d\u043e \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"Options reset to defaults", L"\u041d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u0441\u0431\u0440\u043e\u0448\u0435\u043d\u044b \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"Everything reset to defaults", L"\u0412\u0441\u0451 \u0441\u0431\u0440\u043e\u0448\u0435\u043d\u043e \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e"},
    {L"The menu needs a keyboard key", L"\u041c\u0435\u043d\u044e \u043d\u0443\u0436\u043d\u0430 \u043a\u043b\u0430\u0432\u0438\u0448\u0430 \u043d\u0430 \u043a\u043b\u0430\u0432\u0438\u0430\u0442\u0443\u0440\u0435"},
    {L"On", L"\u0412\u043a\u043b."},
    {L"Off", L"\u0412\u044b\u043a\u043b."},
    {L"after restart", L"\u043f\u043e\u0441\u043b\u0435 \u043f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u043a\u0430"},
    {L"needs HD 3D models", L"\u043d\u0443\u0436\u043d\u044b HD 3D-\u043c\u043e\u0434\u0435\u043b\u0438"},
    {L"not in Mercenaries", L"\u043d\u0435 \u0432 \u00ab\u041d\u0430\u0451\u043c\u043d\u0438\u043a\u0430\u0445\u00bb"},
    {L"\u2191\u2193 choose     Enter / A: select     Esc / B: back     %s: close",
     L"\u2191\u2193 \u0432\u044b\u0431\u043e\u0440     Enter / A - \u0432\u044b\u0431\u0440\u0430\u0442\u044c     Esc / B - \u043d\u0430\u0437\u0430\u0434     %s - \u0437\u0430\u043a\u0440\u044b\u0442\u044c"},
    {L"\u2191\u2193 \u2190\u2192 choose     Enter / A: add     Del / X: clear     Tab / LB RB: page     Esc / B: back",
     L"\u2191\u2193 \u2190\u2192 \u0432\u044b\u0431\u043e\u0440     Enter / A - \u0434\u043e\u0431\u0430\u0432\u0438\u0442\u044c     Del / X - \u043e\u0447\u0438\u0441\u0442\u0438\u0442\u044c     Tab / LB RB - \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0430     Esc / B - \u043d\u0430\u0437\u0430\u0434"},
    {L"\u2191\u2193 choose     \u2190\u2192 change     Tab / LB RB: page     Esc / B: back     %s: close",
     L"\u2191\u2193 \u0432\u044b\u0431\u043e\u0440     \u2190\u2192 \u0438\u0437\u043c\u0435\u043d\u0438\u0442\u044c     Tab / LB RB - \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0430     Esc / B - \u043d\u0430\u0437\u0430\u0434     %s - \u0437\u0430\u043a\u0440\u044b\u0442\u044c"},
    {L"The input is added to this action (and leaves the action it was on).     Cancel: %s",
     L"\u041d\u0430\u0436\u0430\u0442\u0438\u0435 \u0434\u043e\u0431\u0430\u0432\u0438\u0442\u0441\u044f \u043a \u044d\u0442\u043e\u043c\u0443 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044e (\u0438 \u0443\u0439\u0434\u0451\u0442 \u0441 \u0442\u043e\u0433\u043e, \u0433\u0434\u0435 \u0431\u044b\u043b\u043e).     \u041e\u0442\u043c\u0435\u043d\u0430: %s"},
    {L"GRAPHICS", L"\u0413\u0420\u0410\u0424\u0418\u041a\u0410"},
    {L"EFFECTS", L"\u042d\u0424\u0424\u0415\u041a\u0422\u042b"},
    {L"SOUND", L"\u0417\u0412\u0423\u041a"},
    {L"MOVIES", L"\u0420\u041e\u041b\u0418\u041a\u0418"},
    {L"CONTROLS", L"\u0423\u041f\u0420\u0410\u0412\u041b\u0415\u041d\u0418\u0415"},
    {L"MENU", L"\u041c\u0415\u041d\u042e"},
    {L"not available", L"\u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e"},
    {L"\u2014", L"\u2014"},
    {L"Saved", L"\u0421\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u043e"},
    {L"next movie", L"\u0441\u043e \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u0433\u043e \u0440\u043e\u043b\u0438\u043a\u0430"},
    {L"%s is taken by the game window", L"%s \u0437\u0430\u043d\u044f\u0442\u0430 \u043e\u043a\u043d\u043e\u043c \u0438\u0433\u0440\u044b"},
    {L"The new input replaces the old one.     Cancel: %s", L"\u041d\u043e\u0432\u043e\u0435 \u043d\u0430\u0436\u0430\u0442\u0438\u0435 \u0437\u0430\u043c\u0435\u043d\u0438\u0442 \u0441\u0442\u0430\u0440\u043e\u0435.     \u041e\u0442\u043c\u0435\u043d\u0430: %s"},
    {L"SCREEN", L"\u042d\u041a\u0420\u0410\u041d"},
    // round 6
    {L"COMPATIBILITY (change only if something is wrong)", L"\u0421\u041e\u0412\u041c\u0415\u0421\u0422\u0418\u041c\u041e\u0421\u0422\u042c (\u043c\u0435\u043d\u044f\u0442\u044c \u0442\u043e\u043b\u044c\u043a\u043e \u043f\u0440\u0438 \u043d\u0435\u043f\u043e\u043b\u0430\u0434\u043a\u0430\u0445)"},
    {L"TEST (will not be in the release)", L"\u0422\u0415\u0421\u0422 (\u0432 \u0440\u0435\u043b\u0438\u0437 \u043d\u0435 \u0432\u043e\u0439\u0434\u0451\u0442)"},
    {L"%s is already assigned here", L"%s \u0443\u0436\u0435 \u043d\u0430\u0437\u043d\u0430\u0447\u0435\u043d\u043e \u0437\u0434\u0435\u0441\u044c"},
    {L"The control scheme (classic or modern), the keys and gamepad buttons of the game's actions and of the mod's own keys. Changes are saved at once.",
     L"\u0421\u0445\u0435\u043c\u0430 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f (\u043a\u043b\u0430\u0441\u0441\u0438\u0447\u0435\u0441\u043a\u0430\u044f \u0438\u043b\u0438 \u0441\u043e\u0432\u0440\u0435\u043c\u0435\u043d\u043d\u0430\u044f), \u043a\u043b\u0430\u0432\u0438\u0448\u0438 \u0438 \u043a\u043d\u043e\u043f\u043a\u0438 \u0433\u0435\u0439\u043c\u043f\u0430\u0434\u0430 \u0434\u043b\u044f \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0439 \u0438\u0433\u0440\u044b \u0438 \u0434\u043b\u044f \u043a\u043b\u0430\u0432\u0438\u0448 \u0441\u0430\u043c\u043e\u0433\u043e \u043c\u043e\u0434\u0430. \u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442\u0441\u044f \u0441\u0440\u0430\u0437\u0443."},
    {L"Widescreen, HD graphics, lighting, effects, sound, movies. Changes are saved at once.",
     L"\u0428\u0438\u0440\u043e\u043a\u0438\u0439 \u044d\u043a\u0440\u0430\u043d, HD-\u0433\u0440\u0430\u0444\u0438\u043a\u0430, \u043e\u0441\u0432\u0435\u0449\u0435\u043d\u0438\u0435, \u044d\u0444\u0444\u0435\u043a\u0442\u044b, \u0437\u0432\u0443\u043a, \u0440\u043e\u043b\u0438\u043a\u0438. \u0418\u0437\u043c\u0435\u043d\u0435\u043d\u0438\u044f \u0441\u043e\u0445\u0440\u0430\u043d\u044f\u044e\u0442\u0441\u044f \u0441\u0440\u0430\u0437\u0443."},
    {L"The game's own settings window: resolution, full screen, volume. This menu closes first.",
     L"\u0420\u043e\u0434\u043d\u043e\u0435 \u043e\u043a\u043d\u043e \u043d\u0430\u0441\u0442\u0440\u043e\u0435\u043a \u0438\u0433\u0440\u044b: \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u0435, \u043f\u043e\u043b\u043d\u044b\u0439 \u044d\u043a\u0440\u0430\u043d, \u0433\u0440\u043e\u043c\u043a\u043e\u0441\u0442\u044c. \u042d\u0442\u043e \u043c\u0435\u043d\u044e \u043f\u0440\u0438 \u044d\u0442\u043e\u043c \u0437\u0430\u043a\u0440\u043e\u0435\u0442\u0441\u044f."},
    {L"Puts the controls and all 3DD options back to their defaults. Press twice to confirm.",
     L"\u0412\u043e\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u0442 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u0435 \u0438 \u0432\u0441\u0435 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 3DD \u043a \u0437\u043d\u0430\u0447\u0435\u043d\u0438\u044f\u043c \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e. \u0414\u043b\u044f \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u043d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u0432\u0430\u0436\u0434\u044b."},
    {L"Back to the game.", L"\u0412\u0435\u0440\u043d\u0443\u0442\u044c\u0441\u044f \u0432 \u0438\u0433\u0440\u0443."},
    {L"Enter / A adds one more key (left column) or gamepad button (right column) to this action; an action may have several. Del / X empties the cell.",
     L"Enter / A \u0434\u043e\u0431\u0430\u0432\u043b\u044f\u0435\u0442 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044e \u0435\u0449\u0451 \u043e\u0434\u043d\u0443 \u043a\u043b\u0430\u0432\u0438\u0448\u0443 (\u043b\u0435\u0432\u0430\u044f \u043a\u043e\u043b\u043e\u043d\u043a\u0430) \u0438\u043b\u0438 \u043a\u043d\u043e\u043f\u043a\u0443 \u0433\u0435\u0439\u043c\u043f\u0430\u0434\u0430 (\u043f\u0440\u0430\u0432\u0430\u044f \u043a\u043e\u043b\u043e\u043d\u043a\u0430); \u0443 \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u044f \u0438\u0445 \u043c\u043e\u0436\u0435\u0442 \u0431\u044b\u0442\u044c \u043d\u0435\u0441\u043a\u043e\u043b\u044c\u043a\u043e. Del / X \u043e\u0447\u0438\u0449\u0430\u0435\u0442 \u044f\u0447\u0435\u0439\u043a\u0443."},
    {L"A key of the mod itself: one keyboard key and one gamepad button. Enter / A replaces it, Del / X removes it.",
     L"\u041a\u043b\u0430\u0432\u0438\u0448\u0430 \u0441\u0430\u043c\u043e\u0433\u043e \u043c\u043e\u0434\u0430: \u043e\u0434\u043d\u0430 \u043a\u043b\u0430\u0432\u0438\u0448\u0430 \u043a\u043b\u0430\u0432\u0438\u0430\u0442\u0443\u0440\u044b \u0438 \u043e\u0434\u043d\u0430 \u043a\u043d\u043e\u043f\u043a\u0430 \u0433\u0435\u0439\u043c\u043f\u0430\u0434\u0430. Enter / A \u0437\u0430\u043c\u0435\u043d\u044f\u0435\u0442 \u0435\u0451, Del / X \u0443\u0431\u0438\u0440\u0430\u0435\u0442."},
    {L"Puts the control scheme and all keys and gamepad buttons back to their defaults. Press twice to confirm.",
     L"\u0412\u043e\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u0442 \u0441\u0445\u0435\u043c\u0443 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f, \u0432\u0441\u0435 \u043a\u043b\u0430\u0432\u0438\u0448\u0438 \u0438 \u043a\u043d\u043e\u043f\u043a\u0438 \u0433\u0435\u0439\u043c\u043f\u0430\u0434\u0430 \u043a \u0437\u043d\u0430\u0447\u0435\u043d\u0438\u044f\u043c \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e. \u0414\u043b\u044f \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u043d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u0432\u0430\u0436\u0434\u044b."},
    {L"Puts all options of this page back to their defaults. Press twice to confirm.",
     L"\u0412\u043e\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u0442 \u0432\u0441\u0435 \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u044d\u0442\u043e\u0439 \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u044b \u043a \u0437\u043d\u0430\u0447\u0435\u043d\u0438\u044f\u043c \u043f\u043e \u0443\u043c\u043e\u043b\u0447\u0430\u043d\u0438\u044e. \u0414\u043b\u044f \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u043d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u0432\u0430\u0436\u0434\u044b."},
    {L"Back to the first page of the menu.", L"\u041d\u0430\u0437\u0430\u0434, \u043d\u0430 \u043f\u0435\u0440\u0432\u0443\u044e \u0441\u0442\u0440\u0430\u043d\u0438\u0446\u0443 \u043c\u0435\u043d\u044e."},
    // round 7 (10.11, user: "add rows to the F5 menu that exit to the game's title menu and to the desktop")
    {L"Ends the current game and goes back to the title screen, the same way the game does it after a game over. Progress since the last save is lost. Press twice to confirm.",
     L"\u0417\u0430\u0432\u0435\u0440\u0448\u0430\u0435\u0442 \u0442\u0435\u043a\u0443\u0449\u0443\u044e \u0438\u0433\u0440\u0443 \u0438 \u0432\u043e\u0437\u0432\u0440\u0430\u0449\u0430\u0435\u0442 \u043d\u0430 \u0442\u0438\u0442\u0443\u043b\u044c\u043d\u044b\u0439 \u044d\u043a\u0440\u0430\u043d \u2014 \u0442\u0435\u043c \u0436\u0435 \u043f\u0443\u0442\u0451\u043c, \u0447\u0442\u043e \u0438 \u0441\u0430\u043c\u0430 \u0438\u0433\u0440\u0430 \u043f\u043e\u0441\u043b\u0435 \u0433\u0438\u0431\u0435\u043b\u0438. \u041f\u0440\u043e\u0433\u0440\u0435\u0441\u0441 \u043f\u043e\u0441\u043b\u0435 \u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0435\u0433\u043e \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u044f \u043f\u0440\u043e\u043f\u0430\u0434\u0451\u0442. \u0414\u043b\u044f \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u043d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u0432\u0430\u0436\u0434\u044b."},
    {L"Closes the game, as \u201cYes\u201d in its own exit window does. Progress since the last save is lost; the settings of this menu are already saved. Press twice to confirm.",
     L"\u0417\u0430\u043a\u0440\u044b\u0432\u0430\u0435\u0442 \u0438\u0433\u0440\u0443 \u2014 \u043a\u0430\u043a \u043a\u043d\u043e\u043f\u043a\u0430 \u00ab\u0414\u0430\u00bb \u0432 \u0435\u0451 \u0440\u043e\u0434\u043d\u043e\u043c \u043e\u043a\u043d\u0435 \u0432\u044b\u0445\u043e\u0434\u0430. \u041f\u0440\u043e\u0433\u0440\u0435\u0441\u0441 \u043f\u043e\u0441\u043b\u0435 \u043f\u043e\u0441\u043b\u0435\u0434\u043d\u0435\u0433\u043e \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u044f \u043f\u0440\u043e\u043f\u0430\u0434\u0451\u0442; \u043d\u0430\u0441\u0442\u0440\u043e\u0439\u043a\u0438 \u044d\u0442\u043e\u0433\u043e \u043c\u0435\u043d\u044e \u0443\u0436\u0435 \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u044b. \u0414\u043b\u044f \u043f\u043e\u0434\u0442\u0432\u0435\u0440\u0436\u0434\u0435\u043d\u0438\u044f \u043d\u0430\u0436\u043c\u0438\u0442\u0435 \u0434\u0432\u0430\u0436\u0434\u044b."},
    {L"Exit to title screen", L"\u0412\u044b\u0439\u0442\u0438 \u0432 \u0442\u0438\u0442\u0443\u043b\u044c\u043d\u043e\u0435 \u043c\u0435\u043d\u044e"},
    {L"Exit to desktop", L"\u0412\u044b\u0439\u0442\u0438 \u043d\u0430 \u0440\u0430\u0431\u043e\u0447\u0438\u0439 \u0441\u0442\u043e\u043b"},
    {L"Press again: unsaved progress will be lost", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u0435\u0449\u0451 \u0440\u0430\u0437: \u043d\u0435\u0441\u043e\u0445\u0440\u0430\u043d\u0451\u043d\u043d\u044b\u0439 \u043f\u0440\u043e\u0433\u0440\u0435\u0441\u0441 \u043f\u0440\u043e\u043f\u0430\u0434\u0451\u0442"},
    {L"Press again to exit", L"\u041d\u0430\u0436\u043c\u0438\u0442\u0435 \u0435\u0449\u0451 \u0440\u0430\u0437 \u0434\u043b\u044f \u0432\u044b\u0445\u043e\u0434\u0430"},
    {L"only during a game", L"\u0442\u043e\u043b\u044c\u043a\u043e \u0432\u043e \u0432\u0440\u0435\u043c\u044f \u0438\u0433\u0440\u044b"},
    {L"not on the save screen", L"\u043d\u0435 \u043d\u0430 \u044d\u043a\u0440\u0430\u043d\u0435 \u0441\u043e\u0445\u0440\u0430\u043d\u0435\u043d\u0438\u044f"},
    {L"not right now", L"\u0441\u0435\u0439\u0447\u0430\u0441 \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u043d\u043e"},
    // fix 2026-11-11
    {L"needs HD graphics", L"\u043d\u0443\u0436\u043d\u0430 HD-\u0433\u0440\u0430\u0444\u0438\u043a\u0430"},
    {L"needs HD enemies", L"\u043d\u0443\u0436\u043d\u044b HD-\u0432\u0440\u0430\u0433\u0438"},
    {L"needs models on the graphics card", L"\u043d\u0443\u0436\u0435\u043d \u0440\u0430\u0441\u0447\u0451\u0442 \u043d\u0430 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0435"},
    {L"only with \u201cfollows Jill\u201d", L"\u0442\u043e\u043b\u044c\u043a\u043e \u0432 \u0440\u0435\u0436\u0438\u043c\u0435 \u00ab\u0417\u0430 \u0414\u0436\u0438\u043b\u043b\u00bb"},
    {L"needs widescreen", L"\u043d\u0443\u0436\u0435\u043d \u0448\u0438\u0440\u043e\u043a\u0438\u0439 \u044d\u043a\u0440\u0430\u043d"},
    {L"needs HD blood", L"\u043d\u0443\u0436\u043d\u0430 HD-\u043a\u0440\u043e\u0432\u044c"},
    {L"needs your own movies", L"\u043d\u0443\u0436\u043d\u044b \u0441\u0432\u043e\u0438 \u0440\u043e\u043b\u0438\u043a\u0438"},
    {L"LIGHTING (experimental)", L"\u041e\u0421\u0412\u0415\u0429\u0415\u041d\u0418\u0415 (\u044d\u043a\u0441\u043f\u0435\u0440\u0438\u043c\u0435\u043d\u0442)"},
    {L"MOVEMENT", L"\u0414\u0412\u0418\u0416\u0415\u041d\u0418\u0415"},
    {L"no packs in 3DD-Voice", L"\u043d\u0435\u0442 \u043f\u0430\u043a\u0435\u0442\u043e\u0432 \u0432 3DD-Voice"},
    {L"needs dynamic shadows", L"\u043d\u0443\u0436\u043d\u044b \u0434\u0438\u043d\u0430\u043c\u0438\u0447\u0435\u0441\u043a\u0438\u0435 \u0442\u0435\u043d\u0438"},   // fix 2026-11-11d
};
inline const wchar_t* T(Str s, bool ru) { return ru ? kStr[s].ru : kStr[s].en; }

// native action labels (index = Key1..KeyE)
static const Text2 kActionName[kActions] = {
    {L"Forward / up", L"\u0412\u043f\u0435\u0440\u0451\u0434 / \u0432\u0432\u0435\u0440\u0445"},
    {L"Back / down", L"\u041d\u0430\u0437\u0430\u0434 / \u0432\u043d\u0438\u0437"},
    {L"Turn left / left", L"\u041f\u043e\u0432\u043e\u0440\u043e\u0442 \u0432\u043b\u0435\u0432\u043e / \u0432\u043b\u0435\u0432\u043e"},
    {L"Turn right / right", L"\u041f\u043e\u0432\u043e\u0440\u043e\u0442 \u0432\u043f\u0440\u0430\u0432\u043e / \u0432\u043f\u0440\u0430\u0432\u043e"},
    {L"Action (extra)", L"\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435 (\u0434\u043e\u043f.)"},
    {L"Run / cancel", L"\u0411\u0435\u0433 / \u043e\u0442\u043c\u0435\u043d\u0430"},
    {L"Map / skip door", L"\u041a\u0430\u0440\u0442\u0430 / \u043f\u0440\u043e\u043f\u0443\u0441\u043a \u0434\u0432\u0435\u0440\u0438"},
    {L"Change target", L"\u0421\u043c\u0435\u043d\u0430 \u0446\u0435\u043b\u0438"},
    {L"Aim (enemies only)", L"\u041f\u0440\u0438\u0446\u0435\u043b (\u0442\u043e\u043b\u044c\u043a\u043e \u0432\u0440\u0430\u0433\u0438)"},
    {L"Aim (all targets)", L"\u041f\u0440\u0438\u0446\u0435\u043b (\u0432\u0441\u0435 \u0446\u0435\u043b\u0438)"},
    {L"Select (PS): back in some screens", L"Select (PS): \u0432\u044b\u0445\u043e\u0434 \u043d\u0430 \u043d\u0435\u043a\u043e\u0442\u043e\u0440\u044b\u0445 \u044d\u043a\u0440\u0430\u043d\u0430\u0445"},
    {L"Status screen (items)", L"\u042d\u043a\u0440\u0430\u043d \u0441\u043e\u0441\u0442\u043e\u044f\u043d\u0438\u044f (\u043f\u0440\u0435\u0434\u043c\u0435\u0442\u044b)"},
    {L"Action / OK / fire", L"\u0414\u0435\u0439\u0441\u0442\u0432\u0438\u0435 / \u0432\u044b\u0431\u043e\u0440 / \u043e\u0433\u043e\u043d\u044c"},
    {L"PS L2 (unused)", L"L2 PS (\u043d\u0435 \u0438\u0441\u043f\u043e\u043b\u044c\u0437\u0443\u0435\u0442\u0441\u044f)"},
};
static const Text2 kModName[kModActions] = {
    {L"Open this menu", L"\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u044d\u0442\u043e \u043c\u0435\u043d\u044e"},
    {L"HD / original graphics", L"HD / \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u0430\u044f \u0433\u0440\u0430\u0444\u0438\u043a\u0430"},
    {L"60 fps HD models on / off", L"60 \u043a/\u0441 HD-\u043c\u043e\u0434\u0435\u043b\u0435\u0439 \u0432\u043a\u043b./\u0432\u044b\u043a\u043b."},
    {L"Reload weapon", L"\u041f\u0435\u0440\u0435\u0437\u0430\u0440\u044f\u0434\u043a\u0430 \u043e\u0440\u0443\u0436\u0438\u044f"},
};

// ================================================================== options (pilot.ini)
enum OptKind { OptBool, OptChoice, OptNumber };
// Round 6 (09.11 night, user: "widescreen first, then the control scheme, then graphics and effects; tidy the HD rows - too
// many of them and unclear what each does and how it differs from the similar one; every row needs a good description at
// the bottom"). The order of the page = the order of this enum / table. No longer shown (still read from pilot.ini by
// their modules): jill_all_rooms and characters_all_rooms, the room limits of the first pilot builds. The three technical
// switches moved to COMPATIBILITY, the Glock rows to TEST (the user: not for the release).
// Fix 2026-11-11 (user: "move the modern controls switch to the controls page; go through all options: a row that another
// row blocks must be grey; see whether everything sits where it belongs and whether some rows should become one row
// with several values"). The order of the options page = this enum up to O_PageCount; O_Scheme is shown on the controls
// page. Merged into one row each (two pilot.ini keys behind it, mod_menu.h): models + characters -> O_Models (off / Jill,
// weapons, objects / all), control_scheme + modern_stick_run -> O_Scheme (classic / modern / modern + stick run),
// hit_sounds + hit_sound_volume -> O_HitSounds (off, 10..200 %). New: O_BloodBright; the two lighting rows have a group of
// their own; the wounds row sits with the blood (EFFECTS). Greyed rows: Host::OptBlocked.
enum OptId {
    O_Widescreen, O_WideSmooth, O_WideSpeed, O_WideDelay, O_WidePeek, O_Enabled, O_Models, O_Fps60, O_Motion, O_Face,
    O_Shadows, O_ShadowEdges, O_ShadowMasks, O_FlashLight, O_FireLight, O_Leather, O_Wounds, O_Blood, O_BloodBright, O_BloodClear, O_Fire, O_Muzzle, O_Cloud, O_Slime, O_Effects, O_HitSounds, O_VoicePack,
    O_PlayerMovies, O_MovieFit, O_GpuModels, O_MovieRgb32, O_MovieSync, O_GlockKneel, O_DodgeAlways, O_Language,
    O_PageCount, O_Scheme = O_PageCount, O_Count
};
// label = the row; desc = what it does, shown in the footer while the row is selected (three lines at most);
// zeroOff: a number row whose 0 reads "Off"
struct OptionDef { const char* key; int def; OptKind kind; Text2 label; int choices; const Text2* choice; int lo, hi, step; Str group; Text2 desc; bool zeroOff = false; };
static const Text2 kFireChoices[2] = {{L"Original", L"\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b"}, {L"HD", L"HD"}};   // fx 2026-11-11f: the "HD clips" choice is removed
static const Text2 kShadowChoices[3] = {{L"Off", L"\u0412\u044b\u043a\u043b."}, {L"On the floor", L"\u041d\u0430 \u043f\u043e\u043b\u0443"}, {L"On the floor and on characters", L"\u041d\u0430 \u043f\u043e\u043b\u0443 \u0438 \u043d\u0430 \u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0430\u0445"}};   // lighting round 2026-11-10
static const Text2 kShadowEdgeChoices[3] = {{L"Endless floor", L"\u0411\u0435\u0441\u043a\u043e\u043d\u0435\u0447\u043d\u044b\u0439 \u043f\u043e\u043b"}, {L"Cut at the edge", L"\u041e\u0431\u0440\u0435\u0437\u0430\u0442\u044c \u0443 \u043a\u0440\u0430\u044f"}, {L"Cut, rest on walls", L"\u041e\u0431\u0440\u0435\u0437\u0430\u0442\u044c, \u043e\u0441\u0442\u0430\u0442\u043e\u043a \u043d\u0430 \u0441\u0442\u0435\u043d\u044b"}};   // fix 2026-11-11d
static const Text2 kModelChoices[3] = {{L"Off", L"\u0412\u044b\u043a\u043b."}, {L"Jill, weapons, objects", L"\u0414\u0436\u0438\u043b\u043b, \u043e\u0440\u0443\u0436\u0438\u0435, \u043f\u0440\u0435\u0434\u043c\u0435\u0442\u044b"}, {L"All characters and enemies", L"\u0412\u0441\u0435 \u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0438 \u0438 \u0432\u0440\u0430\u0433\u0438"}};
static const Text2 kSchemeChoices[3] = {{L"Classic (tank)", L"\u041a\u043b\u0430\u0441\u0441\u0438\u043a\u0430 (\u0442\u0430\u043d\u043a)"}, {L"Modern", L"\u0421\u043e\u0432\u0440\u0435\u043c\u0435\u043d\u043d\u0430\u044f"}, {L"Modern, stick runs", L"\u0421\u043e\u0432\u0440\u0435\u043c\u0435\u043d\u043d\u0430\u044f, \u0431\u0435\u0433 \u0441\u0442\u0438\u043a\u043e\u043c"}};
static const Text2 kWideChoices[3] = {{L"Off (4:3)", L"\u0412\u044b\u043a\u043b. (4:3)"}, {L"Crop, centred", L"\u041e\u0431\u0440\u0435\u0437\u043a\u0430 \u043f\u043e \u0446\u0435\u043d\u0442\u0440\u0443"},
                                      {L"Crop, follows Jill", L"\u041e\u0431\u0440\u0435\u0437\u043a\u0430 \u0437\u0430 \u0414\u0436\u0438\u043b\u043b"}};
// round 4 (09.11 night): spring time of the follow window 0.2 / 0.35 / 0.6 / 1.0 s (widescreen.h kSmoothTimes)
static const Text2 kSmoothChoices[4] = {{L"Low", L"\u041d\u0438\u0437\u043a\u0430\u044f"}, {L"Medium", L"\u0421\u0440\u0435\u0434\u043d\u044f\u044f"}, {L"High", L"\u0412\u044b\u0441\u043e\u043a\u0430\u044f"}, {L"Maximum", L"\u041c\u0430\u043a\u0441\u0438\u043c\u0430\u043b\u044c\u043d\u0430\u044f"}};
// round 5 (09.11 night): speed limit of the follow window none / 80 / 50 / 32 / 20 rows/s (widescreen.h kSpeeds) and the
// start delay 0 / 0.25 / 0.5 / 1 / 1.5 / 2 s (kDelays)
static const Text2 kSpeedChoices[5] = {{L"None", L"\u041d\u0435\u0442"}, {L"High", L"\u0412\u044b\u0441\u043e\u043a\u0438\u0439"}, {L"Medium", L"\u0421\u0440\u0435\u0434\u043d\u0438\u0439"}, {L"Low", L"\u041d\u0438\u0437\u043a\u0438\u0439"}, {L"Very low", L"\u041e\u0447\u0435\u043d\u044c \u043d\u0438\u0437\u043a\u0438\u0439"}};
static const Text2 kDelayChoices[6] = {{L"None", L"\u041d\u0435\u0442"}, {L"0.25 s", L"0,25 \u0441"}, {L"0.5 s", L"0,5 \u0441"}, {L"1 s", L"1 \u0441"}, {L"1.5 s", L"1,5 \u0441"}, {L"2 s", L"2 \u0441"}};
static const Text2 kKneelChoices[2] = {{L"Whole burst", L"\u0412\u0441\u044f \u043e\u0447\u0435\u0440\u0435\u0434\u044c"}, {L"Stands up after 15 (orig.)", L"\u0412\u0441\u0442\u0430\u0451\u0442 \u0447\u0435\u0440\u0435\u0437 15 (\u043e\u0440\u0438\u0433.)"}};
static const Text2 kLangChoices[3] = {{L"Auto (Windows)", L"\u0410\u0432\u0442\u043e (Windows)"}, {L"English", L"English"}, {L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439"}};
// voice packs 2026-11-11: the voice-over row (pilot.ini voice_pack, model_integration.h / voice_packs.h) has a choice count
// and labels from the host (Host::OptChoiceCount / OptChoiceText): choice 0 = this text, then one per pack in 3DD-Voice
static const Text2 kVoiceOriginal = {L"Original (English)", L"\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b (English)"};
// defaults = the released pilot.ini / the modules' own defaults (models=1 is written by the installer)
static const OptionDef kOptions[O_Count] = {
    {"widescreen", 0, OptChoice, {L"Widescreen", L"\u0428\u0438\u0440\u043e\u043a\u0438\u0439 \u044d\u043a\u0440\u0430\u043d"}, 3, kWideChoices, 0, 2, 1, S_GroupScreen,
     {L"The game is drawn for 4:3 screens. A crop fills a 16:9 screen: the top and bottom of the picture are cut off. \u201cFollows Jill\u201d moves the visible part up and down so that Jill stays in view. The inventory, the map and text on black stay 4:3.",
      L"\u0418\u0433\u0440\u0430 \u043d\u0430\u0440\u0438\u0441\u043e\u0432\u0430\u043d\u0430 \u0434\u043b\u044f \u044d\u043a\u0440\u0430\u043d\u043e\u0432 4:3. \u041e\u0431\u0440\u0435\u0437\u043a\u0430 \u0437\u0430\u043f\u043e\u043b\u043d\u044f\u0435\u0442 \u044d\u043a\u0440\u0430\u043d 16:9: \u0432\u0435\u0440\u0445 \u0438 \u043d\u0438\u0437 \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0438 \u0441\u0440\u0435\u0437\u0430\u044e\u0442\u0441\u044f. \u00ab\u0417\u0430 \u0414\u0436\u0438\u043b\u043b\u00bb \u2014 \u0432\u0438\u0434\u0438\u043c\u0430\u044f \u0447\u0430\u0441\u0442\u044c \u0441\u0430\u043c\u0430 \u0441\u0434\u0432\u0438\u0433\u0430\u0435\u0442\u0441\u044f \u0432\u0432\u0435\u0440\u0445 \u0438 \u0432\u043d\u0438\u0437, \u0447\u0442\u043e\u0431\u044b \u0414\u0436\u0438\u043b\u043b \u043e\u0441\u0442\u0430\u0432\u0430\u043b\u0430\u0441\u044c \u0432 \u043a\u0430\u0434\u0440\u0435. \u0418\u043d\u0432\u0435\u043d\u0442\u0430\u0440\u044c, \u043a\u0430\u0440\u0442\u0430 \u0438 \u0442\u0435\u043a\u0441\u0442 \u043d\u0430 \u0447\u0451\u0440\u043d\u043e\u043c \u043e\u0441\u0442\u0430\u044e\u0442\u0441\u044f 4:3."}},
    {"widescreen_smooth", 2, OptChoice, {L"Follow camera smoothing", L"\u041f\u043b\u0430\u0432\u043d\u043e\u0441\u0442\u044c \u043a\u0430\u043c\u0435\u0440\u044b \u0437\u0430 \u0414\u0436\u0438\u043b\u043b"}, 4, kSmoothChoices, 0, 3, 1, S_GroupScreen,
     {L"How softly the visible part catches up with Jill in the \u201cfollows Jill\u201d mode. Higher is calmer, but Jill stays near the edge of the picture longer.",
      L"\u041a\u0430\u043a \u043c\u044f\u0433\u043a\u043e \u0432\u0438\u0434\u0438\u043c\u0430\u044f \u0447\u0430\u0441\u0442\u044c \u043a\u0430\u0434\u0440\u0430 \u0434\u043e\u0433\u043e\u043d\u044f\u0435\u0442 \u0414\u0436\u0438\u043b\u043b \u0432 \u0440\u0435\u0436\u0438\u043c\u0435 \u00ab\u0417\u0430 \u0414\u0436\u0438\u043b\u043b\u00bb. \u0427\u0435\u043c \u0432\u044b\u0448\u0435, \u0442\u0435\u043c \u0441\u043f\u043e\u043a\u043e\u0439\u043d\u0435\u0435 \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0430, \u043d\u043e \u0442\u0435\u043c \u0434\u043e\u043b\u044c\u0448\u0435 \u0414\u0436\u0438\u043b\u043b \u043e\u0441\u0442\u0430\u0451\u0442\u0441\u044f \u0443 \u043a\u0440\u0430\u044f \u043a\u0430\u0434\u0440\u0430."}},
    {"widescreen_speed", 2, OptChoice, {L"Follow camera speed limit", L"\u041f\u0440\u0435\u0434\u0435\u043b \u0441\u043a\u043e\u0440\u043e\u0441\u0442\u0438 \u043a\u0430\u043c\u0435\u0440\u044b \u0437\u0430 \u0414\u0436\u0438\u043b\u043b"}, 5, kSpeedChoices, 0, 4, 1, S_GroupScreen,
     {L"The fastest the visible part may move after Jill. \u201cMedium\u201d is about a quarter of the screen height per second. A lower limit is calmer when she runs; Jill may leave the picture for a moment.",
      L"\u041d\u0430\u0438\u0431\u043e\u043b\u044c\u0448\u0430\u044f \u0441\u043a\u043e\u0440\u043e\u0441\u0442\u044c, \u0441 \u043a\u043e\u0442\u043e\u0440\u043e\u0439 \u0432\u0438\u0434\u0438\u043c\u0430\u044f \u0447\u0430\u0441\u0442\u044c \u043a\u0430\u0434\u0440\u0430 \u0441\u0434\u0432\u0438\u0433\u0430\u0435\u0442\u0441\u044f \u0437\u0430 \u0414\u0436\u0438\u043b\u043b. \u00ab\u0421\u0440\u0435\u0434\u043d\u0438\u0439\u00bb \u2014 \u043e\u043a\u043e\u043b\u043e \u0447\u0435\u0442\u0432\u0435\u0440\u0442\u0438 \u0432\u044b\u0441\u043e\u0442\u044b \u044d\u043a\u0440\u0430\u043d\u0430 \u0432 \u0441\u0435\u043a\u0443\u043d\u0434\u0443. \u0427\u0435\u043c \u043d\u0438\u0436\u0435 \u043f\u0440\u0435\u0434\u0435\u043b, \u0442\u0435\u043c \u0441\u043f\u043e\u043a\u043e\u0439\u043d\u0435\u0435 \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 \u043f\u0440\u0438 \u0431\u0435\u0433\u0435; \u0414\u0436\u0438\u043b\u043b \u043c\u043e\u0436\u0435\u0442 \u043d\u0435\u043d\u0430\u0434\u043e\u043b\u0433\u043e \u0432\u044b\u0439\u0442\u0438 \u0438\u0437 \u043a\u0430\u0434\u0440\u0430."}},
    {"widescreen_delay", 2, OptChoice, {L"Follow camera start delay", L"\u0417\u0430\u0434\u0435\u0440\u0436\u043a\u0430 \u043a\u0430\u043c\u0435\u0440\u044b \u0437\u0430 \u0414\u0436\u0438\u043b\u043b"}, 6, kDelayChoices, 0, 5, 1, S_GroupScreen,
     {L"How long the camera waits before it starts to move. If Jill comes back or the camera angle changes in that time, the picture does not move at all.",
      L"\u0421\u043a\u043e\u043b\u044c\u043a\u043e \u043a\u0430\u043c\u0435\u0440\u0430 \u0436\u0434\u0451\u0442, \u043f\u0440\u0435\u0436\u0434\u0435 \u0447\u0435\u043c \u043d\u0430\u0447\u0430\u0442\u044c \u0441\u0434\u0432\u0438\u0433. \u0415\u0441\u043b\u0438 \u0437\u0430 \u044d\u0442\u043e \u0432\u0440\u0435\u043c\u044f \u0414\u0436\u0438\u043b\u043b \u0432\u0435\u0440\u043d\u0443\u043b\u0430\u0441\u044c \u0438\u043b\u0438 \u0441\u043c\u0435\u043d\u0438\u043b\u0441\u044f \u0440\u0430\u043a\u0443\u0440\u0441, \u043a\u0430\u0434\u0440 \u043d\u0435 \u0441\u0434\u0432\u0438\u0433\u0430\u0435\u0442\u0441\u044f \u0432\u043e\u0432\u0441\u0435."}},
    {"widescreen_peek", 1, OptBool, {L"Right stick looks up and down", L"\u041e\u0441\u043c\u043e\u0442\u0440 \u043f\u0440\u0430\u0432\u044b\u043c \u0441\u0442\u0438\u043a\u043e\u043c"}, 0, nullptr, 0, 1, 1, S_GroupScreen,
     {L"In widescreen the right stick of the gamepad moves the visible part up or down to show what the crop has cut off; release it and the camera returns to Jill. Does nothing while the right stick is assigned to a game action.",
      L"\u0412 \u0448\u0438\u0440\u043e\u043a\u043e\u043c \u044d\u043a\u0440\u0430\u043d\u0435 \u043f\u0440\u0430\u0432\u044b\u0439 \u0441\u0442\u0438\u043a \u0433\u0435\u0439\u043c\u043f\u0430\u0434\u0430 \u0441\u0434\u0432\u0438\u0433\u0430\u0435\u0442 \u0432\u0438\u0434\u0438\u043c\u0443\u044e \u0447\u0430\u0441\u0442\u044c \u043a\u0430\u0434\u0440\u0430 \u0432\u0432\u0435\u0440\u0445 \u0438\u043b\u0438 \u0432\u043d\u0438\u0437 \u2014 \u0442\u0443\u0434\u0430, \u0433\u0434\u0435 \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 \u043e\u0431\u0440\u0435\u0437\u0430\u043d\u0430. \u041e\u0442\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u0441\u0442\u0438\u043a, \u0438 \u043a\u0430\u043c\u0435\u0440\u0430 \u0432\u0435\u0440\u043d\u0451\u0442\u0441\u044f \u043a \u0414\u0436\u0438\u043b\u043b. \u041d\u0435 \u0434\u0435\u0439\u0441\u0442\u0432\u0443\u0435\u0442, \u043f\u043e\u043a\u0430 \u043d\u0430 \u043f\u0440\u0430\u0432\u044b\u0439 \u0441\u0442\u0438\u043a \u043d\u0430\u0437\u043d\u0430\u0447\u0435\u043d\u043e \u0434\u0435\u0439\u0441\u0442\u0432\u0438\u0435 \u0438\u0433\u0440\u044b."}},
    {"enabled", 1, OptBool, {L"HD graphics (F8)", L"HD-\u0433\u0440\u0430\u0444\u0438\u043a\u0430 (F8)"}, 0, nullptr, 0, 1, 1, S_GroupGraphics,
     {L"The master switch of the mod: HD backgrounds, HD models, HD interface and HD effects at once. Off shows the game as the original. F8 switches it during play - handy for comparing.",
      L"\u0413\u043b\u0430\u0432\u043d\u044b\u0439 \u0432\u044b\u043a\u043b\u044e\u0447\u0430\u0442\u0435\u043b\u044c \u043c\u043e\u0434\u0430: HD-\u0444\u043e\u043d\u044b, HD-\u043c\u043e\u0434\u0435\u043b\u0438, HD-\u0438\u043d\u0442\u0435\u0440\u0444\u0435\u0439\u0441 \u0438 HD-\u044d\u0444\u0444\u0435\u043a\u0442\u044b \u0440\u0430\u0437\u043e\u043c. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u0438\u0433\u0440\u0430 \u0432\u044b\u0433\u043b\u044f\u0434\u0438\u0442 \u043a\u0430\u043a \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b. \u041a\u043b\u0430\u0432\u0438\u0448\u0430 F8 \u043f\u0435\u0440\u0435\u043a\u043b\u044e\u0447\u0430\u0435\u0442 \u043f\u0440\u044f\u043c\u043e \u0432 \u0438\u0433\u0440\u0435 \u2014 \u0443\u0434\u043e\u0431\u043d\u043e \u0441\u0440\u0430\u0432\u043d\u0438\u0432\u0430\u0442\u044c."}},
    {"models", 2, OptChoice, {L"HD 3D models", L"HD-\u043c\u043e\u0434\u0435\u043b\u0438 (3D)"}, 3, kModelChoices, 0, 2, 1, S_GroupGraphics,
     {L"New detailed 3D models instead of the original low-polygon ones. \u201cJill, weapons, objects\u201d: only Jill, the weapons and the objects in the rooms; the other characters and the enemies stay original. \u201cAll\u201d: the other characters, the zombies, Nemesis and the other enemies as well. Off: the original models on the HD backgrounds.",
      L"\u041d\u043e\u0432\u044b\u0435 \u0434\u0435\u0442\u0430\u043b\u044c\u043d\u044b\u0435 3D-\u043c\u043e\u0434\u0435\u043b\u0438 \u0432\u043c\u0435\u0441\u0442\u043e \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0445 \u043d\u0438\u0437\u043a\u043e\u043f\u043e\u043b\u0438\u0433\u043e\u043d\u0430\u043b\u044c\u043d\u044b\u0445. \u00ab\u0414\u0436\u0438\u043b\u043b, \u043e\u0440\u0443\u0436\u0438\u0435, \u043f\u0440\u0435\u0434\u043c\u0435\u0442\u044b\u00bb \u2014 \u0442\u043e\u043b\u044c\u043a\u043e \u0414\u0436\u0438\u043b\u043b, \u043e\u0440\u0443\u0436\u0438\u0435 \u0438 \u043f\u0440\u0435\u0434\u043c\u0435\u0442\u044b \u0432 \u043a\u043e\u043c\u043d\u0430\u0442\u0430\u0445; \u043e\u0441\u0442\u0430\u043b\u044c\u043d\u044b\u0435 \u0433\u0435\u0440\u043e\u0438 \u0438 \u0432\u0440\u0430\u0433\u0438 \u043e\u0441\u0442\u0430\u044e\u0442\u0441\u044f \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u043c\u0438. \u00ab\u0412\u0441\u0435\u00bb \u2014 \u0435\u0449\u0451 \u0438 \u043e\u0441\u0442\u0430\u043b\u044c\u043d\u044b\u0435 \u0433\u0435\u0440\u043e\u0438, \u0437\u043e\u043c\u0431\u0438, \u041d\u0435\u043c\u0435\u0437\u0438\u0441 \u0438 \u0434\u0440\u0443\u0433\u0438\u0435 \u0432\u0440\u0430\u0433\u0438. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0435 \u043c\u043e\u0434\u0435\u043b\u0438 \u043d\u0430 HD-\u0444\u043e\u043d\u0430\u0445."}},
    {"fps60", 1, OptBool, {L"60 frames per second (F7)", L"60 \u043a\u0430\u0434\u0440\u043e\u0432 \u0432 \u0441\u0435\u043a\u0443\u043d\u0434\u0443 (F7)"}, 0, nullptr, 0, 1, 1, S_GroupGraphics,
     {L"HD models move at 60 frames per second: in-between poses are drawn between the game's frames. The game itself still runs 30 times a second, so its speed does not change. Needs the HD 3D models.",
      L"HD-\u043c\u043e\u0434\u0435\u043b\u0438 \u0434\u0432\u0438\u0433\u0430\u044e\u0442\u0441\u044f \u0441 \u0447\u0430\u0441\u0442\u043e\u0442\u043e\u0439 60 \u043a\u0430\u0434\u0440\u043e\u0432 \u0432 \u0441\u0435\u043a\u0443\u043d\u0434\u0443: \u043c\u0435\u0436\u0434\u0443 \u043a\u0430\u0434\u0440\u0430\u043c\u0438 \u0438\u0433\u0440\u044b \u0440\u0438\u0441\u0443\u044e\u0442\u0441\u044f \u043f\u0440\u043e\u043c\u0435\u0436\u0443\u0442\u043e\u0447\u043d\u044b\u0435 \u043f\u043e\u0437\u044b. \u0421\u0430\u043c\u0430 \u0438\u0433\u0440\u0430 \u043f\u043e-\u043f\u0440\u0435\u0436\u043d\u0435\u043c\u0443 \u0441\u0447\u0438\u0442\u0430\u0435\u0442\u0441\u044f 30 \u0440\u0430\u0437 \u0432 \u0441\u0435\u043a\u0443\u043d\u0434\u0443 \u2014 \u0435\u0451 \u0441\u043a\u043e\u0440\u043e\u0441\u0442\u044c \u043d\u0435 \u043c\u0435\u043d\u044f\u0435\u0442\u0441\u044f. \u041d\u0443\u0436\u043d\u044b HD-\u043c\u043e\u0434\u0435\u043b\u0438."}},
    {"motion_smoothing", 1, OptBool, {L"Smooth animation", L"\u0421\u0433\u043b\u0430\u0436\u0438\u0432\u0430\u043d\u0438\u0435 \u0430\u043d\u0438\u043c\u0430\u0446\u0438\u0438"}, 0, nullptr, 0, 1, 1, S_GroupGraphics,
     {L"Removes the stop-motion look of scenes whose animation holds every pose for several frames, and the fine jitter of joints. Only the picture of the HD models changes.",
      L"\u0423\u0431\u0438\u0440\u0430\u0435\u0442 \u00ab\u043f\u043e\u043a\u0430\u0434\u0440\u043e\u0432\u043e\u0441\u0442\u044c\u00bb \u0432 \u0441\u0446\u0435\u043d\u0430\u0445, \u0433\u0434\u0435 \u0430\u043d\u0438\u043c\u0430\u0446\u0438\u044f \u0434\u0435\u0440\u0436\u0438\u0442 \u043a\u0430\u0436\u0434\u0443\u044e \u043f\u043e\u0437\u0443 \u043f\u043e \u043d\u0435\u0441\u043a\u043e\u043b\u044c\u043a\u043e \u043a\u0430\u0434\u0440\u043e\u0432, \u0438 \u043c\u0435\u043b\u043a\u043e\u0435 \u0434\u0440\u043e\u0436\u0430\u043d\u0438\u0435 \u0441\u0443\u0441\u0442\u0430\u0432\u043e\u0432. \u041c\u0435\u043d\u044f\u0435\u0442\u0441\u044f \u0442\u043e\u043b\u044c\u043a\u043e \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 HD-\u043c\u043e\u0434\u0435\u043b\u0435\u0439."}},
    {"face_animation", 1, OptBool, {L"Facial animation and lip sync", L"\u041c\u0438\u043c\u0438\u043a\u0430 \u0438 \u0434\u0432\u0438\u0436\u0435\u043d\u0438\u0435 \u0433\u0443\u0431"}, 0, nullptr, 0, 1, 1, S_GroupGraphics,
     {L"HD characters blink and move their lips when they speak.", L"HD-\u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0438 \u043c\u043e\u0440\u0433\u0430\u044e\u0442 \u0438 \u0448\u0435\u0432\u0435\u043b\u044f\u0442 \u0433\u0443\u0431\u0430\u043c\u0438, \u043a\u043e\u0433\u0434\u0430 \u0433\u043e\u0432\u043e\u0440\u044f\u0442."}},
    {"dynamic_shadows", 2, OptChoice, {L"Dynamic shadows", L"\u0414\u0438\u043d\u0430\u043c\u0438\u0447\u0435\u0441\u043a\u0438\u0435 \u0442\u0435\u043d\u0438"}, 3, kShadowChoices, 0, 2, 1, S_GroupLight,
     {L"HD characters and enemies cast real shadows of their own shape, from the main light of the room, instead of the round blot under their feet. \u201cOn characters\u201d: they also shade themselves and each other - an arm on the body, an enemy on Jill. Experimental; needs the HD models computed on the graphics card.",
      L"HD-\u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0438 \u0438 \u0432\u0440\u0430\u0433\u0438 \u043e\u0442\u0431\u0440\u0430\u0441\u044b\u0432\u0430\u044e\u0442 \u043d\u0430\u0441\u0442\u043e\u044f\u0449\u0438\u0435 \u0442\u0435\u043d\u0438 \u0441\u0432\u043e\u0435\u0439 \u0444\u043e\u0440\u043c\u044b \u043e\u0442 \u0433\u043b\u0430\u0432\u043d\u043e\u0433\u043e \u0438\u0441\u0442\u043e\u0447\u043d\u0438\u043a\u0430 \u0441\u0432\u0435\u0442\u0430 \u043a\u043e\u043c\u043d\u0430\u0442\u044b \u0432\u043c\u0435\u0441\u0442\u043e \u043a\u0440\u0443\u0433\u043b\u043e\u0433\u043e \u043f\u044f\u0442\u043d\u0430 \u043f\u043e\u0434 \u043d\u043e\u0433\u0430\u043c\u0438. \u00ab\u041d\u0430 \u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0430\u0445\u00bb \u2014 \u043e\u043d\u0438 \u0435\u0449\u0451 \u0437\u0430\u0442\u0435\u043d\u044f\u044e\u0442 \u0441\u0430\u043c\u0438 \u0441\u0435\u0431\u044f \u0438 \u0434\u0440\u0443\u0433 \u0434\u0440\u0443\u0433\u0430: \u0440\u0443\u043a\u0430 \u2014 \u0442\u0435\u043b\u043e, \u0432\u0440\u0430\u0433 \u2014 \u0414\u0436\u0438\u043b\u043b. \u042d\u043a\u0441\u043f\u0435\u0440\u0438\u043c\u0435\u043d\u0442\u0430\u043b\u044c\u043d\u0430\u044f \u0444\u0443\u043d\u043a\u0446\u0438\u044f; \u043d\u0443\u0436\u0435\u043d \u0440\u0430\u0441\u0447\u0451\u0442 HD-\u043c\u043e\u0434\u0435\u043b\u0435\u0439 \u043d\u0430 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0435."}},
    // fix 2026-11-11d (user 11.11: "these options into the settings")
    {"shadow_edges", 0, OptChoice, {L"Shadow at floor edges", L"\u0422\u0435\u043d\u044c \u0443 \u043a\u0440\u0430\u044f \u043f\u043e\u043b\u0430"}, 3, kShadowEdgeChoices, 0, 2, 1, S_GroupLight,
     {L"Where a shadow runs past the floor (a catwalk edge, a wall). \u201cEndless floor\u201d: drawn on as before. \u201cCut at the edge\u201d: stops at the edge of the floor. \u201cCut, rest on walls\u201d: the cut part lies on the wall it meets. Floor and walls come from the room's collision data, some edges are approximate.", L"\u0413\u0434\u0435 \u0442\u0435\u043d\u044c \u0432\u044b\u0445\u043e\u0434\u0438\u0442 \u0437\u0430 \u043f\u043e\u043b (\u043a\u0440\u0430\u0439 \u043c\u043e\u0441\u0442\u043a\u043e\u0432, \u0441\u0442\u0435\u043d\u0430). \u00ab\u0411\u0435\u0441\u043a\u043e\u043d\u0435\u0447\u043d\u044b\u0439 \u043f\u043e\u043b\u00bb \u2014 \u0440\u0438\u0441\u0443\u0435\u0442\u0441\u044f \u0434\u0430\u043b\u044c\u0448\u0435, \u043a\u0430\u043a \u0440\u0430\u043d\u044c\u0448\u0435. \u00ab\u041e\u0431\u0440\u0435\u0437\u0430\u0442\u044c \u0443 \u043a\u0440\u0430\u044f\u00bb \u2014 \u043e\u0431\u0440\u044b\u0432\u0430\u0435\u0442\u0441\u044f \u0443 \u043a\u0440\u0430\u044f \u043f\u043e\u043b\u0430. \u00ab\u041e\u0431\u0440\u0435\u0437\u0430\u0442\u044c, \u043e\u0441\u0442\u0430\u0442\u043e\u043a \u043d\u0430 \u0441\u0442\u0435\u043d\u044b\u00bb \u2014 \u043e\u0431\u0440\u0435\u0437\u0430\u043d\u043d\u0430\u044f \u0447\u0430\u0441\u0442\u044c \u043b\u043e\u0436\u0438\u0442\u0441\u044f \u043d\u0430 \u0441\u0442\u0435\u043d\u0443. \u041f\u043e\u043b \u0438 \u0441\u0442\u0435\u043d\u044b \u2014 \u0438\u0437 \u0434\u0430\u043d\u043d\u044b\u0445 \u0441\u0442\u043e\u043b\u043a\u043d\u043e\u0432\u0435\u043d\u0438\u0439 \u043a\u043e\u043c\u043d\u0430\u0442\u044b, \u043d\u0435\u043a\u043e\u0442\u043e\u0440\u044b\u0435 \u043a\u0440\u0430\u044f \u043f\u0440\u0438\u0431\u043b\u0438\u0437\u0438\u0442\u0435\u043b\u044c\u043d\u044b."}},
    {"shadow_over_masks", 1, OptBool, {L"Shadows and background objects", L"\u0422\u0435\u043d\u0438 \u0438 \u043f\u0440\u0435\u0434\u043c\u0435\u0442\u044b \u0444\u043e\u043d\u0430"}, 0, nullptr, 0, 1, 1, S_GroupLight,
     {L"Parts of the background picture that stand in front - railings, beams, crates - hide the shadows behind them, and a floor drawn as such a part (catwalks) keeps the shadow on it. Off: as in earlier versions, those parts wipe out every shadow under them.", L"\u0427\u0430\u0441\u0442\u0438 \u0444\u043e\u043d\u043e\u0432\u043e\u0439 \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0438, \u0441\u0442\u043e\u044f\u0449\u0438\u0435 \u0432\u043f\u0435\u0440\u0435\u0434\u0438, \u2014 \u043f\u0435\u0440\u0438\u043b\u0430, \u0431\u0430\u043b\u043a\u0438, \u044f\u0449\u0438\u043a\u0438 \u2014 \u0437\u0430\u043a\u0440\u044b\u0432\u0430\u044e\u0442 \u0442\u0435\u043d\u0438 \u0437\u0430 \u0441\u043e\u0431\u043e\u0439, \u0430 \u043f\u043e\u043b, \u043d\u0430\u0440\u0438\u0441\u043e\u0432\u0430\u043d\u043d\u044b\u0439 \u0442\u0430\u043a\u043e\u0439 \u0447\u0430\u0441\u0442\u044c\u044e (\u043c\u043e\u0441\u0442\u043a\u0438), \u0434\u0435\u0440\u0436\u0438\u0442 \u0442\u0435\u043d\u044c \u043d\u0430 \u0441\u0435\u0431\u0435. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043a\u0430\u043a \u0432 \u043f\u0440\u0435\u0436\u043d\u0438\u0445 \u0432\u0435\u0440\u0441\u0438\u044f\u0445: \u0442\u0430\u043a\u0438\u0435 \u0447\u0430\u0441\u0442\u0438 \u0441\u0442\u0438\u0440\u0430\u044e\u0442 \u043b\u044e\u0431\u0443\u044e \u0442\u0435\u043d\u044c \u043f\u043e\u0434 \u0441\u043e\u0431\u043e\u0439."}},
    {"flash_light", 1, OptBool, {L"Light of muzzle flashes", L"\u0421\u0432\u0435\u0442 \u0432\u0441\u043f\u044b\u0448\u0435\u043a \u0432\u044b\u0441\u0442\u0440\u0435\u043b\u043e\u0432"}, 0, nullptr, 0, 1, 1, S_GroupLight,
     {L"A shot lights the scene for an instant: a soft halo and a hot core round the flash, a short afterglow, light on the HD characters near it and on the floor. Off: the flash sprite alone.",
      L"\u0412\u044b\u0441\u0442\u0440\u0435\u043b \u043d\u0430 \u043c\u0433\u043d\u043e\u0432\u0435\u043d\u0438\u0435 \u043e\u0441\u0432\u0435\u0449\u0430\u0435\u0442 \u0441\u0446\u0435\u043d\u0443: \u043c\u044f\u0433\u043a\u0438\u0439 \u043e\u0440\u0435\u043e\u043b \u0438 \u0433\u043e\u0440\u044f\u0447\u0435\u0435 \u044f\u0434\u0440\u043e \u0432\u043e\u043a\u0440\u0443\u0433 \u0432\u0441\u043f\u044b\u0448\u043a\u0438, \u043a\u043e\u0440\u043e\u0442\u043a\u043e\u0435 \u043f\u043e\u0441\u043b\u0435\u0441\u0432\u0435\u0447\u0435\u043d\u0438\u0435, \u0441\u0432\u0435\u0442 \u043d\u0430 HD-\u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0430\u0445 \u0440\u044f\u0434\u043e\u043c \u0438 \u043d\u0430 \u043f\u043e\u043b\u0443. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u0442\u043e\u043b\u044c\u043a\u043e \u0441\u0430\u043c \u0441\u043f\u0440\u0430\u0439\u0442 \u0432\u0441\u043f\u044b\u0448\u043a\u0438."}},
    // fx 2026-11-11f (user 11.11: "can the fire have volume effects like our muzzle flashes?"): fx_fire.h namespace light, pilot.ini fire_light
    {"fire_light", 1, OptBool, {L"Light of fire", L"\u0421\u0432\u0435\u0442 \u043e\u0433\u043d\u044f"}, 0, nullptr, 0, 1, 1, S_GroupLight,
     {L"Fire lights the scene: a soft flickering glow round flames, fireballs and explosions, warm light on the HD characters near them and on the floor. Off: the fire sprites alone.",
      L"\u041e\u0433\u043e\u043d\u044c \u043e\u0441\u0432\u0435\u0449\u0430\u0435\u0442 \u0441\u0446\u0435\u043d\u0443: \u043c\u044f\u0433\u043a\u043e\u0435 \u043c\u0435\u0440\u0446\u0430\u044e\u0449\u0435\u0435 \u0441\u0432\u0435\u0447\u0435\u043d\u0438\u0435 \u0432\u043e\u043a\u0440\u0443\u0433 \u043f\u043b\u0430\u043c\u0435\u043d\u0438, \u043e\u0433\u043d\u0435\u043d\u043d\u044b\u0445 \u0448\u0430\u0440\u043e\u0432 \u0438 \u0432\u0437\u0440\u044b\u0432\u043e\u0432, \u0442\u0451\u043f\u043b\u044b\u0439 \u0441\u0432\u0435\u0442 \u043d\u0430 HD-\u043f\u0435\u0440\u0441\u043e\u043d\u0430\u0436\u0430\u0445 \u0440\u044f\u0434\u043e\u043c \u0438 \u043d\u0430 \u043f\u043e\u043b\u0443. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u0442\u043e\u043b\u044c\u043a\u043e \u0441\u0430\u043c\u0438 \u0441\u043f\u0440\u0430\u0439\u0442\u044b \u043e\u0433\u043d\u044f."}},
    // fx 2026-11-11e: Leather materials (model_gpu.h LEATHER; pilot.ini leather_fx)
    {"leather_fx", 1, OptBool, {L"Leather of Nemesis' suit", L"\u041a\u043e\u0436\u0430 \u043a\u043e\u0441\u0442\u044e\u043c\u0430 \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430"}, 0, nullptr, 0, 1, 1, S_GroupLight,
     {L"Nemesis' black suit looks like real leather: highlights that follow the folds, a fine grain, a sheen at glancing angles. Off: the flat look of earlier versions.",
      L"\u0427\u0451\u0440\u043d\u044b\u0439 \u043a\u043e\u0441\u0442\u044e\u043c \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430 \u0432\u044b\u0433\u043b\u044f\u0434\u0438\u0442 \u043a\u0430\u043a \u043d\u0430\u0441\u0442\u043e\u044f\u0449\u0430\u044f \u043a\u043e\u0436\u0430: \u0431\u043b\u0438\u043a\u0438 \u043f\u043e \u0441\u043a\u043b\u0430\u0434\u043a\u0430\u043c, \u043c\u0435\u043b\u043a\u043e\u0435 \u0437\u0435\u0440\u043d\u043e, \u043e\u0442\u0431\u043b\u0435\u0441\u043a \u043f\u043e\u0434 \u0441\u043a\u043e\u043b\u044c\u0437\u044f\u0449\u0438\u043c \u0443\u0433\u043b\u043e\u043c. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043f\u043b\u043e\u0441\u043a\u0438\u0439 \u0432\u0438\u0434, \u043a\u0430\u043a \u0432 \u043f\u0440\u0435\u0436\u043d\u0438\u0445 \u0432\u0435\u0440\u0441\u0438\u044f\u0445."}},
    {"wounds", 1, OptBool, {L"Bullet wounds on enemies", L"\u0420\u0430\u043d\u044b \u043e\u0442 \u043f\u0443\u043b\u044c \u043d\u0430 \u0432\u0440\u0430\u0433\u0430\u0445"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"Bullet hits leave wounds on HD enemies where the bullet struck.", L"\u041f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u044f \u043e\u0441\u0442\u0430\u0432\u043b\u044f\u044e\u0442 \u043d\u0430 HD-\u0432\u0440\u0430\u0433\u0430\u0445 \u0440\u0430\u043d\u044b \u0432 \u0442\u043e\u043c \u043c\u0435\u0441\u0442\u0435, \u043a\u0443\u0434\u0430 \u043f\u043e\u043f\u0430\u043b\u0430 \u043f\u0443\u043b\u044f."}},
    {"blood_fx", 1, OptBool, {L"HD blood", L"HD-\u043a\u0440\u043e\u0432\u044c"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"Blood sprays at hits and pools under killed enemies in high resolution. Off: the original blood sprites.",
      L"\u0411\u0440\u044b\u0437\u0433\u0438 \u043a\u0440\u043e\u0432\u0438 \u043f\u0440\u0438 \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u044f\u0445 \u0438 \u043b\u0443\u0436\u0438 \u043f\u043e\u0434 \u0443\u0431\u0438\u0442\u044b\u043c\u0438 \u0432\u0440\u0430\u0433\u0430\u043c\u0438 \u0432 \u0432\u044b\u0441\u043e\u043a\u043e\u043c \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u0438. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0435 \u0441\u043f\u0440\u0430\u0439\u0442\u044b \u043a\u0440\u043e\u0432\u0438."}},
    // fx 2026-11-11e (user 11.11: "it should also set the brightness of the bullet wound decals and of the splashes and sprays
    // of hits"): the same pilot.ini key, now every kind of red blood
    {"blood_pool_brightness", 100, OptNumber, {L"Brightness of blood", L"\u042f\u0440\u043a\u043e\u0441\u0442\u044c \u043a\u0440\u043e\u0432\u0438"}, 0, nullptr, 40, 160, 10, S_GroupEffects,
     {L"How bright the red blood is: the pools under killed enemies and the drops around them, the bullet wounds on enemies, the sprays and splashes of hits. 100 % is the normal look; lower is darker, higher is brighter.",
      L"\u041d\u0430\u0441\u043a\u043e\u043b\u044c\u043a\u043e \u044f\u0440\u043a\u0430\u044f \u043a\u0440\u0430\u0441\u043d\u0430\u044f \u043a\u0440\u043e\u0432\u044c: \u043b\u0443\u0436\u0438 \u043f\u043e\u0434 \u0443\u0431\u0438\u0442\u044b\u043c\u0438 \u0432\u0440\u0430\u0433\u0430\u043c\u0438 \u0438 \u043a\u0430\u043f\u043b\u0438 \u0432\u043e\u043a\u0440\u0443\u0433 \u043d\u0438\u0445, \u0440\u0430\u043d\u044b \u043e\u0442 \u043f\u0443\u043b\u044c \u043d\u0430 \u0432\u0440\u0430\u0433\u0430\u0445, \u0431\u0440\u044b\u0437\u0433\u0438 \u0438 \u0432\u0441\u043f\u043b\u0435\u0441\u043a\u0438 \u043f\u0440\u0438 \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u044f\u0445. 100 % \u2014 \u043e\u0431\u044b\u0447\u043d\u044b\u0439 \u0432\u0438\u0434; \u043c\u0435\u043d\u044c\u0448\u0435 \u2014 \u0442\u0435\u043c\u043d\u0435\u0435, \u0431\u043e\u043b\u044c\u0448\u0435 \u2014 \u044f\u0440\u0447\u0435."}},
    // release 1.50 (user 11.11: "can the red blood pools get an adjustable transparency? and make them a little transparent by
    // default; making them as they are now must stay possible"): blood_fx.h poolOpacity, 0 % = the solid pools of before
    {"blood_pool_transparency", 20, OptNumber, {L"Transparency of blood pools", L"\u041f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u043e\u0441\u0442\u044c \u043b\u0443\u0436 \u043a\u0440\u043e\u0432\u0438"}, 0, nullptr, 0, 80, 10, S_GroupEffects,
     {L"How much the floor shows through the red blood pools under killed enemies and the stains around them. 0 % is solid, as in earlier versions; higher is more see-through.",
      L"\u041d\u0430\u0441\u043a\u043e\u043b\u044c\u043a\u043e \u043f\u043e\u043b \u043f\u0440\u043e\u0441\u0432\u0435\u0447\u0438\u0432\u0430\u0435\u0442 \u0441\u043a\u0432\u043e\u0437\u044c \u043a\u0440\u0430\u0441\u043d\u044b\u0435 \u043b\u0443\u0436\u0438 \u043a\u0440\u043e\u0432\u0438 \u043f\u043e\u0434 \u0443\u0431\u0438\u0442\u044b\u043c\u0438 \u0432\u0440\u0430\u0433\u0430\u043c\u0438 \u0438 \u043f\u044f\u0442\u043d\u0430 \u0432\u043e\u043a\u0440\u0443\u0433 \u043d\u0438\u0445. 0 % \u2014 \u0441\u043f\u043b\u043e\u0448\u043d\u044b\u0435, \u043a\u0430\u043a \u0432 \u043f\u0440\u0435\u0436\u043d\u0438\u0445 \u0432\u0435\u0440\u0441\u0438\u044f\u0445; \u0431\u043e\u043b\u044c\u0448\u0435 \u2014 \u043f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u0435\u0435."}},
    // fx 2026-11-11f (user 11.11: "the third fire variant, HD clips, has to go altogether, leave only the plain HD")
    {"fire_fx", 1, OptChoice, {L"Fire", L"\u041e\u0433\u043e\u043d\u044c"}, 2, kFireChoices, 0, 1, 1, S_GroupEffects,
     {L"HD: the game's own fire with its frames redrawn sharper. Original: the low-resolution fire.",
      L"\u00abHD\u00bb \u2014 \u0440\u043e\u0434\u043d\u043e\u0439 \u043e\u0433\u043e\u043d\u044c \u0438\u0433\u0440\u044b, \u0435\u0433\u043e \u043a\u0430\u0434\u0440\u044b \u043f\u0435\u0440\u0435\u0440\u0438\u0441\u043e\u0432\u0430\u043d\u044b \u0447\u0451\u0442\u0447\u0435. \u00ab\u041e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u00bb \u2014 \u043e\u0433\u043e\u043d\u044c \u043d\u0438\u0437\u043a\u043e\u0433\u043e \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u044f."}},
    {"muzzle_fx", 1, OptBool, {L"HD muzzle flash and smoke", L"HD-\u0432\u0441\u043f\u044b\u0448\u043a\u0438 \u0438 \u0434\u044b\u043c \u0432\u044b\u0441\u0442\u0440\u0435\u043b\u043e\u0432"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"Muzzle flashes and gun smoke of every weapon in high resolution. Off: the original flashes.",
      L"\u0412\u0441\u043f\u044b\u0448\u043a\u0438 \u0438 \u0434\u044b\u043c \u0432\u044b\u0441\u0442\u0440\u0435\u043b\u043e\u0432 \u0432\u0441\u0435\u0433\u043e \u043e\u0440\u0443\u0436\u0438\u044f \u0432 \u0432\u044b\u0441\u043e\u043a\u043e\u043c \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u0438. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0435 \u0432\u0441\u043f\u044b\u0448\u043a\u0438."}},
    {"nemesis_cloud_fx", 1, OptBool, {L"HD Nemesis gas and puddle", L"HD-\u0433\u0430\u0437 \u0438 \u043b\u0443\u0436\u0430 \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"The purple gas and the purple puddle under the downed Nemesis, drawn sharp. Off: the original gas sprites and the original blurred stain.",
      L"\u0424\u0438\u043e\u043b\u0435\u0442\u043e\u0432\u044b\u0439 \u0433\u0430\u0437 \u0438 \u0444\u0438\u043e\u043b\u0435\u0442\u043e\u0432\u0430\u044f \u043b\u0443\u0436\u0430 \u043f\u043e\u0434 \u043f\u043e\u0432\u0435\u0440\u0436\u0435\u043d\u043d\u044b\u043c \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u043e\u043c \u0432 \u0432\u044b\u0441\u043e\u043a\u043e\u043c \u0440\u0430\u0437\u0440\u0435\u0448\u0435\u043d\u0438\u0438. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0435 \u0441\u043f\u0440\u0430\u0439\u0442\u044b \u0433\u0430\u0437\u0430 \u0438 \u0440\u0430\u0437\u043c\u044b\u0442\u043e\u0435 \u043f\u044f\u0442\u043d\u043e."}},
    // fx 2026-11-11e: the volumetric slime at the living Nemesis' hand (fx_nemesis_cloud.h slimeUpdate; pilot.ini nemesis_slime)
    {"nemesis_slime", 0, OptBool, {L"Volumetric slime at Nemesis' hand", L"\u041e\u0431\u044a\u0451\u043c\u043d\u0430\u044f \u0441\u043b\u0438\u0437\u044c \u0443 \u0440\u0443\u043a\u0438 \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"The purple slime at Nemesis' hand is a real liquid in three dimensions: translucent, with froth, drops on threads and small puddles where they land. Loads the graphics card. Off: the original flat sprites, only sharper.",
      L"\u0424\u0438\u043e\u043b\u0435\u0442\u043e\u0432\u0430\u044f \u0441\u043b\u0438\u0437\u044c \u0443 \u0440\u0443\u043a\u0438 \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430 \u2014 \u043d\u0430\u0441\u0442\u043e\u044f\u0449\u0430\u044f \u043e\u0431\u044a\u0451\u043c\u043d\u0430\u044f \u0436\u0438\u0434\u043a\u043e\u0441\u0442\u044c: \u043f\u043e\u043b\u0443\u043f\u0440\u043e\u0437\u0440\u0430\u0447\u043d\u0430\u044f, \u0441 \u043f\u0435\u043d\u043e\u0439, \u043a\u0430\u043f\u043b\u044f\u043c\u0438 \u043d\u0430 \u043d\u0438\u0442\u044f\u0445 \u0438 \u043b\u0443\u0436\u0438\u0446\u0430\u043c\u0438 \u0442\u0430\u043c, \u0433\u0434\u0435 \u043e\u043d\u0438 \u043f\u0430\u0434\u0430\u044e\u0442. \u041d\u0430\u0433\u0440\u0443\u0436\u0430\u0435\u0442 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0443. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0435 \u043f\u043b\u043e\u0441\u043a\u0438\u0435 \u0441\u043f\u0440\u0430\u0439\u0442\u044b, \u0442\u043e\u043b\u044c\u043a\u043e \u0447\u0451\u0442\u0447\u0435."}},
    {"effects_fx", 1, OptBool, {L"HD water, acid and creature blood", L"HD-\u0432\u043e\u0434\u0430, \u043a\u0438\u0441\u043b\u043e\u0442\u0430 \u0438 \u043a\u0440\u043e\u0432\u044c \u0447\u0443\u0434\u043e\u0432\u0438\u0449"}, 0, nullptr, 0, 1, 1, S_GroupEffects,
     {L"Water splashes, the blood of the Grave Digger and of the final Nemesis, his acid and the acid round of the grenade launcher: the game's own sprites redrawn sharper. Off: as before.",
      L"\u0411\u0440\u044b\u0437\u0433\u0438 \u0432\u043e\u0434\u044b, \u043a\u0440\u043e\u0432\u044c \u0433\u0440\u043e\u0431\u043e\u043a\u043e\u043f\u0430\u0442\u0435\u043b\u044f \u0438 \u0444\u0438\u043d\u0430\u043b\u044c\u043d\u043e\u0433\u043e \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430, \u0435\u0433\u043e \u043a\u0438\u0441\u043b\u043e\u0442\u0430 \u0438 \u043a\u0438\u0441\u043b\u043e\u0442\u043d\u044b\u0439 \u0437\u0430\u0440\u044f\u0434 \u0433\u0440\u0430\u043d\u0430\u0442\u043e\u043c\u0451\u0442\u0430: \u0440\u043e\u0434\u043d\u044b\u0435 \u0441\u043f\u0440\u0430\u0439\u0442\u044b \u0438\u0433\u0440\u044b, \u043f\u0435\u0440\u0435\u0440\u0438\u0441\u043e\u0432\u0430\u043d\u043d\u044b\u0435 \u0447\u0451\u0442\u0447\u0435. \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u043a\u0430\u043a \u0440\u0430\u043d\u044c\u0448\u0435."}},
    {"hit_sound_volume", 100, OptNumber, {L"Bullet hit sounds", L"\u0417\u0432\u0443\u043a\u0438 \u043f\u043e\u043f\u0430\u0434\u0430\u043d\u0438\u0439 \u043f\u0443\u043b\u044c"}, 0, nullptr, 0, 200, 10, S_GroupSound,
     {L"A sound when a bullet hits an enemy's body; the original has none, the mod adds it. The value is its loudness: 100 % is the normal level, \u201cOff\u201d removes the sound.",
      L"\u0417\u0432\u0443\u043a, \u043a\u043e\u0433\u0434\u0430 \u043f\u0443\u043b\u044f \u043f\u043e\u043f\u0430\u0434\u0430\u0435\u0442 \u0432 \u0442\u0435\u043b\u043e \u0432\u0440\u0430\u0433\u0430; \u0432 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u0435 \u0435\u0433\u043e \u043d\u0435\u0442 \u2014 \u0434\u043e\u0431\u0430\u0432\u043b\u0435\u043d \u043c\u043e\u0434\u043e\u043c. \u0417\u043d\u0430\u0447\u0435\u043d\u0438\u0435 \u2014 \u0433\u0440\u043e\u043c\u043a\u043e\u0441\u0442\u044c: 100 % \u2014 \u043e\u0431\u044b\u0447\u043d\u044b\u0439 \u0443\u0440\u043e\u0432\u0435\u043d\u044c, \u00ab\u0412\u044b\u043a\u043b.\u00bb \u2014 \u0431\u0435\u0437 \u0437\u0432\u0443\u043a\u0430."}, true},
    {"voice_pack", 0, OptChoice, {L"Voice-over", L"\u041e\u0437\u0432\u0443\u0447\u043a\u0430"}, 1, nullptr, 0, 0, 1, S_GroupSound,   // voice packs 2026-11-11: choices from the host
     {L"Plays the voices of the game's scenes from a voice pack instead of the original English. Packs are folders in 3DD-Voice next to the game (see README-voice); a line a pack does not have stays original. Takes effect from the next line.",
      L"\u0413\u043e\u043b\u043e\u0441\u0430 \u0432 \u0441\u0446\u0435\u043d\u0430\u0445 \u0438\u0433\u0440\u044b \u0438\u0437 \u043f\u0430\u043a\u0435\u0442\u0430 \u043e\u0437\u0432\u0443\u0447\u043a\u0438 \u0432\u043c\u0435\u0441\u0442\u043e \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u044c\u043d\u044b\u0445 \u0430\u043d\u0433\u043b\u0438\u0439\u0441\u043a\u0438\u0445. \u041f\u0430\u043a\u0435\u0442\u044b \u2014 \u043f\u0430\u043f\u043a\u0438 \u0432 3DD-Voice \u0440\u044f\u0434\u043e\u043c \u0441 \u0438\u0433\u0440\u043e\u0439 (\u0441\u043c. README-voice); \u0440\u0435\u043f\u043b\u0438\u043a\u0430, \u043a\u043e\u0442\u043e\u0440\u043e\u0439 \u043d\u0435\u0442 \u0432 \u043f\u0430\u043a\u0435\u0442\u0435, \u0437\u0432\u0443\u0447\u0438\u0442 \u0432 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u0435. \u0414\u0435\u0439\u0441\u0442\u0432\u0443\u0435\u0442 \u0441\u043e \u0441\u043b\u0435\u0434\u0443\u044e\u0449\u0435\u0439 \u0440\u0435\u043f\u043b\u0438\u043a\u0438."}},
    {"player_movies", 1, OptBool, {L"Your own HD movies", L"\u0421\u0432\u043e\u0438 HD-\u0440\u043e\u043b\u0438\u043a\u0438"}, 0, nullptr, 0, 1, 1, S_GroupMovies,
     {L"Plays your own video files instead of the game's movies when they lie in the 3DD-Movies folder next to the game under the movies' names. How to prepare them: README-movies.",
      L"\u0412\u043c\u0435\u0441\u0442\u043e \u0440\u043e\u043b\u0438\u043a\u043e\u0432 \u0438\u0433\u0440\u044b \u043f\u043e\u043a\u0430\u0437\u044b\u0432\u0430\u044e\u0442\u0441\u044f \u0432\u0430\u0448\u0438 \u0432\u0438\u0434\u0435\u043e\u0444\u0430\u0439\u043b\u044b, \u0435\u0441\u043b\u0438 \u043f\u043e\u043b\u043e\u0436\u0438\u0442\u044c \u0438\u0445 \u0432 \u043f\u0430\u043f\u043a\u0443 3DD-Movies \u0440\u044f\u0434\u043e\u043c \u0441 \u0438\u0433\u0440\u043e\u0439 \u043f\u043e\u0434 \u0438\u043c\u0435\u043d\u0430\u043c\u0438 \u0440\u043e\u043b\u0438\u043a\u043e\u0432. \u041a\u0430\u043a \u0438\u0445 \u043f\u043e\u0434\u0433\u043e\u0442\u043e\u0432\u0438\u0442\u044c \u2014 \u0432 README-movies."}},
    {"movie_fit", 1, OptBool, {L"Movies keep their proportions", L"\u0420\u043e\u043b\u0438\u043a\u0438 \u0431\u0435\u0437 \u0440\u0430\u0441\u0442\u044f\u0433\u0438\u0432\u0430\u043d\u0438\u044f"}, 0, nullptr, 0, 1, 1, S_GroupMovies,   // merge 2026-11-09: player_movies.h re-reads it at every movie open
     {L"A movie whose picture is not the game's 2:1 is fitted into the screen with its own proportions instead of being stretched.",
      L"\u0420\u043e\u043b\u0438\u043a, \u0443 \u043a\u043e\u0442\u043e\u0440\u043e\u0433\u043e \u043a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 \u043d\u0435 2:1, \u043a\u0430\u043a \u0443 \u0438\u0433\u0440\u044b, \u0432\u043f\u0438\u0441\u044b\u0432\u0430\u0435\u0442\u0441\u044f \u0432 \u044d\u043a\u0440\u0430\u043d \u0441\u043e \u0441\u0432\u043e\u0438\u043c\u0438 \u043f\u0440\u043e\u043f\u043e\u0440\u0446\u0438\u044f\u043c\u0438, \u0430 \u043d\u0435 \u0440\u0430\u0441\u0442\u044f\u0433\u0438\u0432\u0430\u0435\u0442\u0441\u044f."}},
    {"gpu_models", 1, OptBool, {L"HD models computed on the graphics card", L"\u0420\u0430\u0441\u0447\u0451\u0442 HD-\u043c\u043e\u0434\u0435\u043b\u0435\u0439 \u043d\u0430 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0435"}, 0, nullptr, 0, 1, 1, S_GroupCompat,
     {L"The graphics card animates the HD models - that is faster. Switch off only if the models look broken; the processor does it then.",
      L"HD-\u043c\u043e\u0434\u0435\u043b\u0438 \u0430\u043d\u0438\u043c\u0438\u0440\u0443\u0435\u0442 \u0432\u0438\u0434\u0435\u043e\u043a\u0430\u0440\u0442\u0430 \u2014 \u0442\u0430\u043a \u0431\u044b\u0441\u0442\u0440\u0435\u0435. \u0412\u044b\u043a\u043b\u044e\u0447\u0430\u0439\u0442\u0435, \u0442\u043e\u043b\u044c\u043a\u043e \u0435\u0441\u043b\u0438 \u043c\u043e\u0434\u0435\u043b\u0438 \u0432\u044b\u0433\u043b\u044f\u0434\u044f\u0442 \u0441\u043b\u043e\u043c\u0430\u043d\u043d\u044b\u043c\u0438: \u0442\u043e\u0433\u0434\u0430 \u0438\u0445 \u0441\u0447\u0438\u0442\u0430\u0435\u0442 \u043f\u0440\u043e\u0446\u0435\u0441\u0441\u043e\u0440."}},
    {"movie_rgb32", 1, OptBool, {L"32-bit movie colour", L"32-\u0431\u0438\u0442\u043d\u044b\u0439 \u0446\u0432\u0435\u0442 \u0440\u043e\u043b\u0438\u043a\u043e\u0432"}, 0, nullptr, 0, 1, 1, S_GroupCompat,
     {L"Movies in full colour, without bands on smooth gradients. Switch off only if movies show wrong.",
      L"\u0420\u043e\u043b\u0438\u043a\u0438 \u0432 \u043f\u043e\u043b\u043d\u043e\u043c \u0446\u0432\u0435\u0442\u0435, \u0431\u0435\u0437 \u043f\u043e\u043b\u043e\u0441 \u043d\u0430 \u043f\u043b\u0430\u0432\u043d\u044b\u0445 \u043f\u0435\u0440\u0435\u0445\u043e\u0434\u0430\u0445. \u0412\u044b\u043a\u043b\u044e\u0447\u0430\u0439\u0442\u0435, \u0442\u043e\u043b\u044c\u043a\u043e \u0435\u0441\u043b\u0438 \u0440\u043e\u043b\u0438\u043a\u0438 \u043f\u043e\u043a\u0430\u0437\u044b\u0432\u0430\u044e\u0442\u0441\u044f \u043d\u0435\u043f\u0440\u0430\u0432\u0438\u043b\u044c\u043d\u043e."}},
    {"movie_sync", 1, OptBool, {L"Movie picture follows its sound", L"\u041a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 \u0440\u043e\u043b\u0438\u043a\u043e\u0432 \u043f\u043e \u0437\u0432\u0443\u043a\u0443"}, 0, nullptr, 0, 1, 1, S_GroupCompat,
     {L"Keeps the picture of a movie in step with its sound. Switch off only if movies stutter.",
      L"\u041a\u0430\u0440\u0442\u0438\u043d\u043a\u0430 \u0440\u043e\u043b\u0438\u043a\u0430 \u0434\u0435\u0440\u0436\u0438\u0442\u0441\u044f \u0432 \u0442\u0430\u043a\u0442 \u0441 \u0435\u0433\u043e \u0437\u0432\u0443\u043a\u043e\u043c. \u0412\u044b\u043a\u043b\u044e\u0447\u0430\u0439\u0442\u0435, \u0442\u043e\u043b\u044c\u043a\u043e \u0435\u0441\u043b\u0438 \u0440\u043e\u043b\u0438\u043a\u0438 \u0438\u0434\u0443\u0442 \u0440\u044b\u0432\u043a\u0430\u043c\u0438."}},
    {"glock_kneel_budget", 1, OptChoice, {L"Glock 18C burst from the knee", L"\u041e\u0447\u0435\u0440\u0435\u0434\u044c Glock 18C \u0441 \u043a\u043e\u043b\u0435\u043d\u0430"}, 2, kKneelChoices, 0, 1, 1, S_GroupTest,
     {L"After a roll dodge Jill fires from one knee. \u201cWhole burst\u201d: she stays down while the fire goes on. \u201cStands up after 15\u201d: she gets up after 15 shots, as the original does with the handgun.",
      L"\u041f\u043e\u0441\u043b\u0435 \u0443\u043a\u043b\u043e\u043d\u0435\u043d\u0438\u044f \u043f\u0435\u0440\u0435\u043a\u0430\u0442\u043e\u043c \u0414\u0436\u0438\u043b\u043b \u0441\u0442\u0440\u0435\u043b\u044f\u0435\u0442 \u0441 \u043a\u043e\u043b\u0435\u043d\u0430. \u00ab\u0412\u0441\u044f \u043e\u0447\u0435\u0440\u0435\u0434\u044c\u00bb \u2014 \u043e\u0441\u0442\u0430\u0451\u0442\u0441\u044f \u043d\u0430 \u043a\u043e\u043b\u0435\u043d\u0435, \u043f\u043e\u043a\u0430 \u0438\u0434\u0451\u0442 \u043e\u0433\u043e\u043d\u044c. \u00ab\u0412\u0441\u0442\u0430\u0451\u0442 \u0447\u0435\u0440\u0435\u0437 15\u00bb \u2014 \u043f\u043e\u0441\u043b\u0435 15 \u0432\u044b\u0441\u0442\u0440\u0435\u043b\u043e\u0432 \u0432\u0441\u0442\u0430\u0451\u0442, \u043a\u0430\u043a \u0432 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u0435 \u0441 \u043e\u0431\u044b\u0447\u043d\u044b\u043c \u043f\u0438\u0441\u0442\u043e\u043b\u0435\u0442\u043e\u043c."}},
    {"glock_dodge_always", 0, OptBool, {L"Dodge always succeeds (Nemesis)", L"\u0423\u043a\u043b\u043e\u043d\u0435\u043d\u0438\u0435 \u0441\u0440\u0430\u0431\u0430\u0442\u044b\u0432\u0430\u0435\u0442 \u0432\u0441\u0435\u0433\u0434\u0430 (\u041d\u0435\u043c\u0435\u0437\u0438\u0441)"}, 0, nullptr, 0, 1, 1, S_GroupTest,
     {L"For looking at the roll dodge. Jill rolls away by herself from every attack of Nemesis that the game lets her dodge and drops to one knee: hold aim and fire to shoot from the knee. No dodge while she is on her knee.",
      L"\u0427\u0442\u043e\u0431\u044b \u043f\u043e\u0441\u043c\u043e\u0442\u0440\u0435\u0442\u044c \u043f\u0435\u0440\u0435\u043a\u0430\u0442. \u0414\u0436\u0438\u043b\u043b \u0441\u0430\u043c\u0430 \u0443\u0445\u043e\u0434\u0438\u0442 \u043f\u0435\u0440\u0435\u043a\u0430\u0442\u043e\u043c \u043e\u0442 \u043a\u0430\u0436\u0434\u043e\u0433\u043e \u0443\u0434\u0430\u0440\u0430 \u041d\u0435\u043c\u0435\u0437\u0438\u0441\u0430, \u043e\u0442 \u043a\u043e\u0442\u043e\u0440\u043e\u0433\u043e \u0438\u0433\u0440\u0430 \u043f\u043e\u0437\u0432\u043e\u043b\u044f\u0435\u0442 \u0443\u043a\u043b\u043e\u043d\u0438\u0442\u044c\u0441\u044f, \u0438 \u0432\u0441\u0442\u0430\u0451\u0442 \u043d\u0430 \u043a\u043e\u043b\u0435\u043d\u043e: \u0434\u0435\u0440\u0436\u0438\u0442\u0435 \u043f\u0440\u0438\u0446\u0435\u043b \u0438 \u043e\u0433\u043e\u043d\u044c, \u0447\u0442\u043e\u0431\u044b \u0441\u0442\u0440\u0435\u043b\u044f\u0442\u044c \u0441 \u043a\u043e\u043b\u0435\u043d\u0430. \u0421 \u043a\u043e\u043b\u0435\u043d\u0430 \u0443\u043a\u043b\u043e\u043d\u0435\u043d\u0438\u044f \u043d\u0435\u0442."}},
    {"menu_language", 0, OptChoice, {L"Menu language", L"\u042f\u0437\u044b\u043a \u043c\u0435\u043d\u044e"}, 3, kLangChoices, 0, 2, 1, S_GroupMenu,
     {L"The language of this menu. \u201cAuto\u201d follows the language of Windows.", L"\u042f\u0437\u044b\u043a \u044d\u0442\u043e\u0433\u043e \u043c\u0435\u043d\u044e. \u00ab\u0410\u0432\u0442\u043e\u00bb \u2014 \u043f\u043e \u044f\u0437\u044b\u043a\u0443 Windows."}},
    {"control_scheme", 0, OptChoice, {L"Control scheme", L"\u0421\u0445\u0435\u043c\u0430 \u0443\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u0438\u044f"}, 3, kSchemeChoices, 0, 2, 1, S_GroupMove,
     {L"Classic: \u201cup\u201d walks where Jill faces, \u201cleft\u201d and \u201cright\u201d turn her on the spot, as in the original. Modern: she walks towards the side of the screen you push the stick or the arrows to; aiming stays as it is. \u201cStick runs\u201d: a full tilt of the stick also runs, without the run button.",
      L"\u00ab\u041a\u043b\u0430\u0441\u0441\u0438\u043a\u0430\u00bb: \u00ab\u0432\u043f\u0435\u0440\u0451\u0434\u00bb \u0432\u0435\u0434\u0451\u0442 \u0442\u0443\u0434\u0430, \u043a\u0443\u0434\u0430 \u0441\u043c\u043e\u0442\u0440\u0438\u0442 \u0414\u0436\u0438\u043b\u043b, \u00ab\u0432\u043b\u0435\u0432\u043e\u00bb \u0438 \u00ab\u0432\u043f\u0440\u0430\u0432\u043e\u00bb \u043f\u043e\u0432\u043e\u0440\u0430\u0447\u0438\u0432\u0430\u044e\u0442 \u0435\u0451 \u043d\u0430 \u043c\u0435\u0441\u0442\u0435 \u2014 \u043a\u0430\u043a \u0432 \u043e\u0440\u0438\u0433\u0438\u043d\u0430\u043b\u0435. \u00ab\u0421\u043e\u0432\u0440\u0435\u043c\u0435\u043d\u043d\u0430\u044f\u00bb: \u043e\u043d\u0430 \u0438\u0434\u0451\u0442 \u0432 \u0442\u0443 \u0441\u0442\u043e\u0440\u043e\u043d\u0443 \u044d\u043a\u0440\u0430\u043d\u0430, \u043a\u0443\u0434\u0430 \u043d\u0430\u043a\u043b\u043e\u043d\u0451\u043d \u0441\u0442\u0438\u043a \u0438\u043b\u0438 \u043d\u0430\u0436\u0430\u0442\u0430 \u0441\u0442\u0440\u0435\u043b\u043a\u0430; \u043f\u0440\u0438\u0446\u0435\u043b\u0438\u0432\u0430\u043d\u0438\u0435 \u043f\u0440\u0435\u0436\u043d\u0435\u0435. \u00ab\u0411\u0435\u0433 \u0441\u0442\u0438\u043a\u043e\u043c\u00bb \u2014 \u0435\u0449\u0451 \u0438 \u0431\u0435\u0433 \u043f\u0440\u0438 \u043f\u043e\u043b\u043d\u043e\u043c \u043d\u0430\u043a\u043b\u043e\u043d\u0435 \u0441\u0442\u0438\u043a\u0430, \u0431\u0435\u0437 \u043a\u043d\u043e\u043f\u043a\u0438 \u0431\u0435\u0433\u0430."}},
};
enum OptStatus { OptLive, OptRestart, OptNextMovie, OptUnavailable };

// what the menu needs from the game (mod_menu.h) or from the offline test
struct Host {
    virtual ~Host() {}
    virtual Bindings& Native() = 0;                          // the game's own tables (edited in place)
    virtual ModBinding& Mod(int m) = 0;
    virtual void CommitNative(bool keyboard, bool pad) = 0; // apply to the game + save bio3.ini
    virtual void CommitMod(int m) = 0;                      // save pilot.ini
    virtual bool PadPresent() = 0;
    virtual int OptGet(int id) = 0;                         // current value
    virtual int OptRunning(int id) = 0;                     // value this run started with / runs with
    virtual OptStatus OptState(int id) = 0;
    // fix 2026-11-11: S_Count = the row can be changed, else which other row blocks it (S_NeedsHd, S_NeedsModels, ...): the
    // row is greyed with that note; its value is kept and "reset to defaults" still resets it
    virtual Str OptBlocked(int) { return S_Count; }
    virtual Str OptReason(int id) = 0;                      // why unavailable (S_NeedsModels, S_NotMerce, S_Unavailable)
    virtual void OptSet(int id, int value) = 0;             // save + apply
    virtual bool NativeWindowAvailable() = 0;
    virtual void OpenNativeWindow() = 0;                    // closes the menu first
    virtual void Close() = 0;
    virtual void Note(const char* line) = 0;                // one log line
    // round 7 (exit rows). Blocked: S_Count = the row works, else why it is greyed (S_ExitNoGame, S_ExitSaveScreen,
    // S_ExitNotNow, S_Unavailable), shown at the right of the row. The actions close the menu first; the game side
    // carries them out after the menu loop (mod_menu.h). The defaults = a host that cannot do either: both rows greyed.
    // voice packs 2026-11-11: a choice row whose OptionDef has no choice table (O_VoicePack) asks the host for its choices;
    // the defaults serve the table (and "Original (English)" alone for the voice-over row)
    virtual int OptChoiceCount(int id) { return kOptions[id].choice ? kOptions[id].choices : 1; }
    virtual std::wstring OptChoiceText(int id, int v, bool ru) {
        const OptionDef& o = kOptions[id];
        if (o.choice) { const int i = (std::max)(0, (std::min)(o.choices - 1, v)); return ru ? o.choice[i].ru : o.choice[i].en; }
        if (id == O_VoicePack && v == 0) return ru ? kVoiceOriginal.ru : kVoiceOriginal.en;
        return std::to_wstring(v);
    }
    virtual Str ExitTitleBlocked() { return S_Unavailable; }
    virtual Str ExitDesktopBlocked() { return S_Unavailable; }
    virtual void ExitToTitle() {}
    virtual void ExitToDesktop() {}
};

// ================================================================== drawing
struct Vtx { float x, y, z, rhw; DWORD c; float u, v; };
class Gfx {
public:
    IDirect3DDevice9* dev = nullptr; float W = 0, H = 0; unsigned frameNo = 0;
    // round 6: the menu is laid out in a view of the frame (W x H at ox, oy): the widescreen crop window while the
    // crop is shown, else the whole frame (fullW x fullH)
    float ox = 0, oy = 0, fullW = 0, fullH = 0;
    struct Tex { IDirect3DTexture9* t = nullptr; int w = 0, h = 0, ink = 0; unsigned used = 0; };
    ~Gfx() { for (auto& f : fonts) DeleteObject(f.second); if (dc) DeleteDC(dc); }   // D3D textures: released at menu close (none at exit)
    void Release() {
        for (auto& e : cache) if (e.second.t) e.second.t->Release();
        cache.clear(); bytes = 0;
        for (auto& f : fonts) DeleteObject(f.second);
        fonts.clear();
        if (dc) { DeleteDC(dc); dc = nullptr; }
    }
    // release the D3D textures only (device reset / new device); GDI objects stay
    void ReleaseTextures() { for (auto& e : cache) if (e.second.t) e.second.t->Release(); cache.clear(); bytes = 0; }
    size_t Bytes() const { return bytes; }
    bool Begin(IDirect3DDevice9* d, float w, float h) {
        if (dev != d) { ReleaseTextures(); dev = d; }
        W = fullW = w; H = fullH = h; ox = oy = 0; ++frameNo;
        d->SetVertexShader(nullptr); d->SetPixelShader(nullptr); d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
        d->SetRenderState(D3DRS_ZENABLE, FALSE); d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE); d->SetRenderState(D3DRS_LIGHTING, FALSE);
        d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); d->SetRenderState(D3DRS_FOGENABLE, FALSE); d->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
        d->SetRenderState(D3DRS_STENCILENABLE, FALSE); d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
        d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE); d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0); d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD); d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); d->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0); d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE); d->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR); d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE); d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
        mode = -1;
        return true;
    }
    void Rect(float x0, float y0, float x1, float y1, DWORD c) {
        Mode(0, nullptr);
        Quad(std::floor(x0), std::floor(y0), std::floor(x1), std::floor(y1), c, 0, 0, 1, 1);
    }
    void View(float x, float y, float w, float h) { ox = std::floor(x); oy = std::floor(y); W = std::floor(w); H = std::floor(h); }
    void Dim(DWORD c) { Mode(0, nullptr); Raw(0, 0, fullW, fullH, c, 0, 0, 1, 1); }   // the whole frame, whatever the view
    void Frame(IDirect3DTexture9* t) {   // the captured game frame, 1:1, opaque
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        Mode(2, t); Raw(0, 0, fullW, fullH, 0xffffffffu, 0, 0, 1, 1);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    }
    // round 6: greedy word wrap of a description (GDI extents only, no textures); the last line takes the rest
    std::vector<std::wstring> Wrap(const std::wstring& s, float px, float maxW, int maxLines) {
        std::vector<std::wstring> out;
        if (s.empty() || maxLines < 1) return out;
        if (!dc) dc = CreateCompatibleDC(nullptr);
        HFONT font = dc ? Font((std::max)(4, int(px + 0.5f)), false) : nullptr;
        if (!font) { out.push_back(s); return out; }
        HGDIOBJ old = SelectObject(dc, font);
        auto width = [&](const std::wstring& t) { SIZE z{}; GetTextExtentPoint32W(dc, t.c_str(), int(t.size()), &z); return float(z.cx); };
        std::wstring line;
        for (size_t i = 0; i < s.size();) {
            size_t j = s.find(L' ', i); if (j == std::wstring::npos) j = s.size();
            const std::wstring word = s.substr(i, j - i); i = j + 1;
            if (word.empty()) continue;
            const std::wstring tried = line.empty() ? word : line + L" " + word;
            if (line.empty() || width(tried) <= maxW || int(out.size()) + 1 >= maxLines) { line = tried; continue; }
            out.push_back(line); line = word;
        }
        if (!line.empty()) out.push_back(line);
        SelectObject(dc, old);
        return out;
    }
    // text with its top at y; align 0 left, 1 centre, 2 right of x; shrinks (then cuts with ...) to maxW; returns the width
    float Text(const std::wstring& s, float x, float y, float px, DWORD c, int align = 0, float maxW = 0, bool bold = false) {
        if (s.empty() || px < 4) return 0;
        const Tex* t = Get(s, int(px + 0.5f), bold);
        if (!t) return 0;
        if (maxW > 0 && t->ink > maxW) {
            const float fit = std::floor(px * maxW / float(t->ink));
            if (fit >= px * 0.62f) t = Get(s, int(fit), bold);
            else {
                std::wstring cut = s; const int smallPx = int(px * 0.62f);
                while (cut.size() > 1) { cut.pop_back(); const Tex* u = Get(cut + L"\u2026", smallPx, bold); if (!u || u->ink <= maxW) { t = u; break; } }
            }
            if (!t) return 0;
        }
        float left = align == 1 ? x - t->ink * 0.5f : align == 2 ? x - t->ink : x;
        left = std::floor(left + 0.5f); const float top = std::floor(y + 0.5f);
        Mode(1, t->t);
        Quad(left - kPad, top - kPad, left - kPad + t->w, top - kPad + t->h, c, 0, 0, 1, 1);
        return float(t->ink);
    }
    float Measure(const std::wstring& s, float px, bool bold = false) { const Tex* t = Get(s, int(px + 0.5f), bold); return t ? float(t->ink) : 0.f; }
    void Trim() {   // textures not drawn for 10 s of menu frames
        for (auto it = cache.begin(); it != cache.end();) {
            if (frameNo - it->second.used > 600) { bytes -= size_t(it->second.w) * it->second.h * 4; if (it->second.t) it->second.t->Release(); it = cache.erase(it); }
            else ++it;
        }
    }

private:
    static constexpr int kPad = 2;
    std::unordered_map<std::wstring, Tex> cache; size_t bytes = 0;
    std::unordered_map<int, HFONT> fonts; HDC dc = nullptr; int mode = -1; IDirect3DBaseTexture9* bound = nullptr;
    void Mode(int m, IDirect3DBaseTexture9* t) {
        if (m != mode) {
            mode = m;
            if (m == 0) {   // solid: diffuse only
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            } else if (m == 1) {   // text: colour from the vertex, alpha = coverage x vertex alpha
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
            } else {   // frame
                dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1); dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
            }
            bound = reinterpret_cast<IDirect3DBaseTexture9*>(1);
        }
        if (bound != t) { dev->SetTexture(0, t); bound = t; }
    }
    void Quad(float x0, float y0, float x1, float y1, DWORD c, float u0, float v0, float u1, float v1) { Raw(x0 + ox, y0 + oy, x1 + ox, y1 + oy, c, u0, v0, u1, v1); }
    void Raw(float x0, float y0, float x1, float y1, DWORD c, float u0, float v0, float u1, float v1) {
        const Vtx v[4] = {{x0 - .5f, y0 - .5f, 0, 1, c, u0, v0}, {x1 - .5f, y0 - .5f, 0, 1, c, u1, v0},
                          {x0 - .5f, y1 - .5f, 0, 1, c, u0, v1}, {x1 - .5f, y1 - .5f, 0, 1, c, u1, v1}};
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
    }
    HFONT Font(int px, bool bold) {
        const int key = px * 2 + (bold ? 1 : 0);
        auto it = fonts.find(key);
        if (it != fonts.end()) return it->second;
        HFONT f = CreateFontW(-px, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        fonts[key] = f;
        return f;
    }
    const Tex* Get(const std::wstring& s, int px, bool bold) {
        if (px < 4) px = 4;
        std::wstring key = s; key += wchar_t(0x1f); key += std::to_wstring(px); key += bold ? L"b" : L"n";
        auto it = cache.find(key);
        if (it != cache.end()) { it->second.used = frameNo; return &it->second; }
        Tex t = Make(s, px, bold);
        if (!t.t) return nullptr;
        t.used = frameNo; bytes += size_t(t.w) * t.h * 4;
        return &(cache[key] = t);
    }
    Tex Make(const std::wstring& s, int px, bool bold) {
        Tex out;
        if (!dev) return out;
        if (!dc) dc = CreateCompatibleDC(nullptr);
        if (!dc) return out;
        HFONT font = Font(px, bold); if (!font) return out;
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE size{}; GetTextExtentPoint32W(dc, s.c_str(), int(s.size()), &size);
        TEXTMETRICW tm{}; GetTextMetricsW(dc, &tm);
        const int w = (std::min)(4096, int(size.cx) + 2 * kPad + px / 6), h = int(tm.tmHeight) + 2 * kPad;
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr; HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) { SelectObject(dc, oldFont); if (bmp) DeleteObject(bmp); return out; }
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        memset(bits, 0, size_t(w) * h * 4);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255)); SetTextAlign(dc, TA_LEFT | TA_TOP);
        TextOutW(dc, kPad, kPad, s.c_str(), int(s.size()));
        GdiFlush();
        IDirect3DTexture9* staging = nullptr;
        if (SUCCEEDED(dev->CreateTexture(UINT(w), UINT(h), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &staging, nullptr))) {
            D3DLOCKED_RECT lr{};
            if (SUCCEEDED(staging->LockRect(0, &lr, nullptr, 0))) {
                for (int y = 0; y < h; ++y) {
                    const uint32_t* src = static_cast<const uint32_t*>(bits) + size_t(y) * w;
                    uint32_t* dst = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch);
                    for (int x = 0; x < w; ++x) {
                        const uint32_t p = src[x]; uint32_t a = (std::max)((std::max)(p & 0xffu, (p >> 8) & 0xffu), (p >> 16) & 0xffu);
                        dst[x] = (a << 24) | 0x00ffffffu;
                    }
                }
                staging->UnlockRect(0);
                if (SUCCEEDED(dev->CreateTexture(UINT(w), UINT(h), 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &out.t, nullptr)) &&
                    FAILED(dev->UpdateTexture(staging, out.t))) { out.t->Release(); out.t = nullptr; }
            }
            staging->Release();
        }
        SelectObject(dc, oldBmp); SelectObject(dc, oldFont); DeleteObject(bmp);
        out.w = w; out.h = h; out.ink = int(size.cx);
        return out;
    }
};

// ================================================================== the menu
enum Nav { NavNone, NavUp, NavDown, NavLeft, NavRight, NavOk, NavBack, NavClear, NavClose, NavPrevPage, NavNextPage, NavHome, NavEnd, NavPageUp, NavPageDown };

class Menu {
public:
    enum Page { PMain, PControls, POptions, kPages };
    bool ru = false;

    void Open(Host* h, bool russian) {
        host = h; ru = russian; page = PMain; for (auto& c : cursor) c = 0; for (auto& s : scroll) s = 0;
        col = 0; capture = CapNone; status.clear(); confirmReset = -1; dirty = true;
    }
    bool Dirty() const { return dirty; }
    void Invalidate() { dirty = true; }
    void Drawn() { dirty = false; }
    bool Capturing() const { return capture != CapNone; }
    bool CapturingKeyboard() const { return capture == CapKey; }
    bool CapturingPad() const { return capture == CapPad; }
    Page CurrentPage() const { return page; }
    int CursorRow() const { return cursor[page]; }   // test hook (round 6: pad tests)
    // test hooks (round 7: exit rows): the row waiting for its second press (-1 none), the selected row greyed, rows of
    // the page, the status line
    int ConfirmRow() const { return confirmReset; }
    bool CursorDisabled() { Build(); return rows[size_t(cursor[page])].disabled; }
    int RowCount() { Build(); return int(rows.size()); }
    const std::wstring& Status() const { return status; }
    std::wstring ValueTextOf(int id, int v) { return ValueText(id, v); }   // test hook (voice packs 2026-11-11)
    // test hooks (offline screenshots)
    void Goto(Page p, int row, int column) { page = p; status.clear(); Build(); col = column; cursor[p] = 0; for (int i = 0; i < int(rows.size()); ++i) if (rows[size_t(i)].selectable && row-- == 0) { cursor[p] = i; break; } dirty = true; }
    void ForceCapture(bool pad, DWORD now) { Build(); StartCapture(pad, now); }
    void SetStatus(const std::wstring& s, DWORD now, bool warn = false) { status = s; statusUntil = now + 3500; statusWarn = warn; dirty = true; }

    // ---- input ----
    void OnKeyDown(UINT vk, int dik, bool repeat, DWORD now) {
        if (!host) return;
        if (capture == CapKey) { if (!repeat) CaptureKey(vk, dik, now); return; }
        if (capture == CapPad) { if (vk == VK_ESCAPE && !repeat) EndCapture(T(S_Cancelled, ru), now); return; }
        Nav n = NavNone;
        const int menuVk = host->Mod(ModMenu).vk;
        if (menuVk && int(vk) == menuVk) n = repeat ? NavNone : NavClose;
        else switch (vk) {
        case VK_UP: n = NavUp; break;
        case VK_DOWN: n = NavDown; break;
        case VK_LEFT: n = NavLeft; break;
        case VK_RIGHT: n = NavRight; break;
        case VK_RETURN: case VK_SPACE: n = repeat ? NavNone : NavOk; break;
        case VK_ESCAPE: case VK_BACK: n = repeat ? NavNone : NavBack; break;
        case VK_DELETE: n = repeat ? NavNone : NavClear; break;
        case VK_TAB: n = repeat ? NavNone : ((GetKeyState(VK_SHIFT) & 0x8000) ? NavPrevPage : NavNextPage); break;
        case VK_HOME: n = NavHome; break;
        case VK_END: n = NavEnd; break;
        case VK_PRIOR: n = NavPageUp; break;
        case VK_NEXT: n = NavPageDown; break;
        default: break;
        }
        if (n != NavNone) Do(n, now);
    }
    // a pad input became active (raw), with its fixed menu meaning (nav); the menu button closes / cancels
    void OnPad(PadBind raw, Nav nav, DWORD now) {
        if (!host) return;
        if (capture == CapPad) { CapturePad(raw, now); return; }
        if (capture == CapKey) return;
        if (nav != NavNone) Do(nav, now);
    }
    void Tick(DWORD now) {
        if (capture != CapNone && now - captureStart > 10000) EndCapture(T(S_Cancelled, ru), now);
        if (!status.empty() && int(now - statusUntil) > 0) { status.clear(); dirty = true; }
    }

    // ---- drawing ----
    void Draw(Gfx& g, DWORD now) {
        Build();
        const float W = g.W, H = g.H, s = H / 1080.f;
        g.Dim(0xa8000000u);
        const float rowH = std::floor(42 * s), text = 25 * s, smallPx = 20 * s;
        const float footH = std::floor(166 * s);   // round 6: three description lines + the key hints
        const float fullH = H * 0.90f, fitH = 104 * s + float(rows.size()) * rowH + 40 * s + footH;   // the main page: only as tall as it needs
        const float pw = (std::min)(W - 40 * s, H * 1.25f), ph = page == PMain ? (std::min)(fullH, fitH) : fullH;
        const float px0 = std::floor((W - pw) * 0.5f), py0 = std::floor((H - ph) * 0.5f);
        const float px1 = px0 + pw, py1 = py0 + ph;
        g.Rect(px0, py0, px1, py1, 0xf00d0e11u);
        const float b = (std::max)(1.f, std::floor(2 * s));
        g.Rect(px0, py0, px1, py0 + b, 0xff3a3d44u); g.Rect(px0, py1 - b, px1, py1, 0xff3a3d44u);
        g.Rect(px0, py0, px0 + b, py1, 0xff3a3d44u); g.Rect(px1 - b, py0, px1, py1, 0xff3a3d44u);
        g.Rect(px0 + b, py0 + b, px1 - b, py0 + b + std::floor(5 * s), kAccent);
        // title
        const float margin = 34 * s, tx = px0 + margin, ty = py0 + 26 * s;
        float w = g.Text(T(S_Menu, ru), tx, ty, 40 * s, kAccentText, 0, 0, true);
        const wchar_t* pageName = page == PControls ? T(S_PageControls, ru) : page == POptions ? T(S_PageOptions, ru) : nullptr;
        if (pageName) { w += g.Text(L"  \u2022  ", tx + w, ty, 40 * s, kDim, 0, 0, true); g.Text(pageName, tx + w, ty, 40 * s, kWhite, 0, 0, true); }
        g.Text(T(S_Paused, ru), px1 - margin, ty + 12 * s, 24 * s, kDim, 2);
        const float listTop = py0 + 104 * s;
        g.Rect(px0 + margin, listTop - 14 * s, px1 - margin, listTop - 14 * s + b, 0xff2a2c31u);
        // footer (round 6): what the selected row does, or the result of the last action above it; the keys at the bottom
        const float footTop = py1 - footH;
        g.Rect(px0 + margin, footTop, px1 - margin, footTop + b, 0xff2a2c31u);
        {
            wchar_t hint[512]; const std::wstring closeName = MenuOpenName();
            const Str captureHint = captureRow.type == RowMod ? S_HintCaptureMod : S_HintCapture;
            if (capture == CapKey) swprintf_s(hint, T(captureHint, ru), L"Esc");
            else if (capture == CapPad) { const std::wstring pad = PadMenuName(); swprintf_s(hint, T(captureHint, ru), pad.empty() ? L"Esc" : (L"Esc / " + pad).c_str()); }
            else if (page == PControls && rows[size_t(cursor[page])].type != RowOption) wcscpy_s(hint, T(S_HintControls, ru));
            else if (page != PMain) swprintf_s(hint, T(S_HintOptions, ru), closeName.c_str());
            else swprintf_s(hint, T(S_HintMain, ru), closeName.c_str());
            g.Text(hint, px0 + margin, py1 - 46 * s, 21 * s, kDim, 0, pw - 2 * margin);
            float y = footTop + 14 * s; const float aboutPx = 22 * s, lineH = std::floor(30 * s);
            if (!status.empty()) { g.Text(status, px0 + margin, y, 24 * s, statusWarn ? kWarn : kGood, 0, pw - 2 * margin); y += std::floor(34 * s); }
            const std::wstring about = capture != CapNone ? std::wstring() : Describe(rows[size_t(cursor[page])]);
            for (const std::wstring& line : g.Wrap(about, aboutPx, pw - 2 * margin, status.empty() ? 3 : 2)) {
                g.Text(line, px0 + margin, y, aboutPx, kText, 0, pw - 2 * margin); y += lineH;
            }
        }
        // rows
        float top = listTop;
        if (page == PControls) ColumnX(px0, pw, margin);   // fix 2026-11-11: the column titles sit on the headings of the key rows (the scheme row comes first)
        const int visible = (std::max)(3, int((footTop - 12 * s - top) / rowH));
        int& sc = scroll[page]; const int cur = cursor[page];
        if (cur < sc + 1) sc = (std::max)(0, cur - 1);
        const int ahead = (cur + 1 < int(rows.size()) && rows[size_t(cur + 1)].type == RowHeader) ? 3 : 2;   // a group heading is not left alone at the bottom
        if (cur > sc + visible - ahead) sc = cur - visible + ahead;
        sc = (std::max)(0, (std::min)(sc, (std::max)(0, int(rows.size()) - visible)));
        for (int i = sc; i < int(rows.size()) && i < sc + visible; ++i) {
            const Row& r = rows[size_t(i)];
            const float y = top + float(i - sc) * rowH, cy = y + (rowH - text * 1.33f) * 0.5f;
            const bool sel = i == cur;
            if (r.type == RowHeader) {
                const float hy = y + rowH - smallPx * 1.55f;
                g.Text(r.label, px0 + margin, hy, smallPx, kGold, 0, 0, true);
                if (r.id) {   // fix 2026-11-11: a heading of key rows (controls page) carries the column titles
                    g.Text(T(S_ColKeyboard, ru), colX[1], hy, smallPx, kDim);
                    g.Text(T(S_ColGamepad, ru), colX[2], hy, smallPx, kDim);
                    if (r.id == 1 && !host->PadPresent()) g.Text(T(S_NoPad, ru), px1 - margin, hy, smallPx, kWarn, 2);
                }
                continue;
            }
            if (sel) { g.Rect(px0 + margin - 10 * s, y + 2 * s, px1 - margin + 10 * s, y + rowH - 2 * s, kSelect); g.Rect(px0 + margin - 10 * s, y + 2 * s, px0 + margin - 4 * s, y + rowH - 2 * s, kAccent); }
            const DWORD labelColor = r.disabled ? kDim : sel ? kWhite : kText;
            if (r.type == RowNative || r.type == RowMod) {
                g.Text(r.label, colX[0], cy, text, labelColor, 0, colX[1] - colX[0] - 16 * s);
                for (int c = 0; c < 2; ++c) {
                    const float x0 = colX[c + 1] - 10 * s, x1 = (c == 0 ? colX[2] - 14 * s : px1 - margin + 4 * s);
                    const bool cell = sel && col == c;
                    if (cell) g.Rect(x0, y + 5 * s, x1, y + rowH - 5 * s, capture != CapNone ? 0x60c8a040u : 0x38ffffffu);
                    std::wstring v = CellText(r, c);
                    DWORD vc = v.empty() ? kDim : sel ? kWhite : kValue;
                    if (v.empty()) v = T(S_PadNone, ru);
                    if (cell && capture != CapNone) {
                        const float pulse = 0.55f + 0.45f * std::sin(float(now % 1200) / 1200.f * 6.2832f);
                        v = capture == CapKey ? T(S_CaptureKey, ru) : T(S_CapturePad, ru);
                        vc = (DWORD(pulse * 255) << 24) | 0x00ffd070u;
                    }
                    g.Text(v, colX[c + 1], cy, text, vc, 0, x1 - colX[c + 1] - 8 * s);
                }
            } else if (r.type == RowOption) {
                const float valueX = px0 + pw * 0.66f, noteX = px1 - margin;
                g.Text(r.label, px0 + margin + 8 * s, cy, text, labelColor, 0, valueX - px0 - margin - pw * 0.15f);
                const int value = host->OptGet(r.id);
                const OptStatus st = host->OptState(r.id);
                std::wstring v = ValueText(r.id, value);
                if (sel && !r.disabled) v = L"\u2039  " + v + L"  \u203a";
                g.Text(v, valueX, cy, text, r.disabled ? kDim : sel ? kWhite : kValue, 1, pw * 0.26f);
                std::wstring note; DWORD nc = kDim;
                if (r.disabled) { const Str blocked = host->OptBlocked(r.id); note = T(blocked != S_Count ? blocked : host->OptReason(r.id), ru); }   // fix 2026-11-11: the row that blocks it
                else if (st == OptRestart && value != host->OptRunning(r.id)) { note = T(S_AfterRestart, ru); nc = kOrange; }
                else if (st == OptNextMovie && value != host->OptRunning(r.id)) { note = T(S_NextMovie, ru); nc = kOrange; }
                if (!note.empty()) g.Text(note, noteX, cy + (text - smallPx) * 0.6f, smallPx, nc, 2, pw * 0.17f);
            } else {
                // round 7: the exit rows carry a longer note at the right (why greyed / what the second press costs)
                const bool exitRow = r.type == RowLink && (r.id == LinkExitTitle || r.id == LinkExitDesktop);
                g.Text(r.label, px0 + margin + 8 * s, cy, text, labelColor, 0, exitRow ? pw * 0.38f : pw * 0.6f);
                if (r.type == RowLink && r.id == LinkNative) g.Text(r.disabled ? T(S_NotMerce, ru) : T(S_MainNativeNote, ru), px1 - margin, cy + (text - smallPx) * 0.6f, smallPx, kDim, 2, pw * 0.36f);
                if (r.type == RowLink && (r.id == LinkControls || r.id == LinkOptions)) g.Text(L"\u203a", px1 - margin, cy, text, sel ? kWhite : kDim, 2);
                if (exitRow && r.disabled) g.Text(T(ExitBlocked(r.id), ru), px1 - margin, cy + (text - smallPx) * 0.6f, smallPx, kDim, 2, pw * 0.36f);
                if (confirmReset == i) g.Text(T(exitRow ? ExitConfirm(r.id) : S_PressAgain, ru), px1 - margin, cy + (text - smallPx) * 0.6f, smallPx, kOrange, 2, exitRow ? pw * 0.56f : pw * 0.36f);
            }
        }
        if (int(rows.size()) > visible) {   // scroll bar
            const float x = px1 - margin + 16 * s, h = footTop - 12 * s - top, bar = h * float(visible) / float(rows.size());
            const float y = top + (h - bar) * float(sc) / float((std::max)(1, int(rows.size()) - visible));
            g.Rect(x, top, x + 4 * s, top + h, 0x30ffffffu); g.Rect(x, y, x + 4 * s, y + bar, 0xa0ffffffu);
        }
    }

private:
    enum RowType { RowHeader, RowLink, RowNative, RowMod, RowOption, RowButton };
    enum LinkId { LinkControls, LinkOptions, LinkNative, LinkResetAll, LinkClose, LinkResetControls, LinkResetOptions, LinkBack,
                  LinkExitTitle, LinkExitDesktop };   // round 7
    enum CaptureMode { CapNone, CapKey, CapPad };
    struct Row { RowType type; int id; std::wstring label; bool selectable, disabled; };
    static constexpr DWORD kAccent = 0xffb3262bu, kAccentText = 0xffe0464bu, kSelect = 0xe0531418u, kWhite = 0xffffffffu, kText = 0xffd6d6d6u,
                           kValue = 0xffeae4d6u, kDim = 0xff8a8f98u, kGold = 0xffc9a35au, kWarn = 0xffff7b6bu, kGood = 0xff9fdc9fu, kOrange = 0xffe8a848u;
    Host* host = nullptr; Page page = PMain; int cursor[kPages]{}, scroll[kPages]{}; int col = 0;
    CaptureMode capture = CapNone; DWORD captureStart = 0; int confirmReset = -1;
    std::wstring status; DWORD statusUntil = 0; bool statusWarn = false; bool dirty = true;
    std::vector<Row> rows; float colX[3]{};

    void ColumnX(float px0, float pw, float margin) { colX[0] = px0 + margin + pw * 0.006f; colX[1] = px0 + pw * 0.40f; colX[2] = px0 + pw * 0.70f; }
    std::wstring MenuOpenName() {
        std::wstring k = KeyNameVk(host->Mod(ModMenu).vk, ru), p = PadMenuName();
        if (k.empty()) return p; if (p.empty()) return k;
        return k + L" / " + p;
    }
    std::wstring PadMenuName() {
        const PadBind b = host->Mod(ModMenu).pad;
        if (!b.Valid()) return std::wstring();
        if (b.ofs == kPadStart) return L"Start";
        if (b.ofs == kPadBack) return L"Back";
        return PadName(b, ru);
    }
    std::wstring Describe(const Row& r) {   // round 6: the footer text of the selected row
        switch (r.type) {
        case RowOption: return ru ? kOptions[r.id].desc.ru : kOptions[r.id].desc.en;
        case RowNative: return T(S_DescBindGame, ru);
        case RowMod: return T(S_DescBindMod, ru);
        case RowLink: case RowButton:
            switch (r.id) {
            case LinkControls: return T(S_DescControls, ru);
            case LinkOptions: return T(S_DescOptions, ru);
            case LinkNative: return T(S_DescNative, ru);
            case LinkResetAll: return T(S_DescResetAll, ru);
            case LinkClose: return T(S_DescClose, ru);
            case LinkResetControls: return T(S_DescResetControls, ru);
            case LinkResetOptions: return T(S_DescResetOptions, ru);
            case LinkBack: return T(S_DescBack, ru);
            case LinkExitTitle: return T(S_DescExitTitle, ru);
            case LinkExitDesktop: return T(S_DescExitDesktop, ru);
            default: return std::wstring();
            }
        default: return std::wstring();
        }
    }
    // round 7 (exit rows): why the row is greyed (S_Count = it works), and the orange note while it waits for its second
    // press - leaving a game loses the progress since the last save; outside a game (title screen) nothing is lost
    Str ExitBlocked(int link) { return link == LinkExitTitle ? host->ExitTitleBlocked() : host->ExitDesktopBlocked(); }
    Str ExitConfirm(int link) { return link == LinkExitDesktop && host->ExitTitleBlocked() == S_ExitNoGame ? S_PressAgainQuit : S_PressAgainExit; }
    std::wstring ValueText(int id, int v) {
        const OptionDef& o = kOptions[id];
        if (o.kind == OptBool) return T(v ? S_On : S_Off, ru);
        if (o.kind == OptChoice) return host->OptChoiceText(id, v, ru);   // voice packs 2026-11-11: table or host choices
        if (o.zeroOff && v == 0) return T(S_Off, ru);   // fix 2026-11-11
        return std::to_wstring(v) + L" %";
    }
    std::wstring CellText(const Row& r, int c) {
        std::wstring out;
        if (r.type == RowMod) {
            const ModBinding& m = host->Mod(r.id);
            return c == 0 ? KeyNameVk(m.vk, ru) : PadName(m.pad, ru);
        }
        const Bindings& b = host->Native();
        for (int s = 0; s < kSlots; ++s) {
            std::wstring name = c == 0 ? (b.key[r.id][s] > 0 ? KeyNameDik(b.key[r.id][s], ru) : std::wstring()) : PadName(b.pad[r.id][s], ru);
            if (name.empty()) continue;
            if (!out.empty()) out += L",  ";
            out += name;
        }
        return out;
    }
    bool OptOff(int id) { return host->OptState(id) == OptUnavailable || host->OptBlocked(id) != S_Count; }   // fix 2026-11-11: greyed
    void Build() {
        rows.clear();
        auto add = [&](RowType t, int id, const std::wstring& label, bool disabled = false) { rows.push_back({t, id, label, t != RowHeader, disabled}); };
        if (page == PMain) {
            add(RowLink, LinkControls, T(S_MainControls, ru));
            add(RowLink, LinkOptions, T(S_MainOptions, ru));
            add(RowLink, LinkNative, T(S_MainNative, ru), !host->NativeWindowAvailable());
            add(RowLink, LinkResetAll, T(S_ResetAll, ru));
            add(RowLink, LinkClose, T(S_Close, ru));
            add(RowLink, LinkExitTitle, T(S_ExitTitle, ru), ExitBlocked(LinkExitTitle) != S_Count);       // round 7: the exits at the very bottom
            add(RowLink, LinkExitDesktop, T(S_ExitDesktop, ru), ExitBlocked(LinkExitDesktop) != S_Count);
        } else if (page == PControls) {
            add(RowHeader, 0, T(S_GroupMove, ru));   // fix 2026-11-11 (user: "move the modern controls switch to the controls page")
            add(RowOption, O_Scheme, ru ? kOptions[O_Scheme].label.ru : kOptions[O_Scheme].label.en, OptOff(O_Scheme));
            add(RowHeader, 1, T(S_GroupGame, ru));
            for (int i = 0; i < kMenuActions; ++i) add(RowNative, kMenuOrder[i], ru ? kActionName[kMenuOrder[i]].ru : kActionName[kMenuOrder[i]].en);
            add(RowHeader, 2, T(S_GroupMod, ru));
            for (int m = 0; m < kModActions; ++m) add(RowMod, m, ru ? kModName[m].ru : kModName[m].en);
            add(RowButton, LinkResetControls, T(S_ResetControls, ru));
            add(RowButton, LinkBack, T(S_Back, ru));
        } else {
            Str group = S_Count;
            for (int i = 0; i < O_PageCount; ++i) {
                if (kOptions[i].group == S_GroupTest) continue;   // release 1.50: the TEST rows (Glock kneel burst, dodge always) are not shown; pilot.ini still reads them
                if (i == O_ShadowEdges) continue;   // fix 2026-11-11d3 (user: the two cut modes have problems in many places, not offered for now): always the endless floor
                if (kOptions[i].group != group) { group = kOptions[i].group; add(RowHeader, 0, T(group, ru)); }
                add(RowOption, i, ru ? kOptions[i].label.ru : kOptions[i].label.en, OptOff(i));
            }
            add(RowButton, LinkResetOptions, T(S_ResetOptions, ru));
            add(RowButton, LinkBack, T(S_Back, ru));
        }
        int& c = cursor[page];
        c = (std::max)(0, (std::min)(c, int(rows.size()) - 1));
        if (!rows[size_t(c)].selectable) { for (int i = c; i < int(rows.size()); ++i) if (rows[size_t(i)].selectable) { c = i; break; } }
    }
    void Move(int delta) {
        const int n = int(rows.size()); int c = cursor[page];
        for (int k = 0; k < n; ++k) {
            c += delta;
            if (c < 0) c = n - 1;
            if (c >= n) c = 0;
            if (rows[size_t(c)].selectable) break;
        }
        cursor[page] = c; confirmReset = -1; dirty = true;
    }
    void Jump(int target, int dir) {
        const int n = int(rows.size()); int c = (std::max)(0, (std::min)(n - 1, target));
        while (c >= 0 && c < n && !rows[size_t(c)].selectable) c += dir;
        if (c >= 0 && c < n) cursor[page] = c;
        confirmReset = -1; dirty = true;
    }
    void Do(Nav n, DWORD now) {
        Build(); dirty = true;
        const Row& r = rows[size_t(cursor[page])];
        switch (n) {
        case NavClose: host->Close(); return;
        case NavUp: Move(-1); return;
        case NavDown: Move(+1); return;
        case NavHome: Jump(0, +1); return;
        case NavEnd: Jump(int(rows.size()) - 1, -1); return;
        case NavPageUp: Jump(cursor[page] - 8, +1); return;
        case NavPageDown: Jump(cursor[page] + 8, -1); return;
        case NavPrevPage: case NavNextPage:
            page = page == PControls ? POptions : PControls; confirmReset = -1; status.clear(); return;
        case NavBack:
            if (page == PMain) host->Close(); else { page = PMain; confirmReset = -1; }
            return;
        default: break;
        }
        if (r.type == RowNative || r.type == RowMod) {
            if (n == NavLeft || n == NavRight) { col = n == NavLeft ? 0 : 1; return; }
            if (n == NavOk) { StartCapture(col == 1, now); return; }
            if (n == NavClear) { ClearCell(r, now); return; }
            return;
        }
        if (r.type == RowOption) {
            if (n != NavLeft && n != NavRight && n != NavOk) return;
            const OptionDef& o = kOptions[r.id];
            { const Str blocked = host->OptBlocked(r.id); if (blocked != S_Count) { SetStatus(T(blocked, ru), now, true); return; } }   // fix 2026-11-11
            if (host->OptState(r.id) == OptUnavailable) { SetStatus(T(host->OptReason(r.id), ru), now, true); return; }
            int v = host->OptGet(r.id);
            const int hi = o.kind == OptChoice && !o.choice ? (std::max)(o.lo, o.lo + host->OptChoiceCount(r.id) - 1) : o.hi;   // voice packs 2026-11-11
            if (n == NavOk) { v += o.step; if (v > hi) v = o.lo; }
            else { v += n == NavLeft ? -o.step : o.step; v = (std::max)(o.lo, (std::min)(hi, v)); }
            if (v != host->OptGet(r.id)) {
                host->OptSet(r.id, v);
                if (r.id == O_Language) ru = MenuRussian(v);
            }
            return;
        }
        if (n != NavOk) return;
        switch (r.id) {
        case LinkControls: page = PControls; status.clear(); return;
        case LinkOptions: page = POptions; status.clear(); return;
        case LinkNative: if (!r.disabled) host->OpenNativeWindow(); return;
        case LinkClose: host->Close(); return;
        case LinkBack: page = PMain; return;
        case LinkResetAll: case LinkResetControls: case LinkResetOptions:
            if (confirmReset != cursor[page]) { confirmReset = cursor[page]; status.clear(); return; }
            confirmReset = -1;
            if (r.id != LinkResetOptions) ResetControls();
            if (r.id != LinkResetControls) ResetOptions();
            SetStatus(T(r.id == LinkResetAll ? S_DefaultsAll : r.id == LinkResetControls ? S_DefaultsControls : S_DefaultsOptions, ru), now);
            return;
        case LinkExitTitle: case LinkExitDesktop: {   // round 7: greyed = the reason in the status line; else press twice
            const Str blocked = ExitBlocked(r.id);
            if (blocked != S_Count) { confirmReset = -1; SetStatus(T(blocked, ru), now, true); return; }
            if (confirmReset != cursor[page]) { confirmReset = cursor[page]; status.clear(); return; }
            confirmReset = -1;
            if (r.id == LinkExitTitle) host->ExitToTitle(); else host->ExitToDesktop();
            return;
        }
        default: return;
        }
    }
    void ResetControls() {
        Bindings& b = host->Native(); DefaultKeyboard(b); DefaultPad(b); host->CommitNative(true, true);
        for (int m = 0; m < kModActions; ++m) { host->Mod(m).vk = kMod[m].vk; host->Mod(m).pad = kMod[m].pad; host->CommitMod(m); }
        if (host->OptState(O_Scheme) != OptUnavailable && host->OptGet(O_Scheme) != kOptions[O_Scheme].def) host->OptSet(O_Scheme, kOptions[O_Scheme].def);   // fix 2026-11-11: the scheme row lives here
        host->Note("MENU_RESET controls (bio3.ini [Keyboard]/[Gamepad] defaults, mod keys F5/F8/F7/R, pad Start/-/-/Y)");
    }
    void ResetOptions() {
        for (int i = 0; i < O_PageCount; ++i) if (host->OptState(i) != OptUnavailable && host->OptGet(i) != kOptions[i].def) host->OptSet(i, kOptions[i].def);
        ru = MenuRussian(host->OptGet(O_Language));
        host->Note("MENU_RESET options (pilot.ini defaults)");
    }
    void ClearCell(const Row& r, DWORD now) {
        if (r.type == RowMod) {
            ModBinding& m = host->Mod(r.id);
            if (col == 0 && r.id == ModMenu) { SetStatus(T(S_MenuNeedsKey, ru), now, true); return; }
            if (col == 0) m.vk = 0; else m.pad = PadBind{};
            host->CommitMod(r.id);
        } else {
            // without a pad the game has not loaded [Gamepad] (405860 reads the section of the pad's name): saving the
            // empty table would wipe the player's pad bindings
            if (col == 1 && !host->PadPresent()) { SetStatus(T(S_NoPad, ru), now, true); return; }
            Bindings& b = host->Native();
            for (int s = 0; s < kSlots; ++s) { if (col == 0) b.key[r.id][s] = -1; else b.pad[r.id][s] = PadBind{}; }
            host->CommitNative(col == 0, col == 1);
        }
        SetStatus(T(S_Cleared, ru), now);
    }
    void StartCapture(bool pad, DWORD now) {
        const Row& r = rows[size_t(cursor[page])];
        if (r.type != RowNative && r.type != RowMod) return;
        col = pad ? 1 : 0;
        if (pad && !host->PadPresent()) { SetStatus(T(S_NoPad, ru), now, true); return; }
        capture = pad ? CapPad : CapKey; captureStart = now; captureRow = r; status.clear(); dirty = true;
    }
    Row captureRow{};
    void EndCapture(const std::wstring& message, DWORD now, bool warn = false) { capture = CapNone; SetStatus(message, now, warn); }
    std::wstring Fmt(Str s, const std::wstring& a, const std::wstring& b = std::wstring()) {
        wchar_t buf[256]; swprintf_s(buf, T(s, ru), a.c_str(), b.c_str()); return buf;
    }
    std::wstring ActionLabel(int a) { return ru ? kActionName[a].ru : kActionName[a].en; }
    std::wstring ModLabel(int m) { return ru ? kModName[m].ru : kModName[m].en; }
    static bool NavKey(int vk) {
        return vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_RETURN || vk == VK_ESCAPE || vk == VK_SPACE ||
               vk == VK_BACK || vk == VK_DELETE || vk == VK_TAB;
    }
    static bool NavPad(PadBind b) {   // A, B, X, the left stick / d-pad and POV: they drive the menu
        return (KindOf(b.ofs) == PadKind::Button && b.ofs <= 50) || (KindOf(b.ofs) == PadKind::Axis && b.ofs <= 4) || KindOf(b.ofs) == PadKind::Pov;
    }
    void CaptureKey(UINT vk, int dik, DWORD now) {
        if (vk == VK_ESCAPE) { EndCapture(T(S_Cancelled, ru), now); return; }
        if (!dik) dik = DikFromVk(int(vk));
        const std::wstring name = KeyNameDik(dik, ru);
        Bindings& b = host->Native();
        char line[160];
        if (captureRow.type == RowMod) {
            const int m = captureRow.id;
            if (m == ModMenu && NavKey(int(vk))) { EndCapture(Fmt(S_ReservedNav, name), now, true); return; }
            if (vk == VK_F1 || vk == VK_F10 || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) { EndCapture(Fmt(S_ReservedGame, name), now, true); return; }
            std::wstring moved;
            for (int o = 0; o < kModActions; ++o) if (o != m && host->Mod(o).vk == int(vk)) {
                if (o == ModMenu) { EndCapture(Fmt(S_ReservedMenu, name), now, true); return; }
                host->Mod(o).vk = 0; host->CommitMod(o); moved = ModLabel(o);
            }
            const int a = FindKey(b, dik);
            if (a >= 0) { RemoveKey(b, a, dik); host->CommitNative(true, false); moved = ActionLabel(a); }
            host->Mod(m).vk = int(vk); host->CommitMod(m);
            sprintf_s(line, "MENU_BIND mod %s key vk=%d dik=%d%s", kMod[m].keyIni, int(vk), dik, moved.empty() ? "" : " (moved)");
            host->Note(line);
            EndCapture(moved.empty() ? Fmt(S_Added, name) : Fmt(S_MovedFrom, name, moved), now);
            return;
        }
        const int a = captureRow.id;
        const int menuDik = DikFromVk(host->Mod(ModMenu).vk);
        if (menuDik && dik == menuDik) { EndCapture(Fmt(S_ReservedMenu, name), now, true); return; }
        int slot = -1;
        for (int s = 0; s < kSlots; ++s) if (b.key[a][s] == dik) slot = s;
        if (slot >= 0) {   // round 6: the same key again changes nothing (it used to be removed); Del / X empties the cell
            sprintf_s(line, "MENU_BIND %s key %d already there", kNative[a].ini, dik); host->Note(line);
            EndCapture(Fmt(S_Already, name), now);
            return;
        }
        if (CountKeys(b, a) >= kSlots) { EndCapture(T(S_Max16, ru), now, true); return; }
        std::wstring moved;
        const int other = FindKey(b, dik);
        if (other >= 0) { RemoveKey(b, other, dik); moved = ActionLabel(other); }
        for (int o = 0; o < kModActions; ++o) if (host->Mod(o).vk && DikFromVk(host->Mod(o).vk) == dik) { host->Mod(o).vk = 0; host->CommitMod(o); moved = ModLabel(o); }
        AppendKey(b, a, dik); host->CommitNative(true, false);
        sprintf_s(line, "MENU_BIND %s key %d added%s", kNative[a].ini, dik, moved.empty() ? "" : " (moved from another action)"); host->Note(line);
        EndCapture(moved.empty() ? Fmt(S_Added, name) : Fmt(S_MovedFrom, name, moved), now);
    }
    void CapturePad(PadBind p, DWORD now) {
        const PadBind menuPad = host->Mod(ModMenu).pad;
        if (SamePad(p, menuPad) && !(captureRow.type == RowMod && captureRow.id == ModMenu)) { EndCapture(T(S_Cancelled, ru), now); return; }
        const std::wstring name = PadName(p, ru);
        Bindings& b = host->Native();
        char line[160];
        if (captureRow.type == RowMod) {
            const int m = captureRow.id;
            if (m == ModMenu && NavPad(p)) { EndCapture(Fmt(S_ReservedNav, name), now, true); return; }
            std::wstring moved;
            for (int o = 0; o < kModActions; ++o) if (o != m && SamePad(host->Mod(o).pad, p)) { host->Mod(o).pad = PadBind{}; host->CommitMod(o); moved = ModLabel(o); }
            const int a = FindPad(b, p);
            if (a >= 0) { RemovePad(b, a, p); host->CommitNative(false, true); moved = ActionLabel(a); }
            host->Mod(m).pad = p; host->CommitMod(m);
            sprintf_s(line, "MENU_BIND mod %s pad %d,%d%s", kMod[m].padIni, p.ofs, p.val, moved.empty() ? "" : " (moved)"); host->Note(line);
            EndCapture(moved.empty() ? Fmt(S_Added, name) : Fmt(S_MovedFrom, name, moved), now);
            return;
        }
        const int a = captureRow.id;
        int slot = -1;
        for (int s = 0; s < kSlots; ++s) if (SamePad(b.pad[a][s], p)) slot = s;
        if (slot >= 0) {   // round 6: the same input again changes nothing (it used to be removed: with a stick that read as "the wrong direction")
            sprintf_s(line, "MENU_BIND %s pad %d,%d already there", kNative[a].ini, p.ofs, p.val); host->Note(line);
            EndCapture(Fmt(S_Already, name), now);
            return;
        }
        if (CountPads(b, a) >= kSlots) { EndCapture(T(S_Max16, ru), now, true); return; }
        std::wstring moved;
        const int other = FindPad(b, p);
        if (other >= 0) { RemovePad(b, other, p); moved = ActionLabel(other); }
        for (int o = 0; o < kModActions; ++o) if (SamePad(host->Mod(o).pad, p)) { host->Mod(o).pad = PadBind{}; host->CommitMod(o); moved = ModLabel(o); }
        AppendPad(b, a, p); host->CommitNative(false, true);
        sprintf_s(line, "MENU_BIND %s pad %d,%d added%s", kNative[a].ini, p.ofs, p.val, moved.empty() ? "" : " (moved from another action)"); host->Note(line);
        EndCapture(moved.empty() ? Fmt(S_Added, name) : Fmt(S_MovedFrom, name, moved), now);
    }
};

// ================================================================== the pad in the open menu (round 6)
// Round 6 (09.11 night, the user with an Xbox pad: "the d-pad jumps over one row, the stick does not; B does not go back and
// does not close; while a button is awaited the d-pad is taken but also scrolls; assigning stick directions is
// unpredictable, the wrong directions end up assigned"):
//  - the scan stepped 4 offsets through the buttons too, so only buttons 0, 4, 8 (A, LB, Back) were ever seen: B, X, Y,
//    RB, the triggers, Start and the stick clicks did nothing in the open menu and could not be assigned;
//  - GOG dinput.dll (goginput.ini dpadtoanalog = TRUE) reports the d-pad on lX/lY AND on the POV hat: two inputs for one
//    press = two steps, and when a capture took the first, the second scrolled. The hat is not scanned then, and each of
//    the four directions is ONE logical input whatever reports it (one step per press, one repeat clock, 90 ms guard);
//  - a captured input that stayed held (a stick) began to repeat as a direction 0.4 s later and walked the cursor off the
//    row just edited - the next capture then went to another action. After a capture nothing is taken until every input
//    has been at rest for 80 ms;
//  - of the two axes of one stick only the one deflected more is taken (a slanted push gave whichever the scan met first).
// Pure logic: the game's DirectInput wrapper (mod_menu.h) or a scripted pad (qa/.../menu/tools/menu_tests.cpp) is the source.
struct PadSource {
    virtual ~PadSource() {}
    virtual int Count() = 0;          // joysticks
    virtual int Type(int ofs) = 0;    // DIDFT type of a DIJOYSTATE offset (1|2 axis, 4|8 button, 0x10 POV), 0 = not there
    virtual int Value(int ofs) = 0;   // axis -1000..1000, button != 0, POV 0..35999 (-1 / low word FFFF at rest)
    virtual void Refresh() = 0;       // poll the device
};
class PadReader {
public:
    struct Input { int ofs; int type; int state; };   // state: 0 rest, else the bound value (POV: angle + 1)
    static constexpr int kMax = 44;
    static constexpr DWORD kFirstRepeat = 420, kRepeat = 110, kBounce = 90, kQuiet = 80;
    // scan the offsets the pad has; dpadIsStick: the d-pad arrives on lX/lY too (GOG dpadtoanalog), the hat is left out
    void Init(PadSource& src, bool dpadIsStick) {
        count = 0; quiet = false; quietSince = 0; hatSkipped = dpadIsStick; seen.clear();
        for (auto& d : dirs) d = Dir{};
        for (int ofs = 0; ofs < 80 && count < kMax; ofs += ofs < 48 ? 4 : 1) {
            const PadKind kind = KindOf(ofs);
            if (kind == PadKind::None) continue;
            const int type = src.Type(ofs) & 0xff;
            if (!type) continue;
            char part[24]; sprintf_s(part, " %d:%s", ofs, (type & 3) ? "axis" : (type & 0x0c) ? "button" : (type & 0x10) ? "pov" : "?"); seen += part;
            if (kind == PadKind::Pov && dpadIsStick) continue;
            inputs[count++] = Input{ofs, type, 0};
        }
    }
    const std::string& Seen() const { return seen; }
    bool HatSkipped() const { return hatSkipped; }
    int Count() const { return count; }
    bool AnyActive() const { for (int i = 0; i < count; ++i) if (inputs[i].state) return true; return false; }
    static Nav NavOf(PadBind b, PadBind menuPad) {
        if (SamePad(b, menuPad)) return NavClose;
        switch (KindOf(b.ofs)) {
        case PadKind::Button:
            switch (b.ofs) { case 48: return NavOk; case 49: return NavBack; case 50: return NavClear; case 52: return NavPrevPage; case 53: return NavNextPage; default: return NavNone; }
        case PadKind::Axis:
            if (b.ofs == 4 || b.ofs == 16) return b.val < 0 ? NavUp : NavDown;
            if (b.ofs == 0 || b.ofs == 12) return b.val < 0 ? NavLeft : NavRight;
            return NavNone;
        case PadKind::Pov:
            return b.val == 0 ? NavUp : b.val == 9000 ? NavRight : b.val == 18000 ? NavDown : b.val == 27000 ? NavLeft : NavNone;
        default: return NavNone;
        }
    }
    // one poll of the open menu. baseline: take the states as they are (menu just opened / closing). stop: the menu was
    // asked to close. The first 240 button / capture events and the first 60 direction steps of a run go to host->Note.
    void Poll(PadSource& src, Menu& menu, Host& host, DWORD now, bool baseline, const bool& stop) {
        if (src.Count() <= 0) return;
        src.Refresh();
        const PadBind menuPad = host.Mod(ModMenu).pad;
        int fresh[kMax]; bool any = false;
        for (int i = 0; i < count; ++i) fresh[i] = State(src, inputs[i]);
        for (const auto& pair : {std::pair<int, int>{0, 4}, std::pair<int, int>{12, 16}}) {   // one stick, two axes: the stronger one
            const int a = Index(pair.first), b = Index(pair.second);
            if (a < 0 || b < 0 || !fresh[a] || !fresh[b]) continue;
            if (inputs[a].state && !inputs[b].state) fresh[b] = 0;
            else if (inputs[b].state && !inputs[a].state) fresh[a] = 0;
            else if (!inputs[a].state && !inputs[b].state) fresh[std::abs(src.Value(pair.first)) >= std::abs(src.Value(pair.second)) ? b : a] = 0;
        }
        bool dirNow[4]{}; int dirSource[4]{-1, -1, -1, -1};
        for (int i = 0; i < count; ++i) {
            if (!fresh[i]) continue;
            any = true;
            const int k = DirIndex(NavOf(BindOf(inputs[i], fresh[i]), menuPad));
            if (k >= 0 && !dirNow[k]) { dirNow[k] = true; dirSource[k] = i; }
        }
        auto settle = [&] {   // take the states as they are, no events
            for (int i = 0; i < count; ++i) inputs[i].state = fresh[i];
            for (int k = 0; k < 4; ++k) { dirs[k].held = dirNow[k]; dirs[k].repeatAt = now + 100000; }
        };
        if (baseline) { settle(); return; }
        if (quiet) {   // after a capture: until everything has been at rest for 80 ms
            settle();
            if (any) quietSince = 0;
            else if (!quietSince) quietSince = now | 1;
            else if (now - quietSince >= kQuiet) quiet = false;
            return;
        }
        const bool capturing = menu.Capturing();
        for (int i = 0; i < count; ++i) {
            Input& p = inputs[i]; const int s = fresh[i];
            if (s == p.state) continue;
            p.state = s;
            if (!s) continue;
            const PadBind b = BindOf(p, s); const Nav n = NavOf(b, menuPad);
            if (capturing) {
                Note(host, menu, "capture", b, src.Value(b.ofs), n, eventLines, 240);
                menu.OnPad(b, n, now);
                if (!menu.Capturing()) { quiet = true; quietSince = 0; settle(); return; }
                continue;
            }
            if (DirIndex(n) >= 0) continue;   // directions: below, one logical input each
            Note(host, menu, "button", b, src.Value(b.ofs), n, eventLines, 240);
            menu.OnPad(b, n, now);
        }
        if (capturing || menu.Capturing() || stop) { settle(); return; }   // a capture began / the menu closes: no steps in this poll
        for (int k = 0; k < 4; ++k) {
            Dir& d = dirs[k];
            if (!dirNow[k]) { d.held = false; continue; }
            const Nav n = Nav(int(NavUp) + k);
            const PadBind b = BindOf(inputs[dirSource[k]], fresh[dirSource[k]]);
            bool step = false;
            if (!d.held) { d.held = true; d.repeatAt = now + kFirstRepeat; step = !d.lastStep || now - d.lastStep >= kBounce; }   // a bounce is no second press
            else if (int(now - d.repeatAt) >= 0) { d.repeatAt = now + kRepeat; step = true; }
            if (!step) continue;
            d.lastStep = now | 1;
            Note(host, menu, "step", b, src.Value(b.ofs), n, stepLines, 60);
            menu.OnPad(b, n, now);
        }
    }

private:
    struct Dir { bool held = false; DWORD repeatAt = 0, lastStep = 0; };
    Input inputs[kMax]{}; int count = 0; Dir dirs[4];   // NavUp, NavDown, NavLeft, NavRight
    bool quiet = false, hatSkipped = false; DWORD quietSince = 0; unsigned eventLines = 0, stepLines = 0; std::string seen;
    static int DirIndex(Nav n) { return n == NavUp ? 0 : n == NavDown ? 1 : n == NavLeft ? 2 : n == NavRight ? 3 : -1; }
    static PadBind BindOf(const Input& p, int state) { return PadBind{p.ofs, (p.type & 0x10) ? state - 1 : state}; }
    int Index(int ofs) const { for (int i = 0; i < count; ++i) if (inputs[i].ofs == ofs) return i; return -1; }
    static int State(PadSource& src, const Input& p) {
        const int v = src.Value(p.ofs);
        if (p.type & 3) {   // axis -1000..1000, hysteresis
            if (p.state) return (p.state < 0 ? v < -300 : v > 300) ? p.state : 0;
            return v < -600 ? -500 : v > 600 ? 500 : 0;
        }
        if (p.type & 0x0c) return v ? 128 : 0;
        if (p.type & 0x10) { if ((v & 0xffff) == 0xffff || v < 0) return 0; return ((v + 2250) / 4500 * 4500) % 36000 + 1; }   // +1: 0 = rest
        return 0;
    }
    static void Note(Host& host, Menu& menu, const char* what, PadBind b, int value, Nav n, unsigned& lines, unsigned limit) {
        if (lines >= limit) return;
        ++lines;
        char line[128]; sprintf_s(line, "MENU_PAD %s %d,%d value=%d nav=%d page=%d", what, b.ofs, b.val, value, int(n), int(menu.CurrentPage()));
        host.Note(line);
    }
};

}  // namespace re3menu
