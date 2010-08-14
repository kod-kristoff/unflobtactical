/*
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "map.h"
#include "model.h"
#include "loosequadtree.h"
#include "renderqueue.h"
#include "surface.h"
#include "text.h"
#include "vertex.h"
#include "../game/material.h"		// bad call to less general directory. FIXME. Move map to game?
#include "../game/unit.h"			// bad call to less general directory. FIXME. Move map to game?
#include "../game/game.h"			// bad call to less general directory. FIXME. Move map to game?
#include "../engine/particleeffect.h"
#include "../engine/particle.h"
#include "../grinliz/glstringutil.h"
#include "../tinyxml/tinyxml.h"
#include "../grinliz/glrectangle.h"

using namespace grinliz;
using namespace micropather;


Map::Map( SpaceTree* tree )
	: itemPool( "mapItemPool", sizeof( MapItem ), sizeof( MapItem ) * 200, false )
{
	memset( pyro, 0, SIZE*SIZE*sizeof(U8) );
	memset( visMap, 0, SIZE*SIZE );
	memset( pathMap, 0, SIZE*SIZE );
	memset( itemDefArr, 0, sizeof(MapItemDef)*MAX_ITEM_DEF );
	nGuardPos = 0;
	nScoutPos = 0;
	nLanderPos = 0;
	lander = 0;
	dayTime = true;
	pathBlocker = 0;
	nImageData = 0;
	enableMeta = false;

	microPather = new MicroPather(	this,			// graph interface
									SIZE*SIZE,		// max possible states (+1)
									6 );			// max adjacent states

	this->tree = tree;
	width = height = SIZE;
	//walkingVertex.Clear();

	gamui::RenderAtom nullAtom;
	overlay0.Init( this, nullAtom, nullAtom, 0 );
	overlay1.Init( this, nullAtom, nullAtom, 0 );

	walkingMap.Init( &overlay0 );
	invalidLightMap.Set( 0, 0, SIZE-1, SIZE-1 );

	gamui::RenderAtom borderAtom = UIRenderer::CalcPaletteAtom( UIRenderer::PALETTE_BLUE, UIRenderer::PALETTE_BLUE, UIRenderer::PALETTE_DARK, 1, 1, true );
#ifdef DEBUG_VISIBILITY
	borderAtom.renderState = (const void*) RENDERSTATE_MAP_TRANSLUCENT;
#else
	borderAtom.renderState = (const void*) RENDERSTATE_MAP_OPAQUE;
#endif
	for( int i=0; i<4; ++i ) {
		border[i].Init( &overlay1, borderAtom );
	}

	// FIXME: need a release texture, since the map can be destroyed. Doesn't leak, but needlessly holds resources.
	TextureManager* texman = TextureManager::Instance();
	backgroundTexture = texman->CreateTexture( "MapBackground", MAP_TEXTURE_SIZE, MAP_TEXTURE_SIZE, Surface::RGB16, Texture::PARAM_NONE, this );
	greyTexture = texman->CreateTexture( "MapGrey", MAP_TEXTURE_SIZE/2, MAP_TEXTURE_SIZE/2, Surface::RGB16, Texture::PARAM_NONE, this );

	backgroundSurface.Set( Surface::RGB16, MAP_TEXTURE_SIZE, MAP_TEXTURE_SIZE );
	backgroundSurface.Clear( 0 );
	greySurface.Set( Surface::RGB16, MAP_TEXTURE_SIZE/2, MAP_TEXTURE_SIZE/2 );
	greySurface.Clear( 0 );

	nSeenIndex = nUnseenIndex = nPastSeenIndex = 0;
	static const float INV64 = 1.0f / 64.0f;

	for( int j=0; j<SIZE+1; ++j ) {
		for( int i=0; i<SIZE+1; ++i ) {
			mapVertex[j*(SIZE+1)+i].Set( (float)i*INV64, (float)(SIZE-j)*INV64 );
		}
	}


	pathQueryID = 1;
	visibilityQueryID = 1;

	dayMap.Set( Surface::RGB16, SIZE, SIZE );
	nightMap.Set( Surface::RGB16, SIZE, SIZE );
	dayMap.Clear( 255 );
	nightMap.Clear( 255 );

	for( int i=0; i<2; ++i ) {
		lightMap[i].Set( Surface::RGB16, SIZE, SIZE );
		lightMap[i].Clear( 0 );
	}

	lightMapTex = texman->CreateTexture( "MapLightMap", SIZE, SIZE, Surface::RGB16, Texture::PARAM_NONE, this );
	ImageManager::Instance()->LoadImage( "objectLightMaps", &lightObject );

	GLOUTPUT(( "Map created. %dK\n", sizeof( *this )/1024 ));
}


Map::~Map()
{
	Clear();
	delete microPather;
}


void Map::SetSize( int w, int h )					
{
	width = w; 
	height = h; 
	
	border[0].SetPos( -1, -1 );
	border[0].SetSize( (float)(w+2), 1 );

	border[1].SetPos( -1, (float)h );
	border[1].SetSize( (float)(w+2), 1 );

	border[2].SetPos( -1, 0 );
	border[2].SetSize( 1, (float)h );

	border[3].SetPos( (float)w, 0 );
	border[3].SetSize( 1, (float)h );
}



void Map::Clear()
{
	// Find everything:
	Rectangle2I b;
	b.Set( 0, 0, SIZE-1, SIZE-1 );
	MapItem* pItem = quadTree.FindItems( b, 0, 0 );

	while( pItem ) {
		MapItem* temp = pItem;
		pItem = pItem->next;
		if ( temp->model )
			tree->FreeModel( temp->model );
		itemPool.Free( temp );
	}
	quadTree.Clear();

	for( int i=0; i<debris.Size(); ++i ) {
		delete debris[i].storage;
		tree->FreeModel( debris[i].crate );
	}
	debris.Clear();

	memset( pyro, 0, SIZE*SIZE*sizeof(U8) );
	memset( visMap, 0, SIZE*SIZE );
	memset( pathMap, 0, SIZE*SIZE );
}


void Map::DrawSeen()
{
	GenerateLightMap();
	if ( nSeenIndex == 0 )
		return;

	CompositingShader shader;
	shader.SetVertex( 2, sizeof(mapVertex[0]), mapVertex );
	shader.SetTexture0( backgroundTexture, 2, sizeof(mapVertex[0]), mapVertex );	
	shader.SetTexture1( lightMapTex, 2, sizeof(mapVertex[0]), mapVertex );

	// the vertices are storred in texture coordinates, to use less space.

	Matrix4 swizzle;
	swizzle.m11 = 64.0f;
	swizzle.m22 = 0.0f;
	swizzle.m32 = -64.0f;	swizzle.m33 = 0.0f;		swizzle.m34 = 64.0f;

	shader.PushMatrix( GPUShader::MODELVIEW_MATRIX );
	shader.MultMatrix( GPUShader::MODELVIEW_MATRIX, swizzle );
	shader.Draw( nSeenIndex, seenIndex );
	shader.PopMatrix( GPUShader::MODELVIEW_MATRIX );
}


void Map::DrawUnseen()
{
	GenerateLightMap();
	if ( nUnseenIndex == 0 )
		return;

	CompositingShader shader;
	shader.SetColor( 0, 0, 0 );
	shader.SetVertex( 2, sizeof(mapVertex[0]), mapVertex );

	Matrix4 swizzle;
	swizzle.m11 = 64.0f;
	swizzle.m22 = 0.0f;
	swizzle.m32 = -64.0f;	swizzle.m33 = 0.0f;		swizzle.m34 = 64.0f;

	shader.PushMatrix( GPUShader::MODELVIEW_MATRIX );
	shader.MultMatrix( GPUShader::MODELVIEW_MATRIX, swizzle );
	shader.Draw( nUnseenIndex, unseenIndex );
	shader.PopMatrix( GPUShader::MODELVIEW_MATRIX );
}


void Map::DrawPastSeen( const Color4F& color )
{
	GenerateLightMap();
	if ( nPastSeenIndex == 0 )
		return;

	CompositingShader shader;
	shader.SetVertex( 2, sizeof(mapVertex[0]), mapVertex );
	shader.SetTexture0( greyTexture, 2, sizeof(mapVertex[0]), mapVertex );	
	shader.SetColor( color.x, color.y, color.z );

	// the vertices are stored in texture coordinates, to use less space.
	Matrix4 swizzle;
	swizzle.m11 = 64.0f;
	swizzle.m22 = 0.0f;
	swizzle.m32 = -64.0f;	swizzle.m33 = 0.0f;		swizzle.m34 = 64.0f;

	shader.PushMatrix( GPUShader::MODELVIEW_MATRIX );
	shader.MultMatrix( GPUShader::MODELVIEW_MATRIX, swizzle );
	shader.Draw( nPastSeenIndex, pastSeenIndex );
	shader.PopMatrix( GPUShader::MODELVIEW_MATRIX );
}



void Map::DrawOverlay( int layer )
{
	if ( layer == 0 )
		overlay0.Render();
	else
		overlay1.Render();
}


void Map::MapImageToWorld( int x, int y, int w, int h, int tileRotation, Matrix2I* m )
{
	Matrix2I t, r, p;
	t.x = x;
	t.y = y;
	r.SetXZRotation( 90*tileRotation );

	switch ( tileRotation ) {
		case 0:
			p.x = 0;	p.y = 0;
			break;

		case 1:
			p.x = 0;	p.y += h-1;
			break;
		case 2:
			p.x += w-1; p.y += h-1;
			break;
		case 3:
			p.x += w-1; p.y += 0;
			break;
		default:
			GLASSERT( 0 );
	}
	*m = t * p * r;
}


void Map::SetTexture( const Surface* s, int x, int y, int tileRotation )
{
	GLASSERT( s->Width() == s->Height() );

	Rectangle2I src;
	src.Set( 0, 0, s->Width()-1, s->Height()-1 );

	Rectangle2I target;
	target.Set( x, y, x+s->Width()-1, y+s->Height()-1 );	// always square for this to work, obviously.

	if ( tileRotation == 0 ) {
		backgroundSurface.BlitImg( target.min, s, src );
	}
	else 
	{
		Matrix2I m, inv;
		MapImageToWorld( x, y, s->Width(), s->Height(), tileRotation, &m );
		m.Invert( &inv );
		
		backgroundSurface.BlitImg( target, s, inv );
	}

	Rectangle2I greyTarget;
	greyTarget.Set( target.min.x/2, target.min.y/2, target.max.x/2, target.max.y/2 );
	for( int j=greyTarget.min.y; j<=greyTarget.max.y; ++j ) {
		for( int i=greyTarget.min.x; i<=greyTarget.max.x; ++i ) {
			Surface::RGBA rgba0 = Surface::CalcRGB16( backgroundSurface.GetImg16( i*2+0, j*2+0 ) );
			Surface::RGBA rgba1 = Surface::CalcRGB16( backgroundSurface.GetImg16( i*2+1, j*2+0 ) );
			Surface::RGBA rgba2 = Surface::CalcRGB16( backgroundSurface.GetImg16( i*2+0, j*2+1 ) );
			Surface::RGBA rgba3 = Surface::CalcRGB16( backgroundSurface.GetImg16( i*2+1, j*2+1 ) );

			int c = (  rgba0.r + rgba0.g + rgba0.g + rgba0.b 
				     + rgba1.r + rgba1.g + rgba1.g + rgba1.b 
				     + rgba2.r + rgba2.g + rgba2.g + rgba2.b 
				     + rgba3.r + rgba3.g + rgba3.g + rgba3.b ) >> 4; 
			
			GLASSERT( c >= 0 && c < 255 );
			Surface::RGBA grey = { (U8)c, (U8)c, (U8)c, 255 };
			//Surface::RGBA grey = { 128, 128, 128, 255 };
			//Surface::RGBA test = Surface::CalcRGB16( Surface::CalcRGB16( grey ) );

			greySurface.SetImg16( i, j, Surface::CalcRGB16( grey ) );
		}
	}


	backgroundTexture->Upload( backgroundSurface );
	greyTexture->Upload( greySurface );
}


void Map::SetLightMaps( const Surface* day, const Surface* night, int x, int y, int tileRotation )
{
	GLASSERT( day );
	GLASSERT( night );
	
	GLASSERT( day->BytesPerPixel() == 2 );
	GLASSERT( night->BytesPerPixel() == 2 );
	GLASSERT(    day->Width() == night->Width() 
		      && day->Height() == night->Height() 
			  && day->Width() == day->Height() );

	Rectangle2I rect;
	rect.Set( 0, 0, day->Width()-1, day->Height()-1 );

	if ( tileRotation == 0 ) {
		Vector2I target = { x, y };
		dayMap.BlitImg( target, day, rect );
		nightMap.BlitImg( target, night, rect );
	}
	else {
		Matrix2I inv, m;
		MapImageToWorld( x, y, day->Width(), day->Height(), tileRotation, &m );
		m.Invert( &inv );
		
		Rectangle2I target;
		target.Set( x, y, x+day->Width()-1, y+day->Height()-1 );

		dayMap.BlitImg( target, day, inv );
		nightMap.BlitImg( target, night, inv );
	}

	// Could optimize full invalidate, but hard to think of a case where it matters.
	if ( dayTime )
		memcpy( lightMap[0].Pixels(), dayMap.Pixels(), SIZE*SIZE*2 );
	else
		memcpy( lightMap[0].Pixels(), nightMap.Pixels(), SIZE*SIZE*2 );
	invalidLightMap.Set( 0, 0, SIZE-1, SIZE-1 );
}


void Map::SetDayTime( bool day )
{
	if ( day != dayTime ) {
		dayTime = day;
		if ( dayTime )
			memcpy( lightMap[0].Pixels(), dayMap.Pixels(), SIZE*SIZE*2 );
		else
			memcpy( lightMap[0].Pixels(), nightMap.Pixels(), SIZE*SIZE*2 );
		invalidLightMap.Set( 0, 0, SIZE-1, SIZE-1 );
	}
}


grinliz::BitArray<Map::SIZE, Map::SIZE, 1>* Map::LockFogOfWar()
{
	invalidLightMap.Set( 0, 0, SIZE-1, SIZE-1 );
	return &fogOfWar;
}


void Map::ReleaseFogOfWar()
{
	invalidLightMap.Set( 0, 0, SIZE-1, SIZE-1 );
	pastSeenFOW.DoUnion( fogOfWar );
}


void Map::GenerateLightMap()
{
	if ( invalidLightMap.IsValid() )
	{
		// Input:
		//		Basemap - input light colors.
		//		Lights: add color to basemap
		//		FogOfWar: flip on or off
		// Output:
		//		processedLightMap:	base + light
		//		finalMap:			base + light + FOW


		// Copy base to the lm[1], and then add lights.
		for( int j=invalidLightMap.min.y; j<=invalidLightMap.max.y; ++j ) {
			for( int i=invalidLightMap.min.x; i<=invalidLightMap.max.x; ++i ) {
				U16 c = lightMap[0].GetImg16( i, j );
				lightMap[1].SetImg16( i, j, c );
			}
		}

		MapItem* item = quadTree.FindItems( invalidLightMap, MapItem::MI_IS_LIGHT, 0 );
		for( ; item; item=item->next ) {

			const MapItemDef& itemDef = itemDefArr[item->itemDefIndex];
			GLASSERT( itemDef.IsLight() );

			Matrix2I wToObj;
			//CalcModelPos( item, 0, 0, &xform );
			Matrix2I xform = item->XForm();
			xform.Invert( &wToObj );

			Rectangle2I mapBounds = item->MapBounds();

			for( int j=mapBounds.min.y; j<=mapBounds.max.y; ++j ) {
				for( int i=mapBounds.min.x; i<=mapBounds.max.x; ++i ) {

					Vector3I world = { i, j, 1 };
					Vector3I object3 = wToObj * world;
					Vector2I object = { object3.x, object3.y };

					if (	object.x >= 0 && object.x < itemDef.cx
						 && object.y >= 0 && object.y < itemDef.cy )
					{
						// Now grab the colors from the image.
						U16 cLight = lightObject.GetImg16( object.x+itemDef.lightTX, object.y+itemDef.lightTY );
						Surface::RGBA rgbLight = Surface::CalcRGB16( cLight );

						// Now add it to the light map.
						U16 c = lightMap[1].GetImg16( i, j );
						Surface::RGBA rgb = Surface::CalcRGB16( c );

						rgb.r = Min( rgb.r + rgbLight.r, 255 );
						rgb.g = Min( rgb.g + rgbLight.g, 255 );
						rgb.b = Min( rgb.b + rgbLight.b, 255 );

						U16 cResult = Surface::CalcRGB16( rgb );
						lightMap[1].SetImg16( i, j, cResult );
					}
				}
			}
		}
		lightMapTex->Upload( lightMap[1] );

		// Test case: 
		// (continue, back) 8.9 k/f
		// Go to strip creation: 5.9 k/f (and that's total - the code below should get the submit down to 400-500 tris possibly?)

#define PUSHQUAD( _arr, _index, _x0, _x1, _y ) \
			_arr[ _index++ ] = (_y+0)*(SIZE+1)+(_x0);	\
			_arr[ _index++ ] = (_y+1)*(SIZE+1)+(_x0);	\
			_arr[ _index++ ] = (_y+1)*(SIZE+1)+(_x1);	\
			_arr[ _index++ ] = (_y+0)*(SIZE+1)+(_x0);	\
			_arr[ _index++ ] = (_y+1)*(SIZE+1)+(_x1);	\
			_arr[ _index++ ] = (_y+0)*(SIZE+1)+(_x1);	

		int countArr[3] = { 0, 0, 0 };
		nSeenIndex = nUnseenIndex = nPastSeenIndex = 0;

		for( int j=0; j<height; ++j ) {
			for( int i=0; i<width; i += 32 ) {

				U32 fog = fogOfWar.Access32( i, j, 0 );
				U32 past = pastSeenFOW.Access32( i, j, 0 );

				past = past ^ fog;				// if the fog is set, then we don't draw the past.
				U32 unseen = ~( fog | past );	// everything else unseen.

				if ( i+32 > width ) {
					// mask off the end bits.
					U32 mask = (1<<(width-i))-1;

					fog &= mask;
					past &= mask;
					unseen &= mask;
				}

				const U32 arr[3] = { fog, past, unseen };
				U16* indexArr[3] = { seenIndex, pastSeenIndex, unseenIndex };

				for ( int k=0; k<3; ++k ) {

					int start=0;
					while( start < 32 ) {
						int end = start+1;

						if ( (1<<start) & arr[k] ) {
							while( end < 32 ) {
								if ( ((1<<end) & arr[k] ) == 0 ) {
									break;
								}
								++end;
							}
							PUSHQUAD( indexArr[k], countArr[k], i+start, i+end, j );
						}
						start = end;
					}
				}
			}
		}
		nSeenIndex = countArr[0];
		nPastSeenIndex = countArr[1];
		nUnseenIndex = countArr[2];
#ifdef SHOW_FOW
		memcpy( pastSeenIndex+nPastSeenIndex, unseenIndex, sizeof(U16)*nUnseenIndex );
		nPastSeenIndex += nUnseenIndex;
		nUnseenIndex = 0;
#endif

#undef PUSHQUAD

		invalidLightMap.Set( 0, 0, -1, -1 );
	}
}



Map::MapItemDef* Map::InitItemDef( int i )
{
	GLASSERT( i > 0 );				// 0 is reserved
	GLASSERT( i < MAX_ITEM_DEF );
	return &itemDefArr[i];
}


const char* Map::GetItemDefName( int i )
{
	const char* result = "";
	if ( i > 0 && i < MAX_ITEM_DEF ) {
		result = itemDefArr[i].name.c_str();
	}
	return result;
}


const Map::MapItemDef* Map::GetItemDef( const char* name )
{
	if ( itemDefMap.Empty() ) {
		for( int i=0; i<MAX_ITEM_DEF; ++i ) {
			if ( !itemDefArr[i].name.empty() ) {
				itemDefMap.Add( itemDefArr[i].name.c_str(), itemDefArr+i );
			}
#ifdef DEBUG
			if ( itemDefArr[i].IsLight() ) {
				GLASSERT( itemDefArr[i].pather[0][0] == 0 );
				GLASSERT( itemDefArr[i].visibility[0][0] == 0 );
			}
#endif
		}
	}
	Map::MapItemDef* item=0;
	itemDefMap.Query( name, &item );
	return item;
}



void Map::DoDamage( int x, int y, const DamageDesc& damage, Rectangle2I* dBounds )
{
	float hp = damage.Total();
	if ( hp <= 0.0f )
		return;

	//bool changed = false;
	const MapItem* root = quadTree.FindItems( x, y, 0, MapItem::MI_IS_LIGHT );

	for( ; root; root=root->next ) {
		if ( root->model ) {
			GLASSERT( root->model->IsFlagSet( Model::MODEL_OWNED_BY_MAP ) );

			DoDamage( root->model, damage, dBounds );
		}
	}
}


void Map::DoDamage( Model* m, const DamageDesc& damageDesc, Rectangle2I* dBounds )
{
	if ( m->IsFlagSet( Model::MODEL_OWNED_BY_MAP ) ) 
	{
		MapItem* item = quadTree.FindItem( m );
		GLASSERT( ( item->flags & MapItem::MI_IS_LIGHT ) == 0 );

		const MapItemDef& itemDef = itemDefArr[item->itemDefIndex];

		int hp = (int)(damageDesc.energy + damageDesc.kinetic );
		GLOUTPUT(( "map damage '%s' (%d,%d) dam=%d\n", itemDef.name.c_str(), item->XForm().x, item->XForm().y, hp ));

		bool destroyed = false;
		if ( itemDef.CanDamage() && item->DoDamage(hp) ) 
		{
			if ( dBounds ) {
				Rectangle2I r = item->MapBounds();
				dBounds->DoUnion( r );
			}

			// Destroy the current model. Replace it with "destroyed"
			// model if there is one. This is as simple as saving off
			// the properties, deleting it, and re-adding. A little
			// tricky since we reach into the matrix itself.
			int x, y, rot;
			WorldToXYR( item->XForm(), &x, &y, &rot, true );
			int def = item->itemDefIndex;
			int flags = item->flags;
			int hp = item->hp;

			DeleteItem( item );
			AddItem( x, y, rot, def, hp, flags );
			destroyed = true;
		}
		if ( !destroyed && itemDef.CanDamage() && itemDef.flammable > 0 ) {
			if ( damageDesc.incind > random.Rand( 256-itemDef.flammable )) {
				Rectangle2I b = item->MapBounds();
				SetPyro( (b.min.x+b.max.x)/2, (b.min.y+b.max.y)/2, 0, 1 );
			}
		}
	}
}


void Map::DoSubTurn( Rectangle2I* change )
{
	for( int i=0; i<SIZE*SIZE; ++i ) {
		if ( pyro[i] ) {
			// FIXME: worth making faster? Set up for debugging (uses utility functions)
			int y = i/SIZE;
			int x = i-y*SIZE;

			if ( PyroSmoke( x, y ) ) {
				int duration = PyroDuration( x, y );
				if ( duration > 0 )
					duration--;
				SetPyro( x, y, duration, 0 );
			}
			else {
				// Spread? Reduce to smoke?
				// If there is nothing left, then fire ends. (ouchie.)
				MapItem* root = quadTree.FindItems( x, y, 0, MapItem::MI_IS_LIGHT );
				while ( root && root->Destroyed() )
					root = root->next;

				if ( !root ) {
					// a few sub-turns of smoke
					SetPyro( x, y, 3, 0 );
				}
				else {
					// Will torch a building in no time. (Adjacent fires do multiple damage.)
					DamageDesc d = { FIRE_DAMAGE_PER_SUBTURN*0.5f, FIRE_DAMAGE_PER_SUBTURN*0.5f, 0.0f };
					DoDamage( x, y, d, change );
					DamageDesc f = { 0, 0, FIRE_DAMAGE_PER_SUBTURN };
					if ( x > 0 ) DoDamage( x-1, y, f, change );
					if ( x < SIZE-1 ) DoDamage( x+1, y, f, change );
					if ( y > 0 ) DoDamage( x, y-1, f, change );
					if ( y < SIZE-1 ) DoDamage( x, y+1, f, change );
				}
			}
		}
	}
}


void Map::EmitParticles( U32 delta )
{
	ParticleSystem* system = ParticleSystem::Instance();
	for( int i=0; i<SIZE*SIZE; ++i ) {
		if ( pyro[i] ) {
			int y = i/SIZE;
			int x = i-y*SIZE;
			bool fire = PyroFire( x, y ) ? true : false;
			Vector3F pos = { (float)x+0.5f, 0.0f, (float)y+0.5f };
			if ( fire )
				system->EmitFlame( delta, pos );
			else
				system->EmitSmoke( delta, pos );
		}
	}
}


void Map::AddSmoke( int x, int y, int subTurns )
{
	if ( PyroOn( x, y ) ) {
		// If on fire, ignore. Already smokin'
		if ( !PyroFire( x, y ) ) {
			SetPyro( x, y, subTurns, 0 );
		}
	}
	else {
		SetPyro( x, y, subTurns, 0 );
	}
}


void Map::SetPyro( int x, int y, int duration, int fire )
{
	GLASSERT( x >= 0 && x < SIZE );
	GLASSERT( y >= 0 && y < SIZE );
	int f = (fire) ? 0x80 : 0;
	int p = duration | f;
	pyro[y*SIZE+x] = p;
	
	const MapItemDef* itemDef = GetItemDef( "fireLight" );
	GLASSERT( itemDef );
	if ( !itemDef )
		return;

	int index = itemDef - itemDefArr;

	if ( fire && duration > 0 ) {
		// should have fire light.
		if ( itemDef ) {
			AddLightItem( x, y, 0, itemDef-itemDefArr, 0xffff, 0 );
		}
	}
	else {
		// clear any light.
		MapItem* root = quadTree.FindItems( x, y, MapItem::MI_IS_LIGHT, 0 );
		for( root; root; root=root->next ) {
			if ( root->itemDefIndex == index ) {
				DeleteItem( root );
				break;
			}
		}
	}
}


Model* Map::CreatePreview( int x, int y, int defIndex, int rotation )
{
	Model* model = 0;
	if ( itemDefArr[defIndex].modelResource ) {

		Matrix2I m;
		Vector3F modelPos;

		XYRToWorld( x, y, rotation*90, &m );
		WorldToModel( m, itemDefArr[defIndex].modelResource->header.IsBillboard(), &modelPos );

		model = tree->AllocModel( itemDefArr[defIndex].modelResource );
		model->SetPos( modelPos );
		model->SetRotation( 90.0f * rotation );
	}
	return model;
}


void Map::WorldToModel( const Matrix2I& mat, bool billboard, grinliz::Vector3F* model )
{
	// Account for rounding from map (integer based) to model (float based) coordinates.
	// Irritating transformation problem.

	if ( billboard ) {
		model->x  = (float)mat.x + 0.5f;
		model->y  = 0.0f;
		model->z  = (float)mat.y + 0.5f;
	}
	else {
		Vector2I a = { 0, 0 };
		Vector2I d = { 1, 1 };

		Vector2I aPrime = mat * a;
		Vector2I dPrime = mat * d;

		int xMin = Min( aPrime.x, dPrime.x ) - mat.x;
		int yMin = Min( aPrime.y, dPrime.y ) - mat.y;

		model->x = (float)(mat.x - xMin);
		model->y = 0.0f;
		model->z = (float)(mat.y - yMin);
	}
}


void Map::XYRToWorld( int x, int y, int rotation, Matrix2I* mat )
{
	GLASSERT( rotation == 0 || rotation == 90 || rotation == 180 || rotation == 270 );

	Matrix2I r, t, gf;

	r.SetXZRotation( rotation );
	t.SetTranslation( x, y );
	if ( mat ) {
		*mat = t*r;
	}
}


void Map::WorldToXYR( const Matrix2I& mat, int *x, int *y, int* r, bool useRot0123 )
{
	int rMul = (useRot0123) ? 1 : 90;
	*x = mat.x;
	*y = mat.y;

	// XZ rotation. (So confusing. Not XY.)
	// 1 0		0  1		-1 0		0 -1
	// 0 1		-1  0		 0 -1		1  0
	if      ( mat.a == 1  && mat.b == 0  && mat.c == 0  && mat.d == 1 ) {
		*r = 0*rMul;
	}
	else if ( mat.a == 0  && mat.b == 1 && mat.c == -1  && mat.d == 0 ) {
		*r = 1*rMul;
	}
	else if ( mat.a == -1 && mat.b == 0  && mat.c == 0  && mat.d == -1 ) {
		*r = 2*rMul;
	}
	else if ( mat.a == 0  && mat.b == -1  && mat.c == 1 && mat.d == 0 ) {
		*r = 3*rMul;
	}
	else {
		// Not valid simple rotation
		GLASSERT( 0 );
		*r = 0;
	}
}


Map::MapItem* Map::AddLightItem( int x, int y, int rotation, int lightDef, int hp, int flags )
{
	GLASSERT( itemDefArr[lightDef].IsLight() );

	int flags0 = flags | MapItem::MI_NOT_IN_DATABASE | MapItem::MI_IS_LIGHT;

	Vector2I lx = { itemDefArr[lightDef].lightOffsetX, 
					itemDefArr[lightDef].lightOffsetY };
	Matrix2I irot;
	irot.SetXZRotation( rotation*90 );
	Vector2I lxp = irot * lx;

	return AddItem( x+lxp.x, y+lxp.y, rotation, lightDef, 0xffff, flags0 );
}


Map::MapItem* Map::AddItem( int x, int y, int rotation, int defIndex, int hp, int flags )
{
	GLASSERT( x >= 0 && x < width );
	GLASSERT( y >= 0 && y < height );
	GLASSERT( rotation >=0 && rotation < 4 );
	GLASSERT( defIndex > 0 && defIndex < MAX_ITEM_DEF );

	if ( defIndex < Map::LIGHT_START && !itemDefArr[defIndex].modelResource ) {
		GLOUTPUT(( "No model resource.\n" ));
		GLASSERT( 0 );
		return 0;
	}

		
	const MapItemDef& itemDef = itemDefArr[defIndex];
	bool metadata = false;

	Matrix2I xform;
	Vector3F modelPos;
	XYRToWorld( x, y, rotation*90, &xform );
	bool isBillboard = itemDefArr[defIndex].modelResource && itemDefArr[defIndex].modelResource->header.IsBillboard();
	WorldToModel( xform, isBillboard, &modelPos );

	if ( StrEqual( itemDef.name.c_str(), "ufo_WallCurve4" ) ) {
		GLOUTPUT(( "Outer UFO wall added: x=%d y=%d r=%d\n", x, y, rotation ));
	}

	// Check for meta data.
	if ( (itemDef.name == "guard") || (itemDef.name == "scout" )) {
		metadata = true;
		if ( !enableMeta ) {
			// If we aren't in MapMaker mode, push back the guard and scout positions for later use.
			// Don't actually add the model.
			if ( itemDef.name == "guard" ) {
				if ( nGuardPos < MAX_GUARD_SCOUT ) {
					guardPos[nGuardPos++].Set( x, y );
				}
			}
			else if ( itemDef.name == "scout" ) {
				if ( nScoutPos < MAX_GUARD_SCOUT ) {
					scoutPos[nScoutPos++].Set( x, y );
				}
			}
			return 0;
		}
	}

	Rectangle2I mapBounds;
	MultMatrix2I( xform, itemDef.Bounds(), &mapBounds );

	// Check bounds on map.
	if ( mapBounds.min.x < 0 || mapBounds.min.y < 0 || mapBounds.max.x >= SIZE || mapBounds.max.y >= SIZE ) {
		GLOUTPUT(( "Out of bounds.\n" ));
		return 0;
	}

	// Check for duplicate. Only check for exact dupe - same origin & rotation.
	// This isn't required, but prevents map creation mistakes.
	MapItem* root = quadTree.FindItems( mapBounds, 0, MapItem::MI_IS_LIGHT );
	while( root ) {
		if ( root->itemDefIndex == defIndex && root->XForm() == xform ) {
			GLOUTPUT(( "Duplicate layer.\n" ));
			return 0;
		}
		root = root->next;
	}

	// Finally add!!
	MapItem* item = (MapItem*) itemPool.Alloc();

	item->model = 0;
	if ( hp == -1 )
		hp = itemDef.hp;

	if ( itemDefArr[defIndex].name == "lander" ) {
		GLASSERT( lander == 0 );
		lander = item;
	}

	item->SetXForm( xform );
	item->itemDefIndex = defIndex;
	item->open = 0;
	item->modelX = modelPos.x;
	item->modelZ = modelPos.z;
	item->modelRotation = rotation;

	item->hp = hp;
	if ( itemDef.IsDoor() )
		flags |= MapItem::MI_DOOR;
	item->flags = flags;

	GLASSERT( mapBounds.min.x >= 0 && mapBounds.max.x < 256 );	// using int8
	GLASSERT( mapBounds.min.y >= 0 && mapBounds.max.y < 256 );	// using int8
	item->mapBounds8.Set( mapBounds.min.x, mapBounds.min.y, mapBounds.max.x, mapBounds.max.y );

	item->next = 0;
	item->light = 0;
	
	// Check for lights.
	if ( itemDefArr[defIndex].HasLight() ) {
		item->light = AddLightItem( x, y, rotation, itemDefArr[defIndex].HasLight(), 0xffff, flags );
	}

	quadTree.Add( item );

	const ModelResource* res = 0;
	if ( itemDefArr[defIndex].CanDamage() && hp == 0 ) {
		res = itemDef.modelResourceDestroyed;
	}
	else {
		res = itemDef.modelResource;
	}
	if ( res ) {
		Model* model = tree->AllocModel( res );
		if ( res->header.IsBillboard() ) {
			item->modelRotation = 0;
		}

		model->SetFlag( Model::MODEL_OWNED_BY_MAP );
		if ( metadata )
			model->SetFlag( Model::MODEL_METADATA );
		model->SetPos( modelPos );
		model->SetRotation( item->ModelRot() );
		item->model = model;
	}

	// Patch the world states:
	ResetPath();
	ClearVisPathMap( mapBounds );
	CalcVisPathMap( mapBounds );

	if ( item->IsLight() ) {
		if ( invalidLightMap.IsValid() )
			invalidLightMap.DoUnion( mapBounds );
		else
			invalidLightMap = mapBounds;
	}

	return item;
}


void Map::DeleteItem( MapItem* item )
{	
	GLASSERT( item );

	// Delete the light first, if it exists:
	if ( item->light ) {
		DeleteItem( item->light );
		item->light = 0;
	}

	quadTree.UnlinkItem( item );
	Rectangle2I mapBounds = item->MapBounds();

	// Reset the lights
	if ( item->IsLight() ) {
		if ( invalidLightMap.IsValid() )
			invalidLightMap.DoUnion( mapBounds );
		else
			invalidLightMap = mapBounds;
	}

	if ( item->model )
		tree->FreeModel( item->model );

	itemPool.Free( item );
	ResetPath();
	ClearVisPathMap( mapBounds );
	CalcVisPathMap( mapBounds );
}


void Map::PopLocation( int team, bool guard, grinliz::Vector2I* pos, float* rotation )
{
	*rotation = 0;

	if ( team == TERRAN_TEAM ) {
		// put 'em in the lander.
		GLASSERT( lander );		

		Matrix2I xform = lander->XForm();
		//CalcModelPos( lander, 0, 0, &xform );

		Vector2I obj = { 2 + (nLanderPos & 1), 5 - (nLanderPos / 2) };
		Vector2I world = xform * obj;
		
		*pos = world;
		*rotation = lander->ModelRot();
		++nLanderPos;
	}
	else if ( team == ALIEN_TEAM ) {
		bool found = false;

		if ( guard && nGuardPos ) {
			int i = random.Rand( nGuardPos );
			*pos = guardPos[i];
			Swap( &guardPos[i], &guardPos[nGuardPos-1] );
			--nGuardPos;
			found = true;
		}
		if ( !found ) { // or scout
			GLASSERT( nScoutPos > 0 );
			int i = random.Rand( nScoutPos );
			*pos = scoutPos[i];
			Swap( &scoutPos[i], &scoutPos[nScoutPos-1] );
			--nScoutPos;

			// FIXME: the scout should always succeed. Need to make sure this can never
			// fail. Each tile needs a minimum scout positions.
		}
	}
	else {
		GLASSERT( 0 );
		pos->Set( 0, 0 );
	}
}


void Map::SaveDebris( const Debris& d, TiXmlElement* parent )
{
	TiXmlElement* debrisElement = new TiXmlElement( "Debris" );
	debrisElement->SetAttribute( "x", d.x );
	debrisElement->SetAttribute( "y", d.y );

	parent->LinkEndChild( debrisElement );
	d.storage->Save( debrisElement );
}


void Map::LoadDebris( const TiXmlElement* debrisElement, ItemDef* const* arr )
{
	GLASSERT( StrEqual( debrisElement->Value(), "Debris" ) );
	GLASSERT( debrisElement );
	if ( debrisElement ) {
		int x=0, y=0;

		debrisElement->QueryIntAttribute( "x", &x );
		debrisElement->QueryIntAttribute( "y", &y );

		Storage* storage = LockStorage( x, y );
		if ( !storage ) {
			storage = new Storage( arr );
		}
		storage->Load( debrisElement );
		ReleaseStorage( x, y, storage, arr );
	}
}


void Map::Save( TiXmlElement* mapElement )
{
	GLASSERT( strcmp( mapElement->Value(), "Map" ) == 0 );
	mapElement->SetAttribute( "sizeX", width );
	mapElement->SetAttribute( "sizeY", height );

	if ( !Engine::mapMakerMode ) {
		TiXmlElement* pastSeenElement = new TiXmlElement( "Seen" );
		mapElement->LinkEndChild( pastSeenElement );
	
		char buf[BitArray<Map::SIZE, Map::SIZE, 1>::STRING_SIZE];
		pastSeenFOW.ToString( buf );
		TiXmlText* pastSeenText = new TiXmlText( buf );
		pastSeenElement->LinkEndChild( pastSeenText );
	}

	TiXmlElement* itemsElement = new TiXmlElement( "Items" );
	mapElement->LinkEndChild( itemsElement );

	Rectangle2I b;
	CalcBounds( &b );
	MapItem* item = quadTree.FindItems( b, 0, MapItem::MI_NOT_IN_DATABASE );

	for( ; item; item=item->next ) {
		TiXmlElement* itemElement = new TiXmlElement( "Item" );

		if ( StrEqual( itemDefArr[item->itemDefIndex].name.c_str(), "ufo_WallCurve4" ) ) {
			int debug = 1;
		}

		int x, y, r;
		WorldToXYR( item->XForm(), &x, &y, &r, true );

		itemElement->SetAttribute( "x", x );
		itemElement->SetAttribute( "y", y );
		if ( r != 0 )
			itemElement->SetAttribute( "rot", r );
		// old, rigid approach: itemElement->SetAttribute( "index", item->itemDefIndex );
		itemElement->SetAttribute( "name", itemDefArr[item->itemDefIndex].name.c_str() );
		if ( item->hp != 0xffff )
			itemElement->SetAttribute( "hp", item->hp );
		if ( item->flags )
			itemElement->SetAttribute( "flags", item->flags );
		itemsElement->LinkEndChild( itemElement );
	}

	TiXmlElement* imagesElement = new TiXmlElement( "Images" );
	mapElement->LinkEndChild( imagesElement );
	for( int i=0; i<nImageData; ++i ) {
		TiXmlElement* imageElement = new TiXmlElement( "Image" );
		imagesElement->LinkEndChild( imageElement );
		imageElement->SetAttribute( "x", imageData[i].x );
		imageElement->SetAttribute( "y", imageData[i].y );
		imageElement->SetAttribute( "size", imageData[i].size );
		imageElement->SetAttribute( "tileRotation", imageData[i].tileRotation );
		imageElement->SetAttribute( "name", imageData[i].name.c_str() );
	}

	TiXmlElement* groundDebrisElement = new TiXmlElement( "GroundDebris" );
	mapElement->LinkEndChild( groundDebrisElement );

	for( int i=0; i<debris.Size(); ++i ) {
		SaveDebris( debris[i], groundDebrisElement );
	}

	TiXmlElement* pyroGroupElement = new TiXmlElement( "PyroGroup" );
	mapElement->LinkEndChild( pyroGroupElement );
	for( int i=0; i<SIZE*SIZE; ++i ) {
		if ( pyro[i] ) {
			int y = i / SIZE;
			int x = i - SIZE*y;

			TiXmlElement* ele = new TiXmlElement( "Pyro" );
			pyroGroupElement->LinkEndChild( ele );
			ele->SetAttribute( "x", x );
			ele->SetAttribute( "y", y );
			ele->SetAttribute( "fire", PyroFire( x, y ) ? 1 : 0 );
			ele->SetAttribute( "duration", PyroDuration( x, y ) );
		}
	}
}


void Map::Load( const TiXmlElement* mapElement, ItemDef* const* arr )
{
	if ( strcmp( mapElement->Value(), "Game" ) == 0 ) {
		mapElement = mapElement->FirstChildElement( "Map" );
	}
	GLASSERT( mapElement );
	GLASSERT( strcmp( mapElement->Value(), "Map" ) == 0 );
	int sizeX = 64;
	int sizeY = 64;

	mapElement->QueryIntAttribute( "sizeX", &sizeX );
	mapElement->QueryIntAttribute( "sizeY", &sizeY );
	SetSize( sizeX, sizeY );
	nImageData = 0;

	pastSeenFOW.ClearAll();
	const TiXmlElement* pastSeenElement = mapElement->FirstChildElement( "Seen" );
	if ( pastSeenElement ) {
		const char* p = pastSeenElement->GetText();
		if ( p ) {
			pastSeenFOW.FromString( p );
		}
	}

	const TiXmlElement* imagesElement = mapElement->FirstChildElement( "Images" );
	if ( imagesElement ) {
		for(	const TiXmlElement* image = imagesElement->FirstChildElement( "Image" );
				image;
				image = image->NextSiblingElement( "Image" ) )
		{
			int x=0, y=0, size=0;
			char buffer[128];
			int tileRotation = 0;
			image->QueryIntAttribute( "x", &x );
			image->QueryIntAttribute( "y", &y );
			image->QueryIntAttribute( "size", &size );
			image->QueryIntAttribute( "tileRotation", &tileRotation );

			// store it to save later:
			const char* name = image->Attribute( "name" ); 
			GLASSERT( nImageData < MAX_IMAGE_DATA );
			imageData[ nImageData ].x = x;
			imageData[ nImageData ].y = y;
			imageData[ nImageData ].size = size;
			imageData[ nImageData ].tileRotation = tileRotation;
			imageData[ nImageData ].name = image->Attribute( "name" );
			++nImageData;
			
			ImageManager* imageManager = ImageManager::Instance();

			Surface background, day, night;

			SNPrintf( buffer, 128, "%s_TEX", image->Attribute( "name" ) );
			imageManager->LoadImage( buffer, &background );
			SNPrintf( buffer, 128, "%s_DAY", image->Attribute( "name" ) );
			imageManager->LoadImage( buffer, &day );
			SNPrintf( buffer, 128, "%s_NGT", image->Attribute( "name" ) );
			imageManager->LoadImage( buffer, &night );

			SetTexture( &background, x*512/64, y*512/64, tileRotation );
			SetLightMaps( &day, &night, x, y, tileRotation );			
		}
	}

	const TiXmlElement* itemsElement = mapElement->FirstChildElement( "Items" );
	if ( itemsElement ) {
		for(	const TiXmlElement* item = itemsElement->FirstChildElement( "Item" );
				item;
				item = item->NextSiblingElement( "Item" ) )
		{
			int x=0;
			int y=0;
			int rot = 0;
			int index = -1;
			int hp = 0xffff;
			int flags = 0;

			item->QueryIntAttribute( "x", &x );
			item->QueryIntAttribute( "y", &y );
			item->QueryIntAttribute( "rot", &rot );
			item->QueryIntAttribute( "hp", &hp );
			item->QueryIntAttribute( "flags", &flags );

			if ( item->QueryIntAttribute( "index", &index ) != TIXML_NO_ATTRIBUTE ) {
				// good to go - have the deprecated 'index' value.
			}
			else if ( item->Attribute( "name" ) ) {
				const char* name = item->Attribute( "name" );
				const MapItemDef* mid = GetItemDef( name );
				if ( mid ) {
					index = mid - itemDefArr;
				} 
				else {
					GLOUTPUT(( "Could not load item '%s'\n", name ));
				}

				if ( StrEqual( name, "ufo_WallCurve4" ) ) {
					int debug = 1;
				}
			}

			if ( index >= 0 ) {
				AddItem( x, y, rot, index, hp, flags );
			}
		}
	}

	const TiXmlElement* groundDebrisElement = mapElement->FirstChildElement( "GroundDebris" );
	if ( groundDebrisElement ) {
		for( const TiXmlElement* debrisElement = groundDebrisElement->FirstChildElement( "Debris" );
			 debrisElement;
			 debrisElement = debrisElement->NextSiblingElement( "Debris" ) )
		{
			LoadDebris( debrisElement, arr );
		}
	}

	const TiXmlElement* pyroGroupElement = mapElement->FirstChildElement( "PyroGroup" );
	if ( pyroGroupElement ) {
		for( const TiXmlElement* pyroElement = pyroGroupElement->FirstChildElement( "Pyro" );
			 pyroElement;
			 pyroElement = pyroElement->NextSiblingElement( "Pyro" ) )
		{
			int x=0, y=0, fire=0, duration=0;
			pyroElement->QueryIntAttribute( "x", &x );
			pyroElement->QueryIntAttribute( "y", &y );
			pyroElement->QueryIntAttribute( "fire", &fire );
			pyroElement->QueryIntAttribute( "duration", &duration );
			SetPyro( x, y, duration, fire );
		}
	}
}


void Map::DeleteAt( int x, int y )
{
	GLASSERT( x >= 0 && x < width );
	GLASSERT( y >= 0 && y < height );

	MapItem* item = quadTree.FindItems( x, y, 0, MapItem::MI_IS_LIGHT );
	// Delete the *last* item so it behaves more naturally when editing.
	while( item && item->next )
		item = item->next;

	if ( item ) {
		DeleteItem( item );
	}
}


void Map::QueryAllDoors( CDynArray< grinliz::Vector2I >* doors )
{
	Rectangle2I bounds;
	CalcBounds( &bounds );

	MapItem* item = quadTree.FindItems( bounds, MapItem::MI_DOOR, 0 );
	while( item ) {
		int x, y, r;
		WorldToXYR( item->XForm(), &x, &y, &r );

		Vector2I v = { x, y };
		doors->Push( v );
		item = item->next;
	}
}


bool Map::OpenDoor( int x, int y, bool open ) 
{
	bool opened = false;

	MapItem* item = quadTree.FindItems( x, y, MapItem::MI_DOOR, 0 );
	GLASSERT( !item || !item->next );	// should only be one...

	for( ; item; item=item->next ) {
		const MapItemDef& itemDef = itemDefArr[item->itemDefIndex];
		GLASSERT( itemDef.IsDoor() );
		GLASSERT( itemDef.modelResourceOpen );

		if ( !item->Destroyed() ) {
			GLASSERT( item->model );

			Vector3F pos = item->model->Pos();
			float rot = item->model->GetRotation();

			const ModelResource* res = 0;
			if ( open && item->open == 0 ) {
				item->open = 1;
				res = itemDef.modelResourceOpen;
			}
			else if ( !open && item->open == 1 ) {
				item->open = 0;
				res = itemDef.modelResource;
			}

			if ( res ) {
				opened = true;
				tree->FreeModel( item->model );

				Model* model = tree->AllocModel( res );
				model->SetFlag( Model::MODEL_OWNED_BY_MAP );
				model->SetPos( pos );
				model->SetRotation( rot );
				item->model = model;

				Rectangle2I mapBounds = item->MapBounds();

				ResetPath();
				ClearVisPathMap( mapBounds );
				CalcVisPathMap( mapBounds );
			}
		}
	}
	return opened;
}


const Storage* Map::GetStorage( int x, int y ) const
{
	for( int i=0; i<debris.Size(); ++i ) {
		if ( debris[i].x == x && debris[i].y ==y ) {
			return debris[i].storage;
		}
	}
	return 0;
}


void Map::FindStorage( const ItemDef* itemDef, int maxLoc, grinliz::Vector2I* loc, int* numLoc )
{
	*numLoc = 0;
	for( int i=0; i<debris.Size() && *numLoc < maxLoc; ++i ) {
		if ( debris[i].storage->IsResupply( itemDef->IsWeapon() ) ) {
			loc[ *numLoc ].Set( debris[i].x, debris[i].y );
			*numLoc += 1;
		}
	}
}


Storage* Map::LockStorage( int x, int y )
{
	Storage* s = 0;
	for( int i=0; i<debris.Size(); ++i ) {
		if ( debris[i].x == x && debris[i].y ==y ) {
			s = debris[i].storage;
			tree->FreeModel( debris[i].crate );
			debris.SwapRemove( i );
			break;
		}
	}
	return s;
}


void Map::ReleaseStorage( int x, int y, Storage* storage, ItemDef* const* arr )
{
#ifdef DEBUG
	for( int i=0; i<debris.Size(); ++i ) {
		if ( debris[i].x == x && debris[i].y == y ) {
			GLASSERT( 0 );
			return;
		}
	}
#endif

	if ( storage && storage->Empty() ) {
		delete storage;
		storage = 0;
	}
	if ( !storage )
		return;

	Debris* d = debris.Push();
	d->storage = storage;

	bool zRotate = false;
	const ModelResource* res = storage->VisualRep( &zRotate );

	Model* model = tree->AllocModel( res );
	if ( zRotate ) {
		model->SetRotation( 90.0f, 2 );
		
		Vector2I v = { x, y };
		int yRot = Random::Hash( &v, sizeof(v) ) % 360;	// generate a random yet consistent rotation

		model->SetRotation( (float)yRot, 1 );
		model->SetPos( (float)x+0.5f, 0.05f, (float)y+0.5f );
	}
	else {
		model->SetPos( (float)x+0.5f, 0.0f, (float)y+0.5f );
	}
	// Don't set: model->SetFlag( Model::MODEL_OWNED_BY_MAP );	not really owned by map, in the sense of mapBounds, etc.
	d->crate = model;
	d->x = x;
	d->y = y;
}


void Map::DumpTile( int x, int y )
{
	if ( InRange( x, 0, SIZE-1 ) && InRange( y, 0, SIZE-1 )) 
	{
		int i=0;
		MapItem* root = quadTree.FindItems( x, y, 0, 0 );
		while ( root ) {
			GLASSERT( root->itemDefIndex > 0 );
			const MapItemDef& itemDef = itemDefArr[ root->itemDefIndex ];
			GLASSERT( !itemDef.name.empty() );

			int r = root->modelRotation;
			UFOText::Draw( 0, 100-12*i, "%s r=%d", itemDef.name.c_str(), r );

			++i;
			root = root->next;
		}
	}
}


void Map::DrawPath()
{
	CompositingShader shader( true );

	for( int j=0; j<SIZE; ++j ) {
		for( int i=0; i<SIZE; ++i ) {
			float x = (float)i;
			float y = (float)j;

			//const Tile& tile = tileArr[j*SIZE+i];
			int path = GetPathMask( PATH_TYPE, i, j );

			if ( path == 0 ) 
				continue;

			Vector3F red[12];
			Vector3F green[12];
			static const int index[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
			int	nRed = 0, nGreen = 0;	
			const float EPS = 0.2f;

			Vector3F edge[5] = {
				{ x, EPS, y+1.0f },
				{ x+1.0f, EPS, y+1.0f },
				{ x+1.0f, EPS, y },
				{ x, EPS, y },
				{ x, EPS, y+1.0f }
			};
			Vector3F center = { x+0.5f, 0, y+0.5f };

			for( int k=0; k<4; ++k ) {
				int mask = (1<<k);

				if ( path & mask ) {
					red[nRed+0] = edge[k];
					red[nRed+1] = edge[k+1];
					red[nRed+2] = center;
					nRed += 3;
				}
				else {
					green[nGreen+0] = edge[k];
					green[nGreen+1] = edge[k+1];
					green[nGreen+2] = center;
					nGreen += 3;
				}
			}
			GLASSERT( nRed <= 12 );
			GLASSERT( nGreen <= 12 );

			shader.SetColor( 1, 0, 0, 0.5f );
			shader.SetVertex( 3, 0, red );
			shader.Draw( nRed, index );

			shader.SetColor( 0, 1, 0, 0.5f );
			shader.SetVertex( 3, 0, green );
			shader.Draw( nGreen, index );
		}
	}
}


void Map::ResetPath()
{
	microPather->Reset();
	++pathQueryID;
	++visibilityQueryID;
}


void Map::SetPathBlocks( const grinliz::BitArray<Map::SIZE, Map::SIZE, 1>& block )
{
	if ( block != pathBlock ) {
		ResetPath();
		pathBlock = block;
	}
}


void Map::ClearVisPathMap( grinliz::Rectangle2I& bounds )
{
	for( int j=bounds.min.y; j<=bounds.max.y; ++j ) {
		for( int i=bounds.min.x; i<=bounds.max.x; ++i ) {
			visMap[j*SIZE+i] = 0;
			pathMap[j*SIZE+i] = 0;
		}
	}
}


void Map::CalcVisPathMap( grinliz::Rectangle2I& bounds )
{
	MapItem* item = quadTree.FindItems( bounds, 0, MapItem::MI_IS_LIGHT );
	while( item ) {
		if ( !item->Destroyed() ) {
			GLASSERT( item->itemDefIndex > 0 );
			const MapItemDef& itemDef = itemDefArr[item->itemDefIndex];
			
			int rot = item->modelRotation;
			GLASSERT( rot >= 0 && rot < 4 );

			Matrix2I mat = item->XForm();

			// Walk the object in object space & write to world.
			for( int j=0; j<itemDef.cy; ++j ) {
				for( int i=0; i<itemDef.cx; ++i ) {
					
					Vector2I obj = { i, j };
					Vector2I world = mat * obj;

					GLASSERT( world.x >= 0 && world.x < SIZE );
					GLASSERT( world.y >= 0 && world.y < SIZE );
					GLASSERT( obj.x < MapItemDef::MAX_CX && obj.y < MapItemDef::MAX_CY );

					// Account for tile rotation. (Actually a bit rotation too, which is handy.)
					// The OR operation is important. This routine will write outside of the bounds,
					// and should do no damage.
					//
					// Open doors don't impede sight or movement.
					//
					if ( item->open == 0 )
					{
						// Path
						U32 p = ( itemDef.pather[obj.y][obj.x] << rot );
						GLASSERT( p == 0 || !itemDef.IsLight() );
						p = p | (p>>4);
						pathMap[ world.y*SIZE + world.x ] |= p;
					}
					if ( item->open == 0 )
					{
						// Visibility
						U32 p = ( itemDef.visibility[obj.y][obj.x] << rot );
						GLASSERT( p == 0 || !itemDef.IsLight() );
						p = p | (p>>4);
						visMap[ world.y*SIZE + world.x ] |= p;
					}
				}
			}
		}
		item = item->next;
	}
}


int Map::GetPathMask( ConnectionType c, int x, int y )
{
	// fast return: if the c is set, we're done.
	if ( c == PATH_TYPE && pathBlock.IsSet( x, y ) ) {
		return 0xf;
	}
	return ( c==PATH_TYPE) ? pathMap[y*SIZE+x] : visMap[y*SIZE+x];
}


float Map::LeastCostEstimate( void* stateStart, void* stateEnd )
{
	Vector2<S16> start, end;
	StateToVec( stateStart, &start );
	StateToVec( stateEnd, &end );

	float dx = (float)(start.x-end.x);
	float dy = (float)(start.y-end.y);

	return sqrtf( dx*dx + dy*dy );
}


bool Map::Connected( ConnectionType c, int x, int y, int dir )
{
	const Vector2I next[4] = {
		{ 0, 1 },
		{ 1, 0 },
		{ 0, -1 },
		{ -1, 0 } 
	};

	GLASSERT( dir >= 0 && dir < 4 );
	GLASSERT( x >=0 && x < SIZE );
	GLASSERT( y >=0 && y < SIZE );

	int bit = 1<<dir;
	Vector2I pos = { x, y };
	Vector2I nextPos = pos + next[dir];
	Rectangle2I mapBounds;
	CalcBounds( &mapBounds );

	// To be connected, it must be clear from a->b and b->a
	if ( mapBounds.Contains( pos ) && mapBounds.Contains( nextPos ) ) {
		int mask0 = GetPathMask( c, pos.x, pos.y );
		int maskN = GetPathMask( c, nextPos.x, nextPos.y );
		int inv = InvertPathMask( bit );

		if ( (( mask0 & bit ) == 0 ) && (( maskN & inv ) == 0 ) ) {
			return true;
		}
	}
	return false;
}


void Map::AdjacentCost( void* state, MP_VECTOR< micropather::StateCost > *adjacent )
{
	Vector2<S16> pos;
	StateToVec( state, &pos );

	const Vector2<S16> next[8] = {
		{ 0, 1 },
		{ 1, 0 },
		{ 0, -1 },
		{ -1, 0 },

		{ 1, 1 },
		{ 1, -1 },
		{ -1, -1 },
		{ -1, 1 },
	};
	const float SQRT2 = 1.414f;
	const float cost[8] = {	1.0f, 1.0f, 1.0f, 1.0f, SQRT2, SQRT2, SQRT2, SQRT2 };

	adjacent->resize( 0 );
	// N S E W
	for( int i=0; i<4; i++ ) {
		if ( Connected( PATH_TYPE, pos.x, pos.y, i ) ) {
			Vector2<S16> nextPos = pos + next[i];

			micropather::StateCost stateCost;
			stateCost.cost = cost[i];
			stateCost.state = VecToState( nextPos );
			adjacent->push_back( stateCost );
		}
	}
	// Diagonals. Need to check if all the NSEW connections work. If
	// so, then the diagonal connection works too.
	for( int i=0; i<4; i++ ) {
		int j=(i+1)&3;
		Vector2<S16> nextPos = pos + next[i+4];
		int iInv = (i+2)&3;
		int jInv = (j+2)&3;
		 
		if (    Connected( PATH_TYPE, pos.x, pos.y, i ) 
			 && Connected( PATH_TYPE, pos.x, pos.y, j )
			 && Connected( PATH_TYPE, nextPos.x, nextPos.y, iInv )
			 && Connected( PATH_TYPE, nextPos.x, nextPos.y, jInv ) ) 
		{
			micropather::StateCost stateCost;
			stateCost.cost = cost[i+4];
			stateCost.state = VecToState( nextPos );
			adjacent->push_back( stateCost );
		}
	}
}


void Map::PrintStateInfo( void* state )
{
	Vector2<S16> pos;
	StateToVec( state, &pos );
	GLOUTPUT(( "[%d,%d]", pos.x, pos.y ));
}


int Map::SolvePath( const void* user, const Vector2<S16>& start, const Vector2<S16>& end, float *cost, MP_VECTOR< Vector2<S16> >* path )
{
	GLASSERT( sizeof( int ) == sizeof( void* ));			// fix this for 64 bit
	GLASSERT( sizeof(Vector2<S16>) == sizeof( void* ) );
	GLASSERT( pathBlocker );
	if ( pathBlocker ) {
		pathBlocker->MakePathBlockCurrent( this, user );
	}
	int result = microPather->Solve(	VecToState( start ),
										VecToState( end ),
										reinterpret_cast< MP_VECTOR<void*>* >( path ),		// sleazy trick if void* is the same size as V2<S16>
										cost );

#ifdef DEBUG
	{
		for( unsigned i=0; i<path->size(); ++i ) {
			GLASSERT( !pathBlock.IsSet( (*path)[i].x, (*path)[i].y ) );
		}
	}
#endif

	/*
	switch( result ) {
		case MicroPather::SOLVED:			GLOUTPUT(( "Solved nPath=%d\n", *nPath ));			break;
		case MicroPather::NO_SOLUTION:		GLOUTPUT(( "NoSolution nPath=%d\n", *nPath ));		break;
		case MicroPather::START_END_SAME:	GLOUTPUT(( "StartEndSame nPath=%d\n", *nPath ));	break;
		case MicroPather::OUT_OF_MEMORY:	GLOUTPUT(( "OutOfMemory nPath=%d\n", *nPath ));		break;
		default:	GLASSERT( 0 );	break;
	}
	*/

	return result;
}


