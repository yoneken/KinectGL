/*
 *  KinectGL.cpp
 *
 *  Created: 2012/05/16
 *  Author: yoneken
 */


#if defined(WIN32)
#include <GL/glut.h>    			// Header File For The GLUT Library
#include <GL/gl.h>
#include <GL/glu.h>
#else /* OS : !win */
#include <GLUT/glut.h>
#endif /* OS */

#include <ole2.h>
#include <NuiApi.h>

#include <stdio.h>
#include <stdlib.h>

/* ASCII code for the escape key. */
#define KEY_ESCAPE 27
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480

int window;									// The number of our GLUT window
int time,timeprev=0;						// For calculating elapsed time

INuiSensor *pNuiSensor;
HANDLE hNextColorFrameEvent;
HANDLE hNextDepthFrameEvent;
HANDLE hNextSkeletonEvent;
HANDLE pVideoStreamHandle;
HANDLE pDepthStreamHandle;

typedef enum _TEXTURE_INDEX{
	IMAGE_TEXTURE = 0,
	DEPTH_TEXTURE,
	EDITED_TEXTURE,
	TEXTURE_NUM,
} TEXTURE_INDEX;
GLuint bg_texture[TEXTURE_NUM];

Vector4 skels[NUI_SKELETON_COUNT][NUI_SKELETON_POSITION_COUNT];
int trackedPlayer = 0;
unsigned short depth[240][320];

/*
 * @brief A general Nui initialization function.  Sets all of the initial parameters.
 */
void initNui(void)	        // We call this right after Nui functions called.
{
	HRESULT hr;

	hr = NuiCreateSensorByIndex(0, &pNuiSensor);
	if(FAILED(hr)) OutputDebugString( L"Cannot connect with kinect0.\r\n");

	hr = pNuiSensor->NuiInitialize(
		//NUI_INITIALIZE_FLAG_USES_DEPTH |
		NUI_INITIALIZE_FLAG_USES_DEPTH_AND_PLAYER_INDEX | 
		NUI_INITIALIZE_FLAG_USES_COLOR | 
		NUI_INITIALIZE_FLAG_USES_SKELETON
		);
	if ( E_NUI_SKELETAL_ENGINE_BUSY == hr ){
		hr = pNuiSensor->NuiInitialize(
			NUI_INITIALIZE_FLAG_USES_DEPTH |
			NUI_INITIALIZE_FLAG_USES_COLOR
			);
	}
	if(FAILED(hr)){
		OutputDebugString( L"Cannot initialize kinect.\r\n");
	}

	hNextColorFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	hNextDepthFrameEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
	hNextSkeletonEvent = CreateEvent( NULL, TRUE, FALSE, NULL );

	if(HasSkeletalEngine(pNuiSensor)){
		hr = pNuiSensor->NuiSkeletonTrackingEnable( hNextSkeletonEvent, 
			//NUI_SKELETON_TRACKING_FLAG_TITLE_SETS_TRACKED_SKELETONS |
			//NUI_SKELETON_TRACKING_FLAG_ENABLE_SEATED_SUPPORT 
			0
			);
		if(FAILED(hr)) OutputDebugString( L"Cannot track skeletons\r\n");
	}

	hr = pNuiSensor->NuiImageStreamOpen(
		NUI_IMAGE_TYPE_COLOR,
		NUI_IMAGE_RESOLUTION_320x240,	// Note: If 640x320, the skeleton event won't come so frequently.
		0,
		2,
		hNextColorFrameEvent,
		&pVideoStreamHandle );
	if(FAILED(hr)){
		OutputDebugString( L"Cannot open image stream\r\n" );
	}

	hr = pNuiSensor->NuiImageStreamOpen(
		HasSkeletalEngine(pNuiSensor) ? NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX : NUI_IMAGE_TYPE_DEPTH,
		NUI_IMAGE_RESOLUTION_320x240,
		0,
		2,
		hNextDepthFrameEvent,
		&pDepthStreamHandle );
	if(FAILED(hr)){
		OutputDebugString( L"Cannot open depth and player stream\r\n" );
	}
/*
	hr = pNuiSensor->NuiImageStreamOpen(
		NUI_IMAGE_TYPE_DEPTH,
		NUI_IMAGE_RESOLUTION_640x480,
		0,
		2,
		hNextDepthFrameEvent,
		&pDepthStreamHandle );
	if(FAILED(hr)){
		printf("Cannot open depth stream\r\n");
	}
*/
}

