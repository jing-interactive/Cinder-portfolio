#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/CameraUi.h"
#include "cinder/Log.h"

#include "AssetManager.h"
#include "MiniConfigImgui.h"

#include "melo.h"

using namespace ci;
using namespace ci::app;
using namespace std;

struct EndlessMeshApp : public App
{
    void setup() override
    {
        log::makeLogger<log::LoggerFileRotating>(fs::path(), "app.%Y.%m.%d.log");

        mCam.setNearClip(1);
        mCam.setFarClip(100000);
        mCamUi = CameraUi(&mCam, getWindow(), -1);

        createConfigImgui();
        gl::enableDepth();

        getWindow()->getSignalResize().connect([&] {
            APP_WIDTH = getWindowWidth();
            APP_HEIGHT = getWindowHeight();
            mCam.setAspectRatio(getWindowAspectRatio());
        });

        getSignalCleanup().connect([&] { writeConfig(); });

        getWindow()->getSignalKeyUp().connect([&](KeyEvent &event) {
            if (event.getCode() == KeyEvent::KEY_ESCAPE)
                quit();
        });

        {
            mRoot = melo::createRootNode();
            auto grid = melo::createGridNode();
            mRoot->addChild(grid);

            auto gltf = melo::createMeshNode(MESH_NAME);
            mRoot->addChild(gltf);
        }

        getSignalUpdate().connect([&] {
            mCam.setEyePoint({ CAM_POS_X, CAM_POS_Y, CAM_POS_Z });
            mRoot->treeUpdate();

            for (auto& child : mRoot->getChildren())
            {
                //mModel->flipV = FLIP_V;
                child->cameraPosition = mCam.getEyePoint();
            }
        });

        getWindow()->getSignalDraw().connect([&] {
            gl::setMatrices(mCam);
            gl::clear();

            mRoot->treeDraw();
        });
    }

    CameraPersp mCam;
    CameraUi mCamUi;
    melo::NodeRef mRoot;
};

CINDER_APP(EndlessMeshApp, RendererGl, [](App::Settings *settings) {
    readConfig();
    //settings->setConsoleWindowEnabled();
    settings->setWindowSize(APP_WIDTH, APP_HEIGHT);
    settings->setMultiTouchEnabled(false);
})