void Map::ShowNearPath(	const grinliz::Vector2I& unitPos,
						const void* user, 
						const grinliz::Vector2<S16>& start, 
					    float maxCost,
					    const grinliz::Vector2F* range )
{
	if ( pathBlocker ) {
		pathBlocker->MakePathBlockCurrent( this, user );
	}
	stateCostArr.clear();
	microPather->SolveForNearStates( VecToState( start ), &stateCostArr, maxCost );

	/*
	GLOUTPUT(( "Near states, result=%d\n", result ));
	for( unsigned m=0; m<stateCostArr.size(); ++m ) {
		Vector2<S16> v;
		StateToVec( stateCostArr[m].state, &v );
		GLOUTPUT(( "  (%d,%d) cost=%.1f\n", v.x, v.y, stateCostArr[m].cost ));
	}
	*/

	walkingMap.SetVisible( true );
	walkingMap.Clear();
	walkingMap.SetSize( (float)(2*MAX_TU+1), (float)(2*MAX_TU+1) );

	Vector2I origin = { (unitPos.x - MAX_TU), (unitPos.y - MAX_TU) };
	walkingMap.SetPos( (float)origin.x, (float)origin.y );

	gamui::RenderAtom atom[3] = {	UIRenderer::CalcIconAtom( ICON_GREEN_WALK_MARK ), 
									UIRenderer::CalcIconAtom( ICON_YELLOW_WALK_MARK ), 
									UIRenderer::CalcIconAtom( ICON_ORANGE_WALK_MARK ) };
	atom[0].renderState = (const void*) RENDERSTATE_MAP_TRANSLUCENT;
	atom[1].renderState = (const void*) RENDERSTATE_MAP_TRANSLUCENT;
	atom[2].renderState = (const void*) RENDERSTATE_MAP_TRANSLUCENT;

	for( unsigned i=0; i<stateCostArr.size(); ++i ) {
		const micropather::StateCost& stateCost = stateCostArr[i];
		Vector2<S16> v;
		StateToVec( stateCost.state, &v );

		// don't draw where standing.
		if ( v.x == unitPos.x && v.y == unitPos.y )
			continue;

		for( int k=0; k<3; ++k ) {
			if ( stateCost.cost >= range[k].x && stateCost.cost < range[k].y ) {
				walkingMap.SetTile( v.x-origin.x, v.y-origin.y, atom[k] );
				break;
			}
		}
	}

}