void storeNuiImage(void)
{
	NUI_IMAGE_FRAME imageFrame;

	HRESULT hr =  pNuiSensor->NuiImageStreamGetNextFrame(
		pVideoStreamHandle,
		0,
		&imageFrame );
	if( FAILED( hr ) ){
		return;
	}
	if(imageFrame.eImageType != NUI_IMAGE_TYPE_COLOR)
		OutputDebugString( L"Image type is not match with the color\r\n" );

	INuiFrameTexture *pTexture = imageFrame.pFrameTexture;
	NUI_LOCKED_RECT LockedRect;
	pTexture->LockRect( 0, &LockedRect, NULL, 0 );
	if( LockedRect.Pitch != 0 ){
		byte * pBuffer = (byte *)LockedRect.pBits;

		NUI_SURFACE_DESC pDesc;
		pTexture->GetLevelDesc(0, &pDesc);
		//printf("w: %d, h: %d, byte/pixel: %d\r\n", pDesc.Width, pDesc.Height, LockedRect.Pitch/pDesc.Width);
		typedef struct t_RGBA{
			byte r;
			byte g;
			byte b;
			byte a;
		};
		t_RGBA *p = (t_RGBA *)pBuffer;
		for(int i=0;i<pTexture->BufferLen()/4;i++){
			byte b = p->b;
			p->b = p->r;
			p->r = b;
			p->a = (byte)255;
			p++;
		}

		glBindTexture(GL_TEXTURE_2D, bg_texture[IMAGE_TEXTURE]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
			pDesc.Width,  pDesc.Height,
			0, GL_RGBA, GL_UNSIGNED_BYTE, pBuffer);
		pTexture->UnlockRect(0);
	}else{
		OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
	}

	pNuiSensor->NuiImageStreamReleaseFrame( pVideoStreamHandle, &imageFrame );
}

void storeNuiDepth(void)
{
	NUI_IMAGE_FRAME depthFrame;

	HRESULT hr = pNuiSensor->NuiImageStreamGetNextFrame(
		pDepthStreamHandle,
		0,
		&depthFrame );
	if( FAILED( hr ) ){
		return;
	}
	if(depthFrame.eImageType != NUI_IMAGE_TYPE_DEPTH_AND_PLAYER_INDEX)
		OutputDebugString( L"Depth type is not match with the depth and players\r\n" );

	INuiFrameTexture *pTexture = depthFrame.pFrameTexture;
	NUI_LOCKED_RECT LockedRect;
	pTexture->LockRect( 0, &LockedRect, NULL, 0 );
	if( LockedRect.Pitch != 0 ){
		unsigned short *pBuffer = (unsigned short *)LockedRect.pBits;
		memcpy(depth, LockedRect.pBits, pTexture->BufferLen());

		NUI_SURFACE_DESC pDesc;
		pTexture->GetLevelDesc(0, &pDesc);
		//printf("w: %d, h: %d, byte/pixel: %d\r\n", pDesc.Width, pDesc.Height, LockedRect.Pitch/pDesc.Width);
		///printf("%d\r\n", pTexture->BufferLen());

		unsigned short *p = (unsigned short *)pBuffer;
		for(int i=0;i<pTexture->BufferLen()/2;i++){
			//*p = (unsigned short)((*p & 0xff00)>>8) | ((*p & 0x00ff)<<8);
			//*p = (unsigned short)((*p & 0xfff8)>>3);
			*p = (unsigned short)(NuiDepthPixelToDepth(*p));
			p++;
		}
		glBindTexture(GL_TEXTURE_2D, bg_texture[DEPTH_TEXTURE]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
			pDesc.Width,  pDesc.Height,
			0, GL_LUMINANCE, GL_UNSIGNED_SHORT, pBuffer);
		pTexture->UnlockRect(0);
	}
	else{
		OutputDebugString( L"Buffer length of received texture is bogus\r\n" );
	}
	pNuiSensor->NuiImageStreamReleaseFrame( pDepthStreamHandle, &depthFrame );

}

