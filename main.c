#include <signal.h>
#include <limits.h>
#include <string.h>
#include <errno.h>

#include "ui-curses.h"
#include "vis.h"
#include "text-util.h"
#include "text-motions.h"
#include "text-objects.h"
#include "util.h"
#include "libutf.h"

#define PAGE      INT_MAX
#define PAGE_HALF (INT_MAX-1)

/** functions to be called from keybindings */
/* ignore key, do nothing */
static const char *nop(Vis*, const char *keys, const Arg *arg);
static const char *macro_record(Vis*, const char *keys, const Arg *arg);
static const char *macro_replay(Vis*, const char *keys, const Arg *arg);
/* temporarily suspend the editor and return to the shell, type 'fg' to get back */
static const char *suspend(Vis*, const char *keys, const Arg *arg);
/* switch to mode indicated by arg->i */
static const char *switchmode(Vis*, const char *keys, const Arg *arg);
/* set mark indicated by arg->i to current cursor position */
static const char *mark_set(Vis*, const char *keys, const Arg *arg);
/* add a new line either before or after the one where the cursor currently is */
static const char *openline(Vis*, const char *keys, const Arg *arg);
/* join lines from current cursor position to movement indicated by arg */
static const char *join(Vis*, const char *keys, const Arg *arg);
/* execute arg->s as if it was typed on command prompt */
static const char *cmd(Vis*, const char *keys, const Arg *arg);
/* perform last action i.e. action_prev again */
static const char *repeat(Vis*, const char *keys, const Arg *arg);
/* replace character at cursor with one read form keyboard */
static const char *replace(Vis*, const char *keys, const Arg *arg);
/* create a new cursor on the previous (arg->i < 0) or next (arg->i > 0) line */
static const char *cursors_new(Vis*, const char *keys, const Arg *arg);
/* try to align all cursors on the same column */
static const char *cursors_align(Vis*, const char *keys, const Arg *arg);
/* remove all but the primary cursor and their selections */
static const char *cursors_clear(Vis*, const char *keys, const Arg *arg);
/* remove the least recently added cursor */
static const char *cursors_remove(Vis*, const char *keys, const Arg *arg);
/* select the word the cursor is currently over */
static const char *cursors_select(Vis*, const char *keys, const Arg *arg);
/* select the next region matching the current selection */
static const char *cursors_select_next(Vis*, const char *keys, const Arg *arg);
/* clear current selection but select next match */
static const char *cursors_select_skip(Vis*, const char *keys, const Arg *arg);
/* adjust action.count by arg->i */
static const char *count(Vis*, const char *keys, const Arg *arg);
/* move to the action.count-th line or if not given either to the first (arg->i < 0)
 *  or last (arg->i > 0) line of file */
static const char *gotoline(Vis*, const char *keys, const Arg *arg);
/* set motion type either LINEWISE or CHARWISE via arg->i */
static const char *motiontype(Vis*, const char *keys, const Arg *arg);
/* make the current action use the operator indicated by arg->i */
static const char *operator(Vis*, const char *keys, const Arg *arg);
/* blocks to read a key and performs movement indicated by arg->i which
 * should be one of MOVE_{RIGHT,LEFT}_{TO,TILL} */
static const char *movement_key(Vis*, const char *keys, const Arg *arg);
/* perform the movement as indicated by arg->i */
static const char *movement(Vis*, const char *keys, const Arg *arg);
/* let the current operator affect the range indicated by the text object arg->i */
static const char *textobj(Vis*, const char *keys, const Arg *arg);
/* move to the other end of selected text */
static const char *selection_end(Vis*, const char *keys, const Arg *arg);
/* restore least recently used selection */
static const char *selection_restore(Vis*, const char *keys, const Arg *arg);
/* use register indicated by arg->i for the current operator */
static const char *reg(Vis*, const char *keys, const Arg *arg);
/* perform arg->i motion with a mark as argument */
static const char *mark_motion(Vis*, const char *keys, const Arg *arg);
/* {un,re}do last action, redraw window */
static const char *undo(Vis*, const char *keys, const Arg *arg);
static const char *redo(Vis*, const char *keys, const Arg *arg);
/* earlier, later action chronologically, redraw window */
static const char *earlier(Vis*, const char *keys, const Arg *arg);
static const char *later(Vis*, const char *keys, const Arg *arg);
/* hange/delete from the current cursor position to the end of
 * movement as indicated by arg->i */
static const char *delete(Vis*, const char *keys, const Arg *arg);
/* insert register content indicated by arg->i at current cursor position */
static const char *insert_register(Vis*, const char *keys, const Arg *arg);
/* show a user prompt to get input with title arg->s */
static const char *prompt_search(Vis*, const char *keys, const Arg *arg);
static const char *prompt_cmd(Vis*, const char *keys, const Arg *arg);
/* exit command mode if the last char is deleted */
static const char *prompt_backspace(Vis*, const char *keys, const Arg *arg);
/* blocks to read 3 consecutive digits and inserts the corresponding byte value */
static const char *insert_verbatim(Vis*, const char *keys, const Arg *arg);
/* scroll window content according to arg->i which can be either PAGE, PAGE_HALF,
 * or an arbitrary number of lines. a multiplier overrides what is given in arg->i.
 * negative values scroll back, positive forward. */
static const char *wscroll(Vis*, const char *keys, const Arg *arg);
/* similar to scroll, but do only move window content not cursor position */
static const char *wslide(Vis*, const char *keys, const Arg *arg);
/* call editor function as indicated by arg->f */
static const char *call(Vis*, const char *keys, const Arg *arg);
/* call window function as indicated by arg->w */
static const char *window(Vis*, const char *keys, const Arg *arg);

