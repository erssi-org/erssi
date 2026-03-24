#include <irssi/src/perl/ui/module.h>

#include <irssi/src/fe-ansi/mainwindows.h>
#include <irssi/src/fe-ansi/gui-windows.h>
#include <irssi/src/fe-ansi/gui-printtext.h>
#include <irssi/src/fe-ansi/statusbar.h>
#include <irssi/src/fe-ansi/textbuffer.h>
#include <irssi/src/fe-ansi/textbuffer-view.h>
#include <irssi/src/fe-ansi/gui-entry.h>

typedef MAIN_WINDOW_REC *Irssi__TextUI__MainWindow;
typedef TEXT_BUFFER_REC *Irssi__TextUI__TextBuffer;
typedef TEXT_BUFFER_VIEW_REC *Irssi__TextUI__TextBufferView;
typedef struct Buffer_Line_Wrapper *Irssi__TextUI__Line;
typedef LINE_CACHE_REC *Irssi__TextUI__LineCache;
typedef LINE_INFO_REC *Irssi__TextUI__LineInfo;
typedef SBAR_ITEM_REC *Irssi__TextUI__StatusbarItem;