void storeNuiSkeleton(void)
{
	NUI_SKELETON_FRAME SkeletonFrame = {0};

	HRESULT hr = pNuiSensor->NuiSkeletonGetNextFrame( 0, &SkeletonFrame );

	bool bFoundSkeleton = true;
	for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ ){
		if( (SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_TRACKED) ||
			(SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_POSITION_ONLY) ){
			bFoundSkeleton = true;
			trackedPlayer = i;
		}
	}

	// no skeletons!
	//
	if( !bFoundSkeleton )
		return;

	// smooth out the skeleton data
	pNuiSensor->NuiTransformSmooth(&SkeletonFrame,NULL);

	// store each skeleton color according to the slot within they are found.
	for( int i = 0 ; i < NUI_SKELETON_COUNT ; i++ )
	{
		if( (SkeletonFrame.SkeletonData[i].eTrackingState == NUI_SKELETON_TRACKED)){
			memcpy(skels[i], SkeletonFrame.SkeletonData[i].SkeletonPositions, sizeof(Vector4)*NUI_SKELETON_POSITION_COUNT);
		}
	}
}

void drawTexture(TEXTURE_INDEX index)
{
	const short vertices[] = {
		0,	  0,
		DEFAULT_WIDTH,	  0,
		0,	DEFAULT_HEIGHT,
		DEFAULT_WIDTH,	DEFAULT_HEIGHT,
	};
	const GLfloat texCoords[] = {
		1.0f,	1.0f,
		0.0f,	1.0f,
		1.0f,	0.0f,
		0.0f,	0.0f,
	};
	glEnable(GL_TEXTURE_2D);
	glShadeModel(GL_FLAT);
	glDisableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_SHORT, 0, vertices);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, texCoords);

	glBindTexture(GL_TEXTURE_2D, bg_texture[index]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindTexture(GL_TEXTURE_2D, 0);

	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisable(GL_TEXTURE_2D);
}

inline void drawNuiSkeleton(int playerID)
{
	int scaleX = DEFAULT_WIDTH;
	int scaleY = DEFAULT_HEIGHT;
	long x=0,y=0;
	unsigned short depth=0;
	float fx=0,fy=0;
	int display_pos[NUI_SKELETON_POSITION_COUNT][2];

	for (int i = 0; i < NUI_SKELETON_POSITION_COUNT; i++)
	{
		NuiTransformSkeletonToDepthImage( skels[playerID][i], &fx, &fy);
		display_pos[i][0] = (int) ( fx / 320.0 * DEFAULT_WIDTH);
		display_pos[i][1] = (int) ( fy / 240.0 * DEFAULT_HEIGHT);
	}

	glColor3ub(255, 255, 0);
	glLineWidth(6);
	glBegin(GL_LINE_STRIP);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SPINE][0], scaleY - display_pos[NUI_SKELETON_POSITION_SPINE][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HEAD][0], scaleY - display_pos[NUI_SKELETON_POSITION_HEAD][1]);
	glEnd();
	glBegin(GL_LINE_STRIP);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SHOULDER_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_SHOULDER_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_ELBOW_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_ELBOW_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_WRIST_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_WRIST_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HAND_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_HAND_LEFT][1]);
	glEnd();
	glBegin(GL_LINE_STRIP);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_SHOULDER_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_SHOULDER_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_SHOULDER_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_ELBOW_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_ELBOW_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_WRIST_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_WRIST_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HAND_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_HAND_RIGHT][1]);
	glEnd();
	glBegin(GL_LINE_STRIP);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HIP_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_HIP_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_KNEE_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_KNEE_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_ANKLE_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_ANKLE_LEFT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_FOOT_LEFT][0], scaleY - display_pos[NUI_SKELETON_POSITION_FOOT_LEFT][1]);
	glEnd();
	glBegin(GL_LINE_STRIP);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][0], scaleY - display_pos[NUI_SKELETON_POSITION_HIP_CENTER][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_HIP_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_HIP_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_KNEE_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_KNEE_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_ANKLE_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_ANKLE_RIGHT][1]);
		glVertex2i( scaleX - display_pos[NUI_SKELETON_POSITION_FOOT_RIGHT][0], scaleY - display_pos[NUI_SKELETON_POSITION_FOOT_RIGHT][1]);
	glEnd();
	glColor3ub(0, 0, 0);
}