enum {
	VIS_ACTION_EDITOR_SUSPEND,
	VIS_ACTION_CURSOR_CHAR_PREV,
	VIS_ACTION_CURSOR_CHAR_NEXT,
	VIS_ACTION_CURSOR_WORD_START_PREV,
	VIS_ACTION_CURSOR_WORD_START_NEXT,
	VIS_ACTION_CURSOR_WORD_END_PREV,
	VIS_ACTION_CURSOR_WORD_END_NEXT,
	VIS_ACTION_CURSOR_LONGWORD_START_PREV,
	VIS_ACTION_CURSOR_LONGWORD_START_NEXT,
	VIS_ACTION_CURSOR_LONGWORD_END_PREV,
	VIS_ACTION_CURSOR_LONGWORD_END_NEXT,
	VIS_ACTION_CURSOR_LINE_UP,
	VIS_ACTION_CURSOR_LINE_DOWN,
	VIS_ACTION_CURSOR_LINE_START,
	VIS_ACTION_CURSOR_LINE_FINISH,
	VIS_ACTION_CURSOR_LINE_BEGIN,
	VIS_ACTION_CURSOR_LINE_END,
	VIS_ACTION_CURSOR_SCREEN_LINE_UP,
	VIS_ACTION_CURSOR_SCREEN_LINE_DOWN,
	VIS_ACTION_CURSOR_SCREEN_LINE_BEGIN,
	VIS_ACTION_CURSOR_SCREEN_LINE_MIDDLE,
	VIS_ACTION_CURSOR_SCREEN_LINE_END,
	VIS_ACTION_CURSOR_BRACKET_MATCH,
	VIS_ACTION_CURSOR_PARAGRAPH_PREV,
	VIS_ACTION_CURSOR_PARAGRAPH_NEXT,
	VIS_ACTION_CURSOR_SENTENCE_PREV,
	VIS_ACTION_CURSOR_SENTENCE_NEXT,
	VIS_ACTION_CURSOR_FUNCTION_START_PREV,
	VIS_ACTION_CURSOR_FUNCTION_END_PREV,
	VIS_ACTION_CURSOR_FUNCTION_START_NEXT,
	VIS_ACTION_CURSOR_FUNCTION_END_NEXT,
	VIS_ACTION_CURSOR_COLUMN,
	VIS_ACTION_CURSOR_LINE_FIRST,
	VIS_ACTION_CURSOR_LINE_LAST,
	VIS_ACTION_CURSOR_WINDOW_LINE_TOP,
	VIS_ACTION_CURSOR_WINDOW_LINE_MIDDLE,
	VIS_ACTION_CURSOR_WINDOW_LINE_BOTTOM,
	VIS_ACTION_CURSOR_SEARCH_NEXT,
	VIS_ACTION_CURSOR_SEARCH_PREV,
	VIS_ACTION_CURSOR_SEARCH_WORD_FORWARD,
	VIS_ACTION_CURSOR_SEARCH_WORD_BACKWARD,
	VIS_ACTION_WINDOW_PAGE_UP,
	VIS_ACTION_WINDOW_PAGE_DOWN,
	VIS_ACTION_WINDOW_HALFPAGE_UP,
	VIS_ACTION_WINDOW_HALFPAGE_DOWN,
	VIS_ACTION_MODE_NORMAL,
	VIS_ACTION_MODE_VISUAL,
	VIS_ACTION_MODE_VISUAL_LINE,
	VIS_ACTION_MODE_INSERT,
	VIS_ACTION_MODE_REPLACE,
	VIS_ACTION_MODE_OPERATOR_PENDING,
	VIS_ACTION_DELETE_CHAR_PREV,
	VIS_ACTION_DELETE_CHAR_NEXT,
	VIS_ACTION_DELETE_LINE_BEGIN,
	VIS_ACTION_DELETE_WORD_PREV,
	VIS_ACTION_JUMPLIST_PREV,
	VIS_ACTION_JUMPLIST_NEXT,
	VIS_ACTION_CHANGELIST_PREV,
	VIS_ACTION_CHANGELIST_NEXT,
	VIS_ACTION_UNDO,
	VIS_ACTION_REDO,
	VIS_ACTION_EARLIER,
	VIS_ACTION_LATER,
	VIS_ACTION_MACRO_RECORD,
	VIS_ACTION_MACRO_REPLAY,
	VIS_ACTION_MARK_SET,
	VIS_ACTION_MARK_GOTO,
	VIS_ACTION_MARK_GOTO_LINE,
	VIS_ACTION_REDRAW,
	VIS_ACTION_REPLACE_CHAR,
	VIS_ACTION_TOTILL_REPEAT,
	VIS_ACTION_TOTILL_REVERSE,
	VIS_ACTION_PROMPT_SEARCH_FORWARD,
	VIS_ACTION_PROMPT_SEARCH_BACKWARD,
	VIS_ACTION_TILL_LEFT,
	VIS_ACTION_TILL_RIGHT,
	VIS_ACTION_TO_LEFT,
	VIS_ACTION_TO_RIGHT,
	VIS_ACTION_REGISTER,
	VIS_ACTION_OPERATOR_CHANGE,
	VIS_ACTION_OPERATOR_DELETE,
	VIS_ACTION_OPERATOR_YANK,
	VIS_ACTION_OPERATOR_SHIFT_LEFT,
	VIS_ACTION_OPERATOR_SHIFT_RIGHT,
	VIS_ACTION_OPERATOR_CASE_LOWER,
	VIS_ACTION_OPERATOR_CASE_UPPER,
	VIS_ACTION_OPERATOR_CASE_SWAP,
	VIS_ACTION_COUNT,
	VIS_ACTION_INSERT_NEWLINE,
	VIS_ACTION_INSERT_TAB,
	VIS_ACTION_INSERT_VERBATIM,
	VIS_ACTION_INSERT_REGISTER,
	VIS_ACTION_WINDOW_NEXT,
	VIS_ACTION_WINDOW_PREV,
	VIS_ACTION_OPEN_LINE_ABOVE,
	VIS_ACTION_OPEN_LINE_BELOW,
	VIS_ACTION_JOIN_LINE_BELOW,
	VIS_ACTION_JOIN_LINES,
	VIS_ACTION_PROMPT_SHOW,
	VIS_ACTION_PROMPT_BACKSPACE,
	VIS_ACTION_PROMPT_ENTER,
	VIS_ACTION_PROMPT_SHOW_VISUAL,
	VIS_ACTION_REPEAT,
	VIS_ACTION_SELECTION_FLIP,
	VIS_ACTION_SELECTION_RESTORE,
	VIS_ACTION_WINDOW_REDRAW_TOP,
	VIS_ACTION_WINDOW_REDRAW_CENTER,
	VIS_ACTION_WINDOW_REDRAW_BOTTOM,
	VIS_ACTION_WINDOW_SLIDE_UP,
	VIS_ACTION_WINDOW_SLIDE_DOWN,
	VIS_ACTION_PUT_AFTER,
	VIS_ACTION_PUT_BEFORE,
	VIS_ACTION_PUT_AFTER_END,
	VIS_ACTION_PUT_BEFORE_END,
	VIS_ACTION_CURSOR_SELECT_WORD,
	VIS_ACTION_CURSORS_NEW_LINE_ABOVE,
	VIS_ACTION_CURSORS_NEW_LINE_BELOW,
	VIS_ACTION_CURSORS_NEW_LINES_BEGIN,
	VIS_ACTION_CURSORS_NEW_LINES_END,
	VIS_ACTION_CURSORS_NEW_MATCH_NEXT,
	VIS_ACTION_CURSORS_NEW_MATCH_SKIP,
	VIS_ACTION_CURSORS_ALIGN,
	VIS_ACTION_CURSORS_REMOVE_ALL,
	VIS_ACTION_CURSORS_REMOVE_LAST,
	VIS_ACTION_TEXT_OBJECT_WORD_OUTER,
	VIS_ACTION_TEXT_OBJECT_WORD_INNER,
	VIS_ACTION_TEXT_OBJECT_LONGWORD_OUTER,
	VIS_ACTION_TEXT_OBJECT_LONGWORD_INNER,
	VIS_ACTION_TEXT_OBJECT_SENTENCE,
	VIS_ACTION_TEXT_OBJECT_PARAGRAPH,
	VIS_ACTION_TEXT_OBJECT_SQUARE_BRACKET_OUTER,
	VIS_ACTION_TEXT_OBJECT_SQUARE_BRACKET_INNER,
	VIS_ACTION_TEXT_OBJECT_PARANTHESE_OUTER,
	VIS_ACTION_TEXT_OBJECT_PARANTHESE_INNER,
	VIS_ACTION_TEXT_OBJECT_ANGLE_BRACKET_OUTER,
	VIS_ACTION_TEXT_OBJECT_ANGLE_BRACKET_INNER,
	VIS_ACTION_TEXT_OBJECT_CURLY_BRACKET_OUTER,
	VIS_ACTION_TEXT_OBJECT_CURLY_BRACKET_INNER,
	VIS_ACTION_TEXT_OBJECT_QUOTE_OUTER,
	VIS_ACTION_TEXT_OBJECT_QUOTE_INNER,
	VIS_ACTION_TEXT_OBJECT_SINGLE_QUOTE_OUTER,
	VIS_ACTION_TEXT_OBJECT_SINGLE_QUOTE_INNER,
	VIS_ACTION_TEXT_OBJECT_BACKTICK_OUTER,
	VIS_ACTION_TEXT_OBJECT_BACKTICK_INNER,
	VIS_ACTION_TEXT_OBJECT_ENTIRE_OUTER,
	VIS_ACTION_TEXT_OBJECT_ENTIRE_INNER,
	VIS_ACTION_TEXT_OBJECT_FUNCTION_OUTER,
	VIS_ACTION_TEXT_OBJECT_FUNCTION_INNER,
	VIS_ACTION_TEXT_OBJECT_LINE_OUTER,
	VIS_ACTION_TEXT_OBJECT_LINE_INNER,
	VIS_ACTION_MOTION_CHARWISE,
	VIS_ACTION_MOTION_LINEWISE,
	VIS_ACTION_NOP,
};