void Map::ClearNearPath()
{
	walkingMap.SetVisible( false );
}


bool Map::CanSee( const grinliz::Vector2I& p, const grinliz::Vector2I& q, ConnectionType connection )
{
	GLASSERT( InRange( q.x-p.x, -1, 1 ) );
	GLASSERT( InRange( q.y-p.y, -1, 1 ) );
	if ( p.x < 0 || p.x >= SIZE || p.y < 0 || p.y >= SIZE )
		return false;
	if ( q.x < 0 || q.x >= SIZE || q.y < 0 || q.y >= SIZE )
		return false;

	int dx = q.x-p.x;
	int dy = q.y-p.y;
	bool canSee = false;
	int dir0 = -1;
	int dir1 = -1;

	switch( dy*4 + dx ) {
		case 4:		// S
			dir0 = 0;
			break;
		case 5:		// SE
			dir0 = 0;
			dir1 = 1;
			break;
		case 1:		// E
			dir0 = 1;
			break;
		case -3:	// NE
			dir0 = 1;
			dir1 = 2;
			break;
		case -4:	// N
			dir0 = 2;
			break;
		case -5:	// NW
			dir0 = 2;
			dir1 = 3;
			break;
		case -1:	// W
			dir0 = 3;
			break;
		case 3:		// SW
			dir0 = 3;
			dir1 = 0;
			break;
		default:
			GLASSERT( 0 );
			break;
	}
	if ( dir1 == -1 ) {
		canSee = Connected( connection, p.x, p.y, dir0 );
	}
	else {
		const Vector2I next[4] = {	{ 0, 1 },
									{ 1, 0 },
									{ 0, -1 },
									{ -1, 0 } };

		Vector2I q0 = p+next[dir0];
		Vector2I q1 = p+next[dir1];

		canSee =  ( Connected( connection, p.x, p.y, dir0 ) && Connected( VISIBILITY_TYPE, q0.x, q0.y, dir1 ) )
			   || ( Connected( connection, p.x, p.y, dir1 ) && Connected( VISIBILITY_TYPE, q1.x, q1.y, dir0 ) );
	}
	return canSee;
}


