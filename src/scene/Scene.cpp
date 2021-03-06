/*
 * Copyright 2011-2012 Arx Libertatis Team (see the AUTHORS file)
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

#include "scene/Scene.h"

#include <cstdio>

#include "ai/Paths.h"

#include "animation/Animation.h"

#include "core/Application.h"
#include "core/GameTime.h"
#include "core/Core.h"

#include "game/EntityManager.h"
#include "game/Inventory.h"
#include "game/Player.h"
#include "game/Spells.h"

#include "gui/Interface.h"

#include "graphics/Draw.h"
#include "graphics/GraphicsModes.h"
#include "graphics/Math.h"
#include "graphics/VertexBuffer.h"
#include "graphics/data/TextureContainer.h"
#include "graphics/effects/DrawEffects.h"
#include "graphics/effects/Halo.h"
#include "graphics/particle/ParticleEffects.h"
#include "graphics/texture/TextureStage.h"
#include "graphics/GraphicsUtility.h"

#include "input/Input.h"

#include "io/log/Logger.h"

#include "scene/Light.h"
#include "scene/Interactive.h"

using std::vector;

//-----------------------------------------------------------------------------
extern EERIE_3DOBJ * eyeballobj;
//-----------------------------------------------------------------------------
extern TextureContainer *enviro;
extern long ZMAPMODE;
extern Color ulBKGColor;

EERIE_PORTAL_DATA * portals = NULL;

static float WATEREFFECT = 0.f;

CircularVertexBuffer<SMY_VERTEX3> * pDynamicVertexBuffer;

namespace {

struct DynamicVertexBuffer {
	
private:
	
	SMY_VERTEX3 * vertices;
	size_t start;
	
public:
	
	size_t nbindices;
	unsigned short * indices;
	size_t offset;
	
	DynamicVertexBuffer() : vertices(NULL), nbindices(0), indices(NULL) { }
	
	void lock() {
		
		arx_assert(!vertices);
		
		if(!indices) {
			indices = new unsigned short[4 * pDynamicVertexBuffer->vb->capacity()];
			start = 0;
		}
		
		BufferFlags flags = (pDynamicVertexBuffer->pos == 0) ? DiscardBuffer : NoOverwrite | DiscardRange;
		
		vertices =  pDynamicVertexBuffer->vb->lock(flags, pDynamicVertexBuffer->pos);
		offset = 0;
	}
	
	SMY_VERTEX3 * append(size_t nbvertices) {
		
		arx_assert(vertices);
		
		if(pDynamicVertexBuffer->pos + nbvertices > pDynamicVertexBuffer->vb->capacity()) {
			return NULL;
		}
		
		SMY_VERTEX3 * pos = vertices + offset;
		
		pDynamicVertexBuffer->pos += nbvertices, offset += nbvertices;
		
		return pos;
	}
	
	void unlock() {
		arx_assert(vertices);
		pDynamicVertexBuffer->vb->unlock(), vertices = NULL;
	}
	
	void draw(Renderer::Primitive primitive) {
		arx_assert(!vertices);
		pDynamicVertexBuffer->vb->drawIndexed(primitive, pDynamicVertexBuffer->pos - start, start, indices, nbindices);
	}
	
	void done() {
		arx_assert(!vertices);
		start = pDynamicVertexBuffer->pos;
		nbindices = 0;
	}
	
	void reset() {
		arx_assert(!vertices);
		start = pDynamicVertexBuffer->pos = 0;
		nbindices = 0;
	}
	
	~DynamicVertexBuffer() {
		delete[] indices;
	}
	
} dynamicVertices;

}

EERIE_FRUSTRUM_PLANE efpPlaneNear;

static vector<EERIEPOLY*> vPolyWater;
static vector<EERIEPOLY*> vPolyLava;

void PopAllTriangleListTransparency();

std::vector<PORTAL_ROOM_DRAW> RoomDraw;
std::vector<long> RoomDrawList;

//*************************************************************************************
//*************************************************************************************
void ApplyWaterFXToVertex(Vec3f * odtv,TexturedVertex * dtv,float power)
{
	power=power*0.05f;
	dtv->uv.x+=EEsin((WATEREFFECT+odtv->x))*power;
	dtv->uv.y+=EEcos((WATEREFFECT+odtv->z))*power;
}

static void ApplyLavaGlowToVertex(Vec3f * odtv,TexturedVertex * dtv, float power) {
	float f;
	long lr, lg, lb;
	power = 1.f - (EEsin((WATEREFFECT+odtv->x+odtv->z)) * 0.05f) * power;
	f = ((dtv->color >> 16) & 255) * power;
	lr = clipByte(f);

	f = ((dtv->color >> 8) & 255) * power;
	lg = clipByte(f);

	f = ((dtv->color) & 255) * power;
	lb = clipByte(f);

	dtv->color = (0xFF000000L | (lr << 16) | (lg << 8) | (lb));
}

void ManageWater_VertexBuffer(EERIEPOLY * ep, const long to, const unsigned long tim, SMY_VERTEX * _pVertex) {
	
	for (long k=0;k<to;k++) 
	{
		ep->tv[k].uv = ep->v[k].uv;
		
		ApplyWaterFXToVertex(&ep->v[k].p,&ep->tv[k],0.35f);
			
		if(ep->type&POLY_FALL)
		{
			ep->tv[k].uv.y-=(float)(tim)*( 1.0f / 1000 );
		}
		
		_pVertex[ep->uslInd[k]].uv = ep->tv[k].uv;
	}
}

void ManageLava_VertexBuffer(EERIEPOLY * ep, const long to, const unsigned long tim, SMY_VERTEX * _pVertex) {
	
	for (long k=0;k<to;k++) 
	{
		ep->tv[k].uv = ep->v[k].uv;
		
		ApplyWaterFXToVertex(&ep->v[k].p, &ep->tv[k], 0.35f); //0.25f
		ApplyLavaGlowToVertex(&ep->v[k].p, &ep->tv[k], 0.6f);
			
		if(ep->type&POLY_FALL)
		{
			ep->tv[k].uv.y-=(float)(tim)*( 1.0f / 12000 );
		}
		
		_pVertex[ep->uslInd[k]].uv = ep->tv[k].uv;
	}
}

void EERIERTPPoly2(EERIEPOLY *ep)
{
	EE_RTP(&ep->v[0],&ep->tv[0]);
	EE_RTP(&ep->v[1],&ep->tv[1]);
	EE_RTP(&ep->v[2],&ep->tv[2]);

	if(ep->type & POLY_QUAD)
		EE_RTP(&ep->v[3],&ep->tv[3]);
	else
		ep->tv[3].p.z=1.f;
}

bool IsSphereInFrustrum(float radius, const Vec3f *point, const EERIE_FRUSTRUM *frustrum);
bool FrustrumsClipSphere(EERIE_FRUSTRUM_DATA * frustrums,EERIE_SPHERE * sphere)
{
	float dists=sphere->origin.x*efpPlaneNear.a + sphere->origin.y*efpPlaneNear.b + sphere->origin.z*efpPlaneNear.c + efpPlaneNear.d;

	if (dists+sphere->radius>0)
	{	
		for (long i=0;i<frustrums->nb_frustrums;i++)
		{
			if (IsSphereInFrustrum(sphere->radius, &sphere->origin, &frustrums->frustrums[i]))
				return false;
		}
	}

	return true;
}

bool VisibleSphere(float x, float y, float z, float radius) {
	
	Vec3f pos(x, y, z);
	if(distSqr(pos, ACTIVECAM->orgTrans.pos) > square(ACTIVECAM->cdepth*0.5f + radius))
		return false;

	long room_num = ARX_PORTALS_GetRoomNumForPosition(&pos);

	if (room_num>=0)
	{
		EERIE_SPHERE sphere;
		sphere.origin = pos;
		sphere.radius = radius;
							
		EERIE_FRUSTRUM_DATA * frustrums=&RoomDraw[room_num].frustrum;

		if (FrustrumsClipSphere(frustrums,&sphere))
			return false;
	}

	return true;
}
bool IsInFrustrum(Vec3f * point,EERIE_FRUSTRUM * frustrum);

bool IsBBoxInFrustrum(EERIE_3D_BBOX * bbox, EERIE_FRUSTRUM * frustrum) {
	
	Vec3f point = bbox->min;
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->max.x, bbox->min.y, bbox->min.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->max.x, bbox->max.y, bbox->min.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->min.x, bbox->max.y, bbox->min.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->min.x, bbox->min.y, bbox->max.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->max.x, bbox->min.y, bbox->max.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = bbox->max;
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	point = Vec3f(bbox->min.x, bbox->max.y, bbox->max.z);
	if(IsInFrustrum(&point, frustrum)) {
		return true;
	}
	
	return	false;
}

bool FrustrumsClipBBox3D(EERIE_FRUSTRUM_DATA * frustrums,EERIE_3D_BBOX * bbox)
{
	for (long i=0;i<frustrums->nb_frustrums;i++)
	{
		if (IsBBoxInFrustrum(bbox,&frustrums->frustrums[i]))
			return false;
	}

	return false;
}

bool ARX_SCENE_PORTAL_Basic_ClipIO(Entity * io) {
	arx_assert(io);
	if(EDITMODE || io == entities.player() || (io->ioflags & IO_FORCEDRAW)) {
		return false;
	}
	
	if(!(USE_PORTALS && portals))
		return false;

	Vec3f posi = io->pos;
	posi.y -= 20.f;

	if(io->room_flags & 1)
		UpdateIORoom(io);

	long room_num = io->room;

	if(room_num == -1) {
		posi.y = io->pos.y-120;
		room_num=ARX_PORTALS_GetRoomNumForPosition(&posi);
	}

	if(room_num >= 0 && size_t(room_num) < RoomDraw.size() && RoomDraw[room_num].count) {
		float yOffset = 0.f;
		float radius = 0.f;
		if(io->ioflags & IO_ITEM) {
			yOffset = -40.f;
			if(io->ioflags & IO_MOVABLE)
				radius = 160.f;
			else
				radius = 75.f;
		} else if(io->ioflags & IO_FIX) {
			yOffset = -60.f;
			radius = 340.f;
		} else if(io->ioflags & IO_NPC) {
			yOffset = -120.f;
			radius = 120.f;
		}

		EERIE_SPHERE sphere;

		if(radius != 0.f) {
			sphere.origin.x=io->pos.x;
			sphere.origin.y=io->pos.y + yOffset;
			sphere.origin.z=io->pos.z;
			sphere.radius=radius;
		}

		EERIE_FRUSTRUM_DATA * frustrums=&RoomDraw[room_num].frustrum;

		if (FrustrumsClipSphere(frustrums,&sphere)) {
			io->bbox1.x=(short)-1;
			io->bbox2.x=(short)-1;
			io->bbox1.y=(short)-1;
			io->bbox2.y=(short)-1;
			return true;
		}
	}
	return false;
}

// USAGE/FUNCTION
//   io can be NULL if io is valid io->bbox3D contains 3D world-bbox
//   bboxmin & bboxmax ARE in fact 2D-screen BBOXes using only (x,y).
// RETURN:
//   return true if IO cannot be seen, false if visible
// TODO:
//   Implement all Portal Methods
//   Return a reduced clipbox which can be used for polys clipping in the case of partial visibility
bool ARX_SCENE_PORTAL_ClipIO(Entity * io, Vec3f * position) {
	
	if(EDITMODE)
		return false;

	if(io==entities.player())
		return false;

	if(io && (io->ioflags & IO_FORCEDRAW))
		return false;

	if(USE_PORTALS && portals) {
		Vec3f posi;
		posi.x=position->x;
		posi.y=position->y-60; //20
		posi.z=position->z;
		long room_num;

		if(io) {
			if(io->room_flags & 1)
				UpdateIORoom(io);

			room_num = io->room;//
		} else {
			room_num = ARX_PORTALS_GetRoomNumForPosition(&posi);
		}

		if(room_num == -1) {
			posi.y = position->y - 120;
			room_num = ARX_PORTALS_GetRoomNumForPosition(&posi);
		}

		if(room_num >= 0 && size_t(room_num) < RoomDraw.size()) {
			if(RoomDraw[room_num].count == 0) {
				if(io) {
					io->bbox1.x=(short)-1;
					io->bbox2.x=(short)-1;
					io->bbox1.y=(short)-1;
					io->bbox2.y=(short)-1;		
				}
				return true;
			}

			if(io) {
				EERIE_SPHERE sphere;
				sphere.origin = (io->bbox3D.min + io->bbox3D.max) * .5f;
				sphere.radius = dist(sphere.origin, io->bbox3D.min) + 10.f;

				EERIE_FRUSTRUM_DATA *frustrums = &RoomDraw[room_num].frustrum;

				if(FrustrumsClipSphere(frustrums, &sphere) ||
				   FrustrumsClipBBox3D(frustrums, &io->bbox3D)
				) {
					io->bbox1.x=(short)-1;
					io->bbox2.x=(short)-1;
					io->bbox1.y=(short)-1;
					io->bbox2.y=(short)-1;
					return true;
				}
			}
		}
	}

	return false;
}

long ARX_PORTALS_GetRoomNumForPosition2(Vec3f * pos,long flag,float * height)
{
	EERIEPOLY * ep; 

	if(flag & 1) {
		ep=CheckInPoly(pos->x,pos->y-150.f,pos->z);

		if (!ep)
			ep=CheckInPoly(pos->x,pos->y-1.f,pos->z);
	} else {
		ep=CheckInPoly(pos->x,pos->y,pos->z);
	}

	if(ep && ep->room>-1) {
		if(height)
			*height=ep->center.y;

		return ep->room;
	}

	// Security... ?
	ep=GetMinPoly(pos->x,pos->y,pos->z);

	if(ep && ep->room > -1) {
		if(height)
			*height=ep->center.y;

		return ep->room;
	} else if( !(flag & 1) ) {
		ep=CheckInPoly(pos->x,pos->y,pos->z);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}
	}

	if(flag & 2) {
		float off=20.f;
		ep=CheckInPoly(pos->x-off,pos->y-off,pos->z);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		ep=CheckInPoly(pos->x-off,pos->y-20,pos->z-off);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		ep=CheckInPoly(pos->x-off,pos->y-20,pos->z+off);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		ep=CheckInPoly(pos->x+off,pos->y-20,pos->z);

		if(ep && ep->room>-1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		ep=CheckInPoly(pos->x+off,pos->y-20,pos->z+off);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		ep=CheckInPoly(pos->x+off,pos->y-20,pos->z-off);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}
	}

	return -1;
}
long ARX_PORTALS_GetRoomNumForCamera(float * height)
{
	EERIEPOLY * ep; 
	ep = CheckInPoly(ACTIVECAM->orgTrans.pos.x,ACTIVECAM->orgTrans.pos.y,ACTIVECAM->orgTrans.pos.z);

	if(ep && ep->room > -1) {
		if(height)
			*height=ep->center.y;

		return ep->room;
	}

	ep = GetMinPoly(ACTIVECAM->orgTrans.pos.x,ACTIVECAM->orgTrans.pos.y,ACTIVECAM->orgTrans.pos.z);

	if(ep && ep->room > -1) {
		if(height)
			*height=ep->center.y;

		return ep->room;
	}

	float dist=0.f;

	while(dist<=20.f) {
		float vvv=radians(ACTIVECAM->angle.b);
		ep=CheckInPoly(	ACTIVECAM->orgTrans.pos.x+EEsin(vvv)*dist,
								ACTIVECAM->orgTrans.pos.y,
								ACTIVECAM->orgTrans.pos.z-EEcos(vvv)*dist);

		if(ep && ep->room > -1) {
			if(height)
				*height=ep->center.y;

			return ep->room;
		}

		dist += 5.f;
	}

	return -1;
}

// flag==1 for player
long ARX_PORTALS_GetRoomNumForPosition(Vec3f * pos,long flag)
{
	long num;
	float height;

	if(flag & 1)
		num=ARX_PORTALS_GetRoomNumForCamera(&height);
	else
		num=ARX_PORTALS_GetRoomNumForPosition2(pos,flag,&height);

	if(num > -1) {
		long nearest = -1;
		float nearest_dist = 99999.f;

		for(long n = 0; n < portals->nb_rooms; n++) {
			for(long lll = 0; lll < portals->room[n].nb_portals; lll++) {
				EERIE_PORTALS *po = &portals->portals[portals->room[n].portals[lll]];
				EERIEPOLY *epp = &po->poly;

				if(PointIn2DPolyXZ(epp, pos->x, pos->z)) {
					float yy;

					if(GetTruePolyY(epp,pos,&yy)) {
						if(height > yy) {
							if(yy >= pos->y && yy-pos->y < nearest_dist) {
								if(epp->norm.y>0)
									nearest = po->room_2;
								else
									nearest = po->room_1;

								nearest_dist = yy - pos->y;
							}
						}
					}
				}
			}
		}

		if(nearest>-1)
			num = nearest;
	}
	
	return num;
}
			
void ARX_PORTALS_InitDrawnRooms()
{
	arx_assert(portals);

	EERIE_PORTALS *ep = &portals->portals[0];

	for(long i=0;i<portals->nb_total;i++) {
		ep->useportal=0;
		ep++;
	}

	RoomDraw.resize(portals->nb_rooms + 1);

	for(size_t i = 0; i < RoomDraw.size(); i++) {
		RoomDraw[i].count=0;
		RoomDraw[i].flags=0;
		RoomDraw[i].frustrum.nb_frustrums=0;
	}

	vPolyWater.clear();
	vPolyLava.clear();

	if(pDynamicVertexBuffer) {
		pDynamicVertexBuffer->vb->setData(NULL, 0, 0, DiscardBuffer);
		dynamicVertices.reset();
	}
}

bool IsInFrustrum(Vec3f * point, EERIE_FRUSTRUM *frustrum)
{
	if (	((point->x*frustrum->plane[0].a + point->y*frustrum->plane[0].b + point->z*frustrum->plane[0].c + frustrum->plane[0].d)>0)
		&&	((point->x*frustrum->plane[1].a + point->y*frustrum->plane[1].b + point->z*frustrum->plane[1].c + frustrum->plane[1].d)>0)
		&&	((point->x*frustrum->plane[2].a + point->y*frustrum->plane[2].b + point->z*frustrum->plane[2].c + frustrum->plane[2].d)>0)
		&&	((point->x*frustrum->plane[3].a + point->y*frustrum->plane[3].b + point->z*frustrum->plane[3].c + frustrum->plane[3].d)>0) )
		return true;

	return false;
}


bool IsSphereInFrustrum(float radius, const Vec3f *point, const EERIE_FRUSTRUM *frustrum)
{
	float dists[4];
	dists[0]=point->x*frustrum->plane[0].a + point->y*frustrum->plane[0].b + point->z*frustrum->plane[0].c + frustrum->plane[0].d;
	dists[1]=point->x*frustrum->plane[1].a + point->y*frustrum->plane[1].b + point->z*frustrum->plane[1].c + frustrum->plane[1].d;
	dists[2]=point->x*frustrum->plane[2].a + point->y*frustrum->plane[2].b + point->z*frustrum->plane[2].c + frustrum->plane[2].d;
	dists[3]=point->x*frustrum->plane[3].a + point->y*frustrum->plane[3].b + point->z*frustrum->plane[3].c + frustrum->plane[3].d;

	if (	(dists[0]+radius>0)
		&&	(dists[1]+radius>0)
		&&	(dists[2]+radius>0)
		&&	(dists[3]+radius>0) )
		return true;

	return false;
	
}

bool FrustrumsClipPoly(EERIE_FRUSTRUM_DATA *frustrums, EERIEPOLY *ep){
	for(long i=0; i<frustrums->nb_frustrums; i++) {
		if(IsSphereInFrustrum(ep->v[0].rhw, &ep->center, &frustrums->frustrums[i]))
			return false;
	}

	return true;
}

void Frustrum_Set(EERIE_FRUSTRUM * fr,long plane,float a,float b,float c,float d)
{
	fr->plane[plane].a=a;
	fr->plane[plane].b=b;
	fr->plane[plane].c=c;
	fr->plane[plane].d=d;
}

void CreatePlane(EERIE_FRUSTRUM * frustrum,long numplane,Vec3f * orgn,Vec3f * pt1,Vec3f * pt2)
{
	float Ax, Ay, Az, Bx, By, Bz, epnlen;
	Ax=pt1->x-orgn->x;
	Ay=pt1->y-orgn->y;
	Az=pt1->z-orgn->z;

	Bx=pt2->x-orgn->x;
	By=pt2->y-orgn->y;
	Bz=pt2->z-orgn->z;

	frustrum->plane[numplane].a=Ay*Bz-Az*By;
	frustrum->plane[numplane].b=Az*Bx-Ax*Bz;
	frustrum->plane[numplane].c=Ax*By-Ay*Bx;

	epnlen = (float)sqrt(	frustrum->plane[numplane].a * frustrum->plane[numplane].a
						+	frustrum->plane[numplane].b * frustrum->plane[numplane].b
						+	frustrum->plane[numplane].c * frustrum->plane[numplane].c	);
	epnlen=1.f/epnlen;
	frustrum->plane[numplane].a*=epnlen;
	frustrum->plane[numplane].b*=epnlen;
	frustrum->plane[numplane].c*=epnlen;
	frustrum->plane[numplane].d=-(	orgn->x * frustrum->plane[numplane].a +
									orgn->y * frustrum->plane[numplane].b +
									orgn->z * frustrum->plane[numplane].c		);

	
}

void CreateFrustrum(EERIE_FRUSTRUM *frustrum, EERIEPOLY *ep, bool cull) {
	if(cull) {
		CreatePlane(frustrum, 0, &ACTIVECAM->orgTrans.pos, &ep->v[0].p, &ep->v[1].p);
		CreatePlane(frustrum, 1, &ACTIVECAM->orgTrans.pos, &ep->v[3].p, &ep->v[2].p);
		CreatePlane(frustrum, 2, &ACTIVECAM->orgTrans.pos, &ep->v[1].p, &ep->v[3].p);
		CreatePlane(frustrum, 3, &ACTIVECAM->orgTrans.pos, &ep->v[2].p, &ep->v[0].p);
	} else {
		CreatePlane(frustrum, 0, &ACTIVECAM->orgTrans.pos, &ep->v[1].p, &ep->v[0].p);
		CreatePlane(frustrum, 1, &ACTIVECAM->orgTrans.pos, &ep->v[2].p, &ep->v[3].p);
		CreatePlane(frustrum, 2, &ACTIVECAM->orgTrans.pos, &ep->v[3].p, &ep->v[1].p);
		CreatePlane(frustrum, 3, &ACTIVECAM->orgTrans.pos, &ep->v[0].p, &ep->v[2].p);
	}
}



void CreateScreenFrustrum(EERIE_FRUSTRUM * frustrum) {
	
	EERIEMATRIX tempViewMatrix;
	Util_SetViewMatrix(tempViewMatrix, ACTIVECAM->orgTrans);
	GRenderer->SetViewMatrix(tempViewMatrix);
	
	EERIEMATRIX matProj;
	GRenderer->GetProjectionMatrix(matProj);
	
	EERIEMATRIX matView;
	GRenderer->GetViewMatrix(matView);
	
	EERIEMATRIX matres;
	MatrixMultiply(&matres, &matView, &matProj);

	float a,b,c,d,n;
	a=matres._14-matres._11;
	b=matres._24-matres._21;
	c=matres._34-matres._31;
	d=matres._44-matres._41;
 b=-b;
	n = (float)(1.f /sqrt(a*a+b*b+c*c));

	Frustrum_Set(frustrum,0,a*n,b*n,c*n,d*n);
	a=matres._14+matres._11;
	b=matres._24+matres._21;
	c=matres._34+matres._31;
	d=matres._44+matres._41;
 b=-b;
	n = (float)(1.f/sqrt(a*a+b*b+c*c));

	Frustrum_Set(frustrum,1,a*n,b*n,c*n,d*n);
	a=matres._14-matres._12;
	b=matres._24-matres._22;
	c=matres._34-matres._32;
	d=matres._44-matres._42;
 b=-b;
	n = (float)(1.f/sqrt(a*a+b*b+c*c));

	Frustrum_Set(frustrum,2,a*n,b*n,c*n,d*n);
	a=matres._14+matres._12;
	b=matres._24+matres._22;
	c=matres._34+matres._32;
	d=matres._44+matres._42;
 b=-b;
	n = (float)(1.f/sqrt(a*a+b*b+c*c));

	Frustrum_Set(frustrum,3,a*n,b*n,c*n,d*n);

	a=matres._14+matres._13;
	b=matres._24+matres._23;
	c=matres._34+matres._33;
	d=matres._44+matres._43;
 b=-b;
	n = (float)(1.f/sqrt(a*a+b*b+c*c));
	efpPlaneNear.a=a*n;
	efpPlaneNear.b=b*n;
	efpPlaneNear.c=c*n;
	efpPlaneNear.d=d*n;
}

void RoomDrawRelease() {
	RoomDrawList.resize(0);
	RoomDraw.resize(0);
}

void RoomFrustrumAdd(long num, const EERIE_FRUSTRUM * fr)
{
	if (RoomDraw[num].frustrum.nb_frustrums<MAX_FRUSTRUMS-1)
	{
		memcpy(&RoomDraw[num].frustrum.frustrums
			[RoomDraw[num].frustrum.nb_frustrums],fr,sizeof(EERIE_FRUSTRUM));		
		RoomDraw[num].frustrum.nb_frustrums++;
		
	}	
}

static void RenderWaterBatch() {
	
	if(!dynamicVertices.nbindices) {
		return;
	}
	
	GRenderer->GetTextureStage(1)->SetColorOp(TextureStage::OpModulate4X, TextureStage::ArgTexture, TextureStage::ArgCurrent);
	GRenderer->GetTextureStage(1)->DisableAlpha();
	
	GRenderer->GetTextureStage(2)->SetColorOp(TextureStage::OpModulate, TextureStage::ArgTexture, TextureStage::ArgCurrent);
	GRenderer->GetTextureStage(2)->DisableAlpha();
	
	dynamicVertices.draw(Renderer::TriangleList);
	
	GRenderer->GetTextureStage(1)->DisableColor();
	GRenderer->GetTextureStage(2)->DisableColor();
	
}

static void RenderWater() {
	
	if(vPolyWater.empty()) {
		return;
	}
	
	size_t iNbIndice = 0;
	int iNb = vPolyWater.size();
	
	dynamicVertices.lock();
	
	GRenderer->SetBlendFunc(Renderer::BlendDstColor, Renderer::BlendOne);
	GRenderer->SetTexture(0, enviro);
	GRenderer->SetTexture(2, enviro);
	
	unsigned short * indices = dynamicVertices.indices;
	
	while(iNb--) {
		EERIEPOLY * ep = vPolyWater[iNb];
		
		unsigned short iNbVertex = (ep->type & POLY_QUAD) ? 4 : 3;
		SMY_VERTEX3 * pVertex = dynamicVertices.append(iNbVertex);
		
		if(!pVertex) {
			dynamicVertices.unlock();
			RenderWaterBatch();
			dynamicVertices.reset();
			dynamicVertices.lock();
			iNbIndice = 0;
			indices = dynamicVertices.indices;
			pVertex = dynamicVertices.append(iNbVertex);
		}
		
		pVertex->p.x = ep->v[0].p.x;
		pVertex->p.y = -ep->v[0].p.y;
		pVertex->p.z = ep->v[0].p.z;
		pVertex->color = 0xFF505050;
		float fTu = ep->v[0].p.x*(1.f/1000) + sin(ep->v[0].p.x*(1.f/200) + arxtime.get_frame_time()*(1.f/1000)) * (1.f/32);
		float fTv = ep->v[0].p.z*(1.f/1000) + cos(ep->v[0].p.z*(1.f/200) + arxtime.get_frame_time()*(1.f/1000)) * (1.f/32);
		if(ep->type & POLY_FALL) {
			fTv += arxtime.get_frame_time() * (1.f/4000);
		}
		pVertex->uv[0].x = fTu;
		pVertex->uv[0].y = fTv;
		fTu = (ep->v[0].p.x + 30.f)*(1.f/1000) + sin((ep->v[0].p.x + 30)*(1.f/200) + arxtime.get_frame_time()*(1.f/1000))*(1.f/28);
		fTv = (ep->v[0].p.z + 30.f)*(1.f/1000) - cos((ep->v[0].p.z + 30)*(1.f/200) + arxtime.get_frame_time()*(1.f/1000))*(1.f/28);
		if (ep->type & POLY_FALL) {
			fTv += arxtime.get_frame_time() * (1.f/4000);
		}
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=(ep->v[0].p.x+60.f)*( 1.0f / 1000 )-EEsin((ep->v[0].p.x+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		fTv=(ep->v[0].p.z+60.f)*( 1.0f / 1000 )-EEcos((ep->v[0].p.z+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		
		if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		
		pVertex++;
		pVertex->p.x=ep->v[1].p.x;
		pVertex->p.y=-ep->v[1].p.y;
		pVertex->p.z=ep->v[1].p.z;
		pVertex->color=0xFF505050;
		fTu=ep->v[1].p.x*( 1.0f / 1000 )+EEsin((ep->v[1].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
		fTv=ep->v[1].p.z*( 1.0f / 1000 )+EEcos((ep->v[1].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
		
		if(ep->type&POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[0].x=fTu;
		pVertex->uv[0].y=fTv;
		fTu=(ep->v[1].p.x+30.f)*( 1.0f / 1000 )+EEsin((ep->v[1].p.x+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
		fTv=(ep->v[1].p.z+30.f)*( 1.0f / 1000 )-EEcos((ep->v[1].p.z+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
		
		if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=(ep->v[1].p.x+60.f)*( 1.0f / 1000 )-EEsin((ep->v[1].p.x+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		fTv=(ep->v[1].p.z+60.f)*( 1.0f / 1000 )-EEcos((ep->v[1].p.z+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		
		if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		pVertex++;
		pVertex->p.x=ep->v[2].p.x;
		pVertex->p.y=-ep->v[2].p.y;
		pVertex->p.z=ep->v[2].p.z;
		pVertex->color=0xFF505050;
		fTu=ep->v[2].p.x*( 1.0f / 1000 )+EEsin((ep->v[2].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
		fTv=ep->v[2].p.z*( 1.0f / 1000 )+EEcos((ep->v[2].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
		
		if(ep->type&POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[0].x=fTu;
		pVertex->uv[0].y=fTv;
		fTu=(ep->v[2].p.x+30.f)*( 1.0f / 1000 )+EEsin((ep->v[2].p.x+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
		fTv=(ep->v[2].p.z+30.f)*( 1.0f / 1000 )-EEcos((ep->v[2].p.z+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
		
		if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=(ep->v[2].p.x+60.f)*( 1.0f / 1000 )-EEsin((ep->v[2].p.x+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		fTv=(ep->v[2].p.z+60.f)*( 1.0f / 1000 )-EEcos((ep->v[2].p.z+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
		
		if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		pVertex++;
		
		*indices++ = iNbIndice++; 
		*indices++ = iNbIndice++; 
		*indices++ = iNbIndice++; 
		dynamicVertices.nbindices += 3;
		
		if(iNbVertex == 4)
		{
			pVertex->p.x=ep->v[3].p.x;
			pVertex->p.y=-ep->v[3].p.y;
			pVertex->p.z=ep->v[3].p.z;
			pVertex->color=0xFF505050;
			fTu=ep->v[3].p.x*( 1.0f / 1000 )+EEsin((ep->v[3].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
			fTv=ep->v[3].p.z*( 1.0f / 1000 )+EEcos((ep->v[3].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 32 );
			
			if(ep->type&POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
			
			pVertex->uv[0].x=fTu;
			pVertex->uv[0].y=fTv;
			fTu=(ep->v[3].p.x+30.f)*( 1.0f / 1000 )+EEsin((ep->v[3].p.x+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
			fTv=(ep->v[3].p.z+30.f)*( 1.0f / 1000 )-EEcos((ep->v[3].p.z+30)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 28 );
			
			if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
			
			pVertex->uv[1].x=fTu;
			pVertex->uv[1].y=fTv;
			fTu=(ep->v[3].p.x+60.f)*( 1.0f / 1000 )-EEsin((ep->v[3].p.x+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
			fTv=(ep->v[3].p.z+60.f)*( 1.0f / 1000 )-EEcos((ep->v[3].p.z+60)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 1000 ))*( 1.0f / 40 );
			
			if (ep->type & POLY_FALL) fTv+=arxtime.get_frame_time()*( 1.0f / 4000 );
			
			pVertex->uv[2].x=fTu;
			pVertex->uv[2].y=fTv;
			pVertex++;
			
			*indices++ = iNbIndice++; 
			*indices++ = iNbIndice - 2; 
			*indices++ = iNbIndice - 3; 
			dynamicVertices.nbindices += 3;
		}
		
	}
	
	dynamicVertices.unlock();
	RenderWaterBatch();
	dynamicVertices.done();
	
	vPolyWater.clear();
	
}

void RenderLavaBatch() {
	
	GRenderer->SetBlendFunc(Renderer::BlendDstColor, Renderer::BlendOne);
	GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate2X, TextureStage::ArgTexture, TextureStage::ArgDiffuse);
	
	if(!dynamicVertices.nbindices) {
		return;
	}
	
	GRenderer->GetTextureStage(1)->SetColorOp(TextureStage::OpModulate4X, TextureStage::ArgTexture, TextureStage::ArgCurrent);
	GRenderer->GetTextureStage(1)->DisableAlpha();
	
	GRenderer->GetTextureStage(2)->SetColorOp(TextureStage::OpModulate, TextureStage::ArgTexture, TextureStage::ArgCurrent);
	GRenderer->GetTextureStage(2)->DisableAlpha();
	
	dynamicVertices.draw(Renderer::TriangleList);
	
	GRenderer->SetBlendFunc(Renderer::BlendZero, Renderer::BlendInvSrcColor);
	GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate);
	
	dynamicVertices.draw(Renderer::TriangleList);
	
	GRenderer->GetTextureStage(1)->DisableColor();
	GRenderer->GetTextureStage(2)->DisableColor();
	
}

void RenderLava() {
	
	if(vPolyLava.empty()) {
		return;
	}
	
	size_t iNbIndice = 0;
	int iNb=vPolyLava.size();
	
	dynamicVertices.lock();
	
	GRenderer->SetBlendFunc(Renderer::BlendDstColor, Renderer::BlendOne);
	GRenderer->SetTexture(0, enviro);
	GRenderer->SetTexture(2, enviro);
	
	unsigned short * indices = dynamicVertices.indices;
	
	while(iNb--) {
		EERIEPOLY * ep = vPolyLava[iNb];
		
		unsigned short iNbVertex = (ep->type & POLY_QUAD) ? 4 : 3;
		SMY_VERTEX3 * pVertex = dynamicVertices.append(iNbVertex);
		
		if(!pVertex) {
			dynamicVertices.unlock();
			RenderLavaBatch();
			dynamicVertices.reset();
			dynamicVertices.lock();
			iNbIndice = 0;
			indices = dynamicVertices.indices;
			pVertex = dynamicVertices.append(iNbVertex);
		}
		
		pVertex->p.x=ep->v[0].p.x;
		pVertex->p.y=-ep->v[0].p.y;
		pVertex->p.z=ep->v[0].p.z;
		pVertex->color=0xFF666666;
		float fTu=ep->v[0].p.x*( 1.0f / 1000 )+EEsin((ep->v[0].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		float fTv=ep->v[0].p.z*( 1.0f / 1000 )+EEcos((ep->v[0].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		pVertex->uv[0].x=fTu;
		pVertex->uv[0].y=fTv;
		fTu=ep->v[0].p.x*( 1.0f / 1000 )+EEsin((ep->v[0].p.x)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		fTv=ep->v[0].p.z*( 1.0f / 1000 )+EEcos((ep->v[0].p.z)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=ep->v[0].p.x*( 1.0f / 600 )+EEsin((ep->v[0].p.x)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		fTv=ep->v[0].p.z*( 1.0f / 600 )+EEcos((ep->v[0].p.z)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		pVertex++;
		pVertex->p.x=ep->v[1].p.x;
		pVertex->p.y=-ep->v[1].p.y;
		pVertex->p.z=ep->v[1].p.z;
		pVertex->color=0xFF666666;
		fTu=ep->v[1].p.x*( 1.0f / 1000 )+EEsin((ep->v[1].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		fTv=ep->v[1].p.z*( 1.0f / 1000 )+EEcos((ep->v[1].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		pVertex->uv[0].x=fTu;
		pVertex->uv[0].y=fTv;
		fTu=ep->v[1].p.x*( 1.0f / 1000 )+EEsin((ep->v[1].p.x)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		fTv=ep->v[1].p.z*( 1.0f / 1000 )+EEcos((ep->v[1].p.z)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=ep->v[1].p.x*( 1.0f / 600 )+EEsin((ep->v[1].p.x)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		fTv=ep->v[1].p.z*( 1.0f / 600 )+EEcos((ep->v[1].p.z)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		pVertex++;
		pVertex->p.x=ep->v[2].p.x;
		pVertex->p.y=-ep->v[2].p.y;
		pVertex->p.z=ep->v[2].p.z;
		pVertex->color=0xFF666666;
		fTu=ep->v[2].p.x*( 1.0f / 1000 )+EEsin((ep->v[2].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		fTv=ep->v[2].p.z*( 1.0f / 1000 )+EEcos((ep->v[2].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
		pVertex->uv[0].x=fTu;
		pVertex->uv[0].y=fTv;
		fTu=ep->v[2].p.x*( 1.0f / 1000 )+EEsin((ep->v[2].p.x)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		fTv=ep->v[2].p.z*( 1.0f / 1000 )+EEcos((ep->v[2].p.z)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
		pVertex->uv[1].x=fTu;
		pVertex->uv[1].y=fTv;
		fTu=ep->v[2].p.x*( 1.0f / 600 )+EEsin((ep->v[2].p.x)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		fTv=ep->v[2].p.z*( 1.0f / 600 )+EEcos((ep->v[2].p.z)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
		
		pVertex->uv[2].x=fTu;
		pVertex->uv[2].y=fTv;
		pVertex++;
		
		*indices++ = iNbIndice++; 
		*indices++ = iNbIndice++; 
		*indices++ = iNbIndice++; 
		dynamicVertices.nbindices += 3;
		
		if(iNbVertex&4)
		{
			pVertex->p.x=ep->v[3].p.x;
			pVertex->p.y=-ep->v[3].p.y;
			pVertex->p.z=ep->v[3].p.z;
			pVertex->color=0xFF666666;
			fTu=ep->v[3].p.x*( 1.0f / 1000 )+EEsin((ep->v[3].p.x)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
			fTv=ep->v[3].p.z*( 1.0f / 1000 )+EEcos((ep->v[3].p.z)*( 1.0f / 200 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 20 );
			pVertex->uv[0].x=fTu;
			pVertex->uv[0].y=fTv;
			fTu=ep->v[3].p.x*( 1.0f / 1000 )+EEsin((ep->v[3].p.x)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
			fTv=ep->v[3].p.z*( 1.0f / 1000 )+EEcos((ep->v[3].p.z)*( 1.0f / 100 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 10 );
			pVertex->uv[1].x=fTu;
			pVertex->uv[1].y=fTv;
			fTu=ep->v[3].p.x*( 1.0f / 600 )+EEsin((ep->v[3].p.x)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
			fTv=ep->v[3].p.z*( 1.0f / 600 )+EEcos((ep->v[3].p.z)*( 1.0f / 160 )+arxtime.get_frame_time()*( 1.0f / 2000 ))*( 1.0f / 11 );
			
			pVertex->uv[2].x=fTu;
			pVertex->uv[2].y=fTv;
			pVertex++;
			
			*indices++ = iNbIndice++; 
			*indices++ = iNbIndice - 2; 
			*indices++ = iNbIndice - 3; 
			dynamicVertices.nbindices += 3;
		}
		
	}
	
	dynamicVertices.unlock();
	RenderLavaBatch();
	dynamicVertices.done();
	
	vPolyLava.clear();
	
}

void ARX_PORTALS_Frustrum_RenderRoom_TransparencyTSoftCull(long room_num);
void ARX_PORTALS_Frustrum_RenderRooms_TransparencyT() {
	
	arx_assert(USE_PORTALS);

	GRenderer->SetFogColor(Color::none);

	GRenderer->SetRenderState(Renderer::AlphaBlending, true);
	GRenderer->SetCulling(Renderer::CullNone);
	GRenderer->SetRenderState(Renderer::DepthWrite, false);

	GRenderer->SetAlphaFunc(Renderer::CmpGreater, .5f);
	
	for(size_t i = 0; i < RoomDrawList.size(); i++) {

		long room_num = RoomDrawList[i];

		if(!RoomDraw[room_num].count)
			continue;

		ARX_PORTALS_Frustrum_RenderRoom_TransparencyTSoftCull(room_num);
	}
	
	GRenderer->SetAlphaFunc(Renderer::CmpNotEqual, 0.f);

	RoomDrawList.clear();

	SetZBias(8);

	GRenderer->SetRenderState(Renderer::DepthWrite, false);

	//render all fx!!
	GRenderer->SetCulling(Renderer::CullCW);
	
	RenderWater();
	RenderLava();
	
	SetZBias(0);
	GRenderer->SetFogColor(ulBKGColor);
	GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate);
	GRenderer->SetRenderState(Renderer::AlphaBlending, false);
}

void ApplyDynLight_VertexBuffer_2(EERIEPOLY *ep,short x,short y,SMY_VERTEX *_pVertex,unsigned short _usInd0,unsigned short _usInd1,unsigned short _usInd2,unsigned short _usInd3);

TILE_LIGHTS tilelights[MAX_BKGX][MAX_BKGZ];

void InitTileLights()
{
	for (long j=0;j<MAX_BKGZ;j++)
	for (long i=0;i<MAX_BKGZ;i++)
	{
		tilelights[i][j].el=NULL;
		tilelights[i][j].max=0;
		tilelights[i][j].num=0;
	}
}

void ComputeTileLights(short x,short z)
{
	tilelights[x][z].num=0;
	float xx=((float)x+0.5f)*ACTIVEBKG->Xdiv;
	float zz=((float)z+0.5f)*ACTIVEBKG->Zdiv;

	for (long i=0;i<TOTPDL;i++)
	{
		if(closerThan(Vec2f(xx, zz), Vec2f(PDL[i]->pos.x, PDL[i]->pos.z), PDL[i]->fallend + 60.f)) {
			
			if (tilelights[x][z].num>=tilelights[x][z].max)
			{
				tilelights[x][z].max++;
				tilelights[x][z].el=(EERIE_LIGHT **)realloc(tilelights[x][z].el,sizeof(EERIE_LIGHT *)*(tilelights[x][z].max));
			}

			tilelights[x][z].el[tilelights[x][z].num]=PDL[i];
			tilelights[x][z].num++;
		}
	}
}

void ClearTileLights() {
	for(long j = 0; j < MAX_BKGZ; j++) for(long i = 0; i < MAX_BKGZ; i++) {
		tilelights[i][j].max = 0;
		tilelights[i][j].num = 0;
		free(tilelights[i][j].el), tilelights[i][j].el = NULL;
	}
}

void ARX_PORTALS_Frustrum_RenderRoomTCullSoft(long room_num, EERIE_FRUSTRUM_DATA *frustrums, long tim) {
	if(!RoomDraw[room_num].count)
		return;

	if(!portals->room[room_num].pVertexBuffer) {
		// No need to spam this for every frame as there will already be an
		// earlier warning
		LogDebug("no vertex data for room " << room_num);
		return;
	}

	SMY_VERTEX * pMyVertex = portals->room[room_num].pVertexBuffer->lock(NoOverwrite);

	unsigned short *pIndices=portals->room[room_num].pussIndice;

	EP_DATA *pEPDATA = &portals->room[room_num].epdata[0];

	for(long lll=0; lll<portals->room[room_num].nb_polys; lll++, pEPDATA++) {
		FAST_BKG_DATA *feg = &ACTIVEBKG->fastdata[pEPDATA->px][pEPDATA->py];

		if(!feg->treat) {
			long ix = std::max(pEPDATA->px - 1, 0);
			long ax = std::min(pEPDATA->px + 1, ACTIVEBKG->Xsize - 1);
			long iz = std::max(pEPDATA->py - 1, 0);
			long az = std::min(pEPDATA->py + 1, ACTIVEBKG->Zsize - 1);

			(void)checked_range_cast<short>(iz);
			(void)checked_range_cast<short>(ix);
			(void)checked_range_cast<short>(az);
			(void)checked_range_cast<short>(ax);

			for(long nz=iz; nz<=az; nz++)
			for(long nx=ix; nx<=ax; nx++) {
				FAST_BKG_DATA * feg2 = &ACTIVEBKG->fastdata[nx][nz];

				if(!feg2->treat) {
					feg2->treat=1;
					ComputeTileLights(static_cast<short>(nx), static_cast<short>(nz));
				}
			}
		}

		EERIEPOLY *ep = &feg->polydata[pEPDATA->idx];

		if(!ep->tex) {
			continue;
		}

		if(ep->type & (POLY_IGNORE | POLY_NODRAW| POLY_HIDE)) {
			continue;
		}

		if(FrustrumsClipPoly(frustrums,ep)) {
			continue;
		}

		//Clipp ZNear + Distance pour les ZMapps!!!
		float fDist=(ep->center.x*efpPlaneNear.a + ep->center.y*efpPlaneNear.b + ep->center.z*efpPlaneNear.c + efpPlaneNear.d);

		if(ep->v[0].rhw<-fDist) {
			continue;
		}

		fDist -= ep->v[0].rhw;

		Vec3f nrm = ep->v[2].p - ACTIVECAM->orgTrans.pos;
		int to = (ep->type & POLY_QUAD) ? 4 : 3;

		if(to == 4) {
			if(	(!(ep->type&POLY_DOUBLESIDED))&&
				(dot( ep->norm , nrm )>0.f)&&
				(dot( ep->norm2 , nrm )>0.f) )
			{
				continue;
			}
		} else {
			if(	(!(ep->type&POLY_DOUBLESIDED))&&
				(dot( ep->norm , nrm )>0.f) )
			{
				continue;
			}
		}

		unsigned short *pIndicesCurr;
		unsigned long *pNumIndices;

		if(ep->type & POLY_TRANS) {
			if(ep->transval>=2.f) { //MULTIPLICATIVE
				pIndicesCurr=pIndices+ep->tex->tMatRoom[room_num].uslStartCull_TMultiplicative+ep->tex->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative;
				pNumIndices=&ep->tex->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative;
			}else if(ep->transval>=1.f) { //ADDITIVE
				pIndicesCurr=pIndices+ep->tex->tMatRoom[room_num].uslStartCull_TAdditive+ep->tex->tMatRoom[room_num].uslNbIndiceCull_TAdditive;
				pNumIndices=&ep->tex->tMatRoom[room_num].uslNbIndiceCull_TAdditive;
			} else if(ep->transval>0.f) { //NORMAL TRANS
				pIndicesCurr=pIndices+ep->tex->tMatRoom[room_num].uslStartCull_TNormalTrans+ep->tex->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans;
				pNumIndices=&ep->tex->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans;
			} else { //SUBTRACTIVE
				pIndicesCurr=pIndices+ep->tex->tMatRoom[room_num].uslStartCull_TSubstractive+ep->tex->tMatRoom[room_num].uslNbIndiceCull_TSubstractive;
				pNumIndices=&ep->tex->tMatRoom[room_num].uslNbIndiceCull_TSubstractive;
			}
		} else {
			pIndicesCurr=pIndices+ep->tex->tMatRoom[room_num].uslStartCull+ep->tex->tMatRoom[room_num].uslNbIndiceCull;
			pNumIndices=&ep->tex->tMatRoom[room_num].uslNbIndiceCull;

			if(ZMAPMODE) {
				if((fDist<200)&&(ep->tex->TextureRefinement)) {
					ep->tex->TextureRefinement->vPolyZMap.push_back(ep);
				}
			}
		}

		SMY_VERTEX *pMyVertexCurr;

		*pIndicesCurr++ = ep->uslInd[0];
		*pIndicesCurr++ = ep->uslInd[1];
		*pIndicesCurr++ = ep->uslInd[2];
		*pNumIndices += 3;

		if(to == 4) {
			*pIndicesCurr++ = ep->uslInd[3];
			*pIndicesCurr++ = ep->uslInd[2];
			*pIndicesCurr++ = ep->uslInd[1];
			*pNumIndices += 3;
		}

		pMyVertexCurr = &pMyVertex[ep->tex->tMatRoom[room_num].uslStartVertex];

		if(!Project.improve) { // Normal View...
			if(ep->type & POLY_GLOW) {
				pMyVertexCurr[ep->uslInd[0]].color = 0xFFFFFFFF;
				pMyVertexCurr[ep->uslInd[1]].color = 0xFFFFFFFF;
				pMyVertexCurr[ep->uslInd[2]].color = 0xFFFFFFFF;

				if(to == 4) {
					pMyVertexCurr[ep->uslInd[3]].color = 0xFFFFFFFF;
				}
			} else {
				if(ep->type & POLY_LAVA) {
					if(!(ep->type & POLY_TRANS)) {
						ApplyDynLight(ep);
					}

					ManageLava_VertexBuffer(ep, to, tim, pMyVertexCurr);

					vPolyLava.push_back(ep);

					pMyVertexCurr[ep->uslInd[0]].color = ep->tv[0].color;
					pMyVertexCurr[ep->uslInd[1]].color = ep->tv[1].color;
					pMyVertexCurr[ep->uslInd[2]].color = ep->tv[2].color;

					if(to&4) {
						pMyVertexCurr[ep->uslInd[3]].color = ep->tv[3].color;
					}
				} else {
					if(!(ep->type & POLY_TRANS)) {
						ApplyDynLight_VertexBuffer_2(ep, pEPDATA->px, pEPDATA->py, pMyVertexCurr, ep->uslInd[0], ep->uslInd[1], ep->uslInd[2], ep->uslInd[3]);
					}

					if(ep->type & POLY_WATER) {
						ManageWater_VertexBuffer(ep, to, tim, pMyVertexCurr);
						vPolyWater.push_back(ep);
					}
				}
			}

			if((ViewMode & VIEWMODE_WIRE) && EERIERTPPoly(ep))
				EERIEPOLY_DrawWired(ep);

		} else { // Improve Vision Activated
			if(!(ep->type & POLY_TRANS)) {
				if(!EERIERTPPoly(ep)) { // RotTransProject Vertices
					continue;
				}

				ApplyDynLight(ep);

				for(int k = 0; k < to; k++) {
					long lr=(ep->tv[k].color>>16) & 255;
					float ffr=(float)(lr);

					float dd= ep->tv[k].rhw;

					dd = clamp(dd, 0.f, 1.f);

					float fb=((1.f-dd)*6.f + (EEfabs(ep->nrml[k].x)+EEfabs(ep->nrml[k].y)))*0.125f;
					float fr=((.6f-dd)*6.f + (EEfabs(ep->nrml[k].z)+EEfabs(ep->nrml[k].y)))*0.125f;

					if (fr<0.f) fr=0.f;
					else fr=max(ffr,fr*255.f);

					fr=min(fr,255.f);
					fb*=255.f;
					fb=min(fb,255.f);
					u8 lfr = fr;
					u8 lfb = fb;
					u8 lfg = 0x1E;

					ep->tv[k].color = (0xff000000L | (lfr << 16) | (lfg << 8) | (lfb));
				}

				pMyVertexCurr[ep->uslInd[0]].color = ep->tv[0].color;
				pMyVertexCurr[ep->uslInd[1]].color = ep->tv[1].color;
				pMyVertexCurr[ep->uslInd[2]].color = ep->tv[2].color;

				if(to == 4) {
					pMyVertexCurr[ep->uslInd[3]].color = ep->tv[3].color;
				}
			}
		}
	}

	portals->room[room_num].pVertexBuffer->unlock();
}


void ARX_PORTALS_Frustrum_RenderRoomTCullSoftRender(long room_num) {


	//render opaque
	GRenderer->SetCulling(Renderer::CullNone);
	int iNbTex=portals->room[room_num].usNbTextures;
	TextureContainer **ppTexCurr=portals->room[room_num].ppTextureContainer;

	while(iNbTex--) {
		TextureContainer *pTexCurr=*ppTexCurr;

		if(ViewMode & VIEWMODE_FLAT)
			GRenderer->ResetTexture(0);
		else
			GRenderer->SetTexture(0, pTexCurr);

		if(pTexCurr->userflags & POLY_METAL)
			GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate2X);
		else
			GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate);

		if(pTexCurr->tMatRoom[room_num].uslNbIndiceCull)
		{
			GRenderer->SetAlphaFunc(Renderer::CmpGreater, .5f);
			portals->room[room_num].pVertexBuffer->drawIndexed(Renderer::TriangleList, pTexCurr->tMatRoom[room_num].uslNbVertex, pTexCurr->tMatRoom[room_num].uslStartVertex,
				&portals->room[room_num].pussIndice[pTexCurr->tMatRoom[room_num].uslStartCull],
				pTexCurr->tMatRoom[room_num].uslNbIndiceCull);
			GRenderer->SetAlphaFunc(Renderer::CmpNotEqual, 0.f);

			EERIEDrawnPolys += pTexCurr->tMatRoom[room_num].uslNbIndiceCull;
			pTexCurr->tMatRoom[room_num].uslNbIndiceCull = 0;
		}

		ppTexCurr++;
	}

	//////////////////////////////
	// ZMapp
	GRenderer->GetTextureStage(0)->SetColorOp(TextureStage::OpModulate);

	GRenderer->SetRenderState(Renderer::AlphaBlending, true);
	GRenderer->SetRenderState(Renderer::DepthWrite, false);

	iNbTex=portals->room[room_num].usNbTextures;
	ppTexCurr=portals->room[room_num].ppTextureContainer;

	// For each tex in portals->room[room_num]
	while(iNbTex--) {
		TextureContainer * pTexCurr	= *ppTexCurr;

		if(pTexCurr->TextureRefinement && pTexCurr->TextureRefinement->vPolyZMap.size()) {

			GRenderer->SetTexture(0, pTexCurr->TextureRefinement);

			dynamicVertices.lock();
			unsigned short *pussInd = dynamicVertices.indices;
			unsigned short iNbIndice = 0;

			vector<EERIEPOLY *>::iterator it = pTexCurr->TextureRefinement->vPolyZMap.begin();

			for(;it != pTexCurr->TextureRefinement->vPolyZMap.end(); ++it) {
				EERIEPOLY * ep = *it;

				unsigned short iNbVertex = (ep->type & POLY_QUAD) ? 4 : 3;
				SMY_VERTEX3 *pVertex = dynamicVertices.append(iNbVertex);

				if(!pVertex) {
					dynamicVertices.unlock();
					if(dynamicVertices.nbindices) {
						dynamicVertices.draw(Renderer::TriangleList);
					}
					dynamicVertices.reset();
					dynamicVertices.lock();
					iNbIndice = 0;
					pussInd = dynamicVertices.indices;
					pVertex = dynamicVertices.append(iNbVertex);
				}

				// PRECALCUL
				float tu[4];
				float tv[4];
				float _fTransp[4];

				bool nrm = EEfabs(ep->nrml[0].y) >= 0.9f || EEfabs(ep->nrml[1].y) >= 0.9f || EEfabs(ep->nrml[2].y) >= 0.9f;

				for(int nu = 0; nu < iNbVertex; nu++) {
					if(nrm) {
						tu[nu] = ep->v[nu].p.x * (1.0f/50);
						tv[nu] = ep->v[nu].p.z * (1.0f/50);
					} else {
						tu[nu] = ep->v[nu].uv.x * 4.f;
						tv[nu] = ep->v[nu].uv.y * 4.f;
					}

					float t = max(10.f, fdist(ACTIVECAM->orgTrans.pos, ep->v[nu].p) - 80.f);

					_fTransp[nu] = (150.f - t) * 0.006666666f;

					if(_fTransp[nu] < 0.f)
						_fTransp[nu] = 0.f;
					// t cannot be greater than 1.f (b should be negative for that)
				}

				// FILL DATA
				for(int idx = 0; idx < iNbVertex; ++idx) {
					pVertex->p.x     =  ep->v[idx].p.x;
					pVertex->p.y     = -ep->v[idx].p.y;
					pVertex->p.z     =  ep->v[idx].p.z;
					pVertex->color   = Color::gray(_fTransp[idx]).toBGR();
					pVertex->uv[0].x = tu[idx];
					pVertex->uv[0].y = tv[idx];
					pVertex++;

					*pussInd++ = iNbIndice++;
					dynamicVertices.nbindices++;
				}

				if(iNbVertex == 4) {
					*pussInd++ = iNbIndice-2;
					*pussInd++ = iNbIndice-3;
					dynamicVertices.nbindices += 2;
				}
			}

			// CLEAR CURRENT ZMAP
			pTexCurr->TextureRefinement->vPolyZMap.clear();

			dynamicVertices.unlock();
			if(dynamicVertices.nbindices) {
				dynamicVertices.draw(Renderer::TriangleList);
			}
			dynamicVertices.done();

		}

		ppTexCurr++;
	}

	GRenderer->SetRenderState(Renderer::DepthWrite, true);
	GRenderer->SetRenderState(Renderer::AlphaBlending, false);
}

//-----------------------------------------------------------------------------

void ARX_PORTALS_Frustrum_RenderRoom_TransparencyTSoftCull(long room_num)
{
	//render transparency
	int iNbTex=portals->room[room_num].usNbTextures;
	TextureContainer **ppTexCurr=portals->room[room_num].ppTextureContainer;

	while(iNbTex--) {

		TextureContainer * pTexCurr = *ppTexCurr;
		GRenderer->SetTexture(0, pTexCurr);

		//NORMAL TRANS
		if(pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans)
		{
			SetZBias(2);
			GRenderer->SetBlendFunc(Renderer::BlendSrcColor, Renderer::BlendDstColor);

			portals->room[room_num].pVertexBuffer->drawIndexed(Renderer::TriangleList, pTexCurr->tMatRoom[room_num].uslNbVertex, pTexCurr->tMatRoom[room_num].uslStartVertex, &portals->room[room_num].pussIndice[pTexCurr->tMatRoom[room_num].uslStartCull_TNormalTrans], pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans);

			EERIEDrawnPolys+=pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans;
			pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TNormalTrans=0;
		}

		//MULTIPLICATIVE
		if(pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative)
		{
			SetZBias(2);
			GRenderer->SetBlendFunc(Renderer::BlendOne, Renderer::BlendOne);

			portals->room[room_num].pVertexBuffer->drawIndexed(Renderer::TriangleList, pTexCurr->tMatRoom[room_num].uslNbVertex, pTexCurr->tMatRoom[room_num].uslStartVertex, &portals->room[room_num].pussIndice[pTexCurr->tMatRoom[room_num].uslStartCull_TMultiplicative], pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative);

			EERIEDrawnPolys+=pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative;
			pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TMultiplicative=0;
		}

		//ADDITIVE
		if(pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TAdditive)
		{
			SetZBias(2);
			GRenderer->SetBlendFunc(Renderer::BlendOne, Renderer::BlendOne);

			portals->room[room_num].pVertexBuffer->drawIndexed(Renderer::TriangleList, pTexCurr->tMatRoom[room_num].uslNbVertex, pTexCurr->tMatRoom[room_num].uslStartVertex, &portals->room[room_num].pussIndice[pTexCurr->tMatRoom[room_num].uslStartCull_TAdditive], pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TAdditive);

			EERIEDrawnPolys+=pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TAdditive;
			pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TAdditive=0;
		}

		//SUBSTRACTIVE
		if(pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TSubstractive)
		{
			SetZBias(8);

			GRenderer->SetBlendFunc(Renderer::BlendZero, Renderer::BlendInvSrcColor);

			portals->room[room_num].pVertexBuffer->drawIndexed(Renderer::TriangleList, pTexCurr->tMatRoom[room_num].uslNbVertex, pTexCurr->tMatRoom[room_num].uslStartVertex, &portals->room[room_num].pussIndice[pTexCurr->tMatRoom[room_num].uslStartCull_TSubstractive], pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TSubstractive);

			EERIEDrawnPolys+=pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TSubstractive;
			pTexCurr->tMatRoom[room_num].uslNbIndiceCull_TSubstractive=0;
		}

		ppTexCurr++;
	}
}

void ARX_PORTALS_Frustrum_ComputeRoom(long room_num, const EERIE_FRUSTRUM * frustrum)
{
	arx_assert(portals);

	if(RoomDraw[room_num].count == 0) {
		RoomDrawList.push_back(room_num);
	}

	RoomFrustrumAdd(room_num,frustrum);
	RoomDraw[room_num].count++;

	float fClippZFar = ACTIVECAM->cdepth * (fZFogEnd*1.1f);

	// Now Checks For room Portals !!!
	for(long lll=0; lll<portals->room[room_num].nb_portals; lll++) {
		if(portals->portals[portals->room[room_num].portals[lll]].useportal)
			continue;

		EERIE_PORTALS *po = &portals->portals[portals->room[room_num].portals[lll]];
		EERIEPOLY *epp = &po->poly;
	
		//clipp NEAR & FAR
		unsigned char ucVisibilityNear=0;
		unsigned char ucVisibilityFar=0;

		float fDist0;
		for(size_t i=0; i<ARRAY_SIZE(epp->v); i++) {
			fDist0 = (efpPlaneNear.a*epp->v[i].p.x)+(efpPlaneNear.b*epp->v[i].p.y)+(efpPlaneNear.c*epp->v[i].p.z)+efpPlaneNear.d;
			if(fDist0 < 0.f)
				ucVisibilityNear++;
			if(fDist0 > fClippZFar)
				ucVisibilityFar++;
		}

		if((ucVisibilityFar & 4) || (ucVisibilityNear & 4)) {
			po->useportal=2;
			continue;
		}

		Vec3f pos = epp->center - ACTIVECAM->orgTrans.pos;
		float fRes = dot(pos, epp->norm);

		EERIERTPPoly2(epp);

		if(!IsSphereInFrustrum(epp->v[0].rhw, &epp->center, frustrum)) {
			continue;
		}

		bool Cull = !(fRes<0.f);
		
		EERIE_FRUSTRUM fd;
		CreateFrustrum(&fd, epp, Cull);

		long roomToCompute = 0;
		bool computeRoom = false;

		if(po->room_1 == room_num && !Cull) {
			roomToCompute = po->room_2;
			computeRoom = true;
		}else if(po->room_2 == room_num && Cull) {
			roomToCompute = po->room_1;
			computeRoom = true;
		}

		if(computeRoom) {
			po->useportal=1;
			ARX_PORTALS_Frustrum_ComputeRoom(roomToCompute, &fd);
		}
	}
}

void ARX_SCENE_Update() {
	arx_assert(USE_PORTALS && portals);

	unsigned long tim = (unsigned long)(arxtime);

	WATEREFFECT+=0.0005f*framedelay;

	long l = ACTIVECAM->cdepth * 0.42f;
	long clip3D = (l / (long)BKG_SIZX) + 1;
	long lcval = clip3D + 4;

	long camXsnap = ACTIVECAM->orgTrans.pos.x * ACTIVEBKG->Xmul;
	long camZsnap = ACTIVECAM->orgTrans.pos.z * ACTIVEBKG->Zmul;
	camXsnap = clamp(camXsnap, 0, ACTIVEBKG->Xsize - 1L);
	camZsnap = clamp(camZsnap, 0, ACTIVEBKG->Zsize - 1L);

	long x0 = std::max(camXsnap - lcval, 0L);
	long x1 = std::min(camXsnap + lcval, ACTIVEBKG->Xsize - 1L);
	long z0 = std::max(camZsnap - lcval, 0L);
	long z1 = std::min(camZsnap + lcval, ACTIVEBKG->Zsize - 1L);

	ACTIVEBKG->Backg[camXsnap + camZsnap * ACTIVEBKG->Xsize].treat = 1;

		PrecalcDynamicLighting(x0, z0, x1, z1);

	// Go for a growing-square-spirallike-render around the camera position
	// (To maximize Z-Buffer efficiency)

	for(long j=z0; j<=z1; j++) {
		for(long i=x0; i<x1; i++) {
			FAST_BKG_DATA *feg = &ACTIVEBKG->fastdata[i][j];
			feg->treat = 0;
		}
	}

	for(long j=0; j<ACTIVEBKG->Zsize; j++) {
		for(long i=0; i<ACTIVEBKG->Xsize; i++) {
			if (tilelights[i][j].num)
				tilelights[i][j].num=0;
		}
	}

	long room_num=ARX_PORTALS_GetRoomNumForPosition(&ACTIVECAM->orgTrans.pos,1);
	if(room_num>-1) {
		ARX_PORTALS_InitDrawnRooms();
		EERIE_FRUSTRUM frustrum;
		CreateScreenFrustrum(&frustrum);
		ARX_PORTALS_Frustrum_ComputeRoom(room_num, &frustrum);

		for(size_t i = 0; i < RoomDrawList.size(); i++) {
			ARX_PORTALS_Frustrum_RenderRoomTCullSoft(RoomDrawList[i], &RoomDraw[RoomDrawList[i]].frustrum, tim);
		}
	}
}

extern long SPECIAL_DRAGINTER_RENDER;
//*************************************************************************************
// Main Background Rendering Proc.
// ie: Big Mess
//*************************************************************************************
///////////////////////////////////////////////////////////
void ARX_SCENE_Render() {

	GRenderer->SetBlendFunc(Renderer::BlendZero, Renderer::BlendInvSrcColor);
	for(size_t i = 0; i < RoomDrawList.size(); i++) {

		long room_num = RoomDrawList[i];

		if(!RoomDraw[room_num].count)
			continue;

		ARX_PORTALS_Frustrum_RenderRoomTCullSoftRender(room_num);
	}

	if(!Project.improve) {
		ARXDRAW_DrawInterShadows();
	}
		
	ARX_THROWN_OBJECT_Manage(checked_range_cast<unsigned long>(framedelay));
		
	GRenderer->GetTextureStage(0)->SetWrapMode(TextureStage::WrapClamp);
	GRenderer->GetTextureStage(0)->SetMipMapLODBias(-0.6f);

	RenderInter();

	GRenderer->GetTextureStage(0)->SetWrapMode(TextureStage::WrapRepeat);
	GRenderer->GetTextureStage(0)->SetMipMapLODBias(-0.3f);
		
	// To render Dragged objs
	if(DRAGINTER) {
		SPECIAL_DRAGINTER_RENDER=1;
		ARX_INTERFACE_RenderCursor();

		SPECIAL_DRAGINTER_RENDER=0;
	}

	PopAllTriangleList();
		
	if(eyeball.exist != 0 && eyeballobj)
		ARXDRAW_DrawEyeBall();

	GRenderer->SetRenderState(Renderer::DepthWrite, false);

	ARXDRAW_DrawPolyBoom();

	PopAllTriangleListTransparency();

	ARX_PORTALS_Frustrum_RenderRooms_TransparencyT();

	Halo_Render();

	GRenderer->SetCulling(Renderer::CullCCW);
	GRenderer->SetRenderState(Renderer::AlphaBlending, false);	
	GRenderer->SetRenderState(Renderer::DepthWrite, true);

#ifdef BUILD_EDITOR
	{
		//TODO copy-paste
		long l = ACTIVECAM->cdepth * 0.42f;
		long clip3D = (l / (long)BKG_SIZX) + 1;
		long lcval = clip3D + 4;

		long camXsnap = ACTIVECAM->orgTrans.pos.x * ACTIVEBKG->Xmul;
		long camZsnap = ACTIVECAM->orgTrans.pos.z * ACTIVEBKG->Zmul;
		camXsnap = clamp(camXsnap, 0, ACTIVEBKG->Xsize - 1L);
		camZsnap = clamp(camZsnap, 0, ACTIVEBKG->Zsize - 1L);

		long x0 = std::max(camXsnap - lcval, 0L);
		long x1 = std::min(camXsnap + lcval, ACTIVEBKG->Xsize - 1L);
		long z0 = std::max(camZsnap - lcval, 0L);
		long z1 = std::min(camZsnap + lcval, ACTIVEBKG->Zsize - 1L);

	if (EDITION==EDITION_LIGHTS)
		ARXDRAW_DrawAllLights(x0,z0,x1,z1);
	}
#endif

}

