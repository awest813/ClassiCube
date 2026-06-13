/* Derived from KallistiOS examples/dreamcast/pvr/pvrline/pvrline.c
 * Copyright (C) 2024 Jason Martin
 * See misc/dreamcast/KOS_ATTRIBUTION.txt */
#include "PVR_Line.h"

void PVR_Line_Draw(vec3f_t* v1, vec3f_t* v2, float width, int color,
	pvr_list_t which_list, pvr_poly_hdr_t* which_hdr) {
	pvr_vertex_t __attribute__((aligned(32))) line_verts[4];
	pvr_vertex_t* vert = line_verts;
	vec3f_t *ov1, *ov2;
	float dx, dy, inverse_magnitude, nx, ny;

	for (int i = 0; i < 4; i++) {
		line_verts[i].flags = PVR_CMD_VERTEX;
		line_verts[i].argb  = color;
		line_verts[i].oargb = 0;
	}
	line_verts[3].flags = PVR_CMD_VERTEX_EOL;

	if (v1->x <= v2->x) {
		ov1 = v1;
		ov2 = v2;
	} else {
		ov1 = v2;
		ov2 = v1;
	}

	/* https://devcry.heiho.net/html/20170820-opengl-line-drawing.html */
	dx = ov2->x - ov1->x;
	dy = ov2->y - ov1->y;
	inverse_magnitude = frsqrt((dx * dx) + (dy * dy)) * (width * 0.5f);
	nx = -dy * inverse_magnitude;
	ny =  dx * inverse_magnitude;

	vert->x = ov1->x + nx; vert->y = ov1->y + ny; vert->z = ov1->z; vert++;
	vert->x = ov1->x - nx; vert->y = ov1->y - ny; vert->z = ov2->z; vert++;
	vert->x = ov2->x + nx; vert->y = ov2->y + ny; vert->z = ov1->z; vert++;
	vert->x = ov2->x - nx; vert->y = ov2->y - ny; vert->z = ov2->z;

	pvr_list_prim(which_list, which_hdr, sizeof(pvr_poly_hdr_t));
	pvr_list_prim(which_list, &line_verts, 4 * sizeof(pvr_vertex_t));
}
