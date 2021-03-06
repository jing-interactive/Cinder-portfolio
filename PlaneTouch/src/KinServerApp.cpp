#include "cinder/app/RendererGl.h"
#include "cinder/app/App.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/scoped.h"
#include "cinder/params/Params.h"
#include "cinder/Log.h"
#include "cinder/PolyLine.h"

#include "cinder/osc/Osc.h"
#include "CinderOpenCV.h"
#include "BlobTracker.h"

#include "DepthSensor.h"
#include "Cinder-VNM/include/MiniConfig.h"
#include "Cinder-VNM/include/TextureHelper.h"
#include "opencv2/rgbd.hpp"

using namespace ci;
using namespace ci::app;
using namespace cv;
using namespace std;

class PlaneTouchApp : public App
{
public:
    void setup() override
    {
        const auto& args = getCommandLineArgs();
        readConfig();
        log::makeLogger<log::LoggerFile>();

        {
            mParams = createConfigUI({ 400, 600 });
            std::vector<string> smoothNames = { "Off", "Light", "Middle", "High" };
            ADD_ENUM_TO_INT(mParams, TRACKING_SMOOTH, smoothNames);

            mParams->addParam("FPS", &mFps, true);
            mParams->addButton("Set Bg", std::bind(&PlaneTouchApp::updateBack, this));
            mParams->addButton("Reset In/Out", [] {
                INPUT_X1 = INPUT_Y1 = OUTPUT_X1 = OUTPUT_Y1 = 0;
                INPUT_X2 = INPUT_Y2 = OUTPUT_X2 = OUTPUT_Y2 = 1;
            });
        }

        ds::DeviceType type = ds::DeviceType(_SENSOR_TYPE);
        ds::Option option;
        option.enableDepth = true;
        mDevice = ds::Device::create(type, option);
        if (!mDevice->isValid())
        {
            CI_LOG_E("Invalid sensor for " << ds::strFromType(type));
            //quit();
            //return;
        }
        mDirtyConnection = mDevice->signalDepthDirty.connect(std::bind(&PlaneTouchApp::updateDepthRelated, this));

        mOscSender = std::make_shared<osc::SenderUdp>(10000, _ADDRESS, _TUIO_PORT);
        mOscSender->bind();

        getWindow()->setSize(1024, 768);

        try
        {
            mShader = gl::GlslProg::create(loadAsset("depthMap.vs"), loadAsset("depthMap.fs"));
            mShader->uniform("image", 0);
        }
        catch (std::exception& e)
        {
            console() << e.what() << std::endl;
        }
    }

    void resize() override
    {
        mLayout.width = getWindowWidth();
        mLayout.height = getWindowHeight();
        mLayout.halfW = mLayout.width / 2;
        mLayout.halfH = mLayout.height / 2;
        mLayout.spc = mLayout.width * 0.04;

        for (int x = 0; x < 2; x++)
        {
            for (int y = 0; y < 2; y++)
            {
                mLayout.canvases[y * 2 + x] = Rectf(
                    mLayout.spc + mLayout.halfW * x,
                    mLayout.spc + mLayout.halfH * y,
                    mLayout.halfW * (1 + x) - mLayout.spc,
                    mLayout.halfH * (1 + y) - mLayout.spc
                    );
            }
        }

        mParams->setPosition(mLayout.canvases[1].getUpperLeft());
    }

    void draw() override
    {
        gl::clear(ColorA::gray(0.5f));
        gl::disableAlphaBlending();

        if (mDepthW == 0) return;

        if (mDepthTexture)
        {
            gl::ScopedGlslProg prog(mShader);
            gl::ScopedTextureBind tex0(mDepthTexture);
            gl::drawSolidRect(mLayout.canvases[0]);
            gl::ScopedTextureBind tex1(mBackTexture);
            gl::drawSolidRect(mLayout.canvases[3]);
            gl::ScopedTextureBind tex2(mDiffTexture);
            gl::drawSolidRect(mLayout.canvases[2]);
        }
        visualizeBlobs(mBlobTracker);
    }

    void keyUp(KeyEvent event) override
    {
        int code = event.getCode();
        if (code == KeyEvent::KEY_ESCAPE)
        {
            quit();
        }
    }

    void update() override
    {
        if (mDepthW == 0) return;

        mFps = getAverageFps();

        mInputRoi.set(
            INPUT_X1 * mDepthW,
            INPUT_Y1 * mDepthH,
            INPUT_X2 * mDepthW,
            INPUT_Y2 * mDepthH
            );
        mOutputMap.set(
            OUTPUT_X1 * mDepthW,
            OUTPUT_Y1 * mDepthH,
            OUTPUT_X2 * mDepthW,
            OUTPUT_Y2 * mDepthH
            );
    }

private:

    Mat K;

