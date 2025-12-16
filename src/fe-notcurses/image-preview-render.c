/*
 image-preview-render.c : Image rendering using notcurses ncvisual

    Copyright (C) 2024 erssi team

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#include "module.h"
#include "image-preview.h"
#include "term-notcurses.h"

#include <irssi/src/core/settings.h>
#include <irssi/src/fe-text/textbuffer-view.h>
#include <irssi/src/fe-text/gui-windows.h>
#include <irssi/src/fe-common/core/fe-windows.h>

#include <notcurses/notcurses.h>

/* Get the best blitter for current terminal */
static ncblitter_e get_best_blitter(struct notcurses *nc)
{
	ncpixelimpl_e pixel_impl;

	if (nc == NULL)
		return NCBLIT_2x2;

	/* Check for pixel graphics support */
	pixel_impl = notcurses_check_pixel_support(nc);
	if (pixel_impl != NCPIXEL_NONE) {
		return NCBLIT_PIXEL;
	}

	/* Fallback to half-blocks */
	return NCBLIT_2x2;
}

/* Render image thumbnail to a child plane */
struct ncplane *image_render_thumbnail(struct notcurses *nc,
                                       struct ncplane *parent,
                                       const char *image_path,
                                       int y_offset,
                                       int x_offset,
                                       int max_cols,
                                       int max_rows)
{
	struct ncvisual *ncv = NULL;
	struct ncvisual_options vopts = {0};
	struct ncplane_options nopts = {0};
	struct ncplane *image_plane = NULL;
	ncvgeom geom = {0};
	int target_rows, target_cols;

	image_preview_debug_print("THUMBNAIL: path=%s y=%d x=%d max=%dx%d",
	                          image_path, y_offset, x_offset, max_cols, max_rows);

	if (nc == NULL || parent == NULL || image_path == NULL) {
		image_preview_debug_print("THUMBNAIL: NULL params nc=%p parent=%p", nc, parent);
		return NULL;
	}

	/* Check if notcurses can open images */
	if (!notcurses_canopen_images(nc)) {
		image_preview_debug_print("THUMBNAIL: notcurses cannot open images!");
		return NULL;
	}

	image_preview_debug_print("THUMBNAIL: notcurses CAN open images");

	/* Load image from file */
	ncv = ncvisual_from_file(image_path);
	if (ncv == NULL) {
		image_preview_debug_print("THUMBNAIL: ncvisual_from_file FAILED for %s", image_path);
		return NULL;
	}

	image_preview_debug_print("THUMBNAIL: image loaded OK");

	/* Set up visual options for geometry calculation */
	vopts.blitter = get_best_blitter(nc);
	vopts.scaling = NCSCALE_SCALE;
	vopts.flags = 0;

	image_preview_debug_print("THUMBNAIL: blitter=%d", vopts.blitter);

	/* Get geometry to calculate dimensions */
	if (ncvisual_geom(nc, ncv, &vopts, &geom) != 0) {
		image_preview_debug_print("THUMBNAIL: ncvisual_geom FAILED");
		ncvisual_destroy(ncv);
		return NULL;
	}

	image_preview_debug_print("THUMBNAIL: geom pixy=%u pixx=%u rcelly=%d rcellx=%d",
	                          geom.pixy, geom.pixx, geom.rcelly, geom.rcellx);

	/* Calculate target dimensions respecting max constraints */
	if (geom.rcelly > 0 && geom.rcellx > 0) {
		/* Use rendered cell geometry */
		target_rows = (int)geom.rcelly;
		target_cols = (int)geom.rcellx;
	} else {
		/* Estimate based on pixel geometry and cell size */
		unsigned int parent_rows, parent_cols;
		ncplane_dim_yx(parent, &parent_rows, &parent_cols);

		/* Assume ~2:1 pixel aspect ratio for terminal cells */
		target_cols = max_cols;
		target_rows = (geom.pixy * max_cols) / (geom.pixx * 2);
		if (target_rows < 1) target_rows = 1;
	}

	/* Apply constraints */
	if (target_rows > max_rows) target_rows = max_rows;
	if (target_cols > max_cols) target_cols = max_cols;
	if (target_rows < 1) target_rows = 1;
	if (target_cols < 1) target_cols = 1;

	image_preview_debug_print("THUMBNAIL: target size %dx%d", target_cols, target_rows);

	/* Create child plane for the image */
	nopts.y = y_offset;
	nopts.x = x_offset;
	nopts.rows = target_rows;
	nopts.cols = target_cols;
	nopts.name = "image-preview";
	nopts.flags = 0;

	image_plane = ncplane_create(parent, &nopts);
	if (image_plane == NULL) {
		image_preview_debug_print("THUMBNAIL: ncplane_create FAILED");
		ncvisual_destroy(ncv);
		return NULL;
	}

	image_preview_debug_print("THUMBNAIL: plane created OK");

	/* Set up final visual options for rendering */
	vopts.n = image_plane;
	vopts.scaling = NCSCALE_SCALE;
	vopts.y = 0;
	vopts.x = 0;
	vopts.flags = NCVISUAL_OPTION_CHILDPLANE;  /* Required for sixel sprixel creation */

	/* Blit the image */
	if (ncvisual_blit(nc, ncv, &vopts) == NULL) {
		image_preview_debug_print("THUMBNAIL: ncvisual_blit FAILED");
		ncplane_destroy(image_plane);
		ncvisual_destroy(ncv);
		return NULL;
	}

	image_preview_debug_print("THUMBNAIL: blit OK, image rendered!");

	/* Cleanup visual (plane keeps the rendered content) */
	ncvisual_destroy(ncv);

	return image_plane;
}