static KeyAction vis_action[] = {
	[VIS_ACTION_EDITOR_SUSPEND] = {
		"editor-suspend",
		"Suspend the editor",
		suspend,
	},
	[VIS_ACTION_CURSOR_CHAR_PREV] = {
		"cursor-char-prev",
		"Move cursor left, to the previous character",
		movement, { .i = MOVE_CHAR_PREV }
	},
	[VIS_ACTION_CURSOR_CHAR_NEXT] = {
		"cursor-char-next",
		"Move cursor right, to the next character",
		movement, { .i = MOVE_CHAR_NEXT }
	},
	[VIS_ACTION_CURSOR_WORD_START_PREV] = {
		"cursor-word-start-prev",
		"Move cursor words backwards",
		movement, { .i = MOVE_WORD_START_PREV }
	},
	[VIS_ACTION_CURSOR_WORD_START_NEXT] = {
		"cursor-word-start-next",
		"Move cursor words forwards",
		movement, { .i = MOVE_WORD_START_NEXT }
	},
	[VIS_ACTION_CURSOR_WORD_END_PREV] = {
		"cursor-word-end-prev",
		"Move cursor backwards to the end of word",
		movement, { .i = MOVE_WORD_END_PREV }
	},
	[VIS_ACTION_CURSOR_WORD_END_NEXT] = {
		"cursor-word-end-next",
		"Move cursor forward to the end of word",
		movement, { .i = MOVE_WORD_END_NEXT }
	},
	[VIS_ACTION_CURSOR_LONGWORD_START_PREV] = {
		"cursor-longword-start-prev",
		"Move cursor WORDS backwards",
		movement, { .i = MOVE_LONGWORD_START_PREV }
	},
	[VIS_ACTION_CURSOR_LONGWORD_START_NEXT] = {
		"cursor-longword-start-next",
		"Move cursor WORDS forwards",
		movement, { .i = MOVE_LONGWORD_START_NEXT }
	},
	[VIS_ACTION_CURSOR_LONGWORD_END_PREV] = {
		"cursor-longword-end-prev",
		"Move cursor backwards to the end of WORD",
		movement, { .i = MOVE_LONGWORD_END_PREV }
	},
	[VIS_ACTION_CURSOR_LONGWORD_END_NEXT] = {
		"cursor-longword-end-next",
		"Move cursor forward to the end of WORD",
		movement, { .i = MOVE_LONGWORD_END_NEXT }
	},
	[VIS_ACTION_CURSOR_LINE_UP] = {
		"cursor-line-up",
		"Move cursor line upwards",
		movement, { .i = MOVE_LINE_UP }
	},
	[VIS_ACTION_CURSOR_LINE_DOWN] = {
		"cursor-line-down",
		"Move cursor line downwards",
		movement, { .i = MOVE_LINE_DOWN }
	},
	[VIS_ACTION_CURSOR_LINE_START] = {
		"cursor-line-start",
		"Move cursor to first non-blank character of the line",
		movement, { .i = MOVE_LINE_START }
	},
	[VIS_ACTION_CURSOR_LINE_FINISH] = {
		"cursor-line-finish",
		"Move cursor to last non-blank character of the line",
		movement, { .i = MOVE_LINE_FINISH }
	},
	[VIS_ACTION_CURSOR_LINE_BEGIN] = {
		"cursor-line-begin",
		"Move cursor to first character of the line",
		movement, { .i = MOVE_LINE_BEGIN }
	},
	[VIS_ACTION_CURSOR_LINE_END] = {
		"cursor-line-end",
		"Move cursor to end of the line",
		movement, { .i = MOVE_LINE_LASTCHAR }
	},
	[VIS_ACTION_CURSOR_SCREEN_LINE_UP] = {
		"cursor-sceenline-up",
		"Move cursor screen/display line upwards",
		movement, { .i = MOVE_SCREEN_LINE_UP }
	},
	[VIS_ACTION_CURSOR_SCREEN_LINE_DOWN] = {
		"cursor-screenline-down",
		"Move cursor screen/display line downwards",
		movement, { .i = MOVE_SCREEN_LINE_DOWN }
	},
	[VIS_ACTION_CURSOR_SCREEN_LINE_BEGIN] = {
		"cursor-screenline-begin",
		"Move cursor to beginning of screen/display line",
		movement, { .i = MOVE_SCREEN_LINE_BEGIN }
	},
	[VIS_ACTION_CURSOR_SCREEN_LINE_MIDDLE] = {
		"cursor-screenline-middle",
		"Move cursor to middle of screen/display line",
		movement, { .i = MOVE_SCREEN_LINE_MIDDLE }
	},
	[VIS_ACTION_CURSOR_SCREEN_LINE_END] = {
		"cursor-screenline-end",
		"Move cursor to end of screen/display line",
		movement, { .i = MOVE_SCREEN_LINE_END }
	},
	[VIS_ACTION_CURSOR_BRACKET_MATCH] = {
		"cursor-match-bracket",
		"Match corresponding symbol if cursor is on a bracket character",
		movement, { .i = MOVE_BRACKET_MATCH }
	},
	[VIS_ACTION_CURSOR_PARAGRAPH_PREV] = {
		"cursor-paragraph-prev",
		"Move cursor paragraph backward",
		movement, { .i = MOVE_PARAGRAPH_PREV }
	},
	[VIS_ACTION_CURSOR_PARAGRAPH_NEXT] = {
		"cursor-paragraph-next",
		"Move cursor paragraph forward",
		movement, { .i = MOVE_PARAGRAPH_NEXT }
	},
	[VIS_ACTION_CURSOR_SENTENCE_PREV] = {
		"cursor-sentence-prev",
		"Move cursor sentence backward",
		movement, { .i = MOVE_SENTENCE_PREV }
	},
	[VIS_ACTION_CURSOR_SENTENCE_NEXT] = {
		"cursor-sentence-next",
		"Move cursor sentence forward",
		movement, { .i = MOVE_SENTENCE_NEXT }
	},
	[VIS_ACTION_CURSOR_FUNCTION_START_PREV] = {
		"cursor-function-start-prev",
		"Move cursor backwards to start of function",
		movement, { .i = MOVE_FUNCTION_START_PREV }
	},
	[VIS_ACTION_CURSOR_FUNCTION_START_NEXT] = {
		"cursor-function-start-next",
		"Move cursor forwards to start of function",
		movement, { .i = MOVE_FUNCTION_START_NEXT }
	},
	[VIS_ACTION_CURSOR_FUNCTION_END_PREV] = {
		"cursor-function-end-prev",
		"Move cursor backwards to end of function",
		movement, { .i = MOVE_FUNCTION_END_PREV }
	},
	[VIS_ACTION_CURSOR_FUNCTION_END_NEXT] = {
		"cursor-function-end-next",
		"Move cursor forwards to end of function",
		movement, { .i = MOVE_FUNCTION_END_NEXT }
	},
	[VIS_ACTION_CURSOR_COLUMN] = {
		"cursor-column",
		"Move cursor to given column of current line",
		movement, { .i = MOVE_COLUMN }
	},
	[VIS_ACTION_CURSOR_LINE_FIRST] = {
		"cursor-line-first",
		"Move cursor to given line (defaults to first)",
		gotoline, { .i = -1 }
	},
	[VIS_ACTION_CURSOR_LINE_LAST] = {
		"cursor-line-last",
		"Move cursor to given line (defaults to last)",
		gotoline, { .i = +1 }
	},
	[VIS_ACTION_CURSOR_WINDOW_LINE_TOP] = {
		"cursor-window-line-top",
		"Move cursor to top line of the window",
		movement, { .i = MOVE_WINDOW_LINE_TOP }
	},
	[VIS_ACTION_CURSOR_WINDOW_LINE_MIDDLE] = {
		"cursor-window-line-middle",
		"Move cursor to middle line of the window",
		movement, { .i = MOVE_WINDOW_LINE_MIDDLE }
	},
	[VIS_ACTION_CURSOR_WINDOW_LINE_BOTTOM] = {
		"cursor-window-line-bottom",
		"Move cursor to bottom line of the window",
		movement, { .i = MOVE_WINDOW_LINE_BOTTOM }
	},
	[VIS_ACTION_CURSOR_SEARCH_NEXT] = {
		"cursor-search-forward",
		"Move cursor to bottom line of the window",
		movement, { .i = MOVE_SEARCH_NEXT }
	},
	[VIS_ACTION_CURSOR_SEARCH_PREV] = {
		"cursor-search-backward",
		"Move cursor to bottom line of the window",
		movement, { .i = MOVE_SEARCH_PREV }
	},
	[VIS_ACTION_CURSOR_SEARCH_WORD_FORWARD] = {
		"cursor-search-word-forward",
		"Move cursor to next occurence of the word under cursor",
		movement, { .i = MOVE_SEARCH_WORD_FORWARD }
	},
	[VIS_ACTION_CURSOR_SEARCH_WORD_BACKWARD] = {
		"cursor-search-word-backward",
		"Move cursor to previous occurence of the word under cursor",
		 movement, { .i = MOVE_SEARCH_WORD_BACKWARD }
	},
	[VIS_ACTION_WINDOW_PAGE_UP] = {
		"window-page-up",
		"Scroll window pages backwards (upwards)",
		wscroll, { .i = -PAGE }
	},
	[VIS_ACTION_WINDOW_HALFPAGE_UP] = {
		"window-halfpage-up",
		"Scroll window half pages backwards (upwards)",
		wscroll, { .i = -PAGE_HALF }
	},
	[VIS_ACTION_WINDOW_PAGE_DOWN] = {
		"window-page-down",
		"Scroll window pages forwards (downwards)",
		wscroll, { .i = +PAGE }
	},
	[VIS_ACTION_WINDOW_HALFPAGE_DOWN] = {
		"window-halfpage-down",
		"Scroll window half pages forwards (downwards)",
		wscroll, { .i = +PAGE_HALF }
	},
	[VIS_ACTION_MODE_NORMAL] = {
		"vis-mode-normal",
		"Enter normal mode",
		switchmode, { .i = VIS_MODE_NORMAL }
	},
	[VIS_ACTION_MODE_VISUAL] = {
		"vis-mode-visual-charwise",
		"Enter characterwise visual mode",
		switchmode, { .i = VIS_MODE_VISUAL }
	},
	[VIS_ACTION_MODE_VISUAL_LINE] = {
		"vis-mode-visual-linewise",
		"Enter linewise visual mode",
		switchmode, { .i = VIS_MODE_VISUAL_LINE }
	},
	[VIS_ACTION_MODE_INSERT] = {
		"vis-mode-insert",
		"Enter insert mode",
		switchmode, { .i = VIS_MODE_INSERT }
	},
	[VIS_ACTION_MODE_REPLACE] = {
		"vis-mode-replace",
		"Enter replace mode",
		switchmode, { .i = VIS_MODE_REPLACE }
	},
	[VIS_ACTION_MODE_OPERATOR_PENDING] = {
		"vis-mode-operator-pending",
		"Enter to operator pending mode",
		switchmode, { .i = VIS_MODE_OPERATOR }
	},
	[VIS_ACTION_DELETE_CHAR_PREV] = {
		"delete-char-prev",
		"Delete the previous character",
		delete, { .i = MOVE_CHAR_PREV }
	},
	[VIS_ACTION_DELETE_CHAR_NEXT] = {
		"delete-char-next",
		"Delete the next character",
		delete, { .i = MOVE_CHAR_NEXT }
	},
	[VIS_ACTION_DELETE_LINE_BEGIN] = {
		"delete-line-begin",
		"Delete until the start of the current line",
		delete, { .i = MOVE_LINE_BEGIN }
	},
	[VIS_ACTION_DELETE_WORD_PREV] = {
		"delete-word-prev",
		"Delete the previous WORD",
		delete, { .i = MOVE_LONGWORD_START_PREV }
	},
	[VIS_ACTION_JUMPLIST_PREV] = {
		"jumplist-prev",
		"Go to older cursor position in jump list",
		movement, { .i = MOVE_JUMPLIST_PREV }
	},
	[VIS_ACTION_JUMPLIST_NEXT] = {
		"jumplist-next",
		"Go to newer cursor position in jump list",
		movement, { .i = MOVE_JUMPLIST_NEXT }
	},
	[VIS_ACTION_CHANGELIST_PREV] = {
		"changelist-prev",
		"Go to older cursor position in change list",
		movement, { .i = MOVE_CHANGELIST_PREV }
	},
	[VIS_ACTION_CHANGELIST_NEXT] = {
		"changelist-next",
		"Go to newer cursor position in change list",
		movement, { .i = MOVE_CHANGELIST_NEXT }
	},
	[VIS_ACTION_UNDO] = {
		"editor-undo",
		"Undo last change",
		undo,
	},
	[VIS_ACTION_REDO] = {
		"editor-redo",
		"Redo last change",
		redo,
	},
	[VIS_ACTION_EARLIER] = {
		"editor-earlier",
		"Goto older text state",
		earlier,
	},
	[VIS_ACTION_LATER] = {
		"editor-later",
		"Goto newer text state",
		later,
	},
	[VIS_ACTION_MACRO_RECORD] = {
		"macro-record",
		"Record macro into given register",
		macro_record,
	},
	[VIS_ACTION_MACRO_REPLAY] = {
		"macro-replay",
		"Replay macro, execute the content of the given register",
		macro_replay,
	},
	[VIS_ACTION_MARK_SET] = {
		"mark-set",
		"Set given mark at current cursor position",
		mark_set,
	},
	[VIS_ACTION_MARK_GOTO] = {
		"mark-goto",
		"Goto the position of the given mark",
		mark_motion, { .i = MOVE_MARK }
	},
	[VIS_ACTION_MARK_GOTO_LINE] = {
		"mark-goto-line",
		"Goto first non-blank character of the line containing the given mark",
		mark_motion, { .i = MOVE_MARK_LINE }
	},
	[VIS_ACTION_REDRAW] = {
		"editor-redraw",
		"Redraw current editor content",
		 call, { .f = vis_draw }
	},
	[VIS_ACTION_REPLACE_CHAR] = {
		"replace-char",
		"Replace the character under the cursor",
		replace,
	},
	[VIS_ACTION_TOTILL_REPEAT] = {
		"totill-repeat",
		"Repeat latest to/till motion",
		movement, { .i = MOVE_TOTILL_REPEAT }
	},
	[VIS_ACTION_TOTILL_REVERSE] = {
		"totill-reverse",
		"Repeat latest to/till motion but in opposite direction",
		movement, { .i = MOVE_TOTILL_REVERSE }
	},
	[VIS_ACTION_PROMPT_SEARCH_FORWARD] = {
		"search-forward",
		"Search forward",
		prompt_search, { .s = "/" }
	},
	[VIS_ACTION_PROMPT_SEARCH_BACKWARD] = {
		"search-backward",
		"Search backward",
		prompt_search, { .s = "?" }
	},
	[VIS_ACTION_TILL_LEFT] = {
		"till-left",
		"Till after the occurrence of character to the left",
		movement_key, { .i = MOVE_LEFT_TILL }
	},
	[VIS_ACTION_TILL_RIGHT] = {
		"till-right",
		"Till before the occurrence of character to the right",
		movement_key, { .i = MOVE_RIGHT_TILL }
	},
	[VIS_ACTION_TO_LEFT] = {
		"to-left",
		"To the first occurrence of character to the left",
		movement_key, { .i = MOVE_LEFT_TO }
	},
	[VIS_ACTION_TO_RIGHT] = {
		"to-right",
		"To the first occurrence of character to the right",
		movement_key, { .i = MOVE_RIGHT_TO }
	},
	[VIS_ACTION_REGISTER] = {
		"register",
		"Use given register for next operator",
		reg,
	},
	[VIS_ACTION_OPERATOR_CHANGE] = {
		"vis-operator-change",
		"Change operator",
		operator, { .i = OP_CHANGE }
	},
	[VIS_ACTION_OPERATOR_DELETE] = {
		"vis-operator-delete",
		"Delete operator",
		operator, { .i = OP_DELETE }
	},
	[VIS_ACTION_OPERATOR_YANK] = {
		"vis-operator-yank",
		"Yank operator",
		operator, { .i = OP_YANK }
	},
	[VIS_ACTION_OPERATOR_SHIFT_LEFT] = {
		"vis-operator-shift-left",
		"Shift left operator",
		operator, { .i = OP_SHIFT_LEFT }
	},
	[VIS_ACTION_OPERATOR_SHIFT_RIGHT] = {
		"vis-operator-shift-right",
		"Shift right operator",
		operator, { .i = OP_SHIFT_RIGHT }
	},
	[VIS_ACTION_OPERATOR_CASE_LOWER] = {
		"vis-operator-case-lower",
		"Lowercase operator",
		operator, { .i = OP_CASE_LOWER }
	},
	[VIS_ACTION_OPERATOR_CASE_UPPER] = {
		"vis-operator-case-upper",
		"Uppercase operator",
		operator, { .i = OP_CASE_UPPER }
	},
	[VIS_ACTION_OPERATOR_CASE_SWAP] = {
		"vis-operator-case-swap",
		"Swap case operator",
		operator, { .i = OP_CASE_SWAP }
	},
	[VIS_ACTION_COUNT] = {
		"vis-count",
		"Count specifier",
		count,
	},
	[VIS_ACTION_INSERT_NEWLINE] = {
		"insert-newline",
		"Insert a line break (depending on file type)",
		call, { .f = vis_insert_nl }
	},
	[VIS_ACTION_INSERT_TAB] = {
		"insert-tab",
		"Insert a tab (might be converted to spaces)",
		call, { .f = vis_insert_tab }
	},
	[VIS_ACTION_INSERT_VERBATIM] = {
		"insert-verbatim",
		"Insert Unicode character based on code point",
		insert_verbatim,
	},
	[VIS_ACTION_INSERT_REGISTER] = {
		"insert-register",
		"Insert specified register content",
		insert_register,
	},
	[VIS_ACTION_WINDOW_NEXT] = {
		"window-next",
		"Focus next window",
		call, { .f = vis_window_next }
	},
	[VIS_ACTION_WINDOW_PREV] = {
		"window-prev",
		"Focus previous window",
		call, { .f = vis_window_prev }
	},
	[VIS_ACTION_OPEN_LINE_ABOVE] = {
		"open-line-above",
		"Begin a new line above the cursor",
		openline, { .i = MOVE_LINE_PREV }
	},
	[VIS_ACTION_OPEN_LINE_BELOW] = {
		"open-line-below",
		"Begin a new line below the cursor",
		openline, { .i = MOVE_LINE_NEXT }
	},
	[VIS_ACTION_JOIN_LINE_BELOW] = {
		"join-line-below",
		"Join line(s)",
		join, { .i = MOVE_LINE_NEXT },
	},
	[VIS_ACTION_JOIN_LINES] = {
		"join-lines",
		"Join selected lines",
		operator, { .i = OP_JOIN }
	},
	[VIS_ACTION_PROMPT_SHOW] = {
		"prompt-show",
		"Show editor command line prompt",
		prompt_cmd, { .s = "" }
	},
	[VIS_ACTION_PROMPT_BACKSPACE] = {
		"prompt-backspace",
		"Delete previous character in prompt",
		prompt_backspace
	},
	[VIS_ACTION_PROMPT_ENTER] = {
		"prompt-enter",
		"Execute current prompt content",
		call, { .f = vis_prompt_enter }
	},
	[VIS_ACTION_PROMPT_SHOW_VISUAL] = {
		"prompt-show-visual",
		"Show editor command line prompt in visual mode",
		prompt_cmd, { .s = "'<,'>" }
	},
	[VIS_ACTION_REPEAT] = {
		"editor-repeat",
		"Repeat latest editor command",
		repeat
	},
	[VIS_ACTION_SELECTION_FLIP] = {
		"selection-flip",
		"Flip selection, move cursor to other end",
		selection_end,
	},
	[VIS_ACTION_SELECTION_RESTORE] = {
		"selection-restore",
		"Restore last selection",
		selection_restore,
	},
	[VIS_ACTION_WINDOW_REDRAW_TOP] = {
		"window-redraw-top",
		"Redraw cursor line at the top of the window",
		window, { .w = view_redraw_top }
	},
	[VIS_ACTION_WINDOW_REDRAW_CENTER] = {
		"window-redraw-center",
		"Redraw cursor line at the center of the window",
		window, { .w = view_redraw_center }
	},
	[VIS_ACTION_WINDOW_REDRAW_BOTTOM] = {
		"window-redraw-bottom",
		"Redraw cursor line at the bottom of the window",
		window, { .w = view_redraw_bottom }
	},
	[VIS_ACTION_WINDOW_SLIDE_UP] = {
		"window-slide-up",
		"Slide window content upwards",
		wslide, { .i = -1 }
	},
	[VIS_ACTION_WINDOW_SLIDE_DOWN] = {
		"window-slide-down",
		"Slide window content downwards",
		wslide, { .i = +1 }
	},
	[VIS_ACTION_PUT_AFTER] = {
		"put-after",
		"Put text after the cursor",
		operator, { .i = OP_PUT_AFTER }
	},
	[VIS_ACTION_PUT_BEFORE] = {
		"put-before",
		"Put text before the cursor",
		operator, { .i = OP_PUT_BEFORE }
	},
	[VIS_ACTION_PUT_AFTER_END] = {
		"put-after-end",
		"Put text after the cursor, place cursor after new text",
		operator, { .i = OP_PUT_AFTER_END }
	},
	[VIS_ACTION_PUT_BEFORE_END] = {
		"put-before-end",
		"Put text before the cursor, place cursor after new text",
		operator, { .i = OP_PUT_BEFORE_END }
	},
	[VIS_ACTION_CURSOR_SELECT_WORD] = {
		"cursors-select-word",
		"Select word under cursor",
		cursors_select,
	},
	[VIS_ACTION_CURSORS_NEW_LINE_ABOVE] = {
		"cursors-new-lines-above",
		"Create a new cursor on the line above",
		cursors_new, { .i = -1 }
	},
	[VIS_ACTION_CURSORS_NEW_LINE_BELOW] = {
		"cursor-new-lines-below",
		"Create a new cursor on the line below",
		cursors_new, { .i = +1 }
	},
	[VIS_ACTION_CURSORS_NEW_LINES_BEGIN] = {
		"cursors-new-lines-begin",
		"Create a new cursor at the start of every line covered by selection",
		operator, { .i = OP_CURSOR_SOL }
	},
	[VIS_ACTION_CURSORS_NEW_LINES_END] = {
		"cursors-new-lines-end",
		"Create a new cursor at the end of every line covered by selection",
		operator, { .i = OP_CURSOR_EOL }
	},
	[VIS_ACTION_CURSORS_NEW_MATCH_NEXT] = {
		"cursors-new-match-next",
		"Select the next region matching the current selection",
		cursors_select_next
	},
	[VIS_ACTION_CURSORS_NEW_MATCH_SKIP] = {
		"cursors-new-match-skip",
		"Clear current selection, but select next match",
		cursors_select_skip,
	},
	[VIS_ACTION_CURSORS_ALIGN] = {
		"cursors-align",
		"Try to align all cursors on the same column",
		cursors_align,
	},
	[VIS_ACTION_CURSORS_REMOVE_ALL] = {
		"cursors-remove-all",
		"Remove all but the primary cursor",
		cursors_clear,
	},
	[VIS_ACTION_CURSORS_REMOVE_LAST] = {
		"cursors-remove-last",
		"Remove least recently created cursor",
		cursors_remove,
	},
	[VIS_ACTION_TEXT_OBJECT_WORD_OUTER] = {
		"text-object-word-outer",
		"A word leading and trailing whitespace included",
		textobj, { .i = TEXT_OBJ_OUTER_WORD }
	},
	[VIS_ACTION_TEXT_OBJECT_WORD_INNER] = {
		"text-object-word-inner",
		"A word leading and trailing whitespace excluded",
		textobj, { .i = TEXT_OBJ_INNER_WORD }
	},
	[VIS_ACTION_TEXT_OBJECT_LONGWORD_OUTER] = {
		"text-object-longword-outer",
		"A WORD leading and trailing whitespace included",
		 textobj, { .i = TEXT_OBJ_OUTER_LONGWORD }
	},
	[VIS_ACTION_TEXT_OBJECT_LONGWORD_INNER] = {
		"text-object-longword-inner",
		"A WORD leading and trailing whitespace excluded",
		 textobj, { .i = TEXT_OBJ_INNER_LONGWORD }
	},
	[VIS_ACTION_TEXT_OBJECT_SENTENCE] = {
		"text-object-sentence",
		"A sentence",
		textobj, { .i = TEXT_OBJ_SENTENCE }
	},
	[VIS_ACTION_TEXT_OBJECT_PARAGRAPH] = {
		"text-object-paragraph",
		"A paragraph",
		textobj, { .i = TEXT_OBJ_PARAGRAPH }
	},
	[VIS_ACTION_TEXT_OBJECT_SQUARE_BRACKET_OUTER] = {
		"text-object-square-bracket-outer",
		"[] block (outer variant)",
		textobj, { .i = TEXT_OBJ_OUTER_SQUARE_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_SQUARE_BRACKET_INNER] = {
		"text-object-square-bracket-inner",
		"[] block (inner variant)",
		textobj, { .i = TEXT_OBJ_INNER_SQUARE_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_PARANTHESE_OUTER] = {
		"text-object-parentheses-outer",
		"() block (outer variant)",
		textobj, { .i = TEXT_OBJ_OUTER_PARANTHESE }
	},
	[VIS_ACTION_TEXT_OBJECT_PARANTHESE_INNER] = {
		"text-object-parentheses-inner",
		"() block (inner variant)",
		textobj, { .i = TEXT_OBJ_INNER_PARANTHESE }
	},
	[VIS_ACTION_TEXT_OBJECT_ANGLE_BRACKET_OUTER] = {
		"text-object-angle-bracket-outer",
		"<> block (outer variant)",
		textobj, { .i = TEXT_OBJ_OUTER_ANGLE_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_ANGLE_BRACKET_INNER] = {
		"text-object-angle-bracket-inner",
		"<> block (inner variant)",
		textobj, { .i = TEXT_OBJ_INNER_ANGLE_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_CURLY_BRACKET_OUTER] = {
		"text-object-curly-bracket-outer",
		"{} block (outer variant)",
		textobj, { .i = TEXT_OBJ_OUTER_CURLY_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_CURLY_BRACKET_INNER] = {
		"text-object-curly-bracket-inner",
		"{} block (inner variant)",
		textobj, { .i = TEXT_OBJ_INNER_CURLY_BRACKET }
	},
	[VIS_ACTION_TEXT_OBJECT_QUOTE_OUTER] = {
		"text-object-quote-outer",
		"A quoted string, including the quotation marks",
		textobj, { .i = TEXT_OBJ_OUTER_QUOTE }
	},
	[VIS_ACTION_TEXT_OBJECT_QUOTE_INNER] = {
		"text-object-quote-inner",
		"A quoted string, excluding the quotation marks",
		textobj, { .i = TEXT_OBJ_INNER_QUOTE }
	},
	[VIS_ACTION_TEXT_OBJECT_SINGLE_QUOTE_OUTER] = {
		"text-object-single-quote-outer",
		"A single quoted string, including the quotation marks",
		textobj, { .i = TEXT_OBJ_OUTER_SINGLE_QUOTE }
	},
	[VIS_ACTION_TEXT_OBJECT_SINGLE_QUOTE_INNER] = {
		"text-object-single-quote-inner",
		"A single quoted string, excluding the quotation marks",
		textobj, { .i = TEXT_OBJ_INNER_SINGLE_QUOTE }
	},
	[VIS_ACTION_TEXT_OBJECT_BACKTICK_OUTER] = {
		"text-object-backtick-outer",
		"A backtick delimited string (outer variant)",
		textobj, { .i = TEXT_OBJ_OUTER_BACKTICK }
	},
	[VIS_ACTION_TEXT_OBJECT_BACKTICK_INNER] = {
		"text-object-backtick-inner",
		"A backtick delimited string (inner variant)",
		textobj, { .i = TEXT_OBJ_INNER_BACKTICK }
	},
	[VIS_ACTION_TEXT_OBJECT_ENTIRE_OUTER] = {
		"text-object-entire-outer",
		"The whole text content",
		textobj, { .i = TEXT_OBJ_OUTER_ENTIRE }
	},
	[VIS_ACTION_TEXT_OBJECT_ENTIRE_INNER] = {
		"text-object-entire-inner",
		"The whole text content, except for leading and trailing empty lines",
		textobj, { .i = TEXT_OBJ_INNER_ENTIRE }
	},
	[VIS_ACTION_TEXT_OBJECT_FUNCTION_OUTER] = {
		"text-object-function-outer",
		"A whole C-like function",
		 textobj, { .i = TEXT_OBJ_OUTER_FUNCTION }
	},
	[VIS_ACTION_TEXT_OBJECT_FUNCTION_INNER] = {
		"text-object-function-inner",
		"A whole C-like function body",
		 textobj, { .i = TEXT_OBJ_INNER_FUNCTION }
	},
	[VIS_ACTION_TEXT_OBJECT_LINE_OUTER] = {
		"text-object-line-outer",
		"The whole line",
		 textobj, { .i = TEXT_OBJ_OUTER_LINE }
	},
	[VIS_ACTION_TEXT_OBJECT_LINE_INNER] = {
		"text-object-line-inner",
		"The whole line, excluding leading and trailing whitespace",
		textobj, { .i = TEXT_OBJ_INNER_LINE }
	},
	[VIS_ACTION_MOTION_CHARWISE] = {
		"motion-charwise",
		"Force motion to be charwise",
		motiontype, { .i = VIS_MOTIONTYPE_CHARWISE }
	},
	[VIS_ACTION_MOTION_LINEWISE] = {
		"motion-linewise",
		"Force motion to be linewise",
		motiontype, { .i = VIS_MOTIONTYPE_LINEWISE }
	},
	[VIS_ACTION_NOP] = {
		"nop",
		"Ignore key, do nothing",
		nop,
	},
};

#include "config.h"

/** key bindings functions */

static const char *nop(Vis *vis, const char *keys, const Arg *arg) {
	return keys;
}

static const char *key2macro(Vis *vis, const char *keys, enum VisMacro *macro) {
	*macro = VIS_MACRO_INVALID;
	if (keys[0] >= 'a' && keys[0] <= 'z')
		*macro = keys[0] - 'a';
	else if (keys[0] == '@')
		*macro = VIS_MACRO_LAST_RECORDED;
	else if (keys[0] == '\0')
		return NULL;
	return keys+1;
}

static const char *macro_record(Vis *vis, const char *keys, const Arg *arg) {
	if (vis_macro_record_stop(vis))
		return keys;
	enum VisMacro macro;
	keys = key2macro(vis, keys, &macro);
	vis_macro_record(vis, macro);
	vis_draw(vis);
	return keys;
}

static const char *macro_replay(Vis *vis, const char *keys, const Arg *arg) {
	enum VisMacro macro;
	keys = key2macro(vis, keys, &macro);
	vis_macro_replay(vis, macro);
	return keys;
}

static const char *suspend(Vis *vis, const char *keys, const Arg *arg) {
	vis_suspend(vis);
	return keys;
}

static const char *repeat(Vis *vis, const char *keys, const Arg *arg) {
	vis_repeat(vis);
	return keys;
}

static const char *cursors_new(Vis *vis, const char *keys, const Arg *arg) {
	View *view = vis_view(vis);
	Text *txt = vis_text(vis);
	size_t pos = view_cursor_get(view);
	if (arg->i > 0)
		pos = text_line_down(txt, pos);
	else if (arg->i < 0)
		pos = text_line_up(txt, pos);
	Cursor *cursor = view_cursors_new(view);
	if (cursor)
		view_cursors_to(cursor, pos);
	return keys;
}

static const char *cursors_align(Vis *vis, const char *keys, const Arg *arg) {
	View *view = vis_view(vis);
	Text *txt = vis_text(vis);
	int mincol = INT_MAX;
	for (Cursor *c = view_cursors(view); c; c = view_cursors_next(c)) {
		size_t pos = view_cursors_pos(c);
		int col = text_line_char_get(txt, pos);
		if (col < mincol)
			mincol = col;
	}
	for (Cursor *c = view_cursors(view); c; c = view_cursors_next(c)) {
		size_t pos = view_cursors_pos(c);
		size_t col = text_line_char_set(txt, pos, mincol);
		view_cursors_to(c, col);
	}
	return keys;
}

static const char *cursors_clear(Vis *vis, const char *keys, const Arg *arg) {
	View *view = vis_view(vis);
	if (view_cursors_count(view) > 1)
		view_cursors_clear(view);
	else
		view_cursors_selection_clear(view_cursor(view));
	return keys;
}

static const char *cursors_select(Vis *vis, const char *keys, const Arg *arg) {
	Text *txt = vis_text(vis);
	View *view = vis_view(vis);
	for (Cursor *cursor = view_cursors(view); cursor; cursor = view_cursors_next(cursor)) {
		Filerange sel = view_cursors_selection_get(cursor);
		Filerange word = text_object_word(txt, view_cursors_pos(cursor));
		if (!text_range_valid(&sel) && text_range_valid(&word)) {
			view_cursors_selection_set(cursor, &word);
			view_cursors_to(cursor, text_char_prev(txt, word.end));
		}
	}
	vis_mode_switch(vis, VIS_MODE_VISUAL);
	return keys;
}

static const char *cursors_select_next(Vis *vis, const char *keys, const Arg *arg) {
	Text *txt = vis_text(vis);
	View *view = vis_view(vis);
	Cursor *cursor = view_cursor(view);
	Filerange sel = view_cursors_selection_get(cursor);
	if (!text_range_valid(&sel))
		return keys;

	size_t len = text_range_size(&sel);
	char *buf = malloc(len+1);
	if (!buf)
		return keys;
	len = text_bytes_get(txt, sel.start, len, buf);
	buf[len] = '\0';
	Filerange word = text_object_word_find_next(txt, sel.end, buf);
	free(buf);

	if (text_range_valid(&word)) {
		cursor = view_cursors_new(view);
		if (!cursor)
			return keys;
		view_cursors_selection_set(cursor, &word);
		view_cursors_to(cursor, text_char_prev(txt, word.end));
	}
	return keys;
}

static const char *cursors_select_skip(Vis *vis, const char *keys, const Arg *arg) {
	View *view = vis_view(vis);
	Cursor *cursor = view_cursor(view);
	keys = cursors_select_next(vis, keys, arg);
	if (cursor != view_cursor(view))
		view_cursors_dispose(cursor);
	return keys;
}

static const char *cursors_remove(Vis *vis, const char *keys, const Arg *arg) {
	View *view = vis_view(vis);
	view_cursors_dispose(view_cursor(view));
	return keys;
}

static const char *replace(Vis *vis, const char *keys, const Arg *arg) {
	if (!keys[0])
		return NULL;
	const char *next = vis_key_next(vis, keys);
	size_t len = next - keys;
	/* TODO: fix
	action_reset(vis, &vis->action_prev);
	vis->action_prev.op = &ops[OP_REPEAT_REPLACE];
	buffer_put(&vis->buffer_repeat, keys, len);
	*/
	vis_replace_key(vis, keys, len);
	text_snapshot(vis_text(vis));
	return next;
}

static const char *count(Vis *vis, const char *keys, const Arg *arg) {
	int digit = keys[-1] - '0';
	int count = vis_count_get(vis);
	if (0 <= digit && digit <= 9) {
		if (digit == 0 && count == 0)
			vis_motion(vis, MOVE_LINE_BEGIN);
		vis_count_set(vis, count * 10 + digit);
	}
	return keys;
}

static const char *gotoline(Vis *vis, const char *keys, const Arg *arg) {
	if (vis_count_get(vis))
		vis_motion(vis, MOVE_LINE);
	else if (arg->i < 0)
		vis_motion(vis, MOVE_FILE_BEGIN);
	else
		vis_motion(vis, MOVE_FILE_END);
	return keys;
}

static const char *motiontype(Vis *vis, const char *keys, const Arg *arg) {
	vis_motion_type(vis, arg->i);
	return keys;
}

static const char *operator(Vis *vis, const char *keys, const Arg *arg) {
	vis_operator(vis, arg->i);
	return keys;
}

static const char *movement_key(Vis *vis, const char *keys, const Arg *arg) {
	if (!keys[0])
		return NULL;
	char key[32];
	const char *next = vis_key_next(vis, keys);
	strncpy(key, keys, next - keys + 1);
	key[sizeof(key)-1] = '\0';
	vis_motion(vis, arg->i, key);
	return next;
}

static const char *movement(Vis *vis, const char *keys, const Arg *arg) {
	vis_motion(vis, arg->i);
	return keys;
}

static const char *textobj(Vis *vis, const char *keys, const Arg *arg) {
	vis_textobject(vis, arg->i);
	return keys;
}

static const char *selection_end(Vis *vis, const char *keys, const Arg *arg) {
	for (Cursor *c = view_cursors(vis_view(vis)); c; c = view_cursors_next(c))
		view_cursors_selection_swap(c);
	return keys;
}

static const char *selection_restore(Vis *vis, const char *keys, const Arg *arg) {
	for (Cursor *c = view_cursors(vis_view(vis)); c; c = view_cursors_next(c))
		view_cursors_selection_restore(c);
	vis_mode_switch(vis, VIS_MODE_VISUAL);
	return keys;
}

static const char *key2register(Vis *vis, const char *keys, enum VisRegister *reg) {
	*reg = VIS_REGISTER_INVALID;
	if (!keys[0])
		return NULL;
	if (keys[0] >= 'a' && keys[0] <= 'z')
		*reg = keys[0] - 'a';
	return keys+1;
}

static const char *reg(Vis *vis, const char *keys, const Arg *arg) {
	enum VisRegister reg;
	keys = key2register(vis, keys, &reg);
	vis_register_set(vis, reg);
	return keys;
}

static const char *key2mark(Vis *vis, const char *keys, int *mark) {
	*mark = VIS_MARK_INVALID;
	if (!keys[0])
		return NULL;
	if (keys[0] >= 'a' && keys[0] <= 'z')
		*mark = keys[0] - 'a';
	else if (keys[0] == '<')
		*mark = MARK_SELECTION_START;
	else if (keys[0] == '>')
		*mark = MARK_SELECTION_END;
	return keys+1;
}

static const char *mark_set(Vis *vis, const char *keys, const Arg *arg) {
	int mark;
	keys = key2mark(vis, keys, &mark);
	vis_mark_set(vis, mark, view_cursor_get(vis_view(vis)));
	return keys;
}

static const char *mark_motion(Vis *vis, const char *keys, const Arg *arg) {
	int mark;
	keys = key2mark(vis, keys, &mark);
	vis_motion(vis, arg->i, mark);
	return keys;
}

static const char *undo(Vis *vis, const char *keys, const Arg *arg) {
	size_t pos = text_undo(vis_text(vis));
	if (pos != EPOS) {
		View *view = vis_view(vis);
		if (view_cursors_count(view) == 1)
			view_cursor_to(view, pos);
		/* redraw all windows in case some display the same file */
		vis_draw(vis);
	}
	return keys;
}

static const char *redo(Vis *vis, const char *keys, const Arg *arg) {
	size_t pos = text_redo(vis_text(vis));
	if (pos != EPOS) {
		View *view = vis_view(vis);
		if (view_cursors_count(view) == 1)
			view_cursor_to(view, pos);
		/* redraw all windows in case some display the same file */
		vis_draw(vis);
	}
	return keys;
}

static const char *earlier(Vis *vis, const char *keys, const Arg *arg) {
	size_t pos = text_earlier(vis_text(vis), MAX(vis_count_get(vis), 1));
	if (pos != EPOS) {
		view_cursor_to(vis_view(vis), pos);
		/* redraw all windows in case some display the same file */
		vis_draw(vis);
	}
	return keys;
}

static const char *later(Vis *vis, const char *keys, const Arg *arg) {
	size_t pos = text_later(vis_text(vis), MAX(vis_count_get(vis), 1));
	if (pos != EPOS) {
		view_cursor_to(vis_view(vis), pos);
		/* redraw all windows in case some display the same file */
		vis_draw(vis);
	}
	return keys;
}

static const char *delete(Vis *vis, const char *keys, const Arg *arg) {
	vis_operator(vis, OP_DELETE);
	vis_motion(vis, arg->i);
	return keys;
}

static const char *insert_register(Vis *vis, const char *keys, const Arg *arg) {
	enum VisRegister regid;
	keys = key2register(vis, keys, &regid);
	Register *reg = vis_register_get(vis, regid);
	if (reg) {
		int pos = view_cursor_get(vis_view(vis));
		vis_insert(vis, pos, reg->data, reg->len);
		view_cursor_to(vis_view(vis), pos + reg->len);
	}
	return keys;
}

static const char *prompt_search(Vis *vis, const char *keys, const Arg *arg) {
	vis_prompt_show(vis, arg->s, "");
	vis_mode_switch(vis, VIS_MODE_PROMPT);
	return keys;
}

static const char *prompt_cmd(Vis *vis, const char *keys, const Arg *arg) {
	vis_prompt_show(vis, ":", arg->s);
	vis_mode_switch(vis, VIS_MODE_PROMPT);
	return keys;
}

static const char *prompt_backspace(Vis *vis, const char *keys, const Arg *arg) {
	char *cmd = vis_prompt_get(vis);
	if (!cmd || !*cmd)
		vis_mode_switch(vis, VIS_MODE_NORMAL);
	else
		delete(vis, keys, &(const Arg){ .i = MOVE_CHAR_PREV });
	free(cmd);
	return keys;
}

static const char *insert_verbatim(Vis *vis, const char *keys, const Arg *arg) {
	Rune rune = 0;
	char buf[4], type = keys[0];
	int len = 0, count = 0, base;
	switch (type) {
	case '\0':
		return NULL;
	case 'o':
	case 'O':
		count = 3;
		base = 8;
		break;
	case 'U':
		count = 4;
		/* fall through */
	case 'u':
		count += 4;
		base = 16;
		break;
	case 'x':
	case 'X':
		count = 2;
		base = 16;
		break;
	default:
		if (type < '0' || type > '9')
			return keys;
		rune = type - '0';
		count = 2;
		base = 10;
		break;
	}

	for (keys++; keys[0] && count > 0; keys++, count--) {
		int v = 0;
		if (base == 8 && '0' <= keys[0] && keys[0] <= '7') {
			v = keys[0] - '0';
		} else if ((base == 10 || base == 16) && '0' <= keys[0] && keys[0] <= '9') {
			v = keys[0] - '0';
		} else if (base == 16 && 'a' <= keys[0] && keys[0] <= 'f') {
			v = 10 + keys[0] - 'a';
		} else if (base == 16 && 'A' <= keys[0] && keys[0] <= 'F') {
			v = 10 + keys[0] - 'A';
		} else {
			count = 0;
			break;
		}
		rune = rune * base + v;
	}

	if (count > 0)
		return NULL;

	if (type == 'u' || type == 'U') {
		len = runetochar(buf, &rune);
	} else {
		buf[0] = rune;
		len = 1;
	}

	if (len > 0) {
		size_t pos = view_cursor_get(vis_view(vis));
		vis_insert(vis, pos, buf, len);
		view_cursor_to(vis_view(vis), pos + len);
	}
	return keys;
}

static const char *cmd(Vis *vis, const char *keys, const Arg *arg) {
	vis_cmd(vis, arg->s);
	return keys;
}

static int argi2lines(Vis *vis, const Arg *arg) {
	switch (arg->i) {
	case -PAGE:
	case +PAGE:
		return view_height_get(vis_view(vis));
	case -PAGE_HALF:
	case +PAGE_HALF:
		return view_height_get(vis_view(vis))/2;
	default:
		if (vis_count_get(vis) > 0)
			return vis_count_get(vis);
		return arg->i < 0 ? -arg->i : arg->i;
	}
}

static const char *wscroll(Vis *vis, const char *keys, const Arg *arg) {
	if (arg->i >= 0)
		view_scroll_down(vis_view(vis), argi2lines(vis, arg));
	else
		view_scroll_up(vis_view(vis), argi2lines(vis, arg));
	return keys;
}

static const char *wslide(Vis *vis, const char *keys, const Arg *arg) {
	if (arg->i >= 0)
		view_slide_down(vis_view(vis), argi2lines(vis, arg));
	else
		view_slide_up(vis_view(vis), argi2lines(vis, arg));
	return keys;
}

static const char *call(Vis *vis, const char *keys, const Arg *arg) {
	arg->f(vis);
	return keys;
}

static const char *window(Vis *vis, const char *keys, const Arg *arg) {
	arg->w(vis_view(vis));
	return keys;
}

static const char *openline(Vis *vis, const char *keys, const Arg *arg) {
	if (arg->i == MOVE_LINE_NEXT) {
		vis_motion(vis, MOVE_LINE_END);
		vis_insert_nl(vis);
	} else {
		vis_motion(vis, MOVE_LINE_BEGIN);
		vis_insert_nl(vis);
		vis_motion(vis, MOVE_LINE_PREV);
	}
	vis_mode_switch(vis, VIS_MODE_INSERT);
	return keys;
}

static const char *join(Vis *vis, const char *keys, const Arg *arg) {
	int count = vis_count_get(vis);
	if (count)
		vis_count_set(vis, count-1);
	vis_operator(vis, OP_JOIN);
	vis_motion(vis, arg->i);
	return keys;
}

static const char *switchmode(Vis *vis, const char *keys, const Arg *arg) {
	vis_mode_switch(vis, arg->i);
	return keys;
}

static Vis *vis;

static KeyBinding *default_bindings[] = {
	[VIS_MODE_BASIC] = basic_movement,
	[VIS_MODE_MOVE] = vis_movements,
	[VIS_MODE_TEXTOBJ] = vis_textobjs,
	[VIS_MODE_OPERATOR_OPTION] = vis_operator_options,
	[VIS_MODE_OPERATOR] = vis_operators,
	[VIS_MODE_NORMAL] = vis_mode_normal,
	[VIS_MODE_VISUAL] = vis_mode_visual,
	[VIS_MODE_VISUAL_LINE] = vis_mode_visual_line,
	[VIS_MODE_READLINE] = vis_mode_readline,
	[VIS_MODE_PROMPT] = vis_mode_prompt,
	[VIS_MODE_INSERT] = vis_mode_insert,
	[VIS_MODE_REPLACE]  = vis_mode_replace,
};

static void signal_handler(int signum, siginfo_t *siginfo, void *context) {
	vis_signal_handler(vis, signum, siginfo, context);
}

int main(int argc, char *argv[]) {

	vis = vis_new(ui_curses_new());
	if (!vis)
		return EXIT_FAILURE;

	for (int i = 0; i < LENGTH(vis_action); i++) {
		KeyAction *action = &vis_action[i];
		if (!vis_action_register(vis, action))
			vis_die(vis, "Could not register action: %s\n", action->name);
	}

	for (int i = 0; i < LENGTH(default_bindings); i++) {
		if (!vis_mode_bindings(vis, i, &default_bindings[i]))
			vis_die(vis, "Could not load default bindings\n");
	}

	/* install signal handlers etc. */
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = signal_handler;
	if (sigaction(SIGBUS, &sa, NULL) || sigaction(SIGINT, &sa, NULL))
		vis_die(vis, "sigaction: %s", strerror(errno));

	sigset_t blockset;
	sigemptyset(&blockset);
	sigaddset(&blockset, SIGWINCH);
	sigprocmask(SIG_BLOCK, &blockset, NULL);
	signal(SIGPIPE, SIG_IGN);

	int status = vis_run(vis, argc, argv);
	vis_free(vis);
	return status;
}
