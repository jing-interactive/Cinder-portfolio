#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/TriMesh.h"
#include "cinder/gl/gl.h"
#include "cinder/CameraUi.h"
#include "cinder/ObjLoader.h"
#include "cinder/Utilities.h"
#include "cinder/Filesystem.h"
#include "MiniConfig.h"
#include "AssetManager.h"

using namespace ci;
using namespace ci::app;
using namespace std;

class MatCapShadersApp : public App {
public:
    void setup() override;
    void mouseDown(MouseEvent event) override;
    void keyDown(KeyEvent event) override;
    void update() override;
    void draw() override;

private:
    void setupCamera();
    void setupBatches();
    void setupTextures();
    void nextTexture();
    void prevTexture();

    // camera
    CameraPersp mCamera;
    CameraUi    mCameraUi;
    float       mVerticalFOV;
    float       mNearClip;
    float       mFarClip;
    vec3        mCamP;
    vec3        mCamTarget;
    vec3        mCamUp;

    // obj file batch
    TriMeshRef       mTriMesh;
    gl::BatchRef     mObjRef;
    gl::GlslProgRef  mMatcapGlsl;

    // wireframe grid batch
    geom::WirePlane  mGrid;
    gl::BatchRef     mGridRef;

    // matcap textures
    gl::Texture2dRef mMatcapTex;
    vector<fs::path> mMatcapPaths;
    vector<fs::path>::iterator mCurrentPath;
};

void MatCapShadersApp::setup()
{
    am::addAssetDirectory("../../SSS/assets");
    setupCamera();
    setupTextures();
    setupBatches();
}

void MatCapShadersApp::setupCamera()
{
    mVerticalFOV = 50.f;
    mNearClip = 0.05f;
    mFarClip = 50.0f;
    mCamP = vec3(0.68, 0.4, 0.48);
    mCamTarget = vec3(0, 0.2, 0);
    mCamUp = vec3(0, 1, 0);

    mCamera.setPerspective(mVerticalFOV, getWindowAspectRatio(), mNearClip, mFarClip);
    mCamera.lookAt(mCamP, mCamTarget, mCamUp);

    mCameraUi = CameraUi(&mCamera, getWindow());
}

void MatCapShadersApp::setupBatches()
{
	mMatcapGlsl = am::glslProg("shaders/matcap_vert.glsl", "shaders/matcap_frag.glsl");
    mMatcapGlsl->uniform("image", 0);
    mMatcapGlsl->uniform("diffuse", 1);

    mTriMesh = am::triMesh(MESH_NAME);
    mObjRef = gl::Batch::create(*mTriMesh, mMatcapGlsl);

    mGrid = geom::WirePlane().subdivisions(ivec2(10, 10)).size(vec2(2, 2));
    mGridRef = gl::Batch::create(mGrid, am::glslProg("color"));
}

void MatCapShadersApp::setupTextures()
{
    // search assets for all .png files in sub directory /matcaps
    for (auto& p : fs::directory_iterator(getAssetPath("matcaps")))
    {
        if (!fs::is_regular_file(p))
            continue;

        if (p.path().extension().string().compare("png"))
            mMatcapPaths.push_back(p.path());
    }

    mCurrentPath = mMatcapPaths.begin();
    mMatcapTex = am::texture2d(mCurrentPath->string());
}

void MatCapShadersApp::nextTexture()
{
    if (mMatcapPaths.size() > 1) {

        mCurrentPath++;
        if (mCurrentPath == mMatcapPaths.end())
            mCurrentPath = mMatcapPaths.begin();

        mMatcapTex = am::texture2d(mCurrentPath->string());
    }
}

void MatCapShadersApp::prevTexture()
{
    if (mMatcapPaths.size() > 1) {

        if (mCurrentPath == mMatcapPaths.begin())
            mCurrentPath = mMatcapPaths.end();
        mCurrentPath--;

        mMatcapTex = am::texture2d(mCurrentPath->string());
    }
}

void MatCapShadersApp::mouseDown(MouseEvent event)
{
}

void MatCapShadersApp::keyDown(KeyEvent event)
{
    if (event.getChar() == 'x')
        nextTexture();
    else if (event.getChar() == 'z')
        prevTexture();
}

void MatCapShadersApp::update()
{
}

void MatCapShadersApp::draw()
{
    gl::enableDepthRead();
    gl::enableDepthWrite();

    gl::clear(Color::black());
    gl::color(1, 1, 1, 1);

    gl::setMatrices(mCamera);

    gl::ScopedTextureBind t0(mMatcapTex, 0);
    gl::ScopedTextureBind t1(am::texture2d(TEX0_NAME), 1);

    mGridRef->draw();
    mObjRef->draw();
}

CINDER_APP(MatCapShadersApp, RendererGl(RendererGl::Options().msaa(16)), [](App::Settings* settings) {
    readConfig();
    settings->setMultiTouchEnabled(false);
    settings->setHighDensityDisplayEnabled(true);
    settings->setWindowSize(1280, 720);
    settings->setTitle("MatCapShaders - Z and X keys toggle matcaps");
    })