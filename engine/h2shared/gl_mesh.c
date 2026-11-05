/*
 * gl_mesh.c -- triangle model functions
 * Copyright (C) 1996-1997  Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "quakedef.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

static unsigned char	used[MAXALIASTRIS];

// the command list holds counts and s/t values that are valid for
// every frame
static int		commands[MAXALIASTRIS*7 + 1];
static int		numcommands;

// all frames will have their vertexes rearranged and expanded
// so they are in the order expected by the command list
static int		vertexorder[MAXALIASTRIS * 3];
static int		numorder;

static int		stripverts[MAXALIASTRIS + 2];
static int		striptris[MAXALIASTRIS];
static int		stripstverts[MAXALIASTRIS + 2];
static int		stripcount;

/* Every directed edge of the triangles, by hash. The strip and fan searches
   look for the first triangle after starttri with a given edge: scanning all
   triangles for each took most of a level's loading on the PS2. A chain holds
   its entries by rising triangle, then corner, which is the scan's order, so
   the strips stay the same. An entry is triangle * 3 + corner + 1; 0 ends. */
#define	EDGE_HASH_SIZE	4096
static unsigned short	edge_head[EDGE_HASH_SIZE];
static unsigned short	edge_next[MAXALIASTRIS * 3 + 1];

#define	EDGE_HASH(v1,s1,v2,s2)	(((v1) * 31 + (s1) * 17 + (v2) * 7 + (s2)) & (EDGE_HASH_SIZE - 1))

static void BuildEdgeIndex (void)
{
	int		j, k, e, h;
	mtriangle_t	*t;

	memset (edge_head, 0, sizeof(edge_head));
	for (j = pheader->numtris - 1; j >= 0; j--)
	{
		t = &triangles[j];
		for (k = 2; k >= 0; k--)
		{
			e = j * 3 + k + 1;
			h = EDGE_HASH(t->vertindex[k], t->stindex[k], t->vertindex[(k+1)%3], t->stindex[(k+1)%3]);
			edge_next[e] = edge_head[h];
			edge_head[h] = e;
		}
	}
}

/* the first triangle after starttri on side facesfront with edge
   (m1,st1) -> (m2,st2): *pj the triangle, *pk the corner; 0 if there is none */
static int FindEdge (int starttri, int facesfront, int m1, int st1, int m2, int st2, int *pj, int *pk)
{
	int		e, j, k;
	mtriangle_t	*check;

	for (e = edge_head[EDGE_HASH(m1, st1, m2, st2)]; e; e = edge_next[e])
	{
		j = (e - 1) / 3;
		if (j <= starttri)
			continue;
		k = (e - 1) % 3;
		check = &triangles[j];
		if (check->facesfront != facesfront)
			continue;
		if (check->vertindex[k] != m1 || check->stindex[k] != st1)
			continue;
		if (check->vertindex[(k+1)%3] != m2 || check->stindex[(k+1)%3] != st2)
			continue;
		*pj = j;
		*pk = k;
		return 1;
	}
	return 0;
}

/*
================
StripLength
================
*/
static int StripLength (int starttri, int startv)
{
	int			m1, m2;
	int			st1, st2;
	int			j;
	mtriangle_t	*last, *check;
	int			k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripstverts[0] = last->stindex[(startv)%3];

	stripverts[1] = last->vertindex[(startv+1)%3];
	stripstverts[1] = last->stindex[(startv+1)%3];

	stripverts[2] = last->vertindex[(startv+2)%3];
	stripstverts[2] = last->stindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+2)%3];
	st1 = last->stindex[(startv+2)%3];
	m2 = last->vertindex[(startv+1)%3];
	st2 = last->stindex[(startv+1)%3];

	// look for a matching triangle
nexttri:
	if (FindEdge (starttri, last->facesfront, m1, st1, m2, st2, &j, &k))
	{
		check = &triangles[j];
		{
			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			if (stripcount & 1)
			{
				m2 = check->vertindex[ (k+2)%3 ];
				st2 = check->stindex[ (k+2)%3 ];
			}
			else
			{
				m1 = check->vertindex[ (k+2)%3 ];
				st1 = check->stindex[ (k+2)%3 ];
			}

			stripverts[stripcount+2] = check->vertindex[ (k+2)%3 ];
			stripstverts[stripcount+2] = check->stindex[ (k+2)%3 ];
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}

done:
	// clear the temp used flags
	for (j = 1; j < stripcount; j++)
		used[striptris[j]] = 0;

	return stripcount;
}

