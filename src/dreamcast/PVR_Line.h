#ifndef CC_PVR_LINE_H
#define CC_PVR_LINE_H
#include <kos.h>

/* Draw a screen-space line as a PVR triangle strip (KOS pvrline pattern). */
void PVR_Line_BuildVerts(pvr_vertex_t* line_verts, vec3f_t* v1, vec3f_t* v2,
	float width, int color);

void PVR_Line_Draw(vec3f_t* v1, vec3f_t* v2, float width, int color,
	pvr_list_t list, pvr_poly_hdr_t* hdr);

#endif