void Map::MapBoundsOfModel( const Model* m, grinliz::Rectangle2I* mapBounds )
{
	GLASSERT( m->IsFlagSet( Model::MODEL_OWNED_BY_MAP ) );
	MapItem* item = quadTree.FindItem( m );
	GLASSERT( item );
	*mapBounds = item->MapBounds();
	GLASSERT( mapBounds->min.x <= mapBounds->max.x );
	GLASSERT( mapBounds->min.y <= mapBounds->max.y );
}


Map::QuadTree::QuadTree()
{
	Clear();
	filterModel = 0;

	int base = 0;
	for( int i=0; i<QUAD_DEPTH+1; ++i ) {
		depthBase[i] = base;
		base += (1<<i)*(1<<i);
	}
}


void Map::QuadTree::Clear()
{
	memset( tree, 0, sizeof(MapItem*)*NUM_QUAD_NODES );
	memset( depthUse, 0, sizeof(int)*(QUAD_DEPTH+1) );
}


void Map::QuadTree::Add( MapItem* item )
{
	int d=0;
	int i = CalcNode( item->mapBounds8, &d );
	item->nextQuad = tree[i];
	tree[i] = item;
	depthUse[d] += 1;

//	GLOUTPUT(( "QuadTree::Add %x id=%d (%d,%d)-(%d,%d) at depth=%d\n", item, item->itemDefIndex,
//				item->mapBounds8.min.x, item->mapBounds8.min.y, item->mapBounds8.max.x, item->mapBounds8.max.y,
//				d ));

	//GLOUTPUT(( "Depth: %2d %2d %2d %2d %2d\n", 
	//		   depthUse[0], depthUse[1], depthUse[2], depthUse[3], depthUse[4] ));
}


