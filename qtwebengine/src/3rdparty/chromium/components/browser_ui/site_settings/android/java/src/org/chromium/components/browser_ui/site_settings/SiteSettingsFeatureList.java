// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.browser_ui.site_settings;

import org.chromium.base.FeatureList;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.library_loader.LibraryLoader;

/**
 * Provides an API for querying the status of Site Settings features.
 */
// TODO(crbug.com/1060097): Remove/update this once a generalized FeatureList exists.
@JNINamespace("browser_ui")
@MainDex
public class SiteSettingsFeatureList {
    public static final String APP_NOTIFICATION_STATUS_MESSAGING = "AppNotificationStatusMessaging";

    private SiteSettingsFeatureList() {}

    /**
     * Returns whether the specified feature is enabled or not.
     *
     * Note: Features queried through this API must be added to the array
     * |kFeaturesExposedToJava| in
     * //components/browser_ui/site_settings/android/site_settings_feature_list.cc
     *
     * @param featureName The name of the feature to query.
     * @return Whether the feature is enabled or not.
     */
    public static boolean isEnabled(String featureName) {
        Boolean testValue = FeatureList.getTestValueForFeature(featureName);
        if (testValue != null) return testValue;
        assert isNativeInitialized();
        return SiteSettingsFeatureListJni.get().isEnabled(featureName);
    }

    /**
     * @return Whether the native FeatureList is initialized or not.
     */
    private static boolean isNativeInitialized() {
        if (FeatureList.hasTestFeatures()) return true;

        if (!LibraryLoader.getInstance().isInitialized()) return false;
        // Even if the native library is loaded, the C++ FeatureList might not be initialized yet.
        // In that case, accessing it will not immediately fail, but instead cause a crash later
        // when it is initialized. Return whether the native FeatureList has been initialized,
        // so the return value can be tested, or asserted for a more actionable stack trace
        // on failure.
        //
        // The FeatureList is however guaranteed to be initialized by the time
        // AsyncInitializationActivity#finishNativeInitialization is called.
        return SiteSettingsFeatureListJni.get().isInitialized();
    }

    @NativeMethods
    interface Natives {
        boolean isInitialized();
        boolean isEnabled(String featureName);
    }
}
