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

#include "uirendering.h"
#include "platformgl.h"
#include "texture.h"
#include "../grinliz/glvector.h"
#include "text.h"

using namespace grinliz;

extern int trianglesRendered;	// FIXME: should go away once all draw calls are moved to the enigine
extern int drawCalls;			// ditto


void UIRenderer::BeginRender()
{
	CHECK_GL_ERROR;
	
	glEnable( GL_TEXTURE_2D );
	glDisable( GL_DEPTH_TEST );

	glColor4f( 1, 1, 1, 1 );
	glDisable( GL_LIGHTING );

	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glDisableClientState( GL_NORMAL_ARRAY );

	glEnable( GL_BLEND );
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	CHECK_GL_ERROR;
}


void UIRenderer::EndRender()
{
	CHECK_GL_ERROR;
	glEnable( GL_DEPTH_TEST );

	glEnableClientState( GL_VERTEX_ARRAY );
	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glDisableClientState( GL_COLOR_ARRAY );
	glEnableClientState( GL_NORMAL_ARRAY );
	CHECK_GL_ERROR;
}


void UIRenderer::BeginRenderState( const void* renderState )
{
	switch ( (int)renderState )
	{
	case RENDERSTATE_NORMAL:
		glColor4f( 1, 1, 1, 1 );
		break;

	case RENDERSTATE_DISABLED:
		glColor4f( 1, 1, 1, 0.5f );
		break;

	case RENDERSTATE_DECO:
		glColor4f( 1, 1, 1, 0.7f );
		break;

	case RENDERSTATE_DECO_DISABLED:
		glColor4f( 1, 1, 1, 0.2f );
		break;

	default:
		GLASSERT( 0 );
		break;
	}

}


void UIRenderer::BeginTexture( const void* textureHandle )
{
	Texture* texture = (Texture*)textureHandle;
	glBindTexture( GL_TEXTURE_2D, texture->GLID() );
}


void UIRenderer::Render( const void* renderState, const void* textureHandle, int nIndex, const int16_t* index, int nVertex, const gamui::Gamui::Vertex* vertex )
{
	glVertexPointer(   2, GL_FLOAT, sizeof(gamui::Gamui::Vertex), &vertex[0].x );
	glTexCoordPointer( 2, GL_FLOAT, sizeof(gamui::Gamui::Vertex), &vertex[0].tx );
	glDrawElements( GL_TRIANGLES, nIndex, GL_UNSIGNED_SHORT, index );
	
	trianglesRendered += nIndex / 3;
	drawCalls++;
}


void UIRenderer::SetAtomCoordFromPixel( int x0, int y0, int x1, int y1, int w, int h, gamui::RenderAtom* atom )
{
	atom->tx0 = (float)x0 / (float)w;
	atom->tx1 = (float)x1 / (float)w;

	atom->ty0 = (float)(h-y1) / (float)h;
	atom->ty1 = (float)(h-y0) / (float)h;
}


gamui::RenderAtom UIRenderer::CalcDecoAtom( int id, bool enabled )
{
	GLASSERT( id >= 0 && id < 32 );
	Texture* texture = TextureManager::Instance()->GetTexture( "iconDeco" );
	int y = id / 8;
	int x = id - y*8;
	float tx0 = (float)x / 8.f;
	float ty0 = (float)y / 4.f;
	float tx1 = tx0 + 1.f/8.f;
	float ty1 = ty0 + 1.f/4.f;

	return gamui::RenderAtom( enabled ? RENDERSTATE_DECO : RENDERSTATE_DECO_DISABLED, texture, tx0, ty0, tx1, ty1, 64, 64 );
}


gamui::RenderAtom UIRenderer::CalcParticleAtom( int id, bool enabled )
{
	GLASSERT( id >= 0 && id < 16 );
	Texture* texture = TextureManager::Instance()->GetTexture( "particleQuad" );
	int y = id / 4;
	int x = id - y*4;
	float tx0 = (float)x / 4.f;
	float ty0 = (float)y / 4.f;
	float tx1 = tx0 + 1.f/4.f;
	float ty1 = ty0 + 1.f/4.f;

	return gamui::RenderAtom( enabled ? RENDERSTATE_NORMAL : RENDERSTATE_DISABLED, texture, tx0, ty0, tx1, ty1, 64, 64 );
}


gamui::RenderAtom UIRenderer::CalcIconAtom( int id, bool enabled )
{
	GLASSERT( id >= 0 && id < 32 );
	Texture* texture = TextureManager::Instance()->GetTexture( "icons" );
	int y = id / 8;
	int x = id - y*8;
	float tx0 = (float)x / 8.f;
	float ty0 = (float)y / 4.f;
	float tx1 = tx0 + 1.f/8.f;
	float ty1 = ty0 + 1.f/4.f;

	return gamui::RenderAtom( enabled ? RENDERSTATE_NORMAL : RENDERSTATE_DISABLED, texture, tx0, ty0, tx1, ty1, 64, 64 );
}


gamui::RenderAtom UIRenderer::CalcPaletteAtom( int id, float w, float h, bool enabled )
{
	Vector2I c = { 0, 0 };
	Texture* texture = TextureManager::Instance()->GetTexture( "palette" );

	switch ( id ) {
	case PALETTE_RED:			c.Set( 174, 174 );			break;
	case PALETTE_GREEN:			c.Set( 74, 74 );			break;
	case PALETTE_BRIGHT_GREEN:	c.Set( 12, 79 );			break;
	case PALETTE_DARK_GREY:		c.Set( 34, 277 );			break;
	default: GLASSERT( 0 );									break;
	}

	gamui::RenderAtom atom( enabled ? RENDERSTATE_NORMAL : RENDERSTATE_DISABLED, texture, 0, 0, 0, 0, w, h );
	SetAtomCoordFromPixel( c.x, c.y, c.x, c.y, 300, 300, &atom );
	return atom;
}


void UIRenderer::GamuiGlyph( int c, gamui::IGamuiText::GlyphMetrics* metric )
{
	int advance=0;
	int width=0;
	Rectangle2I src;
	if ( c >= 32 && c < 128 ) {
		UFOText::Metrics( c-32, &advance, &width, &src );

		static const float CX = (1.f/256.f);
		static const float CY = (1.f/128.f);

		metric->advance = (float)advance;
		metric->width = (float)width;
		metric->height = 16.0f;

		metric->tx0 = (float)src.min.x * CX;
		metric->tx1 = (float)src.max.x * CX;

		metric->ty1 = (float)src.min.y * CY;
		metric->ty0 = (float)src.max.y * CY;
	}
	else {
		metric->advance = 1.0f;
		metric->width = 1.0f;
		metric->height = 16.0f;
		metric->tx0 = metric->ty0 = metric->tx1 = metric->ty1 = 0.0f;
	}
}