void Map::QuadTree::UnlinkItem( MapItem* item )
{
	int d=0;
	int index = CalcNode( item->mapBounds8, &d );
	depthUse[d] -= 1;
	GLASSERT( tree[index] ); // the item should be in the linked list somewhere.

//	GLOUTPUT(( "QuadTree::UnlinkItem %x id=%d\n", item, item->itemDefIndex ));

	MapItem* prev = 0;
	for( MapItem* p=tree[index]; p; prev=p, p=p->nextQuad ) {
		if ( p == item ) {
			if ( prev )
				prev->nextQuad = p->nextQuad;
			else
				tree[index] = p->nextQuad;
			return;
		}
	}
	GLASSERT( 0 );	// should have found the item.
}


Map::MapItem* Map::QuadTree::FindItems( const Rectangle2I& bounds, int required, int excluded )
{
	// Walk the map and pull out items in bounds.
	MapItem* root = 0;

	GLASSERT( bounds.min.x >= 0 && bounds.max.x < 256 );	// using int8
	GLASSERT( bounds.min.y >= 0 && bounds.max.y < 256 );	// using int8
	Rectangle2<U8> bounds8;
	bounds8.Set( bounds.min.x, bounds.min.y, bounds.max.x, bounds.max.y );

	for( int depth=0; depth<QUAD_DEPTH; ++depth ) 
	{
		// Translate into coordinates for this node level:
		int x0 = WorldToNode( bounds.min.x, depth );
		int x1 = WorldToNode( bounds.max.x, depth );
		int y0 = WorldToNode( bounds.min.y, depth );
		int y1 = WorldToNode( bounds.max.y, depth );

		// i, j, x0.. all in node coordinates.
		for( int j=y0; j<=y1; ++j ) {
			for( int i=x0; i<=x1; ++i ) {
				MapItem* pItem = *(tree + depthBase[depth] + NodeOffset( i, j, depth ) );

				if ( filterModel ) {
					while( pItem ) {
						if ( pItem->model == filterModel ) {
							pItem->next = 0;
							return pItem;
						}
						pItem = pItem->nextQuad;
					}
				}
				else {
					while( pItem ) { 
						if (    ( ( pItem->flags & required) == required )
							 && ( ( pItem->flags & excluded ) == 0 )
							 && pItem->mapBounds8.Intersect( bounds8 ) )
						{
							pItem->next = root;
							root = pItem;
						}
						pItem = pItem->nextQuad;
					}
				}
			}
		}
	}
	return root;
}


