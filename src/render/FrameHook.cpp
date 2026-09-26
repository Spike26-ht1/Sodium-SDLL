#include "FrameHook.hpp"

#include <android/log.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>

namespace sodium::render {
namespace {

constexpr const char* kLogTag = "SodiumSDLL";

constexpr const char* kVertexShader = R"(
attribute vec2 aPosition;
attribute vec2 aTexCoord;
varying vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

constexpr const char* kFragmentShader = R"(
precision mediump float;
varying vec2 vTexCoord;
uniform sampler2D uPreviousFrame;
uniform sampler2D uCurrentFrame;
uniform float uBlend;
void main() {
    vec4 previousColor = texture2D(uPreviousFrame, vTexCoord);
    vec4 currentColor = texture2D(uCurrentFrame, vTexCoord);
    gl_FragColor = mix(previousColor, currentColor, uBlend);
}
)";

void* findEglSwapBuffers() {
    void* egl = dlopen("libEGL.so", RTLD_NOW | RTLD_NOLOAD);
    if (!egl) egl = dlopen("libEGL.so", RTLD_NOW);
    if (!egl) return nullptr;
    void* symbol = dlsym(egl, "eglSwapBuffers");
    dlclose(egl);
    return symbol;
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return shader;

    std::array<char, 1024> log{};
    GLsizei length = 0;
    glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Shader compile failed: %.*s", length, log.data());
    glDeleteShader(shader);
    return 0;
}

GLuint createProgram() {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vertex || !fragment) {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
        return 0;
    }

    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        std::array<char, 1024> log{};
        GLsizei length = 0;
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Program link failed: %.*s", length, log.data());
        glDeleteProgram(program);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }

    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

}

FrameHook& FrameHook::instance() {
    static FrameHook hook;
    return hook;
}

void FrameHook::setMultiplier(int multiplier) {
    mMultiplier.store(std::clamp(multiplier, 1, 3), std::memory_order_relaxed);
}

bool FrameHook::install() {
    if (mInstalled.load(std::memory_order_acquire)) return true;

    void* target = findEglSwapBuffers();
    if (!target) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "eglSwapBuffers was not found");
        return false;
    }

    if (!core::installHook(
            target,
            reinterpret_cast<void*>(&FrameHook::swapBuffersDetour),
            reinterpret_cast<void**>(&mOriginal),
            mHook)) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Failed to hook eglSwapBuffers");
        return false;
    }

    mFrameCount.store(0, std::memory_order_relaxed);
    mHasPrevious = false;
    mResourcesInitialized = false;
    mInstalled.store(true, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Frame generator hook installed");
    return true;
}

void FrameHook::uninstall() {
    if (!mInstalled.exchange(false, std::memory_order_acq_rel)) return;

    core::removeHook(mHook);
    mOriginal = nullptr;
    mFrameCount.store(0, std::memory_order_relaxed);

    if (eglGetCurrentContext() != EGL_NO_CONTEXT) destroyResources();
    mHasPrevious = false;
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "Frame generator hook removed");
}