    vector<Vec4f> plane_coefficients;
    Mat plane_mask;

    rgbd::RgbdPlane planeComputer;
    rgbd::RgbdNormals normalsComputer;
    rgbd::DepthCleaner depthCleaner;

    void updateDepthRelated()
    {
        if (mDepthW == 0)
        {
            mDepthW = mDevice->getDepthSize().x;
            mDepthH = mDevice->getDepthSize().y;

            mDiffMat = cv::Mat1b(mDepthH, mDepthW);
            mDiffChannel = Channel(mDepthW, mDepthH, mDiffMat.step, 1,
                mDiffMat.ptr());

            double focal_length = mDevice->focalLength.x;

            int kWindowSize = 5;
            float cx = mDepthW / 2.f + 0.5f;
            float cy = mDepthH / 2.f + 0.5f;

            K = (Mat_<double>(3, 3) << focal_length, 0, cx, 0, focal_length, cy, 0, 0, 1);

            normalsComputer = rgbd::RgbdNormals(mDepthH, mDepthW, CV_32F, K, kWindowSize, rgbd::RgbdNormals::RGBD_NORMALS_METHOD_FALS);
            depthCleaner = rgbd::DepthCleaner(CV_16UC1, kWindowSize);
        }

        Mat normals, points;
        Mat cleanedDepth;

        Mat depth = toOcvRef(mDevice->depthChannel);
#if 1
        depthCleaner(depth, cleanedDepth);
#else
        cleanedDepth = depth;
#endif
        rgbd::depthTo3d(cleanedDepth, K, points);

        normalsComputer(points, normals);

        planeComputer(points, normals, plane_mask, plane_coefficients);

        updateTexture(mDepthTexture, mDevice->depthChannel, getTextureFormatUINT16());

        if (!mBackTexture)
        {
            updateBack();
        }

        mDiffMat.setTo(cv::Scalar::all(0));

        int cx = CENTER_X * mDepthW;
        int cy = CENTER_Y * mDepthH;
        int radius = RADIUS * mDepthH;
        int radius_sq = radius * radius;

        float depthToMmScale = mDevice->getDepthToMmScale();
        float minThresholdInDepthUnit = MIN_THRESHOLD_MM / depthToMmScale;
        float maxThresholdInDepthUnit = MAX_THRESHOLD_MM / depthToMmScale;
        
        uint16_t diff;

        for (int yy = mInputRoi.y1; yy < mInputRoi.y2; yy++)
        {
            // TODO: cache row pointer
            int y = yy;
            for (int xx = mInputRoi.x1; xx < mInputRoi.x2; xx++)
            {
                int x = LEFT_RIGHT_FLIPPED ? (mDepthW - xx) : xx;
                auto dep = *(mDevice->depthChannel.getData(x, y));
                if (dep > 0)
                {
                    auto bg = *mBackChannel.getData(x, y);
                    diff = bg - dep;
                    if (diff <= minThresholdInDepthUnit || diff >= maxThresholdInDepthUnit) continue;
                    // TODO: optimize
                    if (!CIRCLE_MASK_ENABLED || (cx - x) * (cx - x) + (cy - y) * (cy - y) < radius_sq)
                    {
                        mDiffMat(yy, xx) = 255;
                    }
                }
            }
        }

        if (TRACKING_SMOOTH > 0)
        {
            cv::Mat element = getStructuringElement(cv::MORPH_RECT, cv::Size(TRACKING_SMOOTH * 2 + 1, TRACKING_SMOOTH * 2 + 1),
                cv::Point(TRACKING_SMOOTH, TRACKING_SMOOTH));
            cv::morphologyEx(mDiffMat, mDiffMat, cv::MORPH_OPEN, element);
        }

        updateTexture(mDiffTexture, mDiffChannel);
        std::vector<Blob> blobs;
        BlobFinder::Option option;
        option.minArea = MIN_AREA;
        option.handOnlyMode = FINGER_MODE_ENABLED;
        option.handDistance = FINGER_SIZE;
        BlobFinder::execute(mDiffMat, blobs, option);
        mBlobTracker.trackBlobs(blobs);
        sendTuioMessage(*mOscSender, mBlobTracker);
    }

