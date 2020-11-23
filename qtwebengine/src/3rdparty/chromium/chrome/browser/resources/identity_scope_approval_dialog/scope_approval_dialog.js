// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let webview;
let windowId;

/**
 * Points the webview to the starting URL of a scope authorization
 * flow, and unhides the dialog once the page has loaded.
 * @param {string} url The url of the authorization entry point.
 * @param {Object} win The dialog window that contains this page. Can
 *     be left undefined if the caller does not want to display the
 *     window.
 */
function loadAuthUrlAndShowWindow(url, win) {
  // Send popups from the webview to a normal browser window.
  webview.addEventListener('newwindow', function(e) {
    e.window.discard();
    window.open(e.targetUrl);
  });

  webview.addContentScripts([{
    name: 'injectRule',
    matches: ['https://accounts.google.com/*'],
    js: {files: ['inject.js']},
    run_at: 'document_start'
  }]);

  // Request a customized view from GAIA.
  webview.request.onBeforeSendHeaders.addListener(
      function(details) {
        headers = details.requestHeaders || [];
        headers.push({'name': 'X-Browser-View', 'value': 'embedded'});
        return {requestHeaders: headers};
      },
      {
        urls: ['https://accounts.google.com/*'],
      },
      ['blocking', 'requestHeaders']);

  if (!url.toLowerCase().startsWith('https://accounts.google.com/')) {
    document.querySelector('.titlebar').classList.add('titlebar-border');
  }

  webview.src = url;
  webview.addEventListener('loadstop', function() {
    if (win) {
      win.show();
      windowId = win.id;
    }
  });
}

chrome.runtime.onMessageExternal.addListener(function(
    message, sender, sendResponse) {
  chrome.identityPrivate.setConsentResult(message.consentResult, windowId);
});

document.addEventListener('DOMContentLoaded', function() {
  webview = document.querySelector('webview');

  document.querySelector('.titlebar-close-button').onclick = function() {
    window.close();
  };

  chrome.resourcesPrivate.getStrings('identity', function(strings) {
    document.title = strings['window-title'];
  });
});
