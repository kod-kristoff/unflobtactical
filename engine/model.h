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

#ifndef UFOATTACK_MODEL_INCLUDED
#define UFOATTACK_MODEL_INCLUDED

#include "../grinliz/gldebug.h"
#include "../grinliz/gltypes.h"
#include "../grinliz/gltypes.h"
#include "vertex.h"
#include "enginelimits.h"
#include "serialize.h"

class Texture;
class SpaceTree;
class RenderQueue;

struct ModelAtom 
{
	const Texture* texture;
	U32 vertexID;
	U32 indexID;
	U32 nVertex;
	U32 nIndex;
	mutable int trisRendered;

	void Bind( bool bindTextureToVertex ) const;
	void Draw() const;
};


class ModelResource
{
public:
	ModelHeader header;				// loaded

	SphereX boundSphere;			// computed
	grinliz::Fixed boundRadius2D;	// computed, bounding 2D radius centered at 0,0
	Rectangle3X hitBounds;			// for picking - a bounds approximation

	ModelAtom atom[EL_MAX_MODEL_GROUPS];
};


class ModelLoader
{
public:
	ModelLoader( const Texture* texture, int nTextures ) 	{
		this->texture = texture;
		this->nTextures = nTextures;
	}
	~ModelLoader()		{}

	void Load( FILE* fp, ModelResource* res );

private:
	const Texture* texture;
	int nTextures;

	U16		index[EL_MAX_INDEX_IN_MODEL];
	Vertex	vertex[EL_MAX_VERTEX_IN_MODEL];
};


class Model
{
public:
	Model()		{	Init( 0, 0 ); }
	~Model()	{}

	void Init( ModelResource* resource, SpaceTree* tree );

	// Queued rendering
	enum {
		MODEL_TEXTURE,	// use the texture of the models
		NO_TEXTURE,		// no texture at all - shadow pass Z
		TEXTURE_SET		// used for background texture tricks - shadow pass ONE_PASS
	};
	void Queue( RenderQueue* queue, int textureState=MODEL_TEXTURE );

	// Used by the queued rendering system:
	void PushMatrix( bool bindTextureToVertex ) const;
	void PopMatrix( bool bindTextureToVertex ) const;

	bool IsDraggable()	{ return isDraggable; }
	void SetDraggable( bool drag )	{ isDraggable = drag; }
	bool IsHiddenFromTree()			{ return hiddenFromTree; }
	void HideFromTree( bool hide )  { hiddenFromTree = hide; }
	
	const Vector3X& Pos()						{ return pos; }
	void SetPos( const Vector3X& pos );
	void SetPos( float x, float y, float z )	{ Vector3X vec = { grinliz::Fixed(x), grinliz::Fixed(y), grinliz::Fixed(z) }; SetPos( vec ); }
	void SetPos( grinliz::Fixed x, grinliz::Fixed y, grinliz::Fixed z )	{ Vector3X vec = { grinliz::Fixed(x), grinliz::Fixed(y), grinliz::Fixed(z) }; SetPos( vec ); }

	void SetYRotation( grinliz::Fixed rot )		{
		while( rot < 0 )		{ rot += 360; }
		while( rot >= 360 )		{ rot -= 360; }
		this->rot = rot;
	}
	void SetYRotation( float rot )				{ SetYRotation( grinliz::Fixed( rot ) ); }
	const grinliz::Fixed GetYRotation()			{ return rot; }

	void SetSkin( int armor, int skin, int hair );

	void CalcBoundSphere( SphereX* spherex );
	void CalcBoundCircle( CircleX* circlex );
	void CalcHitAABB( Rectangle3X* aabb );

	ModelResource* GetResource()				{ return resource; }
	bool Sentinel()								{ return resource==0 && tree==0; }

	Model* next;	// used by the SpaceTree query
	Model* next0;	// used by the Engine sub-sorting

private:
	SpaceTree* tree;
	ModelResource* resource;
	Vector3X pos;
	grinliz::Fixed rot;
	grinliz::Fixed textureOffsetX;

	bool isDraggable;
	bool hiddenFromTree;
};


#endif // UFOATTACK_MODEL_INCLUDED