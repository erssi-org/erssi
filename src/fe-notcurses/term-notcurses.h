#ifndef IRSSI_FE_NOTCURSES_TERM_NOTCURSES_H
#define IRSSI_FE_NOTCURSES_TERM_NOTCURSES_H

#include <notcurses/notcurses.h>

/* TERM_WINDOW implementation using notcurses ncplane */
struct _TERM_WINDOW {
	struct ncplane *plane;       /* The notcurses plane for this window */
	int x, y;                    /* Position relative to standard plane */
	int width, height;           /* Window dimensions */
};

/* Global notcurses context */
typedef struct {
	struct notcurses *nc;        /* Main notcurses context */
	struct ncplane *stdplane;    /* Standard plane (root window) */

	/* Current styling state */
	uint64_t current_channels;   /* FG/BG colors as notcurses channels */
	uint16_t current_style;      /* NCSTYLE_* flags */

	/* Cached state for optimization */
	unsigned int last_fg;        /* Last foreground color */
	unsigned int last_bg;        /* Last background color */
	int last_attrs;              /* Last attribute flags */

	/* Cursor state */
	int cursor_x, cursor_y;      /* Logical cursor position for refresh */
	int cursor_visible;          /* Whether cursor should be visible */

	/* Refresh batching */
	int freeze_counter;          /* Freeze/thaw nesting level */
} NC_CONTEXT;

/* Global context pointer */
extern NC_CONTEXT *nc_ctx;

/* Internal initialization functions */
NC_CONTEXT *nc_context_init(void);
void nc_context_deinit(NC_CONTEXT *ctx);

#endif /* IRSSI_FE_NOTCURSES_TERM_NOTCURSES_H */
