/*****************************************************************************************
 * SGCT                                                                                  *
 * Simple Graphics Cluster Toolkit                                                       *
 *                                                                                       *
 * Copyright (c) 2012-2019                                                               *
 * For conditions of distribution and use, see copyright notice in sgct.h                *
 ****************************************************************************************/

#include <sgct/touch.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>

namespace sgct::core {

std::string getTouchPointInfo(const Touch::TouchPoint& tp) {
    std::string result = "id(" + std::to_string(tp.id) + "),";

    result += "action(";
    switch (tp.action) {
        case Touch::TouchPoint::TouchAction::Pressed:
            result += "Pressed";
            break;
        case Touch::TouchPoint::TouchAction::Moved:
            result += "Moved";
            break;
        case Touch::TouchPoint::TouchAction::Released:
            result += "Released";
            break;
        case Touch::TouchPoint::TouchAction::Stationary:
            result += "Stationary";
            break;
        default:
            result += "NoAction";
    }
    result += "),";

    result += "pixelCoords(" + std::to_string(tp.pixelCoords.x) + "," +
              std::to_string(tp.pixelCoords.y) + "),";

    result += "normPixelCoords(" + std::to_string(tp.normPixelCoords.x) + "," +
              std::to_string(tp.normPixelCoords.y) + "),";

    result += "normPixelDiff(" + std::to_string(tp.normPixelDiff.x) + "," +
              std::to_string(tp.normPixelDiff.y) + ")";

    return result;
}

std::vector<Touch::TouchPoint> Touch::getLatestTouchPoints() const {
    return _touchPoints;
}

void Touch::setLatestPointsHandled() {
    _touchPoints.clear();
}

void Touch::processPoint(int id, int action, double x, double y, int width, int height) {
    glm::vec2 size = glm::vec2(static_cast<float>(width), static_cast<float>(height));
    glm::vec2 pos = glm::vec2(static_cast<float>(x), static_cast<float>(y));
    glm::vec2 normpos = pos / size;

    using TouchAction = TouchPoint::TouchAction;
    TouchAction touchAction = [](int a) -> TouchPoint::TouchAction {
        switch (a) {
            case GLFW_PRESS:   return TouchAction::Pressed;
            case GLFW_MOVE:    return TouchAction::Moved;
            case GLFW_RELEASE: return TouchAction::Released;
            case GLFW_REPEAT:  return TouchAction::Stationary;
            default:           return TouchAction::NoAction;
        }
    }(action);

    glm::vec2 prevPos = pos;

    const auto prevPosMapIt = _previousTouchPositions.find(id);
    if (prevPosMapIt != _previousTouchPositions.end()) {
        prevPos = prevPosMapIt->second;
        if (touchAction == TouchAction::Released) {
            _previousTouchPositions.erase(prevPosMapIt);
        }
        else {
            prevPosMapIt->second = pos;
        }
    }
    else {
        _previousTouchPositions.insert(std::pair<int, glm::ivec2>(id, pos));
    }

    // Add to end of corrected ordered vector if new touch point
    const auto lastIdIdx = std::find(_prevTouchIds.begin(), _prevTouchIds.end(), id);
    if (lastIdIdx == _prevTouchIds.end()) {
        _prevTouchIds.push_back(id);
    }

    // Check if position has not changed and make the point stationary then
    if (touchAction == TouchAction::Moved && glm::distance(pos, prevPos) == 0) {
        touchAction = TouchAction::Stationary;
    }

    _touchPoints.push_back({id, touchAction, pos, normpos, (pos - prevPos) / size});
}

void Touch::processPoints(GLFWtouch* points, int count, int width, int height) {
    for (int i = 0; i < count; ++i) {
        GLFWtouch p = points[i];
        processPoint(p.id, p.action, p.x, p.y, width, height);
    }

    // Ensure that the order to the touch points are the same as last touch event. Note
    // that the ID of a touch point is always the same but their order can vary.
    // Example:
    // lastTouchIds_    touchPoints
    //     0                 0
    //     3                 1 
    //     2                 2
    //     4
    // Will result in:
    //                  touchPoints
    //                       0 (no swap)
    //                       2 (2 will swap with 1)
    //                       1

    int touchIndex = 0; // Index to first unsorted element in touchPoints array
    for (int prevTouchPointId : _prevTouchIds) {
        const auto touchPointIt = std::find_if(
            _touchPoints.begin(),
            _touchPoints.end(),
            [prevTouchPointId](const TouchPoint& p) { return p.id == prevTouchPointId; }
        );
        // Swap current container location with the location it was in last touch event
        if (touchPointIt != _touchPoints.end() &&
            std::distance(_touchPoints.begin(), touchPointIt) != touchIndex)
        {
            std::swap(*(_touchPoints.begin() + touchIndex), *touchPointIt);
            ++touchIndex;
        }
    }

    // Ignore stationary state and count none ended points
    _allPointsStationary = true;
    std::vector<int> endedTouchIds;
    for (const TouchPoint& touchPoint : _touchPoints) {
        if (touchPoint.action != TouchPoint::TouchAction::Stationary) {
            _allPointsStationary = false;
        }

        if (touchPoint.action == TouchPoint::TouchAction::Released) {
            endedTouchIds.push_back(touchPoint.id);
        }
    }

    for (int endedId : endedTouchIds) {
        auto foundIdx = std::find(_prevTouchIds.begin(), _prevTouchIds.end(), endedId);
        if (foundIdx != _prevTouchIds.end()) {
            _prevTouchIds.erase(foundIdx);
        }
    }
}

bool Touch::areAllPointsStationary() const {
    return _allPointsStationary;
}

} // namespace sgct::core