/* Destroy an image plane and all its children (including sprixel planes) */
void image_render_destroy(struct ncplane *plane)
{
	if (plane != NULL) {
		/* Use family_destroy to properly clean up child planes created by
		 * ncvisual_blit with NCVISUAL_OPTION_CHILDPLANE. Without this,
		 * child planes get reparented to stdplane instead of destroyed. */
		ncplane_family_destroy(plane);
	}
}

/* Render visible image previews for a textbuffer view */
void image_preview_render_view(TEXT_BUFFER_VIEW_REC *view, WINDOW_REC *window)
{
	LINE_REC *line;
	IMAGE_PREVIEW_REC *preview;
	int line_y;
	int max_width, max_height;
	struct ncplane *parent_plane;
	int lines_checked = 0;
	int previews_found = 0;
	MAIN_WINDOW_REC *mainwin;
	int screen_x_offset, screen_y_offset;

	image_preview_debug_print("RENDER: view=%p window=%p nc_ctx=%p", view, window, nc_ctx);

	if (view == NULL || window == NULL || nc_ctx == NULL || nc_ctx->nc == NULL) {
		image_preview_debug_print("RENDER: NULL check failed view=%p window=%p nc_ctx=%p",
		                          view, window, nc_ctx);
		return;
	}

	if (!image_preview_enabled()) {
		image_preview_debug_print("RENDER: preview disabled");
		return;
	}

	/* Get mainwindow for screen coordinates */
	mainwin = WINDOW_MAIN(window);
	if (mainwin == NULL) {
		image_preview_debug_print("RENDER: mainwin is NULL");
		return;
	}

	/* Calculate screen offset for the text area:
	 * - first_line/first_column: absolute screen position of mainwindow
	 * - statusbar_lines_top: lines used by top statusbar
	 * - statusbar_columns_left: columns used by left sidepanel */
	screen_y_offset = mainwin->first_line + mainwin->statusbar_lines_top;
	screen_x_offset = mainwin->first_column + mainwin->statusbar_columns_left;

	image_preview_debug_print("RENDER: mainwin first_line=%d first_col=%d sb_top=%d sb_left=%d",
	                          mainwin->first_line, mainwin->first_column,
	                          mainwin->statusbar_lines_top, mainwin->statusbar_columns_left);
	image_preview_debug_print("RENDER: screen offset y=%d x=%d", screen_y_offset, screen_x_offset);

	/* Get parent plane (stdplane for now) */
	parent_plane = nc_ctx->stdplane;
	image_preview_debug_print("RENDER: parent_plane=%p", parent_plane);

	/* Get max dimensions from settings */
	max_width = settings_get_int(IMAGE_PREVIEW_MAX_WIDTH);
	max_height = settings_get_int(IMAGE_PREVIEW_MAX_HEIGHT);
	image_preview_debug_print("RENDER: max_width=%d max_height=%d", max_width, max_height);

	/* Clear existing planes first */
	image_preview_clear_planes();

	/* Iterate visible lines in the view */
	if (view->startline == NULL) {
		image_preview_debug_print("RENDER: view->startline is NULL");
		return;
	}

	line_y = view->ypos;
	image_preview_debug_print("RENDER: starting at ypos=%d height=%d", view->ypos, view->height);

	for (line = view->startline; line != NULL && line_y < view->height; line = line->next) {
		lines_checked++;
		preview = image_preview_get(line);

		if (preview != NULL && preview->cache_path != NULL && !preview->fetch_failed) {
			int screen_y;
			int screen_x;

			previews_found++;

			/* Calculate absolute screen position for the image */
			screen_y = screen_y_offset + line_y + 1;  /* +1 to put below the text line */
			screen_x = screen_x_offset;

			image_preview_debug_print("RENDER: found preview at line_y=%d screen_y=%d screen_x=%d path=%s",
			                          line_y, screen_y, screen_x, preview->cache_path);

			/* Render thumbnail at calculated screen position */
			preview->plane = image_render_thumbnail(
				nc_ctx->nc,
				parent_plane,
				preview->cache_path,
				screen_y,
				screen_x,
				max_width,
				max_height
			);

			if (preview->plane != NULL) {
				image_preview_debug_print("RENDER: thumbnail created at screen y=%d x=%d", screen_y, screen_x);
				preview->y_position = screen_y;
				preview->height_rows = max_height;  /* TODO: get actual height */

				/* Skip lines for the image height */
				line_y += preview->height_rows;
			} else {
				image_preview_debug_print("RENDER: image_render_thumbnail returned NULL!");
			}
		}

		line_y++;
	}

	image_preview_debug_print("RENDER: checked %d lines, found %d previews", lines_checked, previews_found);
}
