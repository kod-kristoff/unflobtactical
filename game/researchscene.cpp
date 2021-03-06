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

#include "researchscene.h"
#include "../engine/engine.h"
#include "../engine/loosequadtree.h"
#include "research.h"

#include "gamelimits.h"
#include "game.h"
#include "cgame.h"
#include "helpscene.h"

using namespace gamui;
using namespace grinliz;

ResearchScene::ResearchScene( Game* _game, ResearchSceneData* _data ) : Scene( _game ), data( _data )
{
	const Screenport& port = GetEngine()->GetScreenport();

	//backgroundUI.Init( game, &gamui2D, false );

	RenderAtom backgroundAtom(	(const void*)UIRenderer::RENDERSTATE_UI_NORMAL_OPAQUE, 
								(const void*)TextureManager::Instance()->GetTexture( "background_research" ), 0, 0, 1, 1 );
	background.Init( &gamui2D, backgroundAtom, false );
	background.SetSize( port.UIWidth(), port.UIHeight() );

	const ButtonLook& blue = game->GetButtonLook( Game::BLUE_BUTTON );
	const ButtonLook& green = game->GetButtonLook( Game::GREEN_BUTTON );

	title.Init( &gamui2D );
	title.SetText( "title" );

	const float X0 = GAME_BUTTON_SIZE_F() + GAME_GUTTER_X();

	mainDescription.Init( &gamui2D );
	mainDescription.SetSize( port.UIWidth() - GAME_GUTTER_X() - X0, 200 );
	mainDescription.SetText( "description" );

	image.Init( &gamui2D, Game::CalcDecoAtom( DECO_RESEARCH, true ), true );
	image.SetSize( GAME_BUTTON_SIZE_F(), GAME_BUTTON_SIZE_F() );

	RenderAtom nullAtom;
	researchImage.Init( &gamui2D, nullAtom, true );
	researchImage.SetSize( GAME_BUTTON_SIZE_F(), GAME_BUTTON_SIZE_F()*2.0f );

	for( int i=0; i<MAX_OPTIONS; ++i ) {

		optionButton[i].Init( &gamui2D, blue );
		optionButton[i].SetSize( GAME_BUTTON_SIZE_F(), GAME_BUTTON_SIZE_F() );
		optionButton[i].SetText( "test" );

		optionName[i].Init( &gamui2D );
		optionName[i].SetText( "Research" );

		optionRequires[i].Init( &gamui2D );
		optionRequires[i].SetText( "Requires" );

		if ( i ) {
			optionButton[0].AddToToggleGroup( &optionButton[i] );
		}
	}

	okayButton.Init( &gamui2D, blue );
	okayButton.SetSize( GAME_BUTTON_SIZE_F(), GAME_BUTTON_SIZE_F() );
	okayButton.SetDeco(	Game::CalcDecoAtom( DECO_OKAY_CHECK, true ),
						Game::CalcDecoAtom( DECO_OKAY_CHECK, false ) );	

	helpButton.Init( &gamui2D, green );
	helpButton.SetSize( GAME_BUTTON_SIZE_F(), GAME_BUTTON_SIZE_F() );
	helpButton.SetDeco(  Game::CalcDecoAtom( DECO_HELP, true ), Game::CalcDecoAtom( DECO_HELP, false ) );	

	SetOptions();
	SetDescription();
}


ResearchScene::~ResearchScene()
{
}


void ResearchScene::Resize()
{
	const Screenport& port = GetEngine()->GetScreenport();
	background.SetSize( port.UIWidth(), port.UIHeight() );

	static const float SPACE = 5.0f;
	static const float X0 = GAME_BUTTON_SIZE_F() + GAME_GUTTER_X();
	title.SetPos( X0, 0 );
	mainDescription.SetPos( X0, GAME_GUTTER_X() );
	mainDescription.SetSize( port.UIWidth() - GAME_GUTTER_X() - X0, 200 );
	image.SetPos( 0, 0 );

	researchImage.SetPos( image.X(), image.Y()+image.Height()+GAME_GUTTER_Y() );

	for( int i=0; i<MAX_OPTIONS; ++i ) {
		float y = port.UIHeight() - (SPACE+GAME_BUTTON_SIZE_F())*(float)(MAX_OPTIONS-i) + SPACE;
		optionButton[i].SetPos( X0, y );
		optionName[i].SetPos( X0+GAME_GUTTER_X()+GAME_BUTTON_SIZE_F(), y+5 );
		optionRequires[i].SetPos(  X0+GAME_GUTTER_X()+GAME_BUTTON_SIZE_F(), y+25 );
	}
	okayButton.SetPos( 0, port.UIHeight()-GAME_BUTTON_SIZE_F() );
	helpButton.SetPos( port.UIWidth()-GAME_BUTTON_SIZE_F(), port.UIHeight()-GAME_BUTTON_SIZE_F() );
}


void ResearchScene::Draw3D()
{
}

