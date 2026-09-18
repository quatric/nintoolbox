/***************************************************************************
 *                         _______ _______ _______                         *
 *                        |  ___  |____   |  ___  |                        *
 *                        | |   |_|    / /| |   |_|                        *
 *                        | |_____    / / | |_____                         *
 *                        |_____  |  / /  |_____  |                        *
 *                         _    | | / /    _    | |                        *
 *                        | |___| |/ /____| |___| |                        *
 *                        |_______|_______|_______|                        *
 *                                                                         *
 *                            Wiimms SZS Tools                             *
 *                          https://szs.wiimm.de/                          *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This file is part of the SZS project.                                 *
 *   Visit https://szs.wiimm.de/ for project details and sources.          *
 *                                                                         *
 *   Copyright (c) 2011-2024 by Dirk Clemens <wiimm@wiimm.de>              *
 *                                                                         *
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   See file gpl-2.0.txt or http://www.gnu.org/licenses/gpl-2.0.txt       *
 *                                                                         *
 ***************************************************************************/

//
// KCL V2 support (Wii U / Switch multi model collision) + shared helpers
// for the file variants of KillzXGaming/KCollisionLibrary ("KCL", MIT):
//
//  - V2 file header (magic 0x02020000, always big endian), model octree
//    with Divide/Values/NoData nodes and 1..N model sections.
//  - 20 byte V2 prisms (extra global triangle index), 0-based triangle
//    lists terminated by 0xffff (V1: 1-based, 0 terminator).
//  - triangle-box overlap tests for model subdivision and polygon
//    octree creation (ported from KCollisionLibrary).
//
// The in-memory triangle list (kcl->tridata) is variant agnostic, so all
// analysis, OBJ/GLB export and patching work unchanged. The V1 octree
// kept in memory always uses big endian float encoding; files with
// another variant get their octree rebuilt on demand.
//

#include "lib-kcl.h"
#include <math.h>

//
///////////////////////////////////////////////////////////////////////////////
///////////////			endian helpers			///////////////
///////////////////////////////////////////////////////////////////////////////

typedef struct kcl_rw_t
{
	bool le; // true: little endian file encoding
} kcl_rw_t;

static inline u16 kcl_rd16 (const kcl_rw_t *rw, const void *p)
{
	return rw->le ? le16 (p) : be16 (p);
}

static inline u32 kcl_rd32 (const kcl_rw_t *rw, const void *p)
{
	return rw->le ? le32 (p) : be32 (p);
}

static inline float kcl_rdf4 (const kcl_rw_t *rw, const void *p)
{
	return rw->le ? lef4 (p) : bef4 (p);
}

static inline void kcl_wr16 (const kcl_rw_t *rw, void *p, u16 v)
{
	if (rw->le)
		write_le16 (p, v);
	else
		write_be16 (p, v);
}

static inline void kcl_wr32 (const kcl_rw_t *rw, void *p, u32 v)
{
	if (rw->le)
		write_le32 (p, v);
	else
		write_be32 (p, v);
}

static inline void kcl_wrf4 (const kcl_rw_t *rw, void *p, float v)
{
	if (rw->le)
		write_lef4 (p, v);
	else
		write_bef4 (p, v);
}

// next power-of-2 exponent with 2^res >= value (KCL Maths.GetNext2Exponent)
static int kcl_next2exp (double value)
{
	if (value <= 1.0)
		return 0;
	int exp = 0;
	double p = 1.0;
	while (p < value)
	{
		p *= 2.0;
		exp++;
	}
	return exp;
}

//
// /////////////////////////////////////////////////////////////////////////////
///////////////		triangle-box overlap tests		///////////////
///////////////////////////////////////////////////////////////////////////////
//
// Ported from KCollisionLibrary (TriangleBoxIntersect.TriBoxOverlap and
// TriangleHelper.TriangleCubeOverlap, both after Tomas Akenine-Moller).
// Used for model subdivision (grouping) and polygon octree creation.
// All tests are conservative: an overlapping triangle is always reported,
// borderline epsilon differences only add triangles to more cubes.
//
// NOTE: KCollisionLibrary's planeBoxOverlap() is dead code in practice
// (its Set() helper takes Vector3 by value and never stores anything,
// so the plane test always passes). The port below behaves identically
// by skipping that test step.

typedef struct kcl_tri3_t
{
	double v[3][3]; // 3 vertices
	double n[3]; // unit face normal, (0,0,0) if degenerate
} kcl_tri3_t;

static void kcl_tri3_init (kcl_tri3_t *t, const double3 *pt)
{
	t->v[0][0] = pt[0].x;
	t->v[0][1] = pt[0].y;
	t->v[0][2] = pt[0].z;
	t->v[1][0] = pt[1].x;
	t->v[1][1] = pt[1].y;
	t->v[1][2] = pt[1].z;
	t->v[2][0] = pt[2].x;
	t->v[2][1] = pt[2].y;
	t->v[2][2] = pt[2].z;

	const double ax = t->v[1][0] - t->v[0][0], ay = t->v[1][1] - t->v[0][1],
			   az = t->v[1][2] - t->v[0][2];
	const double bx = t->v[2][0] - t->v[0][0], by = t->v[2][1] - t->v[0][1],
			   bz = t->v[2][2] - t->v[0][2];
	double nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
	const double len = sqrt (nx * nx + ny * ny + nz * nz);
	if (len > 1e-18)
	{
		t->n[0] = nx / len;
		t->n[1] = ny / len;
		t->n[2] = nz / len;
	}
	else
		t->n[0] = t->n[1] = t->n[2] = 0.0;
}

//--- TriBoxOverlap port (box by center + half size, per axis) ---

