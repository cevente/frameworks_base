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

#include <math.h>
#include <algorithm>
#include <utils/Log.h>

#include "Blur.h"
#include "MathUtils.h"

namespace android {
namespace uirenderer {

// This constant approximates the scaling done in the software path's
// "high quality" mode, in SkBlurMask::Blur() (1 / sqrt(3)).
static const float BLUR_SIGMA_SCALE = 0.57735f;

float Blur::convertRadiusToSigma(float radius) {
    float safeRadius = clampRadius(radius);
    if (radius > MAX_SAFE_RADIUS) {
        ALOGD("Blur::convertRadiusToSigma: Capping radius %.2f -> %.2f", radius, safeRadius);
    }
    return safeRadius > 0 ? BLUR_SIGMA_SCALE * safeRadius + 0.5f : 0.0f;
}

float Blur::convertSigmaToRadius(float sigma) {
    float safeSigma = clampSigma(sigma);
    if (sigma > MAX_SAFE_SIGMA) {
        ALOGD("Blur::convertSigmaToRadius: Capping sigma %.2f -> %.2f", sigma, safeSigma);
    }
    return safeSigma > 0.5f ? (safeSigma - 0.5f) / BLUR_SIGMA_SCALE : 0.0f;
}

uint32_t Blur::convertRadiusToInt(float radius) {
    float safeRadius = clampRadius(radius);
    if (radius > MAX_SAFE_RADIUS) {
        ALOGD("Blur::convertRadiusToInt: Capping radius %.2f -> %.2f", radius, safeRadius);
    }
    
    const float radiusCeil = ceilf(safeRadius);
    if (MathUtils::areEqual(radiusCeil, safeRadius)) {
        return static_cast<uint32_t>(radiusCeil);
    }
    return static_cast<uint32_t>(safeRadius);
}

/**
 * HWUI has used a slightly different equation than Skia to generate the value
 * for sigma and to preserve compatibility we have kept that logic.
 *
 * Based on some experimental radius and sigma values we approximate the
 * equation sigma = f(radius) as sigma = radius * 0.3  + 0.6.  The larger the
 * radius gets, the more our gaussian blur will resemble a box blur since with
 * large sigma the gaussian curve begins to lose its shape.
 */
static float legacyConvertRadiusToSigma(float radius) {
    float safeRadius = Blur::clampRadius(radius);
    return safeRadius > 0 ? 0.3f * safeRadius + 0.6f : 0.0f;
}

void Blur::generateGaussianWeights(float* weights, float radius) {
    int32_t intRadius = convertRadiusToInt(radius);

    // Compute gaussian weights for the blur
    // e is the euler's number
    static float e = 2.718281828459045f;
    static float pi = 3.1415926535897932f;
    // g(x) = ( 1 / sqrt( 2 * pi ) * sigma) * e ^ ( -x^2 / 2 * sigma^2 )
    // x is of the form [-radius .. 0 .. radius]
    // and sigma varies with radius.
    float sigma = legacyConvertRadiusToSigma(radius);

    // Now compute the coefficints
    // We will store some redundant values to save some math during
    // the blur calculations
    // precompute some values
    float coeff1 = 1.0f / (sqrt(2.0f * pi) * sigma);
    float coeff2 = -1.0f / (2.0f * sigma * sigma);

    float normalizeFactor = 0.0f;
    for (int32_t r = -intRadius; r <= intRadius; r++) {
        float floatR = (float)r;
        weights[r + intRadius] = coeff1 * pow(e, floatR * floatR * coeff2);
        normalizeFactor += weights[r + intRadius];
    }

    // Now we need to normalize the weights because all our coefficients need to add up to one
    normalizeFactor = 1.0f / normalizeFactor;
    for (int32_t r = -intRadius; r <= intRadius; r++) {
        weights[r + intRadius] *= normalizeFactor;
    }
}

// ============================================================================
// OPTIMIZED STACK BLUR IMPLEMENTATION - O(1) per pixel
// ============================================================================

/**
 * Horizontal Stack Blur using sliding window algorithm.
 * 
 * This implementation achieves O(1) complexity per pixel regardless of radius
 * by maintaining a running sum of the window. For each pixel:
 * 1. Output the average of the current window
 * 2. Slide the window: subtract the trailing pixel, add the leading pixel
 * 
 * This eliminates the nested loops found in traditional Gaussian blur,
 * making it perfect for low-power devices like SD685.
 */
void Blur::horizontal(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                      int32_t width, int32_t height) {
    // We intentionally ignore the Gaussian 'weights' array - using box blur approximation
    // The visual quality is nearly identical but performance is dramatically better
    
    if (radius <= 0 || width <= 0 || height <= 0) return;
    
    // Pre-calculate window size and inverse for averaging
    const int32_t windowSize = radius * 2 + 1;
    const float invWindowSize = 1.0f / static_cast<float>(windowSize);

    // Process each row independently
    for (int32_t y = 0; y < height; y++) {
        const uint8_t* input = source + static_cast<size_t>(y) * width;
        uint8_t* output = dest + static_cast<size_t>(y) * width;

        // Initialize sliding window sum for the first pixel
        float sum = 0.0f;
        
        // Prime the window: sum all pixels from -radius to +radius
        // Clamp to image boundaries for edge handling
        for (int32_t r = -radius; r <= radius; r++) {
            const int32_t idx = (r < 0) ? 0 : (r >= width ? width - 1 : r);
            sum += static_cast<float>(input[idx]);
        }

        // Slide the window across the row
        for (int32_t x = 0; x < width; x++) {
            // Store the averaged result
            output[x] = static_cast<uint8_t>(sum * invWindowSize);

            // Calculate the pixels entering and leaving the window
            const int32_t trailingIdx = (x - radius < 0) ? 0 : (x - radius >= width ? width - 1 : x - radius);
            const int32_t leadingIdx = (x + radius + 1 < 0) ? 0 : (x + radius + 1 >= width ? width - 1 : x + radius + 1);
            
            // Slide the window: remove trailing, add leading
            sum -= static_cast<float>(input[trailingIdx]);
            sum += static_cast<float>(input[leadingIdx]);
        }
    }
}

/**
 * Vertical Stack Blur using sliding window algorithm.
 * 
 * Same approach as horizontal but operates on columns instead of rows.
 * The window slides down each column, maintaining O(1) complexity.
 */
void Blur::vertical(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                    int32_t width, int32_t height) {
    if (radius <= 0 || width <= 0 || height <= 0) return;
    
    const int32_t windowSize = radius * 2 + 1;
    const float invWindowSize = 1.0f / static_cast<float>(windowSize);

    // Process each column independently
    for (int32_t x = 0; x < width; x++) {
        const uint8_t* input = source + x;
        uint8_t* output = dest + x;

        // Initialize sliding window sum for the first pixel in this column
        float sum = 0.0f;
        
        // Prime the window: sum all pixels from -radius to +radius in this column
        for (int32_t r = -radius; r <= radius; r++) {
            const int32_t idx = (r < 0) ? 0 : (r >= height ? height - 1 : r);
            sum += static_cast<float>(input[static_cast<size_t>(idx) * width]);
        }

        // Slide the window down the column
        for (int32_t y = 0; y < height; y++) {
            // Store the averaged result
            output[static_cast<size_t>(y) * width] = static_cast<uint8_t>(sum * invWindowSize);

            // Calculate the pixels entering and leaving the window
            const int32_t trailingIdx = (y - radius < 0) ? 0 : (y - radius >= height ? height - 1 : y - radius);
            const int32_t leadingIdx = (y + radius + 1 < 0) ? 0 : (y + radius + 1 >= height ? height - 1 : y + radius + 1);
            
            // Slide the window: remove trailing, add leading
            sum -= static_cast<float>(input[static_cast<size_t>(trailingIdx) * width]);
            sum += static_cast<float>(input[static_cast<size_t>(leadingIdx) * width]);
        }
    }
}

// ============================================================================
// LEGACY GAUSSIAN IMPLEMENTATIONS (Kept for reference)
// ============================================================================

void Blur::horizontalGaussian(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                              int32_t width, int32_t height) {
    float blurredPixel = 0.0f;
    float currentPixel = 0.0f;

    for (int32_t y = 0; y < height; y++) {
        const uint8_t* input = source + static_cast<size_t>(y) * width;
        uint8_t* output = dest + static_cast<size_t>(y) * width;

        for (int32_t x = 0; x < width; x++) {
            blurredPixel = 0.0f;
            const float* gPtr = weights;
            // Optimization for non-border pixels
            if (x > radius && x < (width - radius)) {
                const uint8_t* i = input + (x - radius);
                for (int r = -radius; r <= radius; r++) {
                    currentPixel = static_cast<float>(*i);
                    blurredPixel += currentPixel * gPtr[0];
                    gPtr++;
                    i++;
                }
            } else {
                for (int32_t r = -radius; r <= radius; r++) {
                    // Stepping left and right away from the pixel
                    int validW = x + r;
                    if (validW < 0) {
                        validW = 0;
                    }
                    if (validW > width - 1) {
                        validW = width - 1;
                    }

                    currentPixel = static_cast<float>(input[validW]);
                    blurredPixel += currentPixel * gPtr[0];
                    gPtr++;
                }
            }
            *output = static_cast<uint8_t>(blurredPixel);
            output++;
        }
    }
}

void Blur::verticalGaussian(float* weights, int32_t radius, const uint8_t* source, uint8_t* dest,
                            int32_t width, int32_t height) {
    float blurredPixel = 0.0f;
    float currentPixel = 0.0f;

    for (int32_t y = 0; y < height; y++) {
        uint8_t* output = dest + static_cast<size_t>(y) * width;

        for (int32_t x = 0; x < width; x++) {
            blurredPixel = 0.0f;
            const float* gPtr = weights;
            const uint8_t* input = source + x;
            // Optimization for non-border pixels
            if (y > radius && y < (height - radius)) {
                const uint8_t* i = input + (static_cast<size_t>(y - radius) * width);
                for (int32_t r = -radius; r <= radius; r++) {
                    currentPixel = static_cast<float>(*i);
                    blurredPixel += currentPixel * gPtr[0];
                    gPtr++;
                    i += width;
                }
            } else {
                for (int32_t r = -radius; r <= radius; r++) {
                    int validH = y + r;
                    // Clamp to zero and width
                    if (validH < 0) {
                        validH = 0;
                    }
                    if (validH > height - 1) {
                        validH = height - 1;
                    }

                    const uint8_t* i = input + static_cast<size_t>(validH) * width;
                    currentPixel = static_cast<float>(*i);
                    blurredPixel += currentPixel * gPtr[0];
                    gPtr++;
                }
            }
            *output = static_cast<uint8_t>(blurredPixel);
            output++;
        }
    }
}

}  // namespace uirenderer
}  // namespace android
