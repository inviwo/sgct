/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/window.h>

#include <sgct/clustermanager.h>
#include <sgct/config.h>
#include <sgct/engine.h>
#include <sgct/error.h>
#include <sgct/logger.h>
#include <sgct/mpcdi.h>
#include <sgct/networkmanager.h>
#include <sgct/nonlinearprojection.h>
#include <sgct/ogl_headers.h>
#include <sgct/settings.h>
#include <sgct/texturemanager.h>
#include <sgct/shaders/internalshaders.h>
#include <sgct/helpers/stringfunctions.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

#ifdef WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
HDC hDC;
#endif // WIN32

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define Err(code, msg) Error(Error::Component::Window, code, msg)

namespace {
    // abock(2019-10-02); glbinding doesn't come with default bindings against the wgl.xml
    // file, so we have to resolve these manually

#ifdef WIN32
    bool AreFunctionsResolved = false;
    using BindSwapBarrier = GLboolean(*)(GLuint group, GLuint barrier);
    BindSwapBarrier wglBindSwapBarrierNV = nullptr;
    using JoinSwapGroup = GLboolean(*)(HDC hDC, GLuint group);
    JoinSwapGroup wglJoinSwapGroupNV = nullptr;
    using QueryMaxSwapGroups = GLboolean(*)(HDC hDC, GLuint* mGroups, GLuint* mBarriers);
    QueryMaxSwapGroups wglQueryMaxSwapGroupsNV = nullptr;
    using QueryFrameCount = GLboolean(*)(HDC hDC, GLuint* count);
    QueryFrameCount wglQueryFrameCountNV = nullptr;
    using ResetFrameCount = GLboolean(*)(HDC hDC);
    ResetFrameCount wglResetFrameCountNV = nullptr;
#endif // WIN32

    void windowResizeCallback(GLFWwindow* window, int width, int height) {
        width = std::max(width, 1);
        height = std::max(height, 1);

        sgct::core::Node& node = sgct::core::ClusterManager::instance().getThisNode();
        for (int i = 0; i < node.getNumberOfWindows(); i++) {
            if (node.getWindow(i).getWindowHandle() == window) {
                node.getWindow(i).setWindowResolution(glm::ivec2(width, height));
            }
        }
    }

    void frameBufferResizeCallback(GLFWwindow* window, int width, int height) {
        width = std::max(width, 1);
        height = std::max(height, 1);

        sgct::core::Node& node = sgct::core::ClusterManager::instance().getThisNode();
        for (int i = 0; i < node.getNumberOfWindows(); i++) {
            if (node.getWindow(i).getWindowHandle() == window) {
                node.getWindow(i).setFramebufferResolution(glm::ivec2(width, height));
            }
        }
    }

    void windowFocusCallback(GLFWwindow* window, int state) {
        sgct::core::Node& node = sgct::core::ClusterManager::instance().getThisNode();

        for (int i = 0; i < node.getNumberOfWindows(); i++) {
            if (node.getWindow(i).getWindowHandle() == window) {
                node.getWindow(i).setFocused(state == GLFW_TRUE);
            }
        }
    }
} // namespace