static bool kcl_tribox_overlap (const kcl_tri3_t *t, const double center[3], const double half[3])
{
	double v0[3] = { t->v[0][0] - center[0], t->v[0][1] - center[1], t->v[0][2] - center[2] };
	double v1[3] = { t->v[1][0] - center[0], t->v[1][1] - center[1], t->v[1][2] - center[2] };
	double v2[3] = { t->v[2][0] - center[0], t->v[2][1] - center[1], t->v[2][2] - center[2] };
	double e0[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
	double e1[3] = { v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2] };
	double e2[3] = { v0[0] - v2[0], v0[1] - v2[1], v0[2] - v2[2] };

#define KCL_AX_X01(ex, ey, fx, fy) \
	{ \
		const double _p0 = ex * v0[1] - ey * v0[2], _p2 = ex * v2[1] - ey * v2[2]; \
		const double _mn = _p0 < _p2 ? _p0 : _p2, _mx = _p0 < _p2 ? _p2 : _p0; \
		const double _r = fx * half[1] + fy * half[2]; \
		if (_mn > _r || _mx < -_r) \
			return false; \
	}
#define KCL_AX_Y02(ex, ez, fx, fz) \
	{ \
		const double _p0 = -ex * v0[0] + ez * v0[2], _p2 = -ex * v2[0] + ez * v2[2]; \
		const double _mn = _p0 < _p2 ? _p0 : _p2, _mx = _p0 < _p2 ? _p2 : _p0; \
		const double _r = fx * half[0] + fz * half[2]; \
		if (_mn > _r || _mx < -_r) \
			return false; \
	}
#define KCL_AX_Z12(ex, ey, fx, fy) \
	{ \
		const double _p1 = ex * v1[0] - ey * v1[1], _p2 = ex * v2[0] - ey * v2[1]; \
		const double _mn = _p2 < _p1 ? _p2 : _p1, _mx = _p2 < _p1 ? _p1 : _p2; \
		const double _r = fx * half[0] + fy * half[1]; \
		if (!(_mn <= _r && _mx >= -_r)) \
			return false; \
	}
#define KCL_AX_Z0(ex, ey, fx, fy) \
	{ \
		const double _p0 = ex * v0[0] - ey * v0[1], _p1 = ex * v1[0] - ey * v1[1]; \
		const double _mn = _p0 < _p1 ? _p0 : _p1, _mx = _p0 < _p1 ? _p1 : _p0; \
		const double _r = fx * half[0] + fy * half[1]; \
		if (_mn > _r || _mx < -_r) \
			return false; \
	}
#define KCL_AX_X2(ex, ey, fx, fy) \
	{ \
		const double _p0 = ex * v0[1] - ey * v0[2], _p1 = ex * v1[1] - ey * v1[2]; \
		const double _mn = _p0 < _p1 ? _p0 : _p1, _mx = _p0 < _p1 ? _p1 : _p0; \
		const double _r = fx * half[0] + fy * half[1]; \
		if (_mn > _r || _mx < -_r) \
			return false; \
	}
	// KCL_AX_X2 BUG COMPAT: upstream uses half Y/Z here (see X01); the
	// upstream variant only ever widens, so use the wider (correct) form.
#define KCL_AX_Y1(ex, ez, fx, fz) \
	{ \
		const double _p0 = -ex * v0[0] + ez * v0[2], _p1 = -ex * v1[0] + ez * v1[2]; \
		const double _mn = _p0 < _p1 ? _p0 : _p1, _mx = _p0 < _p1 ? _p1 : _p0; \
		const double _r = fx * half[0] + fz * half[2]; \
		if (_mn > _r || _mx < -_r) \
			return false; \
	}

	double fex = fabs (e0[0]), fey = fabs (e0[1]), fez = fabs (e0[2]);
	KCL_AX_X01 (e0[2], e0[1], fez, fey);
	KCL_AX_Y02 (e0[2], e0[0], fez, fex);
	KCL_AX_Z12 (e0[1], e0[0], fey, fex);

	fex = fabs (e1[0]);
	fey = fabs (e1[1]);
	fez = fabs (e1[2]);
	KCL_AX_X01 (e1[2], e1[1], fez, fey);
	KCL_AX_Y02 (e1[2], e1[0], fez, fex);
	KCL_AX_Z0 (e1[1], e1[0], fey, fex);

	fex = fabs (e2[0]);
	fey = fabs (e2[1]);
	fez = fabs (e2[2]);
	{
		// X2/Y1 as upstream (half Y/Z and half X/Z respectively)
		const double _p0 = e2[2] * v0[1] - e2[1] * v0[2], _p1 = e2[2] * v1[1] - e2[1] * v1[2];
		const double _mn = _p0 < _p1 ? _p0 : _p1, _mx = _p0 < _p1 ? _p1 : _p0;
		const double _r = fez * half[1] + fey * half[2];
		if (_mn > _r || _mx < -_r)
			return false;
	}
	KCL_AX_Y1 (e2[2], e2[0], fez, fex);
	KCL_AX_Z12 (e2[1], e2[0], fey, fex);

	// AABB test per axis
	double mn, mx;
	mn = mx = v0[0];
	if (v1[0] < mn)
		mn = v1[0];
	if (v1[0] > mx)
		mx = v1[0];
	if (v2[0] < mn)
		mn = v2[0];
	if (v2[0] > mx)
		mx = v2[0];
	if (mn > half[0] || mx < -half[0])
		return false;
	mn = mx = v0[1];
	if (v1[1] < mn)
		mn = v1[1];
	if (v1[1] > mx)
		mx = v1[1];
	if (v2[1] < mn)
		mn = v2[1];
	if (v2[1] > mx)
		mx = v2[1];
	if (mn > half[1] || mx < -half[1])
		return false;
	mn = mx = v0[2];
	if (v1[2] < mn)
		mn = v1[2];
	if (v1[2] > mx)
		mx = v1[2];
	if (v2[2] < mn)
		mn = v2[2];
	if (v2[2] > mx)
		mx = v2[2];
	if (mn > half[2] || mx < -half[2])
		return false;

	// plane test: always true upstream (dead Set()), skipped here (same outcome)
	return true;

#undef KCL_AX_X01
#undef KCL_AX_Y02
#undef KCL_AX_Z12
#undef KCL_AX_Z0
#undef KCL_AX_X2
#undef KCL_AX_Y1
}

//--- TriangleCubeOverlap port (box by min position + size) ---

static bool kcl_axis_test (double a1, double a2, double b1, double b2, double c1,
	double c2, double half)
{
	const double p = a1 * b1 + a2 * b2;
	const double q = a1 * c1 + a2 * c2;
	const double r = half * (fabs (a1) + fabs (a2));
	const double mn = p < q ? p : q, mx = p < q ? q : p;
	return mn > r || mx < -r;
}