bool FrameHook::initResources() {
    if (mResourcesInitialized) return true;

    mProgram = createProgram();
    if (!mProgram) return false;

    static constexpr GLfloat vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenBuffers(1, &mVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    mPositionLocation = glGetAttribLocation(mProgram, "aPosition");
    mTexCoordLocation = glGetAttribLocation(mProgram, "aTexCoord");
    mPreviousTextureLocation = glGetUniformLocation(mProgram, "uPreviousFrame");
    mCurrentTextureLocation = glGetUniformLocation(mProgram, "uCurrentFrame");
    mBlendLocation = glGetUniformLocation(mProgram, "uBlend");

    if (mPositionLocation < 0 || mTexCoordLocation < 0 ||
        mPreviousTextureLocation < 0 || mCurrentTextureLocation < 0 || mBlendLocation < 0) {
        destroyResources();
        return false;
    }

    glGenTextures(1, &mPreviousTexture);
    glGenTextures(1, &mCurrentTexture);
    if (!mPreviousTexture || !mCurrentTexture) {
        destroyResources();
        return false;
    }

    for (const GLuint texture : {mPreviousTexture, mCurrentTexture}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    mResourcesInitialized = true;
    return true;
}

bool FrameHook::resizeResources(GLint width, GLint height) {
    if (width <= 0 || height <= 0) return false;
    if (mWidth == width && mHeight == height) return true;

    mWidth = width;
    mHeight = height;
    mHasPrevious = false;

    for (const GLuint texture : {mPreviousTexture, mCurrentTexture}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool FrameHook::captureCurrentFrame(GLint width, GLint height) {
    glBindTexture(GL_TEXTURE_2D, mCurrentTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

bool FrameHook::drawFrameTexture(float blend) {
    glUseProgram(mProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mPreviousTexture);
    glUniform1i(mPreviousTextureLocation, 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, mCurrentTexture);
    glUniform1i(mCurrentTextureLocation, 1);
    glUniform1f(mBlendLocation, blend);

    glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
    glEnableVertexAttribArray(static_cast<GLuint>(mPositionLocation));
    glVertexAttribPointer(static_cast<GLuint>(mPositionLocation), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(static_cast<GLuint>(mTexCoordLocation));
    glVertexAttribPointer(static_cast<GLuint>(mTexCoordLocation), 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(GLfloat), reinterpret_cast<void*>(2 * sizeof(GLfloat)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(static_cast<GLuint>(mPositionLocation));
    glDisableVertexAttribArray(static_cast<GLuint>(mTexCoordLocation));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    return glGetError() == GL_NO_ERROR;
}

void FrameHook::restoreGlState(GLint previousProgram,
                               GLint previousFramebuffer,
                               GLint previousArrayBuffer,
                               GLint previousElementArrayBuffer,
                               GLint previousActiveTexture,
                               GLint previousTexture0,
                               GLint previousTexture1,
                               const GLint* previousViewport,
                               GLboolean previousScissor,
                               GLboolean previousDepth,
                               GLboolean previousBlend,
                               GLboolean previousCull,
                               GLboolean previousStencil) {
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(previousElementArrayBuffer));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture1));

    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);

    if (previousScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (previousDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (previousBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (previousCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (previousStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
}

void FrameHook::destroyResources() {
    if (mPreviousTexture) glDeleteTextures(1, &mPreviousTexture);
    if (mCurrentTexture) glDeleteTextures(1, &mCurrentTexture);
    if (mVertexBuffer) glDeleteBuffers(1, &mVertexBuffer);
    if (mProgram) glDeleteProgram(mProgram);

    mPreviousTexture = 0;
    mCurrentTexture = 0;
    mVertexBuffer = 0;
    mProgram = 0;
    mResourcesInitialized = false;
    mWidth = 0;
    mHeight = 0;
}

EGLBoolean FrameHook::swapBuffersDetour(EGLDisplay display, EGLSurface surface) {
    return instance().onSwapBuffers(display, surface);
}

EGLBoolean FrameHook::onSwapBuffers(EGLDisplay display, EGLSurface surface) {
    ++mFrameCount;

    if (!mOriginal) return EGL_FALSE;

    const int multiplier = mMultiplier.load(std::memory_order_relaxed);
    if (multiplier <= 1 || eglGetCurrentContext() == EGL_NO_CONTEXT) {
        return mOriginal(display, surface);
    }

    EGLint width = 0;
    EGLint height = 0;
    if (eglQuerySurface(display, surface, EGL_WIDTH, &width) != EGL_TRUE ||
        eglQuerySurface(display, surface, EGL_HEIGHT, &height) != EGL_TRUE ||
        width <= 0 || height <= 0) {
        return mOriginal(display, surface);
    }

    if (!initResources() || !resizeResources(width, height)) {
        return mOriginal(display, surface);
    }

    GLint previousProgram = 0;
    GLint previousFramebuffer = 0;
    GLint previousArrayBuffer = 0;
    GLint previousElementArrayBuffer = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture0 = 0;
    GLint previousTexture1 = 0;
    GLint previousViewport[4]{};

    const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean previousBlend = glIsEnabled(GL_BLEND);
    const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean previousStencil = glIsEnabled(GL_STENCIL_TEST);

    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &previousElementArrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture1);
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glGetIntegerv(GL_VIEWPORT, previousViewport);

    // eglSwapBuffers is the presentation boundary. The frame should be in the default framebuffer here.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!captureCurrentFrame(width, height)) {
        restoreGlState(previousProgram, previousFramebuffer, previousArrayBuffer, previousElementArrayBuffer,
                       previousActiveTexture, previousTexture0, previousTexture1, previousViewport,
                       previousScissor, previousDepth, previousBlend, previousCull, previousStencil);
        return mOriginal(display, surface);
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glViewport(0, 0, width, height);

    // The first captured frame has no predecessor, so it is presented unchanged.
    if (!mHasPrevious) {
        const EGLBoolean result = mOriginal(display, surface);
        std::swap(mPreviousTexture, mCurrentTexture);
        mHasPrevious = (result == EGL_TRUE);
        restoreGlState(previousProgram, previousFramebuffer, previousArrayBuffer, previousElementArrayBuffer,
                       previousActiveTexture, previousTexture0, previousTexture1, previousViewport,
                       previousScissor, previousDepth, previousBlend, previousCull, previousStencil);
        return result;
    }

    EGLBoolean result = EGL_TRUE;
    for (int i = 1; i < multiplier; ++i) {
        const float blend = static_cast<float>(i) / static_cast<float>(multiplier);
        if (!drawFrameTexture(blend)) {
            result = EGL_FALSE;
            break;
        }
        result = mOriginal(display, surface);
        if (result != EGL_TRUE) break;
    }

    // Present the real rendered frame last.
    if (result == EGL_TRUE) {
        if (!drawFrameTexture(1.0f)) {
            result = EGL_FALSE;
        } else {
            result = mOriginal(display, surface);
        }
    }

    std::swap(mPreviousTexture, mCurrentTexture);
    mHasPrevious = (result == EGL_TRUE);

    restoreGlState(previousProgram, previousFramebuffer, previousArrayBuffer, previousElementArrayBuffer,
                   previousActiveTexture, previousTexture0, previousTexture1, previousViewport,
                   previousScissor, previousDepth, previousBlend, previousCull, previousStencil);
    return result;
}

}