/*
 * @brief A general OpenGL initialization function.  Sets all of the initial parameters.
 */
void initGL(int Width, int Height)
{
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	glDisable(GL_DEPTH_TEST);
	glShadeModel(GL_SMOOTH);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, Width, Height);
	gluOrtho2D(0.0, Width, 0.0, Height);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	for(int i=0;i<TEXTURE_NUM;i++){
		glGenTextures(1, &bg_texture[i]);
		glBindTexture(GL_TEXTURE_2D, bg_texture[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	}
}

/*
 * @brief Any User DeInitialization Goes Here
 */
void deinitialize(void)
{
	for(int i=0;i<TEXTURE_NUM;i++){
		glDeleteTextures(1, &bg_texture[i]);
	}

	CloseHandle( hNextColorFrameEvent );
	hNextColorFrameEvent = NULL;

	CloseHandle( hNextDepthFrameEvent );
	hNextDepthFrameEvent = NULL;

	if(HasSkeletalEngine(pNuiSensor)) pNuiSensor->NuiSkeletonTrackingDisable();

	pNuiSensor->NuiShutdown();
	pNuiSensor->Release();
	pNuiSensor = NULL;
}

/*
 * @brief The function called when our window is resized (which shouldn't happen, because we're fullscreen)
 */
void reSizeGL(int Width, int Height)
{
	if (Height==0)						// Prevent A Divide By Zero If The Window Is Too Small
		Height=1;

	glViewport(0, 0, Width, Height);	// Reset The Current Viewport And Perspective Transformation

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0.0, Width, 0.0, Height);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

/*
 * @brief The function called whenever a normal key is pressed.
 */
void NormalKeyPressed(unsigned char keys, int x, int y)
{
	if (keys == KEY_ESCAPE) {
		deinitialize();
		exit(0);
	}
}

/*
 * @brief The function called whenever a special key is pressed.
 */
void SpecialKeyPressed(int key, int x, int y)
{
	switch (key) {
		case GLUT_KEY_F1:
			break;
	}
}

/*
 * @brief The main drawing function.
 */
void drawGL()
{
	time=glutGet(GLUT_ELAPSED_TIME);
	int milliseconds = time - timeprev;
	timeprev = time;

	float dt = milliseconds / 1000.0f;						// Convert Milliseconds To Seconds

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	
	drawTexture(DEPTH_TEXTURE);
	//drawTexture(IMAGE_TEXTURE);
	drawNuiSkeleton(trackedPlayer);

	glFlush ();					// Flush The GL Rendering Pipeline
	glutSwapBuffers();			// swap buffers to display, since we're double buffered.
}

void idleGL()
{
	const int EventNum = 3;
	HANDLE hEvents[EventNum] = { hNextDepthFrameEvent, hNextColorFrameEvent, hNextSkeletonEvent};

	switch(WaitForMultipleObjects( EventNum, hEvents, FALSE, 0 )){
		case WAIT_TIMEOUT:
			return;
		case WAIT_OBJECT_0 + 0:
			storeNuiDepth();
			break;
		case WAIT_OBJECT_0 + 1:
			storeNuiImage();
			break;
		case WAIT_OBJECT_0 + 2:
			storeNuiSkeleton();
			break;
	}
	glutPostRedisplay();
}

int main(int argc , char ** argv) {
	glutInit(&argc, argv);

	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_ALPHA | GLUT_DEPTH);
	glutInitWindowSize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
	glutInitWindowPosition(0, 0);
	window = glutCreateWindow("Kinect SDK v1.5 with OpenGL");
	glutReshapeFunc(&reSizeGL);
	glutDisplayFunc(&drawGL);
	glutIdleFunc(&idleGL);
	glutKeyboardFunc(&NormalKeyPressed);
	glutSpecialFunc(&SpecialKeyPressed);

	initNui();
	initGL(DEFAULT_WIDTH, DEFAULT_HEIGHT);
	
	glutMainLoop();

	return 0;
}
