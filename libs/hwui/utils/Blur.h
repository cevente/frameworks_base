/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HWUI_BLUR_H
#define ANDROID_HWUI_BLUR_H

#include <cutils/compiler.h>
#include <stdint.h>

namespace android {
namespace uirenderer {

class Blur {
public:
    // SD685 optimized limits - now safe with Stack Blur O(1) algorithm
    // Higher values produce the ultra-clean industrial frosted glass effect
    static constexpr float MAX_SAFE_RADIUS = 40.0f;   // Now safe with O(1) algorithm
    static constexpr float MAX_SAFE_SIGMA = 25.0f;    // Corresponding sigma
    
    // If radius > 0, return the corresponding sigma, else return 0
    static float convertRadiusToSigma(float radius);
    // If sigma > 0.5, return the corresponding radius, else return 0
    static float convertSigmaToRadius(float sigma);
    // If the original radius was on an integer boundary then after the sigma to
    // radius conversion a small rounding error may be introduced. This function
    // accounts for that error and snaps to the appropriate integer boundary.
    static uint32_t convertRadiusToInt(float radius);
    
    // Helper functions to clamp values to safe limits
    static float clampRadius(float radius) {
        return (radius > MAX_SAFE_RADIUS) ? MAX_SAFE_RADIUS : 
               (radius < 0 ? 0 : radius);
    }
    
    static float clampSigma(float sigma) {
        return (sigma > MAX_SAFE_SIGMA) ? MAX_SAFE_SIGMA : 
               (sigma < 0 ? 0 : sigma);
    }

    static void generateGaussianWeights(float* weights, float radius);
    
    // Optimized Stack Blur implementations - O(1) per pixel
    static void horizontal(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                           int32_t width, int32_t height);
    static void vertical(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                         int32_t width, int32_t height);
    
    // Legacy Gaussian blur (kept for reference, not used)
    static void horizontalGaussian(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                                   int32_t width, int32_t height);
    static void verticalGaussian(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                                 int32_t width, int32_t height);
};

}  // namespace uirenderer
}  // namespace android

#endif  // ANDROID_HWUI_BLUR_H
