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

#ifndef UFOATTACK_CAMERA_INCLUDED
#define UFOATTACK_CAMERA_INCLUDED

#include "../grinliz/gldebug.h"
#include "../grinliz/gltypes.h"
#include "../grinliz/glvector.h"
#include "../grinliz/glmatrix.h"
#include "../grinliz/glgeometry.h"

class UFOStream;

/*	By default, the camera is looking down the -z axis with y up.
	View rotation is handled by this class to account for device
	turning.
*/
class Camera
{
public:
	Camera();
	~Camera()	{}

	float GetTilt()	const							{ return tilt; }
	void SetTilt( float value )						{ tilt = value;	valid = false; }
	void DeltaTilt( float delta )					{ tilt += delta; valid = false; }

	float GetYRotation() const						{ return yRotation; }
	void SetYRotation( float value )				{ yRotation = value;  valid = false; }
	// The rotation used by billboards (about y) to look at he camera.
	float GetBillboardYRotation() const				{	return yRotation; }

	// Move the camera around a center pole. The idea of rotation from the users point of view.
	void Orbit( const grinliz::Vector3F& pole, float delta );

	// 0-3, in view space (corresponds to device rotation). Normally one.
	void SetViewRotation( int v )					{ viewRotation = v; valid = false; }

	// Position in world coordinates.
	const grinliz::Vector3F& PosWC() const			{ return posWC; }
	void SetPosWC( const grinliz::Vector3F& value )	{ posWC = value; valid = false; }
	void SetPosWC( float x, float y, float z )		{ posWC.Set( x, y, z ); valid = false; }
	void DeltaPosWC( float x, float y, float z )	{ posWC.x += x; posWC.y += y; posWC.z += z; valid = false; }

	// Draws the camera and submits the glMultMatrix to OpenGL
	void DrawCamera();
								
	enum {
		NORMAL,
		UP,
		RIGHT
	};
	const grinliz::Vector4F* EyeDir()				{ if ( !valid ) CalcWorldXForm();
													  return eyeDir; }	
	const grinliz::Vector3F* EyeDir3()				{ if ( !valid ) CalcWorldXForm();
													  return eyeDir3; }

	// Saves camera position but not view rotation (which comes from the device.)
	void Save( UFOStream* ) const;
	void Load( UFOStream* );

private:
	// Position of the camera in the world - no view rotation, no inverse.
	void CalcWorldXForm();
	void CalcEyeDir();

	int viewRotation;

	float tilt;
	float yRotation;
	grinliz::Vector3F posWC;

	bool valid;
	grinliz::Vector4F eyeDir[3];
	grinliz::Vector3F eyeDir3[3];
	grinliz::Matrix4 worldXForm;
};


#endif // UFOATTACK_CAMERA_INCLUDED

