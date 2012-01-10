/*
 * Copyright 2011 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code'). 

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public 
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied 
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see 
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these 
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx 
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o 
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
// Code: Cyril Meynier
//
// Copyright (c) 1999-2000 ARKANE Studios SA. All rights reserved

#include "graphics/effects/Fog.h"

#include "animation/Animation.h"

#include "core/Config.h"
#include "core/Core.h"
#include "core/GameTime.h"

#include "graphics/Math.h"
#include "graphics/Draw.h"
#include "graphics/particle/ParticleEffects.h"

#include "io/log/Logger.h"

EERIE_3DOBJ * fogobj = NULL;

FOG_DEF fogs[MAX_FOG];

//*************************************************************************************
// Used to Set 3D Object Visual for Fogs
//*************************************************************************************
void ARX_FOGS_Set_Object(EERIE_3DOBJ * _fogobj)
{
	fogobj = _fogobj;
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_FirstInit()
{
	LogDebug("Fogs Init");

	ARX_FOGS_Clear();
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_Clear()
{
	for (long i = 0; i < MAX_FOG; i++)
	{
		memset(&fogs[i], 0, sizeof(FOG_DEF));
	}
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_TranslateSelected(Vec3f * trans)
{
	for (long i = 0; i < MAX_FOG; i++)
	{
		if (fogs[i].selected)
		{
			fogs[i].pos.x += trans->x;
			fogs[i].pos.y += trans->y;
			fogs[i].pos.z += trans->z;
		}
	}
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_UnselectAll()
{
	for (long i = 0; i < MAX_FOG; i++)
	{
		fogs[i].selected = 0;
	}
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_Select(long n)
{
	if (fogs[n].selected) fogs[n].selected = 0;
	else fogs[n].selected = 1;
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_KillByIndex(long num)
{
	if ((num >= 0) && (num < MAX_FOG))
	{
		memset(&fogs[num], 0, sizeof(FOG_DEF));
	}
}

void ARX_FOGS_KillSelected()
{
	for (long i = 0; i < MAX_FOG; i++)
	{
		if (fogs[i].selected)
		{
			ARX_FOGS_KillByIndex(i);
		}
	}
}
//*************************************************************************************
//*************************************************************************************
long ARX_FOGS_GetFree()
{
	for (long i = 0; i < MAX_FOG; i++)
	{
		if (!fogs[i].exist)  return i;
	}

	return -1;
}
//*************************************************************************************
//*************************************************************************************
long ARX_FOGS_Count()
{
	long count = 0;

	for (long i = 0; i < MAX_FOG; i++)
	{
		if (fogs[i].exist)  count++;
	}

	return count;
}
void ARX_FOGS_TimeReset()
{
}
void AddPoisonFog(Vec3f * pos, float power)
{
	int iDiv = 4 - config.video.levelOfDetail;

	float flDiv = static_cast<float>(1 << iDiv);
	long count = checked_range_cast<long>(FrameDiff / flDiv);

	if (count < 1) count = 1;

	while (count) 
	{
		count--;
		long j = ARX_PARTICLES_GetFree();

		if ((j != -1) && (!arxtime.is_paused()) && (rnd() * 2000.f < power))
		{
			ParticleCount++;
			particle[j].special		=	FADE_IN_AND_OUT | ROTATING | MODULATE_ROTATION | DISSIPATING;
			particle[j].exist		=	true;
			particle[j].zdec		=	0;
			particle[j].ov.x		=	pos->x + 100.f - 200.f * rnd();
			particle[j].ov.y		=	pos->y + 100.f - 200.f * rnd();
			particle[j].ov.z		=	pos->z + 100.f - 200.f * rnd();
			float speed				=	1.f;
			float fval				=	speed * ( 1.0f / 5 );
			particle[j].scale.x		=	particle[j].scale.y		=	particle[j].scale.z		=	10.f;
			particle[j].move.x		=	(speed - rnd()) * fval;
			particle[j].move.y		=	(speed - speed * rnd()) * ( 1.0f / 15 );
			particle[j].move.z		=	(speed - rnd()) * fval;
			particle[j].scale.x		=	particle[j].scale.y		=	8;
			particle[j].timcreation	=	static_cast<long>(arxtime.get_updated());
			particle[j].tolive		=	4500 + (unsigned long)(rnd() * 4500);
			particle[j].tc			=	TC_smoke;
			particle[j].siz			=	(80 + rnd() * 80 * 2.f) * ( 1.0f / 3 );
			particle[j].rgb = Color3f(rnd() * (1.f/3), 1.f, rnd() * (1.f/10));
			particle[j].fparam		=	0.001f;
		}
	}
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_Render() {
	
	if (arxtime.is_paused()) return;

	int iDiv = 4 - config.video.levelOfDetail;

	float flDiv = static_cast<float>(1 << iDiv);

	for (long i = 0; i < MAX_FOG; i++)
	{
		if (fogs[i].exist)
		{
			float fval;

			long count = checked_range_cast<long>(FrameDiff / flDiv);

			if (count < 1) count = 1;

			while (count)
			{
				count--;
				long j = ARX_PARTICLES_GetFree();

				if ((j != -1) && (rnd() * 2000.f < (float)fogs[i].frequency))
				{
					ParticleCount++;
					particle[j].special = FADE_IN_AND_OUT | ROTATING | MODULATE_ROTATION | DISSIPATING;
					particle[j].exist = true;
					particle[j].zdec = 0;

					if (fogs[i].special & FOG_DIRECTIONAL)
					{
						particle[j].ov.x = fogs[i].pos.x;
						particle[j].ov.y = fogs[i].pos.y;
						particle[j].ov.z = fogs[i].pos.z;
						fval = fogs[i].speed * ( 1.0f / 10 );
						particle[j].move.x = (fogs[i].move.x * fval);
						particle[j].move.y = (fogs[i].move.y * fval);
						particle[j].move.z = (fogs[i].move.z * fval);
					}
					else
					{
						particle[j].ov.x = fogs[i].pos.x + 100.f - 200.f * rnd();
						particle[j].ov.y = fogs[i].pos.y + 100.f - 200.f * rnd();
						particle[j].ov.z = fogs[i].pos.z + 100.f - 200.f * rnd();
						fval = fogs[i].speed * ( 1.0f / 5 );
						particle[j].move.x = (fogs[i].speed - rnd() * 2.f) * fval;
						particle[j].move.y = (fogs[i].speed - rnd() * 2.f) * ( 1.0f / 15 );
						particle[j].move.z = (fogs[i].speed - rnd() * 2.f) * fval;
					}

					particle[j].scale.x		=	particle[j].scale.y		=	particle[j].scale.z		=	fogs[i].scale;
					particle[j].timcreation	=	(long)arxtime;
					particle[j].tolive		=	fogs[i].tolive + (unsigned long)(rnd() * fogs[i].tolive);
					particle[j].tc			=	TC_smoke;
					particle[j].siz			=	(fogs[i].size + rnd() * fogs[i].size * 2.f) * ( 1.0f / 3 );
					particle[j].rgb = fogs[i].rgb;
					particle[j].fparam		=	fogs[i].rotatespeed;
				}

				fogs[i].lastupdate = (unsigned long)(arxtime); 
			}
		}
	}
}
//*************************************************************************************
//*************************************************************************************
void ARX_FOGS_RenderAll()
{
	Anglef angle = Anglef::ZERO;

	GRenderer->SetRenderState(Renderer::AlphaBlending, false);

	for (long i = 0; i < MAX_FOG; i++)
	{
		if (fogs[i].exist)
		{
			if (fogobj)
				DrawEERIEInter(fogobj, &angle, &fogs[i].pos, NULL);

			fogs[i].bboxmin = BBOXMIN;
			fogs[i].bboxmax = BBOXMAX;

			if(fogs[i].special & FOG_DIRECTIONAL) {
				EERIEDraw3DLine(fogs[i].pos, fogs[i].pos + fogs[i].move * 50.f, Color::white); 
			}

			if(fogs[i].selected) {
				EERIEDraw2DLine(fogs[i].bboxmin.x, fogs[i].bboxmin.y, fogs[i].bboxmax.x, fogs[i].bboxmin.y, 0.01f, Color::yellow);
				EERIEDraw2DLine(fogs[i].bboxmax.x, fogs[i].bboxmin.y, fogs[i].bboxmax.x, fogs[i].bboxmax.y, 0.01f, Color::yellow);
				EERIEDraw2DLine(fogs[i].bboxmax.x, fogs[i].bboxmax.y, fogs[i].bboxmin.x, fogs[i].bboxmax.y, 0.01f, Color::yellow);
				EERIEDraw2DLine(fogs[i].bboxmin.x, fogs[i].bboxmax.y, fogs[i].bboxmin.x, fogs[i].bboxmin.y, 0.01f, Color::yellow);
			}
		}
	}
}