void ResearchScene::Tap( int action, const grinliz::Vector2F& screen, const grinliz::Ray& world )
{
	grinliz::Vector2F ui;
	GetEngine()->GetScreenport().ViewToUI( screen, &ui );

	const UIItem* item = 0;
	if ( action == GAME_TAP_DOWN ) {
		gamui2D.TapDown( ui.x, ui.y );
		return;
	}
	else if ( action == GAME_TAP_CANCEL ) {
		gamui2D.TapCancel();
		return;
	}
	else if ( action == GAME_TAP_UP ) {
		item = gamui2D.TapUp( ui.x, ui.y );
	}

	if ( item == &okayButton ) {
		data->research->SetCurrent( 0 );
		for( int i=0; i<MAX_OPTIONS; i++ ) {
			if ( optionButton[i].Down() && optionButton[i].Enabled() ) {
				data->research->SetCurrent( optionName[i].GetText() );
				break;
			}
		}
		game->PopScene();
	}
	else if ( item == &helpButton ) {
		game->PushScene( Game::HELP_SCENE, new HelpSceneData( "researchHelp", false ) );
	}
}


void ResearchScene::SetDescription()
{
	Research* r = data->research;
	if ( r->ResearchReady() ) {
		const Research::Task* task = r->Current();
		title.SetText( task->name );
		mainDescription.SetText( r->GetDescription( task ) );

		GLString textureName( task->name );
		textureName += "Research";
		if ( TextureManager::Instance()->IsTexture( textureName.c_str() ) ) {

			RenderAtom imageAtom( (const void*)UIRenderer::RENDERSTATE_UI_NORMAL,
								  (const void*)TextureManager::Instance()->GetTexture( textureName.c_str() ), 0, 0, 1, 1 );
			researchImage.SetAtom( imageAtom );
		}
		else {
			RenderAtom nullAtom;
			researchImage.SetAtom( nullAtom );
		}
	}
	else {
		title.SetText( "" );
		if ( r->ResearchInProgress() ) {
			mainDescription.SetText( "Research in progress." );
		}
		else {
			int i;
			for( i=0; i<MAX_OPTIONS; i++ ) {
				if ( optionButton[i].Down() && optionButton[i].Enabled() ) {
					mainDescription.SetText( "Research in progress." );
					break;
				}
			}
			if ( i == MAX_OPTIONS ) {
				mainDescription.SetText( "Research requirements not met." );
			}
		}
	};
}


void ResearchScene::SetOptions()
{
	Research* r = data->research;
	int count = 0;
	const Research::Task* taskArr = r->TaskArr();
	static const int SZ=64;
	char buf[SZ];
	bool downSet = false;

	for( int pass=0; pass<2; ++pass ) {
		for( int i=0; i<r->NumTasks() && count < MAX_OPTIONS; ++i ) {
			if ( !taskArr[i].IsComplete() && taskArr[i].HasPreReq() ) {
				if ( pass == 0 && !taskArr[i].HasItems() )
					continue;
				if ( pass == 1 && taskArr[i].HasItems() )
					continue;

				int percent = (int)(100.0f * (float)taskArr[i].rp / (float)taskArr[i].rpRequired );
				SNPrintf( buf, SZ, "%d%%", percent );
				optionButton[count].SetText( buf );

				optionName[count].SetText( taskArr[i].name );

				if ( r->Current() && StrEqual( r->Current()->name, taskArr[i].name ) ) {
					optionButton[count].SetDown();
					downSet = true;
				}

				GLASSERT( Research::MAX_ITEMS_REQUIRED == 4 );
				if ( taskArr[i].item[0] || taskArr[i].item[1] || taskArr[i].item[2] || taskArr[i].item[3] ) {
					SNPrintf( buf, SZ, "Requires: %s %s %s %s", 
								taskArr[i].item[0] ? taskArr[i].item[0] : "",
								taskArr[i].item[1] ? taskArr[i].item[1] : "",
								taskArr[i].item[2] ? taskArr[i].item[2] : "",
								taskArr[i].item[3] ? taskArr[i].item[3] : "" );
					optionRequires[count].SetText( buf );
				}
				else {
					optionRequires[count].SetText( "" );
				}

				if ( taskArr[i].HasItems() ) {
					optionRequires[count].SetEnabled( false );
					optionButton[count].SetEnabled( true );
				}
				else {
					optionRequires[count].SetEnabled( true );
					optionButton[count].SetEnabled( false );
				}
				++count;
			}
		}
	}
	for( /*count*/; count<MAX_OPTIONS; ++count ) {
		optionButton[count].SetEnabled( false );
		optionButton[count].SetText( "" );
		optionName[count].SetText( "" );
		optionRequires[count].SetText( "" );
	}
	if ( !downSet ) {
		for( int i=0; i<MAX_OPTIONS; ++i ) {
			if ( optionButton[i].Enabled() ) {
				optionButton[i].SetDown();
				break;
			}
		}
	}
}

