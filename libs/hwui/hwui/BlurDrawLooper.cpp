/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "BlurDrawLooper.h"
#include <SkBlurTypes.h>
#include <SkColorSpace.h>
#include <SkMaskFilter.h>
#include <algorithm>
#include <utils/Log.h>

namespace android {

BlurDrawLooper::BlurDrawLooper(SkColor4f color, float blurSigma, SkPoint offset)
        : mColor(color), 
          mBlurSigma(std::min(blurSigma, MAX_SAFE_SIGMA)),
          mOffset(offset) {
    if (blurSigma > MAX_SAFE_SIGMA) {
        ALOGD("BlurDrawLooper: Capping blur sigma: %.2f -> %.2f (max: %.2f)", 
              blurSigma, mBlurSigma, MAX_SAFE_SIGMA);
    }
}

BlurDrawLooper::~BlurDrawLooper() = default;

SkPoint BlurDrawLooper::apply(Paint* paint) const {
    paint->setColor(mColor);
    
    if (mBlurSigma > 0) {
        float safeSigma = std::min(mBlurSigma, MAX_SAFE_SIGMA);
        paint->setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, safeSigma, true));
    }
    return mOffset;
}

sk_sp<BlurDrawLooper> BlurDrawLooper::Make(SkColor4f color, SkColorSpace* cs, float blurSigma,
                                           SkPoint offset) {
    if (cs) {
        SkPaint tmp;
        tmp.setColor(color, cs);
        color = tmp.getColor4f();
    }
    
    float cappedSigma = std::min(blurSigma, MAX_SAFE_SIGMA);
    if (blurSigma > MAX_SAFE_SIGMA) {
        ALOGD("BlurDrawLooper::Make(): Capping blur sigma: %.2f -> %.2f", blurSigma, cappedSigma);
    }
    
    return sk_sp<BlurDrawLooper>(new BlurDrawLooper(color, cappedSigma, offset));
}

}  // namespace android