    void visualizeBlobs(const BlobTracker &blobTracker)
    {
        static uint8_t sPalette[][3] =
        {
            { 255, 0, 0 },
            { 122, 0, 0 },
            { 255, 255, 0 },
            { 122, 122, 0 },
            { 255, 0, 255 },
            { 122, 0, 122 },
            { 0, 0, 255 },
            { 0, 0, 122 },
            { 0, 255, 255 },
            { 0, 122, 122 },
        };
        const size_t sPaletteCount = _countof(sPalette);

        vec2 scale;
        scale.x = (mLayout.halfW - mLayout.spc * 2) / mDepthW;
        scale.y = (mLayout.halfH - mLayout.spc * 2) / mDepthH;
        gl::pushModelMatrix();
        gl::translate(mLayout.canvases[2].getUpperLeft());
        gl::scale(scale);

        if (CIRCLE_MASK_ENABLED)
        {
            float cx = CENTER_X * mDepthW;
            if (LEFT_RIGHT_FLIPPED) cx = mDepthW - cx;
            float cy = CENTER_Y * mDepthH;
            float radius = RADIUS * mDepthH;
            gl::drawStrokedCircle(vec2(cx, cy), radius, 40);
        }
        {
            gl::ScopedColor scope(ColorAf(1, 0, 0, 0.5f));
            gl::drawStrokedRect(mInputRoi);
        }
        {
            gl::ScopedColor scope(ColorAf(0, 1, 0, 0.5f));
            gl::drawStrokedRect(mOutputMap);
        }

        char idName[10];
        for (const auto &blob : blobTracker.trackedBlobs)
        {
            int idx = blob.id % sPaletteCount;
            gl::color(Color8u(sPalette[idx][0], sPalette[idx][1], sPalette[idx][2]));
            PolyLine2 line;
            for (const auto &pt : blob.pts)
            {
                line.push_back(vec2(pt.x, pt.y));
            }
            line.setClosed();
            gl::drawSolid(line);
            sprintf(idName, "#%d", blob.id);
            gl::drawStringCentered(idName, vec2(blob.center.x, blob.center.y));
        }
        gl::color(Color::white());
        gl::popModelMatrix();
    }

    int remapTuioId(int srcId)
    {
#define kMagicNumber 100
        return (srcId % kMagicNumber) + kMagicNumber * SERVER_ID;
    }

    void sendTuioMessage(osc::SenderUdp &sender, const BlobTracker &blobTracker)
    {
        osc::Bundle bundle;

        osc::Message alive;
        {
            alive.setAddress("/tuio/2Dcur");
            alive.append("alive");
        }

        osc::Message fseq;
        {
            fseq.setAddress("/tuio/2Dcur");
            fseq.append("fseq");
            fseq.append((int32_t)getElapsedFrames());
        }

        SERVER_COUNT = math<int>::max(1, SERVER_COUNT);
        SERVER_ID = math<int>::clamp(SERVER_ID, 0, SERVER_COUNT - 1);
        float newRegion = 1 / (float)SERVER_COUNT;
        for (const auto &blob : blobTracker.trackedBlobs)
        {
            //Point2f center(blob.center.x + depthOrigin.x, blob.center.y + depthOrigin.y);
            vec2 center(blob.center.x, blob.center.y);

            if (!mInputRoi.contains(center)) continue;

            int blobId = remapTuioId(blob.id);
            osc::Message set;
            set.setAddress("/tuio/2Dcur");
            set.append("set");
            set.append(blobId);             // id
            float mappedX = lmap(center.x / mDepthW, INPUT_X1, INPUT_X2, OUTPUT_X1, OUTPUT_X2);
            mappedX = (SERVER_ID + mappedX) * newRegion;
            float mappedY = lmap(center.y / mDepthH, INPUT_Y1, INPUT_Y2, OUTPUT_Y1, OUTPUT_Y2);
            set.append(mappedX);
            set.append(mappedY);
            set.append(blob.velocity.x / mOutputMap.getWidth());
            set.append(blob.velocity.y / mOutputMap.getHeight());
            set.append(0.0f);     // m
            bundle.append(set);                         // add message to bundle

            alive.append(blobId);               // add blob to list of ALL active IDs
        }

        bundle.append(alive);    //add message to bundle
        bundle.append(fseq);     //add message to bundle

        sender.send(bundle); //send bundle
    }

    void updateBack()
    {
        mBackChannel = mDevice->depthChannel.clone();
        updateTexture(mBackTexture, mBackChannel, getTextureFormatUINT16());
    }

    float mFps = 0;

    struct Layout
    {
        float width;
        float height;
        float halfW;
        float halfH;
        float spc;

        Rectf canvases[4];
    } mLayout;

    ds::DeviceRef mDevice;
    signals::Connection mDirtyConnection;
    params::InterfaceGlRef mParams;
    std::shared_ptr<osc::SenderUdp> mOscSender;
    int mDepthW = 0;
    int mDepthH = 0;

    gl::TextureRef mDepthTexture;

    // vision
    Channel16u mBackChannel;
    gl::TextureRef mBackTexture;
    BlobTracker mBlobTracker;

    Channel mDiffChannel;
    cv::Mat1b mDiffMat;
    gl::TextureRef mDiffTexture;

    Rectf mInputRoi;
    Rectf mOutputMap;

    gl::GlslProgRef	mShader;
};

CINDER_APP(PlaneTouchApp, RendererGl)