/*
===========
FanLength
===========
*/
static int FanLength (int starttri, int startv)
{
	int		m1, m2;
	int		st1, st2;
	int		j;
	mtriangle_t	*last, *check;
	int		k;

	used[starttri] = 2;

	last = &triangles[starttri];

	stripverts[0] = last->vertindex[(startv)%3];
	stripstverts[0] = last->stindex[(startv)%3];

	stripverts[1] = last->vertindex[(startv+1)%3];
	stripstverts[1] = last->stindex[(startv+1)%3];

	stripverts[2] = last->vertindex[(startv+2)%3];
	stripstverts[2] = last->stindex[(startv+2)%3];

	striptris[0] = starttri;
	stripcount = 1;

	m1 = last->vertindex[(startv+0)%3];
	st1 = last->stindex[(startv+2)%3];
	m2 = last->vertindex[(startv+2)%3];
	st2 = last->stindex[(startv+1)%3];

	// look for a matching triangle
nexttri:
	if (FindEdge (starttri, last->facesfront, m1, st1, m2, st2, &j, &k))
	{
		check = &triangles[j];
		{
			// this is the next part of the fan

			// if we can't use this triangle, this tristrip is done
			if (used[j])
				goto done;

			// the new edge
			m2 = check->vertindex[ (k+2)%3 ];
			st2 = check->stindex[ (k+2)%3 ];

			stripverts[stripcount+2] = m2;
			stripstverts[stripcount+2] = st2;
			striptris[stripcount] = j;
			stripcount++;

			used[j] = 2;
			goto nexttri;
		}
	}

done:
	// clear the temp used flags
	for (j = 1; j < stripcount; j++)
		used[striptris[j]] = 0;

	return stripcount;
}


/*
================
BuildTris

Generate a list of trifans or strips
for the model, which holds for all frames
================
*/
static void BuildTris (void)
{
	int		i, j, k;
	int		startv;
	float	s, t;
	int		len, bestlen, besttype;
	int		bestverts[MAXALIASTRIS + 2];
	int		besttris[MAXALIASTRIS];
	int		beststverts[MAXALIASTRIS + 2];
	int		type;

	//
	// build tristrips
	//
	numorder = 0;
	numcommands = 0;
	memset (used, 0, sizeof(used));
	BuildEdgeIndex ();
	for (i = 0; i < pheader->numtris; i++)
	{
		// pick an unused triangle and start the trifan
		if (used[i])
			continue;

		bestlen = 0;
		besttype = 0;
		for (type = 0 ; type < 2 ; type++)
		{
			for (startv = 0; startv < 3; startv++)
			{
				if (type == 1)
					len = StripLength (i, startv);
				else
					len = FanLength (i, startv);
				if (len > bestlen)
				{
					besttype = type;
					bestlen = len;
					for (j = 0; j < bestlen+2; j++)
					{
						beststverts[j] = stripstverts[j];
						bestverts[j] = stripverts[j];
					}
					for (j = 0; j < bestlen; j++)
						besttris[j] = striptris[j];
				}
			}
		}

		// mark the tris on the best strip as used
		for (j = 0; j < bestlen; j++)
			used[besttris[j]] = 1;

		if (besttype == 1)
			commands[numcommands++] = (bestlen+2);
		else
			commands[numcommands++] = -(bestlen+2);

		for (j = 0; j < bestlen+2; j++)
		{
			int		tmp;

			// emit a vertex into the reorder buffer
			k = bestverts[j];
			vertexorder[numorder++] = k;

			k = beststverts[j];

			// emit s/t coords into the commands stream
			s = stverts[k].s;
			t = stverts[k].t;

			if (!triangles[besttris[0]].facesfront && stverts[k].onseam)
				s += pheader->skinwidth / 2;	// on back side
			s = (s + 0.5) / pheader->skinwidth;
			t = (t + 0.5) / pheader->skinheight;

		//	*(float *)&commands[numcommands++] = s;
		//	*(float *)&commands[numcommands++] = t;
			// NOTE: 4 == sizeof(int)
			//	   == sizeof(float)
			memcpy (&tmp, &s, 4);
			commands[numcommands++] = tmp;
			memcpy (&tmp, &t, 4);
			commands[numcommands++] = tmp;
		}
	}

	commands[numcommands++] = 0;		// end of list marker
	DEBUG_Printf ("%3i tri %3i vert %3i cmd\n", pheader->numtris, numorder, numcommands);
}


/*
================
GL_MakeAliasModelDisplayLists
================
*/
void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *hdr)
{
	int		i, j;
	int		*cmds;
	trivertx_t	*verts;

	DEBUG_Printf ("meshing %s...\n", m->name);
	BuildTris ();		// trifans or lists

	hdr->poseverts = numorder;

	cmds = (int *) Hunk_AllocName (numcommands * 4, "cmds");
	hdr->commands = (byte *)cmds - (byte *)hdr;
	memcpy (cmds, commands, numcommands * 4);

	verts = (trivertx_t *) Hunk_AllocName (hdr->numposes * hdr->poseverts * sizeof(trivertx_t), "verts");
	hdr->posedata = (byte *)verts - (byte *)hdr;
	for (i = 0; i < hdr->numposes; i++)
	{
		for (j = 0; j < numorder; j++)
			*verts++ = poseverts[i][vertexorder[j]];
	}
}

