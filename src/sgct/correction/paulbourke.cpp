/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/correction/paulbourke.h>

#include <sgct/engine.h>
#include <sgct/messagehandler.h>
#include <sgct/viewport.h>
#include <sgct/window.h>

#if (_MSC_VER >= 1400)
    #define _sscanf sscanf_s
#else
    #define _sscanf sscanf
#endif

namespace {
    constexpr const int MaxLineLength = 1024;
} // namespace

namespace sgct::core::correction {

Buffer generatePaulBourkeMesh(const std::string& path, const glm::ivec2& pos,
                              const glm::ivec2& size)
{
    Buffer buf;

    MessageHandler::instance()->printInfo(
        "CorrectionMesh: Reading Paul Bourke spherical mirror mesh data from '%s'",
        path.c_str()
    );

    FILE* meshFile = nullptr;
    bool loadSuccess = false;
#if (_MSC_VER >= 1400)
    loadSuccess = fopen_s(&meshFile, path.c_str(), "r") == 0;
#else
    meshFile = fopen(path.c_str(), "r");
    loadSuccess = meshFile != nullptr;
#endif
    if (!loadSuccess) {
        MessageHandler::instance()->printError(
            "CorrectionMesh: Failed to open warping mesh file '%s'", path.c_str()
        );
        return Buffer();
    }

    char lineBuffer[MaxLineLength];

    // get the fist line containing the mapping type _id
    int mappingType = -1;
    if (fgets(lineBuffer, MaxLineLength, meshFile) != nullptr) {
        _sscanf(lineBuffer, "%d", &mappingType);
    }

    // get the mesh dimensions
    glm::ivec2 meshSize = glm::ivec2(-1, -1);
    if (fgets(lineBuffer, MaxLineLength, meshFile) != nullptr) {
        if (_sscanf(lineBuffer, "%d %d", &meshSize[0], &meshSize[1]) == 2) {
            buf.vertices.reserve(meshSize.x * meshSize.y);
        }
    }

    // check if everyting useful is set
    if (mappingType == -1 || meshSize.x == -1 || meshSize.y == -1) {
        MessageHandler::instance()->printError("CorrectionMesh: Invalid data");
        return Buffer();
    }

    // get all data
    float x, y, s, t, intensity;
    while (!feof(meshFile)) {
        if (fgets(lineBuffer, MaxLineLength, meshFile) != nullptr) {
            if (_sscanf(lineBuffer, "%f %f %f %f %f", &x, &y, &s, &t, &intensity) == 5) {
                CorrectionMeshVertex vertex;
                vertex.x = x;
                vertex.y = y;
                vertex.s = s;
                vertex.t = t;

                vertex.r = intensity;
                vertex.g = intensity;
                vertex.b = intensity;
                vertex.a = 1.f;

                buf.vertices.push_back(vertex);
            }
        }
    }

    // generate indices
    for (int c = 0; c < (meshSize.x - 1); c++) {
        for (int r = 0; r < (meshSize.y - 1); r++) {
            const int i0 = r * meshSize.x + c;
            const int i1 = r * meshSize.x + (c + 1);
            const int i2 = (r + 1) * meshSize.x + (c + 1);
            const int i3 = (r + 1) * meshSize.x + c;

            // 3      2
            //  x____x
            //  |   /|
            //  |  / |
            //  | /  |
            //  |/   |
            //  x----x
            // 0      1

            // triangle 1
            buf.indices.push_back(i0);
            buf.indices.push_back(i1);
            buf.indices.push_back(i2);

            // triangle 2
            buf.indices.push_back(i0);
            buf.indices.push_back(i2);
            buf.indices.push_back(i3);
        }
    }

    const float aspect = Engine::instance()->getCurrentWindow().getAspectRatio() *
                   (size.x / size.y);
    for (unsigned int i = 0; i < buf.vertices.size(); i++) {
        // convert to [0, 1] (normalize)
        buf.vertices[i].x /= aspect;
        buf.vertices[i].x = (buf.vertices[i].x + 1.f) / 2.f;
        buf.vertices[i].y = (buf.vertices[i].y + 1.f) / 2.f;
        
        // scale, re-position and convert to [-1, 1]
        buf.vertices[i].x = (buf.vertices[i].x * size.x + pos.x) * 2.f - 1.f;
        buf.vertices[i].y = (buf.vertices[i].y * size.y + pos.y) * 2.f - 1.f;

        // convert to viewport coordinates
        buf.vertices[i].s = buf.vertices[i].s * size.x + pos.x;
        buf.vertices[i].t = buf.vertices[i].t * size.y + pos.y;
    }

    buf.isComplete = true;
    buf.geometryType = GL_TRIANGLES;

    return buf;
}

} // namespace sgct::core::correction