static bool kcl_tricube_overlap (const kcl_tri3_t *t, const double minpos[3], double boxsize)
{
	const double half = boxsize / 2.0;
	const double cx = minpos[0] + half, cy = minpos[1] + half, cz = minpos[2] + half;
	const double v0[3] = { t->v[0][0] - cx, t->v[0][1] - cy, t->v[0][2] - cz };
	const double v1[3] = { t->v[1][0] - cx, t->v[1][1] - cy, t->v[1][2] - cz };
	const double v2[3] = { t->v[2][0] - cx, t->v[2][1] - cy, t->v[2][2] - cz };

	if ((v0[0] < v1[0] ? (v0[0] < v2[0] ? v0[0] : v2[0]) : (v1[0] < v2[0] ? v1[0] : v2[0])) > half
		|| (v0[0] > v1[0] ? (v0[0] > v2[0] ? v0[0] : v2[0]) : (v1[0] > v2[0] ? v1[0] : v2[0]))
			< -half)
		return false;
	if ((v0[1] < v1[1] ? (v0[1] < v2[1] ? v0[1] : v2[1]) : (v1[1] < v2[1] ? v1[1] : v2[1])) > half
		|| (v0[1] > v1[1] ? (v0[1] > v2[1] ? v0[1] : v2[1]) : (v1[1] > v2[1] ? v1[1] : v2[1]))
			< -half)
		return false;
	if ((v0[2] < v1[2] ? (v0[2] < v2[2] ? v0[2] : v2[2]) : (v1[2] < v2[2] ? v1[2] : v2[2])) > half
		|| (v0[2] > v1[2] ? (v0[2] > v2[2] ? v0[2] : v2[2]) : (v1[2] > v2[2] ? v1[2] : v2[2]))
			< -half)
		return false;

	const double d = t->n[0] * v0[0] + t->n[1] * v0[1] + t->n[2] * v0[2];
	const double r = half * (fabs (t->n[0]) + fabs (t->n[1]) + fabs (t->n[2]));
	if (d > r || d < -r)
		return false;

	double ex = v1[0] - v0[0], ey = v1[1] - v0[1], ez = v1[2] - v0[2];
	if (kcl_axis_test (ez, -ey, v0[1], v0[2], v2[1], v2[2], half))
		return false;
	if (kcl_axis_test (-ez, ex, v0[0], v0[2], v2[0], v2[2], half))
		return false;
	if (kcl_axis_test (ey, -ex, v1[0], v1[1], v2[0], v2[1], half))
		return false;

	ex = v2[0] - v1[0];
	ey = v2[1] - v1[1];
	ez = v2[2] - v1[2];
	if (kcl_axis_test (ez, -ey, v0[1], v0[2], v2[1], v2[2], half))
		return false;
	if (kcl_axis_test (-ez, ex, v0[0], v0[2], v2[0], v2[2], half))
		return false;
	if (kcl_axis_test (ey, -ex, v0[0], v0[1], v1[0], v1[1], half))
		return false;

	ex = v0[0] - v2[0];
	ey = v0[1] - v2[1];
	ez = v0[2] - v2[2];
	if (kcl_axis_test (ez, -ey, v0[1], v0[2], v1[1], v1[2], half))
		return false;
	if (kcl_axis_test (-ez, ex, v0[0], v0[2], v1[0], v1[2], half))
		return false;
	if (kcl_axis_test (ey, -ex, v1[0], v1[1], v2[0], v2[1], half))
		return false;
	return true;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			V2 reader			///////////////
///////////////////////////////////////////////////////////////////////////////

// validate the V2 model octree: 8 keys at 'pos', Divide nodes recurse.
// returns true if well formed and fully inside [0,size).
static bool kcl_v2_validate_moct (const kcl_rw_t *rw, const u8 *data, uint size, uint pos,
	int depth)
{
	if (depth > 16 || pos > size || size - pos < 8 * 4)
		return false;
	for (uint i = 0; i < 8; i++)
	{
		const u32 key = kcl_rd32 (rw, data + pos + 4 * i);
		const uint flags = key >> 30;
		if (flags == 0) // Divide: child offset in u32 units from node start
		{
			const u32 rel = key & 0x3fffffff;
			if (rel > (size - pos) / 4)
				return false;
			if (!kcl_v2_validate_moct (rw, data, size, pos + rel * 4, depth + 1))
				return false;
		}
		else if (flags != 2 && flags != 3) // must be Values(2) or NoData(3)
			return false;
	}
	return true;
}

enumError ScanRawKCL_V2 (kcl_t *kcl, const void *data, uint data_size,
	const kcl_analyze_t *kap, bool use_data)
{
	DASSERT (kcl);
	DASSERT (data);
	DASSERT (kap);
	(void)use_data; // raw V2 data is never kept; the octree is rebuilt on demand

	kcl_rw_t rw_storage = { kap->is_le };
	const kcl_rw_t *rw = &rw_storage;
	const u8 *base = data;

	KCL_ACTION_LOG (kcl, "ScanRawKCL_V2() %s [%s]\n", kcl->fname, rw->le ? "LE" : "BE");
	kcl->fform = FF_KCL;

	if (data_size < KCL_V2_HEAD_SIZE)
		return ERROR0 (ERR_INVALID_DATA, "Invalid V2 KCL file: %s\n", kcl->fname ? kcl->fname : "?");

	const u32 oct_off = kcl_rd32 (rw, base + 4);
	const u32 arr_off = kcl_rd32 (rw, base + 8);
	const u32 n_models = kcl_rd32 (rw, base + 12);
	if (oct_off != KCL_V2_HEAD_SIZE || n_models < 1 || n_models > 4096 || arr_off & 3
		|| arr_off < KCL_V2_HEAD_SIZE + 8 || arr_off > data_size
		|| n_models > (data_size - arr_off) / 4)
	{
		return ERROR0 (ERR_INVALID_DATA, "Invalid V2 KCL header: %s\n",
			kcl->fname ? kcl->fname : "?");
	}

	// light validation of the model octree (data itself is not needed:
	// all models are merged into a single triangle list)
	if (!kcl_v2_validate_moct (rw, base, data_size, oct_off, 0))
	{
		return ERROR0 (ERR_INVALID_DATA, "Invalid V2 KCL model octree: %s\n",
			kcl->fname ? kcl->fname : "?");
	}

	// read model offsets
	u32 *moff = MALLOC (n_models * sizeof (*moff));
	for (uint i = 0; i < n_models; i++)
	{
		moff[i] = kcl_rd32 (rw, base + arr_off + 4 * i);
		if (moff[i] & 3 || moff[i] < KCL_V1_HEAD_SIZE || moff[i] >= data_size)
		{
			FREE (moff);
			return ERROR0 (ERR_INVALID_DATA, "Invalid V2 KCL model #%u: %s\n", i,
				kcl->fname ? kcl->fname : "?");
		}
	}

	double3 min, max; // min and max of all incoming vertex points
	min.x = min.y = min.z = -1000.0;
	max.x = min.y = max.z = 1000.0;

	bool have_thick = false;
	for (uint m = 0; m < n_models; m++)
	{
		const u8 *mp = base + moff[m];
		if (moff[m] > data_size || data_size - moff[m] < KCL_V1_HEAD_SIZE)
		{
			FREE (moff);
			return ERROR0 (ERR_INVALID_DATA, "Truncated V2 KCL model #%u: %s\n", m,
				kcl->fname ? kcl->fname : "?");
		}
		const u32 poff = kcl_rd32 (rw, mp + 0);
		const u32 noff = kcl_rd32 (rw, mp + 4);
		const u32 troff = kcl_rd32 (rw, mp + 8);
		const u32 ooff = kcl_rd32 (rw, mp + 12);
		// V2 model header is always 60 bytes; prism offset has no -0x10 bias
		if (poff != KCL_V1_HEAD_SIZE || noff <= poff || troff <= noff || ooff < troff
			|| (noff - poff) % 12 || (troff - noff) % 12 || (ooff - troff) % 20
			|| moff[m] > data_size - ooff)
		{
			FREE (moff);
			return ERROR0 (ERR_INVALID_DATA, "Invalid V2 KCL model #%u layout: %s\n", m,
				kcl->fname ? kcl->fname : "?");
		}

		if (!have_thick)
		{
			kcl->unknown_0x10 = kcl_rdf4 (rw, mp + 16);
			kcl->min_octree.x = kcl_rdf4 (rw, mp + 20);
			kcl->min_octree.y = kcl_rdf4 (rw, mp + 24);
			kcl->min_octree.z = kcl_rdf4 (rw, mp + 28);
			kcl->mask[0] = kcl_rd32 (rw, mp + 32);
			kcl->mask[1] = kcl_rd32 (rw, mp + 36);
			kcl->mask[2] = kcl_rd32 (rw, mp + 40);
			kcl->coord_rshift = kcl_rd32 (rw, mp + 44);
			kcl->y_lshift = kcl_rd32 (rw, mp + 48);
			kcl->z_lshift = kcl_rd32 (rw, mp + 52);
			kcl->unknown_0x38 = kcl_rdf4 (rw, mp + 56);
			have_thick = true;
		}

		const uint n_vert = (noff - poff) / 12;
		const uint n_norm = (troff - noff) / 12;
		const uint n_tri = (ooff - troff) / 20;
		PRINT ("V2 model #%u: N(vert)=%u, N(norm)=%u, N(tri)=%u\n", m, n_vert, n_norm, n_tri);
		if (!n_tri)
			continue;

		const u8 *vertbase = mp + poff;
		const u8 *normbase = mp + noff;
		const u8 *prism = mp + troff;

		kcl_tridata_t *td = GrowListSize (&kcl->tridata, n_tri, 100);
		DASSERT (td);
		memset (td, 0, sizeof (*td) * n_tri);

		for (uint ti = 0; ti < n_tri; ti++, td++, prism += 20)
		{
			u16 in[6];
			for (uint k = 0; k < 6; k++)
				in[k] = kcl_rd16 (rw, prism + 4 + 2 * k);

			if (in[0] < n_vert)
			{
				const u8 *p = vertbase + 12 * in[0];
				td->pt[0].x = kcl_rdf4 (rw, p);
				td->pt[0].y = kcl_rdf4 (rw, p + 4);
				td->pt[0].z = kcl_rdf4 (rw, p + 8);
				MinMax3 (&min, &max, td->pt, 1);
			}

			for (uint p = 0; p < 4; p++)
				if (in[p + 1] < n_norm)
				{
					const u8 *q = normbase + 12 * in[p + 1];
					td->normal[p].x = kcl_rdf4 (rw, q);
					td->normal[p].y = kcl_rdf4 (rw, q + 4);
					td->normal[p].z = kcl_rdf4 (rw, q + 8);
				}

			td->length = kcl_rdf4 (rw, prism);
			td->in_flag = in[5];
			td->cur_flag = patch_kcl_flag ? patch_kcl_flag[in[5]] : in[5];
		}
	}
	FREE (moff);

	if (!kcl->tridata.used)
		return ERROR0 (ERR_INVALID_DATA, "V2 KCL without triangles: %s\n",
			kcl->fname ? kcl->fname : "?");

	// same post processing as V1: clip box + triangle points
	const bool clip = (KCL_MODE & KCLMD_CLIP) != 0;
	if (clip)
	{
		const double3 *kclip = GetKclClip ();
		double temp;
		temp = (max.x - min.x) * kclip->x;
		kcl->tri_minval.x = min.x - temp;
		kcl->tri_maxval.x = max.x + temp;

		temp = (max.y - min.y) * kclip->y;
		kcl->tri_minval.y = min.y - temp;
		kcl->tri_maxval.y = max.y + temp;

		temp = (max.z - min.z) * kclip->z;
		kcl->tri_minval.z = min.z - temp;
		kcl->tri_maxval.z = max.z + temp;
	}

	CalcPointsTriData ((kcl_tridata_t *)kcl->tridata.list, kcl->tridata.used, kcl, clip);
	kcl->norm_valid = true;

	// no usable in-memory octree: it is rebuilt on demand (always BE float)
	kcl->octree_valid = false;
	kcl->octree_nkeys = 0;
	kcl->min_max_valid = false;
	return ERR_OK;
}

//
///////////////////////////////////////////////////////////////////////////////
///////////////			V2 writer			///////////////
///////////////////////////////////////////////////////////////////////////////

// triangle index list of one model group
typedef struct kcl_grp_t
{
	uint *tri; // indices into kcl->tridata
	uint n_tri;
	int block; // model octree block index 0..7
	struct kcl_grp_t *child[8]; // NULL unless subdivided
	bool subdivided;
} kcl_grp_t;

static kcl_grp_t *kcl_grp_new (void)
{
	kcl_grp_t *g = CALLOC (1, sizeof (*g));
	return g;
}

static void kcl_grp_free (kcl_grp_t *g)
{
	if (!g)
		return;
	for (int i = 0; i < 8; i++)
		kcl_grp_free (g->child[i]);
	FREE (g->tri);
	FREE (g);
}

static void kcl_grp_add (kcl_grp_t *g, uint ti)
{
	g->tri = REALLOC (g->tri, (g->n_tri + 1) * sizeof (*g->tri));
	g->tri[g->n_tri++] = ti;
}

// subdivide [tris,n) within box (minpos,size) into 8 spatial groups.
// triangles near borders are referenced by several groups (conservative).
static kcl_grp_t **kcl_divide_models (const kcl_tridata_t *td_base, const uint *tris,
	uint n, const double minpos[3], double boxsize, int level, int *n_groups)
{
	// single model fast path (also used at level 0 for small inputs)
	if (level == 0 && n <= KCL_V2_MAX_MODEL_PRISMS)
	{
		kcl_grp_t **groups = MALLOC (sizeof (*groups));
		groups[0] = kcl_grp_new ();
		groups[0]->block = 0;
		for (uint i = 0; i < n; i++)
			kcl_grp_add (groups[0], tris[i]);
		*n_groups = 1;
		return groups;
	}

	kcl_grp_t **groups = CALLOC (8, sizeof (*groups));
	const double half = boxsize / 2.0;
	int index = 0;
	for (int z = 0; z < 2; z++)
		for (int y = 0; y < 2; y++)
			for (int x = 0; x < 2; x++)
			{
				double cube[3]
					= { minpos[0] + half * x, minpos[1] + half * y, minpos[2] + half * z };
				double center[3] = { cube[0] + half / 2.0, cube[1] + half / 2.0,
					cube[2] + half / 2.0 };
				// epsilon against float border misses (test is conservative anyway)
				const double eps = half * 1e-7 + 1e-9;
				double h[3] = { half / 2.0 + eps, half / 2.0 + eps, half / 2.0 + eps };

				kcl_grp_t *g = kcl_grp_new ();
				g->block = index;
				for (uint i = 0; i < n; i++)
				{
					const kcl_tridata_t *td = td_base + tris[i];
					if (td->status & TD_INVALID)
						continue;
					kcl_tri3_t t;
					kcl_tri3_init (&t, td->pt);
					if (kcl_tribox_overlap (&t, center, h))
						kcl_grp_add (g, tris[i]);
				}

				if (g->n_tri >= KCL_V2_MAX_MODEL_PRISMS && level < 16)
				{
					// too many prisms: subdivide recursively
					uint *sub = MALLOC (g->n_tri * sizeof (*sub));
					memcpy (sub, g->tri, g->n_tri * sizeof (*sub));
					int n_sub = 0;
					kcl_grp_t **children = kcl_divide_models (
						td_base, sub, g->n_tri, cube, half, level + 1, &n_sub);
					FREE (sub);
					if (n_sub == 1 && !children[0]->subdivided)
					{
						// cannot split further: keep as single model
						FREE (g->tri);
						g->tri = children[0]->tri;
						g->n_tri = children[0]->n_tri;
						children[0]->tri = 0;
						children[0]->n_tri = 0;
						kcl_grp_free (children[0]);
						FREE (children);
					}
					else
					{
						for (int c = 0; c < n_sub && c < 8; c++)
							g->child[c] = children[c];
						g->subdivided = true;
						FREE (g->tri);
						g->tri = 0;
						g->n_tri = 0;
						FREE (children);
					}
				}
				groups[index++] = g;
			}
	*n_groups = 8;
	return groups;
}

// polygon octree node (per model triangle octree)
typedef struct kcl_pno_t
{
	u16 *tri; // local prism indices
	uint n_tri;
	struct kcl_pno_t *child[8];
	bool is_branch;
} kcl_pno_t;

static void kcl_pno_free (kcl_pno_t *n)
{
	if (!n)
		return;
	for (int i = 0; i < 8; i++)
		kcl_pno_free (n->child[i]);
	FREE (n->tri);
	FREE (n);
}

static kcl_pno_t *kcl_pno_build (kcl_t *kcl, const kcl_tridata_t *td_base, const u16 *tris,
	uint n, const double pos[3], double size, int blow, int depth)
{
	kcl_pno_t *node = CALLOC (1, sizeof (*node));

	// blown up search box (EFE method, like KCL)
	const double cx = pos[0] + size / 2.0, cy = pos[1] + size / 2.0, cz = pos[2] + size / 2.0;
	const double ns = size + blow;
	const double np[3] = { cx - ns / 2.0, cy - ns / 2.0, cz - ns / 2.0 };

	u16 *found = MALLOC ((n ? n : 1) * sizeof (*found));
	uint n_found = 0;
	for (uint i = 0; i < n; i++)
	{
		const kcl_tridata_t *td = td_base + tris[i];
		kcl_tri3_t t;
		kcl_tri3_init (&t, td->pt);
		if (kcl_tricube_overlap (&t, np, ns))
			found[n_found++] = tris[i];
	}

	const double half = size / 2.0;
	if (n_found > kcl->max_cube_triangles && half >= (double)kcl->min_cube_size
		&& depth < (int)kcl->max_octree_depth)
	{
		node->is_branch = true;
		int index = 0;
		for (int z = 0; z < 2; z++)
			for (int y = 0; y < 2; y++)
				for (int x = 0; x < 2; x++)
				{
					double cp[3] = { pos[0] + half * x, pos[1] + half * y,
						pos[2] + half * z };
					node->child[index++]
						= kcl_pno_build (kcl, td_base, found, n_found, cp, half, blow, depth + 1);
				}
		FREE (found);
	}
	else
	{
		node->tri = found;
		node->n_tri = n_found;
	}
	return node;
}

static uint kcl_pno_count (const kcl_pno_t *n)
{
	uint count = 1;
	if (n->is_branch)
		for (int i = 0; i < 8; i++)
			count += kcl_pno_count (n->child[i]);
	return count;
}

// triangle list pool: deduplicate identical lists (ported logic)
typedef struct kcl_pool_t
{
	u16 *offs; // list start offsets (u16 units) into buf
	u16 **lists; // list contents
	uint *lens;
	uint n;
	uint total; // total u16 incl terminators
	u16 last_term; // offset of last terminator (shared empty lists)
} kcl_pool_t;

static void kcl_pool_add (kcl_pool_t *p, u16 *list, uint len)
{
	for (uint i = 0; i < p->n; i++)
		if (p->lens[i] == len && !memcmp (p->lists[i], list, len * 2))
			return; // duplicate
	p->lists = REALLOC (p->lists, (p->n + 1) * sizeof (*p->lists));
	p->lens = REALLOC (p->lens, (p->n + 1) * sizeof (*p->lens));
	p->offs = REALLOC (p->offs, (p->n + 1) * sizeof (*p->offs));
	p->lists[p->n] = list;
	p->lens[p->n] = len;
	p->offs[p->n] = (u16)p->total;
	p->total += len + 1; // + terminator
	p->n++;
}

static void kcl_pool_collect (kcl_pool_t *p, const kcl_pno_t *n)
{
	if (n->is_branch)
	{
		for (int i = 0; i < 8; i++)
			kcl_pool_collect (p, n->child[i]);
		return;
	}
	if (n->n_tri)
		kcl_pool_add (p, n->tri, n->n_tri);
}

static uint kcl_pool_find (const kcl_pool_t *p, const u16 *list, uint len)
{
	for (uint i = 0; i < p->n; i++)
		if (p->lens[i] == len && !memcmp (p->lists[i], list, len * 2))
			return p->offs[i];
	// empty lists share the last terminator
	return p->last_term;
}

// serialize one model section; returns malloced buffer in *out, size>=60
static uint kcl_write_model (kcl_t *kcl, const kcl_rw_t *rw, const kcl_tridata_t *td_base,
	const kcl_grp_t *g, uint global_base, u8 **out)
{
	const uint n = g->n_tri;

	//--- deduplicate positions (pt #0) and normals
	float3List_t vlist, nlist;
	InitializeF3L (&vlist, n + 1);
	InitializeF3L (&nlist, 4 * n + 1);
	u16 *pidx = MALLOC ((n ? n : 1) * sizeof (*pidx));
	u16 (*nidx)[4] = MALLOC ((n ? n : 1) * sizeof (*nidx));
	for (uint i = 0; i < n; i++)
	{
		const kcl_tridata_t *td = td_base + g->tri[i];
		float3 tmp;
		tmp.x = td->pt[0].x;
		tmp.y = td->pt[0].y;
		tmp.z = td->pt[0].z;
		pidx[i] = FindInsertFloatF3L (&vlist, &tmp, false);
		for (uint k = 0; k < 4; k++)
		{
			tmp.x = td->normal[k].x;
			tmp.y = td->normal[k].y;
			tmp.z = td->normal[k].z;
			nidx[i][k] = FindInsertFloatF3L (&nlist, &tmp, false);
		}
	}
	if (vlist.used > 0xffff || nlist.used > 0xffff)
	{
		ResetF3L (&vlist);
		ResetF3L (&nlist);
		FREE (pidx);
		FREE (nidx);
		return 0; // must split further (caller guarantees small groups)
	}

	//--- model bbox over all 3 points
	double mn[3] = { 1e100, 1e100, 1e100 }, mx[3] = { -1e100, -1e100, -1e100 };
	for (uint i = 0; i < n; i++)
	{
		const kcl_tridata_t *td = td_base + g->tri[i];
		for (uint k = 0; k < 3; k++)
			for (uint a = 0; a < 3; a++)
			{
				if (td->pt[k].v[a] < mn[a])
					mn[a] = td->pt[k].v[a];
				if (td->pt[k].v[a] > mx[a])
					mx[a] = td->pt[k].v[a];
			}
	}
	if (!n)
	{
		mn[0] = mn[1] = mn[2] = 0.0;
		mx[0] = mx[1] = mx[2] = 0.0;
	}
	const double sz[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };
	const int ex = kcl_next2exp (sz[0]), ey = kcl_next2exp (sz[1]), ez = kcl_next2exp (sz[2]);
	int cpow = kcl_next2exp (sz[0] < sz[1] ? (sz[0] < sz[2] ? sz[0] : sz[2])
										   : (sz[1] < sz[2] ? sz[1] : sz[2]));
	const int maxroot = kcl_next2exp ((double)kcl->max_cube_size);
	if (cpow > maxroot)
		cpow = maxroot;
	const uint rshift = (uint)cpow;
	const uint yshift = (uint)(ex - cpow);
	const uint zshift = (uint)(ex - cpow + ey - cpow);
	const u32 mask[3] = { ex > 31 ? 0 : (u32)(0xffffffffu << ex),
		ey > 31 ? 0 : (u32)(0xffffffffu << ey),
		ez > 31 ? 0 : (u32)(0xffffffffu << ez) };
	const uint csize = 1u << cpow;

	//--- polygon octree over local prism indices 0..n-1
	u16 *local = MALLOC ((n ? n : 1) * sizeof (*local));
	for (uint i = 0; i < n; i++)
		local[i] = (u16)i;
	// local prism order == group order; build needs tridata mapped by local
	// index -> use a remapped base: build directly on group order via trampoline.
	// Simplest: temporary compact tridata pointer table.
	const kcl_tridata_t **tab = MALLOC ((n ? n : 1) * sizeof (*tab));
	for (uint i = 0; i < n; i++)
		tab[i] = td_base + g->tri[i];
	// kcl_pno_build indexes td_base by local id, so create a compact copy
	kcl_tridata_t *compact = MALLOC ((n ? n : 1) * sizeof (*compact));
	for (uint i = 0; i < n; i++)
		memcpy (compact + i, tab[i], sizeof (*compact));
	FREE (tab);

	uint ncx = (uint)(csize ? ((1u << ex) / csize) : 1);
	uint ncy = (uint)(csize ? ((1u << ey) / csize) : 1);
	uint ncz = (uint)(csize ? ((1u << ez) / csize) : 1);
	if (!ncx)
		ncx = 1;
	if (!ncy)
		ncy = 1;
	if (!ncz)
		ncz = 1;

	// root cubes: may be several (like KCL); serialize as flat root array
	kcl_pno_t **roots = MALLOC (ncx * ncy * ncz * sizeof (*roots));
	uint ri = 0;
	for (uint z = 0; z < ncz; z++)
		for (uint y = 0; y < ncy; y++)
			for (uint x = 0; x < ncx; x++)
			{
				double rp[3]
					= { mn[0] + csize * x, mn[1] + csize * y, mn[2] + csize * z };
				roots[ri++] = kcl_pno_build (
					kcl, compact, local, n, rp, (double)csize, (int)kcl->cube_blow, 0);
			}
	FREE (local);

	//--- serialize octree: keys first (BFS), then pooled lists
	uint nkeys = 0;
	for (uint r = 0; r < ri; r++)
		nkeys += kcl_pno_count (roots[r]);
	kcl_pool_t pool;
	memset (&pool, 0, sizeof (pool));
	for (uint r = 0; r < ri; r++)
		kcl_pool_collect (&pool, roots[r]);
	pool.last_term = (u16)(pool.total ? pool.total - 1 : 0);

	const uint keysize = nkeys * 4;
	const uint listsize = (pool.total ? pool.total : 1) * 2;
	u8 *oct = CALLOC (1, keysize + listsize);

	// BFS key writing with patchable branch offsets (u32 units from node start)
	// queue of branches awaiting children
	typedef struct kcl_qb_t
	{
		const kcl_pno_t *node;
		uint keypos;
	} kcl_qb_t;
	kcl_qb_t *queue = MALLOC ((nkeys + 1) * sizeof (*queue));
	uint qn = 0, keypos = 0;
	for (uint r = 0; r < ri; r++)
	{
		const kcl_pno_t *nd = roots[r];
		if (nd->is_branch)
		{
			queue[qn].node = nd;
			queue[qn].keypos = keypos;
			qn++;
		}
		else if (nd->n_tri)
			kcl_wr32 (rw, oct + keypos,
				0x80000000u | (keysize + kcl_pool_find (&pool, nd->tri, nd->n_tri) * 2
								  - keypos - 2));
		else
			kcl_wr32 (rw, oct + keypos,
				0x80000000u | (keysize + pool.last_term * 2 - keypos - 2));
		keypos += 4;
	}
	for (uint q = 0; q < qn; q++)
	{
		const uint child_base = keypos; // byte offset of the 8 children
		// patch parent: relative u32 distance
		kcl_wr32 (rw, oct + queue[q].keypos, (child_base - queue[q].keypos) / 4);
		for (int c = 0; c < 8; c++)
		{
			const kcl_pno_t *nd = queue[q].node->child[c];
			if (nd->is_branch)
			{
				queue[qn].node = nd;
				queue[qn].keypos = keypos;
				qn++;
			}
			else if (nd->n_tri)
				kcl_wr32 (rw, oct + keypos,
					0x80000000u | (keysize + kcl_pool_find (&pool, nd->tri, nd->n_tri) * 2
									  - keypos - 2));
			else
				kcl_wr32 (rw, oct + keypos,
					0x80000000u | (keysize + pool.last_term * 2 - keypos - 2));
			keypos += 4;
		}
	}
	DASSERT (keypos == keysize);
	FREE (queue);

	// lists
	u8 *lp = oct + keysize;
	for (uint i = 0; i < pool.n; i++)
	{
		for (uint k = 0; k < pool.lens[i]; k++, lp += 2)
			kcl_wr16 (rw, lp, pool.lists[i][k]);
		kcl_wr16 (rw, lp, KCL_V2_LEAF_TERM);
		lp += 2;
	}
	if (!pool.n) // no lists at all: single terminator for empty leaves
		kcl_wr16 (rw, lp, KCL_V2_LEAF_TERM);
	for (uint r = 0; r < ri; r++)
		kcl_pno_free (roots[r]);
	FREE (roots);
	FREE (pool.offs);
	FREE (pool.lists);
	FREE (pool.lens);
	FREE (compact);

	//--- assemble model section
	const uint pos_size = 12 * vlist.used;
	const uint nrm_size = 12 * nlist.used;
	const uint pri_size = 20 * n;
	const uint oct_size = keysize + listsize;
	const uint pos_off = KCL_V1_HEAD_SIZE;
	const uint nrm_off = pos_off + pos_size;
	const uint pri_off = nrm_off + nrm_size;
	const uint oct_off = pri_off + pri_size;
	const uint total = oct_off + oct_size;

	u8 *buf = CALLOC (1, total ? total : 1);
	kcl_wr32 (rw, buf + 0, pos_off);
	kcl_wr32 (rw, buf + 4, nrm_off);
	kcl_wr32 (rw, buf + 8, pri_off);
	kcl_wr32 (rw, buf + 12, oct_off);
	kcl_wrf4 (rw, buf + 16, kcl->unknown_0x10);
	kcl_wrf4 (rw, buf + 20, (float)mn[0]);
	kcl_wrf4 (rw, buf + 24, (float)mn[1]);
	kcl_wrf4 (rw, buf + 28, (float)mn[2]);
	kcl_wr32 (rw, buf + 32, mask[0]);
	kcl_wr32 (rw, buf + 36, mask[1]);
	kcl_wr32 (rw, buf + 40, mask[2]);
	kcl_wr32 (rw, buf + 44, rshift);
	kcl_wr32 (rw, buf + 48, yshift);
	kcl_wr32 (rw, buf + 52, zshift);
	kcl_wrf4 (rw, buf + 56, kcl->unknown_0x38);

	u8 *dp = buf + pos_off;
	for (uint i = 0; i < vlist.used; i++, dp += 12)
	{
		kcl_wrf4 (rw, dp, vlist.list[i].x);
		kcl_wrf4 (rw, dp + 4, vlist.list[i].y);
		kcl_wrf4 (rw, dp + 8, vlist.list[i].z);
	}
	dp = buf + nrm_off;
	for (uint i = 0; i < nlist.used; i++, dp += 12)
	{
		kcl_wrf4 (rw, dp, nlist.list[i].x);
		kcl_wrf4 (rw, dp + 4, nlist.list[i].y);
		kcl_wrf4 (rw, dp + 8, nlist.list[i].z);
	}
	ResetF3L (&vlist);
	ResetF3L (&nlist);

	dp = buf + pri_off;
	for (uint i = 0; i < n; i++, dp += 20)
	{
		const kcl_tridata_t *td = td_base + g->tri[i];
		kcl_wrf4 (rw, dp, td->length);
		kcl_wr16 (rw, dp + 4, pidx[i]);
		kcl_wr16 (rw, dp + 6, nidx[i][0]);
		kcl_wr16 (rw, dp + 8, nidx[i][1]);
		kcl_wr16 (rw, dp + 10, nidx[i][2]);
		kcl_wr16 (rw, dp + 12, nidx[i][3]);
		kcl_wr16 (rw, dp + 14, (u16)td->cur_flag);
		kcl_wr32 (rw, dp + 16, global_base + i);
	}
	FREE (pidx);
	FREE (nidx);

	memcpy (buf + oct_off, oct, oct_size);
	FREE (oct);

	*out = buf;
	return total;
}

// model octree node for file assembly
typedef struct kcl_mno_t
{
	bool is_branch;
	struct kcl_mno_t *child[8];
	int model; // >=0: Values|model, -1: NoData
} kcl_mno_t;

static kcl_mno_t *kcl_mno_leaf (int model)
{
	kcl_mno_t *n = CALLOC (1, sizeof (*n));
	n->model = model;
	return n;
}

static kcl_mno_t *kcl_mno_branch (void)
{
	kcl_mno_t *n = CALLOC (1, sizeof (*n));
	n->is_branch = true;
	n->model = -1;
	return n;
}

static void kcl_mno_free (kcl_mno_t *n)
{
	if (!n)
		return;
	for (int i = 0; i < 8; i++)
		kcl_mno_free (n->child[i]);
	FREE (n);
}

static uint kcl_mno_count (const kcl_mno_t *n)
{
	uint c = 8; // this branch's children... (only called on branches)
	if (!n->is_branch)
		return 0;
	for (int i = 0; i < 8; i++)
		if (n->child[i] && n->child[i]->is_branch)
			c += kcl_mno_count (n->child[i]);
	return c;
}

// assign model indices depth first (block order); mirrors KCL CreateModelOctree
static void kcl_moct_assign (kcl_grp_t *g, kcl_mno_t *node, uint *counter)
{
	if (g->subdivided)
	{
		node->is_branch = true;
		for (int c = 0; c < 8; c++)
		{
			node->child[c] = CALLOC (1, sizeof (*node->child[c]));
			node->child[c]->model = -1;
			if (g->child[c])
				kcl_moct_assign (g->child[c], node->child[c], counter);
		}
		return;
	}
	if (g->n_tri)
		node->model = (int)((*counter)++);
}

// collect groups with triangles depth first (same order as assignment)
static void kcl_moct_collect (kcl_grp_t *g, kcl_grp_t **ordered, uint *idx)
{
	if (g->subdivided)
	{
		for (int c = 0; c < 8; c++)
			if (g->child[c])
				kcl_moct_collect (g->child[c], ordered, idx);
		return;
	}
	if (g->n_tri)
		ordered[(*idx)++] = g;
}

// build the file model octree from top level groups; assigns model indices
// depth first and returns the 8 root children (single model -> 8x Values|0)
static kcl_mno_t **kcl_build_moct (kcl_grp_t **groups, int n_groups, uint *n_models)
{
	kcl_mno_t **roots = CALLOC (8, sizeof (*roots));
	for (int i = 0; i < 8; i++)
		roots[i] = kcl_mno_leaf (-1);

	if (n_groups == 1 && !groups[0]->subdivided)
	{
		for (int i = 0; i < 8; i++)
			roots[i]->model = 0;
		*n_models = groups[0]->n_tri ? 1 : 0;
		return roots;
	}

	uint counter = 0;
	for (int i = 0; i < n_groups && i < 8; i++)
		if (groups[i])
			kcl_moct_assign (groups[i], roots[groups[i]->block], &counter);
	*n_models = counter;
	return roots;
}

enumError CreateRawKCL_V2 (kcl_t *kcl, bool out_le)
{
	DASSERT (kcl);

	kcl_rw_t rw_storage = { out_le };
	const kcl_rw_t *rw = &rw_storage;

	CalcNormalsKCL (kcl, false);

	const uint n_tri = kcl->tridata.used;
	const kcl_tridata_t *td_base = (kcl_tridata_t *)kcl->tridata.list;

	//--- group triangles into models
	uint *all = MALLOC ((n_tri ? n_tri : 1) * sizeof (*all));
	uint n_all = 0;
	for (uint i = 0; i < n_tri; i++)
		if (!(td_base[i].status & TD_INVALID))
			all[n_all++] = i;

	double fmin[3] = { 1e100, 1e100, 1e100 }, fmax[3] = { -1e100, -1e100, -1e100 };
	for (uint i = 0; i < n_all; i++)
	{
		const kcl_tridata_t *td = td_base + all[i];
		for (uint k = 0; k < 3; k++)
			for (uint a = 0; a < 3; a++)
			{
				if (td->pt[k].v[a] < fmin[a])
					fmin[a] = td->pt[k].v[a];
				if (td->pt[k].v[a] > fmax[a])
					fmax[a] = td->pt[k].v[a];
			}
	}
	if (!n_all)
		fmin[0] = fmin[1] = fmin[2] = fmax[0] = fmax[1] = fmax[2] = 0.0;

	const double fsize[3] = { fmax[0] - fmin[0], fmax[1] - fmin[1], fmax[2] - fmin[2] };
	const int fex = kcl_next2exp (fsize[0]), fey = kcl_next2exp (fsize[1]),
			  fez = kcl_next2exp (fsize[2]);
	const int fmaxexp = fex > fey ? (fex > fez ? fex : fez) : (fey > fez ? fey : fez);
	const double box = 1.0 * (1u << fmaxexp);

	int n_groups = 0;
	kcl_grp_t **groups = kcl_divide_models (
		td_base, all, n_all, fmin, box > 0 ? box : 2.0, 0, &n_groups);
	FREE (all);

	//--- file model octree + model order (depth first, block order)
	uint n_models = 0;
	kcl_mno_t **roots = kcl_build_moct (groups, n_groups, &n_models);

	// collect models in assignment order: walk octree leaves Values|m, then
	// map m -> group. Simpler: collect groups depth-first in same order.
	kcl_grp_t **ordered = MALLOC ((n_models ? n_models : 1) * sizeof (*ordered));
	uint oi = 0;
	if (n_groups == 1 && !groups[0]->subdivided)
	{
		if (groups[0]->n_tri)
			ordered[oi++] = groups[0];
	}
	else
		for (int i = 0; i < n_groups && i < 8; i++)
			if (groups[i])
				kcl_moct_collect (groups[i], ordered, &oi);
	DASSERT (oi == n_models);

	//--- write model sections
	u8 **sections = MALLOC ((n_models ? n_models : 1) * sizeof (*sections));
	uint *sec_size = CALLOC (n_models ? n_models : 1, sizeof (*sec_size));
	uint global = 0;
	for (uint m = 0; m < n_models; m++)
	{
		sec_size[m] = kcl_write_model (kcl, rw, td_base, ordered[m], global, sections + m);
		if (!sec_size[m])
		{
			for (uint k = 0; k < m; k++)
				FREE (sections[k]);
			FREE (sections);
			FREE (sec_size);
			FREE (ordered);
			for (int i = 0; i < 8; i++)
				kcl_mno_free (roots[i]);
			FREE (roots);
			for (int i = 0; i < n_groups; i++)
				kcl_grp_free (groups[i]);
			FREE (groups);
			return ERROR0 (ERR_INVALID_DATA, "Too many vertices/normals for V2 model #%u\n", m);
		}
		global += ordered[m]->n_tri;
	}
	FREE (ordered);

	//--- serialize model octree (8 root keys, then BFS children)
	uint moct_keys = 8;
	for (int i = 0; i < 8; i++)
		if (roots[i]->is_branch)
			moct_keys += kcl_mno_count (roots[i]);
	u8 *moct = CALLOC (1, moct_keys * 4);
	typedef struct kcl_mqb_t
	{
		kcl_mno_t *node;
		uint keypos;
	} kcl_mqb_t;
	kcl_mqb_t *mq = MALLOC ((moct_keys + 1) * sizeof (*mq));
	uint mqn = 0, mkpos = 0;
	for (int i = 0; i < 8; i++, mkpos += 4)
	{
		if (roots[i]->is_branch)
		{
			mq[mqn].node = roots[i];
			mq[mqn].keypos = mkpos;
			mqn++;
		}
		else if (roots[i]->model >= 0)
			kcl_wr32 (rw, moct + mkpos, 0x80000000u | (u32)roots[i]->model);
		else
			kcl_wr32 (rw, moct + mkpos, 0xc0000000u);
	}
	for (uint q = 0; q < mqn; q++)
	{
		const uint cbase = mkpos;
		kcl_wr32 (rw, moct + mq[q].keypos, (cbase - mq[q].keypos) / 4);
		for (int c = 0; c < 8; c++, mkpos += 4)
		{
			kcl_mno_t *nd = mq[q].node->child[c];
			if (nd && nd->is_branch)
			{
				mq[mqn].node = nd;
				mq[mqn].keypos = mkpos;
				mqn++;
			}
			else if (nd && nd->model >= 0)
				kcl_wr32 (rw, moct + mkpos, 0x80000000u | (u32)nd->model);
			else
				kcl_wr32 (rw, moct + mkpos, 0xc0000000u);
		}
	}
	DASSERT (mkpos == moct_keys * 4);
	FREE (mq);
	for (int i = 0; i < 8; i++)
		kcl_mno_free (roots[i]);
	FREE (roots);

	//--- assemble file
	const uint moct_size = moct_keys * 4;
	const uint arr_off = KCL_V2_HEAD_SIZE + moct_size;
	uint data_off = arr_off + 4 * n_models;
	// 4-align sections
	uint *sec_off = MALLOC ((n_models ? n_models : 1) * sizeof (*sec_off));
	for (uint m = 0; m < n_models; m++)
	{
		data_off = (data_off + 3) & ~3u;
		sec_off[m] = data_off;
		data_off += sec_size[m];
	}

	u8 *old = kcl->raw_data_alloced ? kcl->raw_data : 0;
	u8 *file = CALLOC (1, data_off ? data_off : 1);
	write_be32 (file + 0, KCL_V2_MAGIC); // version is always big endian
	kcl_wr32 (rw, file + 4, KCL_V2_HEAD_SIZE);
	kcl_wr32 (rw, file + 8, arr_off);
	kcl_wr32 (rw, file + 12, n_models);
	kcl_wrf4 (rw, file + 16, (float)fmin[0]);
	kcl_wrf4 (rw, file + 20, (float)fmin[1]);
	kcl_wrf4 (rw, file + 24, (float)fmin[2]);
	kcl_wrf4 (rw, file + 28, (float)fmax[0]);
	kcl_wrf4 (rw, file + 32, (float)fmax[1]);
	kcl_wrf4 (rw, file + 36, (float)fmax[2]);
	kcl_wr32 (rw, file + 40, (u32)fex);
	kcl_wr32 (rw, file + 44, (u32)fey);
	kcl_wr32 (rw, file + 48, (u32)fez);
	kcl_wr32 (rw, file + 52, n_tri);
	memcpy (file + KCL_V2_HEAD_SIZE, moct, moct_size);
	FREE (moct);
	for (uint m = 0; m < n_models; m++)
	{
		kcl_wr32 (rw, file + arr_off + 4 * m, sec_off[m]);
		memcpy (file + sec_off[m], sections[m], sec_size[m]);
		FREE (sections[m]);
	}
	FREE (sections);
	FREE (sec_size);
	FREE (sec_off);
	for (int i = 0; i < n_groups; i++)
		kcl_grp_free (groups[i]);
	FREE (groups);

	kcl->raw_data = file;
	kcl->raw_data_size = data_off;
	kcl->raw_data_alloced = true;
	kcl->model_modified = false;
	// in-memory V1 octree (if any) no longer matches: drop it, rebuild lazily
	if (kcl->octree_alloced)
		FREE (kcl->octree);
	kcl->octree = 0;
	kcl->octree_size = 0;
	kcl->octree_nkeys = 0;
	kcl->octree_alloced = false;
	kcl->octree_valid = false;

	KCL_ACTION_LOG (
		kcl, "CreateRawKCL_V2() N=%u models=%u size=%u [%s]\n", n_tri, n_models, data_off,
		out_le ? "LE" : "BE");
	FREE (old);
	return ERR_OK;
}