namespace sgct {

bool Window::_useSwapGroups = false;
bool Window::_isBarrierActive = false;
bool Window::_isSwapGroupMaster = false;
GLFWwindow* Window::_sharedHandle = nullptr;

Window::Window(int id) : _id(id) {}

void Window::applyWindow(const config::Window& window) {
    if (window.name) {
        setName(*window.name);
    }
    if (!window.tags.empty()) {
        setTags(window.tags);
    }
    if (window.bufferBitDepth) {
        ColorBitDepth bd = [](config::Window::ColorBitDepth cbd) {
            switch (cbd) {
                case config::Window::ColorBitDepth::Depth8:
                    return ColorBitDepth::Depth8;
                case config::Window::ColorBitDepth::Depth16:
                    return ColorBitDepth::Depth16;
                case config::Window::ColorBitDepth::Depth16Float:
                    return ColorBitDepth::Depth16Float;
                case config::Window::ColorBitDepth::Depth32Float:
                    return ColorBitDepth::Depth32Float;
                case config::Window::ColorBitDepth::Depth16Int:
                    return ColorBitDepth::Depth16Int;
                case config::Window::ColorBitDepth::Depth32Int:
                    return ColorBitDepth::Depth32Int;
                case config::Window::ColorBitDepth::Depth16UInt:
                    return ColorBitDepth::Depth16UInt;
                case config::Window::ColorBitDepth::Depth32UInt:
                    return ColorBitDepth::Depth32UInt;
                default:
                    throw std::logic_error("Unhandled case label");
            }
        }(*window.bufferBitDepth);
        setColorBitDepth(bd);
    }
    if (window.isFullScreen) {
        setWindowMode(*window.isFullScreen);
    }
    if (window.isFloating) {
        setFloating(*window.isFloating);
    }
    if (window.alwaysRender) {
        setRenderWhileHidden(*window.alwaysRender);
    }
    if (window.isHidden) {
        setVisible(*window.isHidden);
    }
    if (window.doubleBuffered) {
        setDoubleBuffered(*window.doubleBuffered);
    }
    if (window.msaa) {
        setNumberOfAASamples(*window.msaa);
    }
    if (window.hasAlpha) {
        setAlpha(*window.hasAlpha);
    }
    if (window.useFxaa) {
        setUseFXAA(*window.useFxaa);
    }
    if (window.isDecorated) {
        setWindowDecoration(*window.isDecorated);
    }
    if (window.draw2D) {
        setCallDraw2DFunction(*window.draw2D);
    }
    if (window.draw3D) {
        setCallDraw3DFunction(*window.draw3D);
    }
    if (window.blitPreviousWindow) {
        setBlitPreviousWindow(*window.blitPreviousWindow);
    }
    if (window.monitor) {
        setFullScreenMonitorIndex(*window.monitor);
    }
    if (window.mpcdi) {
        core::mpcdi::ReturnValue r = core::mpcdi::parseMpcdiConfiguration(*window.mpcdi);
        setWindowPosition(glm::ivec2(0));
        initWindowResolution(r.resolution);
        setFramebufferResolution(r.resolution);
        setFixResolution(true);

        for (const core::mpcdi::ReturnValue::ViewportInfo& vp : r.viewports) {
            std::unique_ptr<core::Viewport> v = std::make_unique<core::Viewport>(this);
            v->applySettings(vp.proj);
            v->setMpcdiWarpMesh(vp.meshData);
            addViewport(std::move(v));
        }
        return;
    }
    if (window.stereo) {
        StereoMode sm = [](config::Window::StereoMode mode) {
            switch (mode) {
                case config::Window::StereoMode::NoStereo:
                    return StereoMode::NoStereo;
                case config::Window::StereoMode::Active:
                    return StereoMode::Active;
                case config::Window::StereoMode::AnaglyphRedCyan:
                    return StereoMode::AnaglyphRedCyan;
                case config::Window::StereoMode::AnaglyphAmberBlue:
                    return StereoMode::AnaglyphAmberBlue;
                case config::Window::StereoMode::AnaglyphRedCyanWimmer:
                    return StereoMode::AnaglyphRedCyanWimmer;
                case config::Window::StereoMode::Checkerboard:
                    return StereoMode::Checkerboard;
                case config::Window::StereoMode::CheckerboardInverted:
                    return StereoMode::CheckerboardInverted;
                case config::Window::StereoMode::VerticalInterlaced:
                    return StereoMode::VerticalInterlaced;
                case config::Window::StereoMode::VerticalInterlacedInverted:
                    return StereoMode::VerticalInterlacedInverted;
                case config::Window::StereoMode::Dummy:
                    return StereoMode::Dummy;
                case config::Window::StereoMode::SideBySide:
                    return StereoMode::SideBySide;
                case config::Window::StereoMode::SideBySideInverted:
                    return StereoMode::SideBySideInverted;
                case config::Window::StereoMode::TopBottom:
                    return StereoMode::TopBottom;
                case config::Window::StereoMode::TopBottomInverted:
                    return StereoMode::TopBottomInverted;
                default:
                    throw std::logic_error("Unhandled case label");
            }
        }(*window.stereo);
        setStereoMode(sm);
    }
    if (window.pos) {
        setWindowPosition(*window.pos);
    }

    initWindowResolution(window.size);

    if (window.resolution) {
        setFramebufferResolution(*window.resolution);
        setFixResolution(true);
    }

    for (const config::Viewport& viewport : window.viewports) {
        std::unique_ptr<core::Viewport> vp = std::make_unique<core::Viewport>(this);
        vp->applyViewport(viewport);
        addViewport(std::move(vp));
    }
}

void Window::setName(std::string name) {
    _name = std::move(name);
}

void Window::setTags(std::vector<std::string> tags) {
    _tags = std::move(tags);
}

const std::string& Window::getName() const {
    return _name;
}

bool Window::hasTag(const std::string& tag) const {
    return std::find(_tags.cbegin(), _tags.cend(), tag) != _tags.cend();
}

int Window::getId() const {
    return _id;
}

bool Window::isFocused() const {
    return _hasFocus;
}

void Window::close() {
    makeOpenGLContextCurrent(Context::Shared);

    Logger::Info("Deleting screen capture data for window %d", _id);
    _screenCaptureLeftOrMono = nullptr;
    _screenCaptureRight = nullptr;

    // delete FBO stuff
    if (_finalFBO) {
        Logger::Info("Releasing OpenGL buffers for window %d", _id);
        _finalFBO = nullptr;
        destroyFBOs();
    }

    Logger::Info("Deleting VBOs for window %d", _id);
    glDeleteBuffers(1, &_vbo);
    _vbo = 0;

    Logger::Info("Deleting VAOs for window %d", _id);
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;

    _stereo.shader.deleteProgram();

    // Current handle must be set at the end to properly destroy the window
    makeOpenGLContextCurrent(Context::Window);

    _currentViewport = nullptr;
    _viewports.clear();

    glfwSetWindowSizeCallback(_windowHandle, nullptr);
    glfwSetFramebufferSizeCallback(_windowHandle, nullptr);
    glfwSetWindowFocusCallback(_windowHandle, nullptr);
    glfwSetWindowIconifyCallback(_windowHandle, nullptr);

#ifdef WIN32
    if (_useSwapGroups && glfwExtensionSupported("WGL_NV_swap_group")) {
        wglBindSwapBarrierNV(1, 0); // un-bind
        wglJoinSwapGroupNV(hDC, 0); // un-join
    }
#endif
}

void Window::init() {
    if (!_isFullScreen) {
        if (_setWindowPos) {
            glfwSetWindowPos(_windowHandle, _windowPos.x, _windowPos.y);
        }
        glfwSetWindowSizeCallback(_windowHandle, windowResizeCallback);
        glfwSetFramebufferSizeCallback(_windowHandle, frameBufferResizeCallback);
        glfwSetWindowFocusCallback(_windowHandle, windowFocusCallback);
    }

    std::string title = "SGCT node: " +
        core::ClusterManager::instance().getThisNode().getAddress() + " (" +
        (core::NetworkManager::instance().isComputerServer() ? "master" : "client") +
        ": " + std::to_string(_id) + ")";

    setWindowTitle(_name.empty() ? title.c_str() : _name.c_str());

    // swap the buffers and update the window
    glfwSwapBuffers(_windowHandle);
}

void Window::initOGL() {
    _colorFormat = GL_BGRA;
    
    switch (_bufferColorBitDepth) {
        case ColorBitDepth::Depth8:
            _internalColorFormat = GL_RGBA8;
            _colorDataType = GL_UNSIGNED_BYTE;
            _bytesPerColor = 1;
            break;
        case ColorBitDepth::Depth16:
            _internalColorFormat = GL_RGBA16;
            _colorDataType = GL_UNSIGNED_SHORT;
            _bytesPerColor = 2;
            break;
        case ColorBitDepth::Depth16Float:
            _internalColorFormat = GL_RGBA16F;
            _colorDataType = GL_HALF_FLOAT;
            _bytesPerColor = 2;
            break;
        case ColorBitDepth::Depth32Float:
            _internalColorFormat = GL_RGBA32F;
            _colorDataType = GL_FLOAT;
            _bytesPerColor = 4;
            break;
        case ColorBitDepth::Depth16Int:
            _internalColorFormat = GL_RGBA16I;
            _colorDataType = GL_SHORT;
            _bytesPerColor = 2;
            break;
        case ColorBitDepth::Depth32Int:
            _internalColorFormat = GL_RGBA32I;
            _colorDataType = GL_INT;
            _bytesPerColor = 2;
            break;
        case ColorBitDepth::Depth16UInt:
            _internalColorFormat = GL_RGBA16UI;
            _colorDataType = GL_UNSIGNED_SHORT;
            _bytesPerColor = 2;
            break;
        case ColorBitDepth::Depth32UInt:
            _internalColorFormat = GL_RGBA32UI;
            _colorDataType = GL_UNSIGNED_INT;
            _bytesPerColor = 4;
            break;
        default:
            throw std::logic_error("Unhandled case label");
    }
    
    createTextures();
    createVBOs(); // must be created before FBO
    createFBOs();
    initScreenCapture();
    loadShaders();

    for (const std::unique_ptr<core::Viewport>& vp : _viewports) {
        if (!vp->hasSubViewports()) {
            continue;
        }

        setCurrentViewport(vp.get());
        vp->getNonLinearProjection()->setStereo(_stereoMode != StereoMode::NoStereo);
        vp->getNonLinearProjection()->init(
            _internalColorFormat,
            _colorFormat,
            _colorDataType,
            _nAASamples
        );

        glm::vec2 viewport = glm::vec2(_framebufferRes) * vp->getSize();
        vp->getNonLinearProjection()->update(std::move(viewport));
    }

#ifdef WIN32
    if (!AreFunctionsResolved && glfwExtensionSupported("WGL_NV_swap_group")) {
        // abock(2019-10-02); I had to hand-resolve these functions as glbindings does not
        // come with build-in support for the wgl.xml functions
        // See https://github.com/cginternals/glbinding/issues/132 for when it is resolved

        wglBindSwapBarrierNV = reinterpret_cast<BindSwapBarrier>(
            glfwGetProcAddress("wglBindSwapBarrierNV")
        );
        assert(wglBindSwapBarrierNV);
        wglJoinSwapGroupNV = reinterpret_cast<JoinSwapGroup>(
            glfwGetProcAddress("wglJoinSwapGroupNV")
        );
        assert(wglJoinSwapGroupNV);
        wglQueryMaxSwapGroupsNV = reinterpret_cast<QueryMaxSwapGroups>(
            glfwGetProcAddress("wglQueryMaxSwapGroupsNV")
        );
        assert(wglQueryMaxSwapGroupsNV);
        wglQueryFrameCountNV = reinterpret_cast<QueryFrameCount>(
            glfwGetProcAddress("wglQueryFrameCountNV")
        );
        assert(wglQueryFrameCountNV);
        wglResetFrameCountNV = reinterpret_cast<ResetFrameCount>(
            glfwGetProcAddress("wglResetFrameCountNV")
        );
        assert(wglResetFrameCountNV);

        if (!wglBindSwapBarrierNV || !wglJoinSwapGroupNV || !wglQueryMaxSwapGroupsNV ||
            !wglQueryFrameCountNV || !wglResetFrameCountNV)
        {
            Logger::Error("Error resolving swapgroup functions");
            Logger::Info(
                "wglBindSwapBarrierNV(: %p\twglJoinSwapGroupNV: %p\t"
                "wglQueryMaxSwapGroupsNV: %p\twglQueryFrameCountNV: %p\t"
                "wglResetFrameCountNV: %p",
                wglBindSwapBarrierNV, wglJoinSwapGroupNV, wglQueryMaxSwapGroupsNV,
                wglQueryFrameCountNV,wglResetFrameCountNV
            );
            throw Err(8000, "Error resolving swapgroup functions");
        };

        AreFunctionsResolved = true;
    }
#endif // WIN32
}

void Window::initContextSpecificOGL() {
    makeOpenGLContextCurrent(Context::Window);
    for (int j = 0; j < getNumberOfViewports(); j++) {
        core::Viewport& vp = getViewport(j);
        vp.loadData();
        if (vp.hasBlendMaskTexture() || vp.hasBlackLevelMaskTexture()) {
            _hasAnyMasks = true;
        }
    }
}

unsigned int Window::getFrameBufferTexture(TextureIndex index) {
    switch (index) {
        case TextureIndex::LeftEye:
            if (_frameBufferTextures.leftEye == 0) {
                generateTexture(_frameBufferTextures.leftEye, TextureType::Color);
            }
            return _frameBufferTextures.leftEye;
        case TextureIndex::RightEye:
            if (_frameBufferTextures.rightEye == 0) {
                generateTexture(_frameBufferTextures.rightEye, TextureType::Color);
            }
            return _frameBufferTextures.rightEye;
        case TextureIndex::Intermediate:
            if (_frameBufferTextures.intermediate == 0) {
                generateTexture(_frameBufferTextures.intermediate, TextureType::Color);
            }
            return _frameBufferTextures.intermediate;
        case TextureIndex::Depth:
            if (_frameBufferTextures.depth == 0) {
                generateTexture(_frameBufferTextures.depth, TextureType::Depth);
            }
            return _frameBufferTextures.depth;
        case TextureIndex::Normals:
            if (_frameBufferTextures.normals == 0) {
                generateTexture(_frameBufferTextures.normals, TextureType::Normal);
            }
            return _frameBufferTextures.normals;
        case TextureIndex::Positions:
            if (_frameBufferTextures.positions == 0) {
                generateTexture(_frameBufferTextures.positions, TextureType::Position);
            }
            return _frameBufferTextures.positions;
        default:
            throw std::logic_error("Unhandled case label");
    }
}

void Window::setVisible(bool state) {
    if (state != _isVisible) {
        if (_windowHandle) {
            if (state) {
                glfwShowWindow(_windowHandle);
            }
            else {
                glfwHideWindow(_windowHandle);
            }
        }
        _isVisible = state;
    }
}

void Window::setRenderWhileHidden(bool state) {
    _shouldRenderWhileHidden = state;
}

void Window::setFocused(bool state) {
    _hasFocus = state;
}

void Window::setWindowTitle(const char* title) {
    glfwSetWindowTitle(_windowHandle, title);
}

void Window::setWindowResolution(glm::ivec2 resolution) {
    // In case this callback gets triggered from elsewhere than SGCT's glfwPollEvents, we
    // want to make sure the actual resizing is deferred to the end of the frame. This can
    // happen if some other library pulls events from the operating system for example by
    // calling nextEventMatchingMask (MacOS) or PeekMessage (Windows). If we were to set
    // the actual resolution directly, we may render half a frame with resolution A and
    // the other half with resolution b, which is undefined behaviour.
    // _hasPendingWindowRes is checked in Window::updateResolution, which is called from
    // Engine's render loop after glfwPollEvents.
    _pendingWindowRes = std::move(resolution);
}

void Window::setFramebufferResolution(glm::ivec2 resolution) {
    // Defer actual update of framebuffer resolution until next call to updateResolutions.
    // (Same reason as described for setWindowResolution above.)
    if (!_useFixResolution) {
        _pendingFramebufferRes = std::move(resolution);
    }
}

void Window::swap(bool takeScreenshot) {
    if (!(_isVisible || _shouldRenderWhileHidden)) {
        return;
    }

    makeOpenGLContextCurrent(Context::Window);
        
    if (takeScreenshot) {
        if (Settings::instance().getCaptureFromBackBuffer() && _isDoubleBuffered) {
            if (_screenCaptureLeftOrMono) {
                _screenCaptureLeftOrMono->saveScreenCapture(
                    0,
                    _stereoMode == StereoMode::Active ?
                        core::ScreenCapture::CaptureSource::LeftBackBuffer :
                        core::ScreenCapture::CaptureSource::BackBuffer
                );
            }
            if (_screenCaptureRight && _stereoMode == StereoMode::Active) {
                _screenCaptureLeftOrMono->saveScreenCapture(
                    0,
                    core::ScreenCapture::CaptureSource::RightBackBuffer
                );
            }
        }
        else {
            if (_screenCaptureLeftOrMono) {
                _screenCaptureLeftOrMono->saveScreenCapture(_frameBufferTextures.leftEye);
            }
            if (_screenCaptureRight && _stereoMode > StereoMode::NoStereo &&
                _stereoMode < Window::StereoMode::SideBySide)
            {
                _screenCaptureRight->saveScreenCapture(_frameBufferTextures.rightEye);
            }
        }
    }

    // swap
    _windowResOld = _windowRes;

    if (_isDoubleBuffered) {
        glfwSwapBuffers(_windowHandle);
    }
    else {
        glFinish();
    }
}

void Window::updateResolutions() {
    if (_pendingWindowRes.has_value()) {
        _windowRes = *_pendingWindowRes;
        float ratio = static_cast<float>(_windowRes.x) / static_cast<float>(_windowRes.y);

        // Set field of view of each of this window's viewports to match new aspect ratio,
        // adjusting only the horizontal (x) values
        for (int j = 0; j < getNumberOfViewports(); ++j) {
            core::Viewport& vp = getViewport(j);
            vp.updateFovToMatchAspectRatio(_aspectRatio, ratio);
            Logger::Debug(
                "Update aspect ratio in viewport# %d (%f --> %f)", j, _aspectRatio, ratio
            );
        }
        _aspectRatio = ratio;

        // Redraw window
        if (_windowHandle) {
            glfwSetWindowSize(_windowHandle, _windowRes.x, _windowRes.y);
        }

        Logger::Debug(
            "Resolution changed to %dx%d in window %d", _windowRes.x, _windowRes.y, _id
        );
        _pendingWindowRes = std::nullopt;
    }

    if (_pendingFramebufferRes.has_value()) {
        _framebufferRes = *_pendingFramebufferRes;

        Logger::Debug(
            "Framebuffer resolution changed to %dx%d for window %d",
            _framebufferRes.x, _framebufferRes.y, _id
        );

        _pendingFramebufferRes = std::nullopt;
    }
}

void Window::setHorizFieldOfView(float hFovDeg) {
    // Set field of view of each of this window's viewports to match new horiz/vert
    // aspect ratio, adjusting only the horizontal (x) values.
    for (int j = 0; j < getNumberOfViewports(); ++j) {
        getViewport(j).setHorizontalFieldOfView(hFovDeg);
    }
    Logger::Debug(
        "Horizontal FOV changed to %f for window %d", hFovDeg, getNumberOfViewports(), _id
    );
}

void Window::initWindowResolution(glm::ivec2 resolution) {
    _windowRes = resolution;
    _windowResOld = _windowRes;
    _aspectRatio = static_cast<float>(resolution.x) / static_cast<float>(resolution.y);
    _isWindowResolutionSet = true;

    if (!_useFixResolution) {
        _framebufferRes = resolution;
    }
}

bool Window::update() {
    if (!_isVisible || !isWindowResized()) {
        return false;
    }
    makeOpenGLContextCurrent(Context::Window);

    resizeFBOs();

    auto resizePBO = [this](core::ScreenCapture& sc) {
        const int nCaptureChannels = _hasAlpha ? 4 : 3;
        if (Settings::instance().getCaptureFromBackBuffer()) {
            // capture from buffer supports only 8-bit per color component
            sc.setTextureTransferProperties(GL_UNSIGNED_BYTE);
            const glm::ivec2 res = getResolution();
            sc.initOrResize(res, nCaptureChannels, 1);
        }
        else {
            // default: capture from texture (supports HDR)
            sc.setTextureTransferProperties(_colorDataType);
            const glm::ivec2 res = getFramebufferResolution();
            sc.initOrResize(res, nCaptureChannels, _bytesPerColor);
        }
    };
    if (_screenCaptureLeftOrMono) {
        resizePBO(*_screenCaptureLeftOrMono);
    }
    if (_screenCaptureRight) {
        resizePBO(*_screenCaptureRight);
    }

    // resize non linear projection buffers
    for (const std::unique_ptr<core::Viewport>& vp : _viewports) {
        if (vp->hasSubViewports()) {
            glm::vec2 viewport = glm::vec2(_framebufferRes) * vp->getSize();
            vp->getNonLinearProjection()->update(std::move(viewport));
        }
    }

    return true;
}

void Window::makeOpenGLContextCurrent(Context context) {
    glfwMakeContextCurrent(context == Context::Shared ? _sharedHandle : _windowHandle);
}

bool Window::isWindowResized() const {
    return (_windowRes.x != _windowResOld.x || _windowRes.y != _windowResOld.y);
}

bool Window::isBarrierActive() {
    return _isBarrierActive;
}

bool Window::isUsingSwapGroups() {
    return _useSwapGroups;
}

bool Window::isSwapGroupMaster() {
    return _isSwapGroupMaster;
}

bool Window::isFullScreen() const {
    return _isFullScreen;
}

bool Window::isFloating() const {
    return _isFloating;
}

bool Window::isDoubleBuffered() const {
    return _isDoubleBuffered;
}

bool Window::isVisible() const {
    return _isVisible;
}

bool Window::isRenderingWhileHidden() const {
    return _shouldRenderWhileHidden;
}

bool Window::isFixResolution() const {
    return _useFixResolution;
}

bool Window::isStereo() const {
    return _stereoMode != StereoMode::NoStereo;
}

void Window::setWindowPosition(glm::ivec2 positions) {
    _windowPos = std::move(positions);
    _setWindowPos = true;
}

void Window::setWindowMode(bool fullscreen) {
    _isFullScreen = fullscreen;
}

void Window::setFloating(bool floating) {
    _isFloating = floating;
}

void Window::setDoubleBuffered(bool doubleBuffered) {
    _isDoubleBuffered = doubleBuffered;
}

void Window::setWindowDecoration(bool state) {
    _isDecorated = state;
}

void Window::setFullScreenMonitorIndex(int index) {
    _monitorIndex = index;
}

void Window::setBarrier(bool state) {
    if (_useSwapGroups && state != _isBarrierActive) {
        Logger::Info("Enabling Nvidia swap barrier");

#ifdef WIN32
        _isBarrierActive = wglBindSwapBarrierNV(1, state ? 1 : 0) == GL_TRUE;
#endif // WIN32
    }
}

void Window::setFixResolution(bool state) {
    _useFixResolution = state;
}

void Window::setUseFXAA(bool state) {
    _useFXAA = state;
    Logger::Debug("FXAA status: %s for window %d", state ? "enabled" : "disabled", _id);
}

void Window::setUseQuadbuffer(bool state) {
    _useQuadBuffer = state;
    if (_useQuadBuffer) {
        glfwWindowHint(GLFW_STEREO, GLFW_TRUE);
        Logger::Info("Window %d: Enabling quadbuffered rendering", _id);
    }
}

void Window::setCallDraw2DFunction(bool state) {
    _hasCallDraw2DFunction = state;
    if (!_hasCallDraw2DFunction) {
        Logger::Info("Window %d: Draw 2D function disabled", _id);
    }
}

void Window::setCallDraw3DFunction(bool state) {
    _hasCallDraw3DFunction = state;
    if (!_hasCallDraw3DFunction) {
        Logger::Info("Window %d: Draw 3D function disabled", _id);
    }
}

void Window::setBlitPreviousWindow(bool state) {
    _shouldBitPreviousWindow = state;
    if (_shouldBitPreviousWindow) {
        Logger::Info("Window %d: BlitPreviousWindow enabled", _id);
    }
}

void Window::openWindow(GLFWwindow* share, bool isLastWindow) {
    glfwWindowHint(GLFW_DEPTH_BITS, 32);
    glfwWindowHint(GLFW_DECORATED, _isDecorated ? GLFW_TRUE : GLFW_FALSE);

    const int antiAliasingSamples = getNumberOfAASamples();
    glfwWindowHint(GLFW_SAMPLES, antiAliasingSamples > 1 ? antiAliasingSamples : 0);

    glfwWindowHint(GLFW_AUTO_ICONIFY, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, _isFloating ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_DOUBLEBUFFER, _isDoubleBuffered ? GLFW_TRUE : GLFW_FALSE);
    if (!_isVisible) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    setUseQuadbuffer(_stereoMode == StereoMode::Active);

    GLFWmonitor* monitor = nullptr;
    if (_isFullScreen) {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        const int refreshRateHint = Settings::instance().getRefreshRateHint();
        if (refreshRateHint > 0) {
            glfwWindowHint(GLFW_REFRESH_RATE, refreshRateHint);
        }
        
        if (_monitorIndex > 0 && _monitorIndex < count) {
            monitor = monitors[_monitorIndex];
        }
        else {
            monitor = glfwGetPrimaryMonitor();
            if (_monitorIndex >= count) {
                Logger::Info(
                    "Window(%d): Invalid monitor index (%d). Computer has %d monitors",
                    _id, _monitorIndex, count
                );
            }
        }

        if (!_isWindowResolutionSet) {
            const GLFWvidmode* currentMode = glfwGetVideoMode(monitor);
            _windowRes = glm::ivec2(currentMode->width, currentMode->height);
        }
    }

    _windowHandle = glfwCreateWindow(_windowRes.x, _windowRes.y, "SGCT", monitor, share);
    if (_windowHandle == nullptr) {
        throw Err(8001, "Error opening window");
    }

    _sharedHandle = share != nullptr ? share : _windowHandle;
    glfwMakeContextCurrent(_windowHandle);

    // Mac for example scales the window size != frame buffer size
    glm::ivec2 bufferSize;
    glfwGetFramebufferSize(_windowHandle, &bufferSize[0], &bufferSize[1]);

    _windowInitialRes = _windowRes;
    _scale = glm::vec2(bufferSize) / glm::vec2(_windowRes);
    if (!_useFixResolution) {
        _framebufferRes = bufferSize;
    }
        
    // Swap inerval:
    //  -1 = adaptive sync
    //   0 = vertical sync off
    //   1 = wait for vertical sync
    //   2 = fix when using swapgroups in xp and running half the framerate

    // If we would set multiple windows to use vsync, we would get a framerate of (monitor
    // refreshrate)/(number of windows), which is something that might really slow down a
    // multi-monitor application. Setting last window to the requested interval, which
    // does mean all other windows will respect the last window in the pipeline.
    glfwSwapInterval(isLastWindow ? Settings::instance().getSwapInterval() : 0);
    
    // if client, disable mouse pointer
    if (!Engine::instance().isMaster()) {
        glfwSetInputMode(_windowHandle, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }

    _hasFocus = glfwGetWindowAttrib(_windowHandle, GLFW_FOCUSED) == GLFW_TRUE;
        
    glfwMakeContextCurrent(_sharedHandle);

    _screenCaptureLeftOrMono = std::make_unique<core::ScreenCapture>();
    if (useRightEyeTexture()) {
        _screenCaptureRight = std::make_unique<core::ScreenCapture>();
    }
    _finalFBO = std::make_unique<core::OffScreenBuffer>();
}

void Window::initNvidiaSwapGroups() {
#ifdef WIN32
    if (glfwExtensionSupported("WGL_NV_swap_group")) {
        Logger::Info("Joining Nvidia swap group");

        hDC = wglGetCurrentDC();

        unsigned int maxBarrier = 0;
        unsigned int maxGroup = 0;
        wglQueryMaxSwapGroupsNV(hDC, &maxGroup, &maxBarrier);
        Logger::Info(
            "WGL_NV_swap_group extension is supported. Max number of groups: %d. "
            "Max number of barriers: %d", maxGroup, maxBarrier
        );

        // wglJoinSwapGroupNV adds hDC to the swap group specified by group. If hDC is a
        // member of a different group, it is implicitly removed from that group first. A
        // swap group is specified as an integer between 0 and maxGroups returned by
        // wglQueryMaxSwapGroupsNV. If group is zero, the hDC is unbound from its current
        // group, if any. If group is larger than maxGroups, wglJoinSwapGroupNV fails.
        _useSwapGroups = wglJoinSwapGroupNV(hDC, 1) == GL_TRUE;
        Logger::Info("Joining swapgroup 1 [%s]", _useSwapGroups ? "ok" : "failed");
    }
    else {
        _useSwapGroups = false;
    }
#endif
}

void Window::initScreenCapture() {
    auto initializeCapture = [this](core::ScreenCapture& sc) {
        const int nCaptureChannels = _hasAlpha ? 4 : 3;
        if (Settings::instance().getCaptureFromBackBuffer()) {
            // capturing from buffer supports only 8-bit per color component capture
            const glm::ivec2 res = getResolution();
            sc.initOrResize(res, nCaptureChannels, 1);
            sc.setTextureTransferProperties(GL_UNSIGNED_BYTE);
        }
        else {
            // default: capture from texture (supports HDR)
            const glm::ivec2 res = getFramebufferResolution();
            sc.initOrResize(res, nCaptureChannels, _bytesPerColor);
            sc.setTextureTransferProperties(_colorDataType);
        }

        Settings::CaptureFormat format = Settings::instance().getCaptureFormat();
        switch (format) {
            case Settings::CaptureFormat::PNG:
                sc.setCaptureFormat(core::ScreenCapture::CaptureFormat::PNG);
                break;
            case Settings::CaptureFormat::TGA:
                sc.setCaptureFormat(core::ScreenCapture::CaptureFormat::TGA);
                break;
            case Settings::CaptureFormat::JPG:
                sc.setCaptureFormat(core::ScreenCapture::CaptureFormat::JPEG);
                break;
            default:
                throw std::logic_error("Unhandled case label");
        }
    };


    if (_screenCaptureLeftOrMono) {
        using namespace core;
        if (useRightEyeTexture()) {
            _screenCaptureLeftOrMono->init(_id, ScreenCapture::EyeIndex::StereoLeft);
        }
        else {
            _screenCaptureLeftOrMono->init(_id, ScreenCapture::EyeIndex::Mono);
        }
        initializeCapture(*_screenCaptureLeftOrMono);
    }

    if (_screenCaptureRight) {
        _screenCaptureRight->init(_id, core::ScreenCapture::EyeIndex::StereoRight);
        initializeCapture(*_screenCaptureRight);
    }
}

unsigned int Window::getSwapGroupFrameNumber() {
    unsigned int frameNumber = 0;

#ifdef WIN32
    if (_isBarrierActive && glfwExtensionSupported("WGL_NV_swap_group")) {
        wglQueryFrameCountNV(hDC, &frameNumber);
    }
#endif
    return frameNumber;
}

void Window::resetSwapGroupFrameNumber() {
#ifdef WIN32
    if (_isBarrierActive) {
        _isSwapGroupMaster = glfwExtensionSupported("WGL_NV_swap_group") &&
                             wglResetFrameCountNV(hDC);
        if (_isSwapGroupMaster) {
            Logger::Info("Resetting frame counter");
        }
        else {
            Logger::Info("Resetting frame counter failed");
        }
    }
#endif
}

void Window::createTextures() {
    GLint max;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
    if (_framebufferRes.x > max || _framebufferRes.y > max) {
        Logger::Error("Window %d: Requested framebuffer too big (Max: %d)",_id, max);
        return;
    }

    // Create left and right color & depth textures; don't allocate the right eye image if
    // stereo is not used create a postFX texture for effects
    generateTexture(_frameBufferTextures.leftEye, TextureType::Color);
    if (useRightEyeTexture()) {
        generateTexture(_frameBufferTextures.rightEye, TextureType::Color);
    }
    if (Settings::instance().useDepthTexture()) {
        generateTexture(_frameBufferTextures.depth, TextureType::Depth);
    }
    if (_useFXAA) {
        generateTexture(_frameBufferTextures.intermediate, TextureType::Color);
    }
    if (Settings::instance().useNormalTexture()) {
        generateTexture(_frameBufferTextures.normals, TextureType::Normal);
    }
    if (Settings::instance().usePositionTexture()) {
        generateTexture(_frameBufferTextures.positions, TextureType::Position);
    }

    Logger::Debug("Targets initialized successfully for window %d", _id);
}

void Window::generateTexture(unsigned int& id, Window::TextureType type) {
    glDeleteTextures(1, &id);
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    
    // Determine the internal texture format, the texture format, and the pixel type
    const GLenum internalFormat = [this](Window::TextureType t) -> GLenum {
        switch (t) {
            case TextureType::Color:
                return _internalColorFormat;
            case TextureType::Depth:
                return GL_DEPTH_COMPONENT32;
            case TextureType::Normal:
            case TextureType::Position:
                return Settings::instance().getBufferFloatPrecision();
            default:
                throw std::logic_error("Unhandled case label");
        }
    }(type);

    const glm::ivec2 res = _framebufferRes;
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, res.x, res.y);
    Logger::Debug("%dx%d texture generated for window %d", res.x, res.y, id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
}

void Window::createFBOs() {
    _finalFBO->setInternalColorFormat(_internalColorFormat);
    _finalFBO->createFBO(_framebufferRes.x, _framebufferRes.y, _nAASamples);

    Logger::Debug(
        "Window %d: FBO initiated successfully. Number of samples: %d",
        _id, _finalFBO->isMultiSampled() ? _nAASamples : 1
    );
}

void Window::createVBOs() {
    constexpr const std::array<const float, 20> QuadVerts = {
        0.f, 0.f, -1.f, -1.f, -1.f,
        1.f, 0.f,  1.f, -1.f, -1.f,
        0.f, 1.f, -1.f,  1.f, -1.f,
        1.f, 1.f,  1.f,  1.f, -1.f
    };

    glGenVertexArrays(1, &_vao);
    Logger::Debug("Window: Generating VAO: %d", _vao);

    glGenBuffers(1, &_vbo);
    Logger::Debug("Window: Generating VBO: %d", _vbo);

    glBindVertexArray(_vao);
    glBindBuffer(GL_ARRAY_BUFFER, _vbo);

    //2TF + 3VF = 2*4 + 3*4 = 20
    glBufferData(GL_ARRAY_BUFFER, 20 * sizeof(float), QuadVerts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        5 * sizeof(float),
        reinterpret_cast<void*>(8)
    );

    glBindVertexArray(0);
}

void Window::loadShaders() {
    if (_stereoMode <= StereoMode::Active || _stereoMode >= StereoMode::SideBySide) {
        return;
    }

    // reload shader program if it exists
    if (_stereo.shader.isLinked()) {
        _stereo.shader.deleteProgram();
    }

    std::string stereoVertShader = core::shaders::AnaglyphVert;
    std::string stereoFragShader = [](sgct::Window::StereoMode mode) {
        switch (mode) {
            case StereoMode::AnaglyphRedCyan:
                return core::shaders::AnaglyphRedCyanFrag;
            case StereoMode::AnaglyphAmberBlue:
                return core::shaders::AnaglyphAmberBlueFrag;
            case StereoMode::AnaglyphRedCyanWimmer:
                return core::shaders::AnaglyphRedCyanWimmerFrag;
            case StereoMode::Checkerboard:
                return core::shaders::CheckerBoardFrag;
            case StereoMode::CheckerboardInverted:
                return core::shaders::CheckerBoardInvertedFrag;
            case StereoMode::VerticalInterlaced:
                return core::shaders::VerticalInterlacedFrag;
            case StereoMode::VerticalInterlacedInverted:
                return core::shaders::VerticalInterlacedInvertedFrag;
            default:
                return core::shaders::DummyStereoFrag;
        }
    }(_stereoMode);

    _stereo.shader = ShaderProgram("StereoShader");
    _stereo.shader.addShaderSource(stereoVertShader, stereoFragShader);
    _stereo.shader.createAndLinkProgram();
    _stereo.shader.bind();
    _stereo.leftTexLoc = glGetUniformLocation(_stereo.shader.getId(), "leftTex");
    glUniform1i(_stereo.leftTexLoc, 0);
    _stereo.rightTexLoc = glGetUniformLocation(_stereo.shader.getId(), "rightTex");
    glUniform1i(_stereo.rightTexLoc, 1);
    ShaderProgram::unbind();
}

void Window::renderScreenQuad() const {
    glBindVertexArray(_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

core::OffScreenBuffer* Window::getFBO() const {
    return _finalFBO.get();
}

GLFWwindow* Window::getWindowHandle() const {
    return _windowHandle;
}

glm::ivec2 Window::getFinalFBODimensions() const {
    return _framebufferRes;
}

void Window::resizeFBOs() {
    if (_useFixResolution) {
        return;
    }

    makeOpenGLContextCurrent(Context::Shared);
    destroyFBOs();
    createTextures();

    _finalFBO->resizeFBO(_framebufferRes.x, _framebufferRes.y, _nAASamples);
        
    if (!_finalFBO->isMultiSampled()) {
        _finalFBO->bind();
        _finalFBO->attachColorTexture(_frameBufferTextures.leftEye);
        _finalFBO->unbind();
    }
}

void Window::destroyFBOs() {
    glDeleteTextures(1, &_frameBufferTextures.leftEye);
    _frameBufferTextures.leftEye = 0;
    glDeleteTextures(1, &_frameBufferTextures.rightEye);
    _frameBufferTextures.rightEye = 0;
    glDeleteTextures(1, &_frameBufferTextures.depth);
    _frameBufferTextures.depth = 0;
    glDeleteTextures(1, &_frameBufferTextures.intermediate);
    _frameBufferTextures.intermediate = 0;
    glDeleteTextures(1, &_frameBufferTextures.positions);
    _frameBufferTextures.positions = 0;
}

Window::StereoMode Window::getStereoMode() const {
    return _stereoMode;
}

void Window::addViewport(std::unique_ptr<core::Viewport> vpPtr) {
    _viewports.push_back(std::move(vpPtr));
    Logger::Debug("Adding viewport (total %d)", _viewports.size());
}

core::BaseViewport* Window::getCurrentViewport() const {
    if (_currentViewport == nullptr) {
        Logger::Error("Window %d: No current viewport", _id);
    }
    return _currentViewport;
}

const core::Viewport& Window::getViewport(int index) const {
    return *_viewports[index];
}

core::Viewport& Window::getViewport(int index) {
    return *_viewports[index];
}

glm::ivec4 Window::getCurrentViewportPixelCoords() const {
    return glm::ivec4(
        static_cast<int>(getCurrentViewport()->getPosition().x * _framebufferRes.x),
        static_cast<int>(getCurrentViewport()->getPosition().y * _framebufferRes.y),
        static_cast<int>(getCurrentViewport()->getSize().x * _framebufferRes.x),
        static_cast<int>(getCurrentViewport()->getSize().y * _framebufferRes.y)
    );
}

int Window::getNumberOfViewports() const {
    return static_cast<int>(_viewports.size());
}

void Window::setNumberOfAASamples(int samples) {
    _nAASamples = samples;
}

int Window::getNumberOfAASamples() const {
    return _nAASamples;
}

void Window::setStereoMode(StereoMode sm) {
    _stereoMode = sm;
    if (_windowHandle) {
        loadShaders();
    }
}

core::ScreenCapture* Window::getScreenCapturePointer(Eye eye) const {
    switch (eye) {
        case Eye::MonoOrLeft: return _screenCaptureLeftOrMono.get();
        case Eye::Right:      return _screenCaptureRight.get();
        default:              throw std::logic_error("Unhandled case label");
    }
}

void Window::setCurrentViewport(core::BaseViewport* vp) {
    _currentViewport = vp;
}

bool Window::useRightEyeTexture() const {
    return _stereoMode != StereoMode::NoStereo && _stereoMode < StereoMode::SideBySide;
}

void Window::setAlpha(bool state) {
    _hasAlpha = state;
}

bool Window::hasAlpha() const {
    return _hasAlpha;
}

void Window::setColorBitDepth(ColorBitDepth cbd) {
    _bufferColorBitDepth = cbd;
}

Window::ColorBitDepth Window::getColorBitDepth() const {
    return _bufferColorBitDepth;
}

float Window::getHorizFieldOfViewDegrees() const {
    return _viewports[0]->getHorizontalFieldOfViewDegrees();
}

glm::ivec2 Window::getResolution() const {
    return _windowRes;
}

glm::ivec2 Window::getFramebufferResolution() const {
    return _framebufferRes;
}

glm::ivec2 Window::getInitialResolution() const {
    return _windowInitialRes;
}

glm::vec2 Window::getScale() const {
    return _scale;
}

float Window::getAspectRatio() const {
    return _aspectRatio;
}

int Window::getFramebufferBPCC() const {
    return _bytesPerColor;
}

bool Window::hasAnyMasks() const {
    return _hasAnyMasks;
}

bool Window::useFXAA() const {
    return _useFXAA;
}

void Window::bindStereoShaderProgram() const {
    _stereo.shader.bind();
}

int Window::getStereoShaderLeftTexLoc() const {
    return _stereo.leftTexLoc;
}

int Window::getStereoShaderRightTexLoc() const {
    return _stereo.rightTexLoc;
}

bool Window::shouldCallDraw2DFunction() const {
    return _hasCallDraw2DFunction;
}

bool Window::shouldCallDraw3DFunction() const {
    return _hasCallDraw3DFunction;
}

bool Window::shouldBlitPreviousWindow() const {
    return _shouldBitPreviousWindow;
}

} // namespace sgct