Map::MapItem* Map::QuadTree::FindItem( const Model* model )
{
	GLASSERT( model->IsFlagSet( Model::MODEL_OWNED_BY_MAP ) );
	Rectangle2I b;

	// May or may not have 'mapBoundsCache' when called.
	if ( model->mapBoundsCache.min.x >= 0 ) {
		b = model->mapBoundsCache;
	}
	else {
		// Need a little space around the coordinates to account
		// for object rotation.
		b.min.x = Clamp( (int)model->X()-2, 0, SIZE-1 );
		b.min.y = Clamp( (int)model->Z()-2, 0, SIZE-1 );
		b.max.x = Clamp( (int)model->X()+2, 0, SIZE-1 );
		b.max.y = Clamp( (int)model->Z()+2, 0, SIZE-1 );
	}
	filterModel = model;
	MapItem* root = FindItems( b, 0, 0 );
	filterModel = 0;
	GLASSERT( root && root->next == 0 );
	return root;
}


int Map::QuadTree::CalcNode( const Rectangle2<U8>& bounds, int* d )
{
	int offset = 0;

	for( int depth=QUAD_DEPTH-1; depth>0; --depth ) 
	{
		int x0 = WorldToNode( bounds.min.x, depth );
		int y0 = WorldToNode( bounds.min.y, depth );

		int wSize = SIZE >> depth;

		Rectangle2<U8> wBounds;
		wBounds.Set(	NodeToWorld( x0, depth ),
						NodeToWorld( y0, depth ),
						NodeToWorld( x0, depth ) + wSize - 1,
						NodeToWorld( y0, depth ) + wSize - 1 );

		if ( wBounds.Contains( bounds ) ) {
			offset = depthBase[depth] + NodeOffset( x0, y0, depth );
			if ( d )
				*d = depth;
			break;
		}
	}
	GLASSERT( offset >= 0 && offset < NUM_QUAD_NODES );
	return offset;
}


void Map::CreateTexture( Texture* t )
{
	if ( t == backgroundTexture ) {
		t->Upload( backgroundSurface );
	}
	else if ( t == greyTexture ) {
		t->Upload( greySurface );
	}
	else if ( t == lightMapTex ) {
		t->Upload( lightMap[1] );
	}
	else {
		GLASSERT( 0 );
	}
}


void Map::BeginRender()
{
	gamuiShader.PushMatrix( GPUShader::MODELVIEW_MATRIX );

	Matrix4 rot;
	rot.SetXRotation( 90 );
	gamuiShader.MultMatrix( GPUShader::MODELVIEW_MATRIX, rot );
}


void Map::EndRender()
{
	gamuiShader.PopMatrix( GPUShader::MODELVIEW_MATRIX );
}


void Map::BeginRenderState( const void* renderState )
{
	const float ALPHA = 0.5f;
	switch( (int)renderState ) {
		case UIRenderer::RENDERSTATE_UI_NORMAL_OPAQUE:
		case RENDERSTATE_MAP_OPAQUE:
			gamuiShader.SetColor( 1, 1, 1, 1 );
			gamuiShader.SetBlend( false );
			break;

		case UIRenderer::RENDERSTATE_UI_NORMAL:
		case RENDERSTATE_MAP_NORMAL:
			gamuiShader.SetColor( 1.0f, 1.0f, 1.0f, 0.8f );
			gamuiShader.SetBlend( true );
			break;

		case RENDERSTATE_MAP_TRANSLUCENT:
			gamuiShader.SetColor( 1, 1, 1, ALPHA );
			gamuiShader.SetBlend( true );
			break;
		default:
			GLASSERT( 0 );
	}
}


void Map::BeginTexture( const void* textureHandle )
{
	gamuiShader.SetTexture0( (Texture*)textureHandle );
}


void Map::Render( const void* renderState, const void* textureHandle, int nIndex, const int16_t* index, int nVertex, const gamui::Gamui::Vertex* vertex )
{
	//glVertexPointer(   2, GL_FLOAT, sizeof(gamui::Gamui::Vertex), &vertex[0].x );
	//glTexCoordPointer( 2, GL_FLOAT, sizeof(gamui::Gamui::Vertex), &vertex[0].tx ); 
	gamuiShader.SetVertex( 2, sizeof(gamui::Gamui::Vertex), &vertex[0].x );
	gamuiShader.SetTexture0( 2, sizeof(gamui::Gamui::Vertex), &vertex[0].tx );

	gamuiShader.Draw( nIndex, (const uint16_t*)index );
}
