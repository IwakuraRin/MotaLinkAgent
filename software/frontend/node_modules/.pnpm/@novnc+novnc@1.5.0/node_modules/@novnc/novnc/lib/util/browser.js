"use strict";

function _typeof(o) { "@babel/helpers - typeof"; return _typeof = "function" == typeof Symbol && "symbol" == typeof Symbol.iterator ? function (o) { return typeof o; } : function (o) { return o && "function" == typeof Symbol && o.constructor === Symbol && o !== Symbol.prototype ? "symbol" : typeof o; }, _typeof(o); }
Object.defineProperty(exports, "__esModule", {
  value: true
});
exports.hasScrollbarGutter = exports.dragThreshold = void 0;
exports.isAndroid = isAndroid;
exports.isBlink = isBlink;
exports.isChrome = isChrome;
exports.isChromeOS = isChromeOS;
exports.isChromium = isChromium;
exports.isEdge = isEdge;
exports.isFirefox = isFirefox;
exports.isGecko = isGecko;
exports.isIOS = isIOS;
exports.isMac = isMac;
exports.isOpera = isOpera;
exports.isSafari = isSafari;
exports.isTouchDevice = void 0;
exports.isWebKit = isWebKit;
exports.isWindows = isWindows;
exports.supportsCursorURIs = void 0;
var Log = _interopRequireWildcard(require("./logging.js"));
function _getRequireWildcardCache(e) { if ("function" != typeof WeakMap) return null; var r = new WeakMap(), t = new WeakMap(); return (_getRequireWildcardCache = function _getRequireWildcardCache(e) { return e ? t : r; })(e); }
function _interopRequireWildcard(e, r) { if (!r && e && e.__esModule) return e; if (null === e || "object" != _typeof(e) && "function" != typeof e) return { "default": e }; var t = _getRequireWildcardCache(r); if (t && t.has(e)) return t.get(e); var n = { __proto__: null }, a = Object.defineProperty && Object.getOwnPropertyDescriptor; for (var u in e) if ("default" !== u && {}.hasOwnProperty.call(e, u)) { var i = a ? Object.getOwnPropertyDescriptor(e, u) : null; i && (i.get || i.set) ? Object.defineProperty(n, u, i) : n[u] = e[u]; } return n["default"] = e, t && t.set(e, n), n; }
/*
 * noVNC: HTML5 VNC client
 * Copyright (C) 2019 The noVNC Authors
 * Licensed under MPL 2.0 (see LICENSE.txt)
 *
 * See README.md for usage and integration instructions.
 *
 * Browser feature support detection
 */

// Touch detection
var isTouchDevice = exports.isTouchDevice = 'ontouchstart' in document.documentElement ||
// requried for Chrome debugger
document.ontouchstart !== undefined ||
// required for MS Surface
navigator.maxTouchPoints > 0 || navigator.msMaxTouchPoints > 0;
window.addEventListener('touchstart', function onFirstTouch() {
  exports.isTouchDevice = isTouchDevice = true;
  window.removeEventListener('touchstart', onFirstTouch, false);
}, false);

// The goal is to find a certain physical width, the devicePixelRatio
// brings us a bit closer but is not optimal.
var dragThreshold = exports.dragThreshold = 10 * (window.devicePixelRatio || 1);
var _supportsCursorURIs = false;
try {
  var target = document.createElement('canvas');
  target.style.cursor = 'url("data:image/x-icon;base64,AAACAAEACAgAAAIAAgA4AQAAFgAAACgAAAAIAAAAEAAAAAEAIAAAAAAAEAAAAAAAAAAAAAAAAAAAAAAAAAD/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////AAAAAAAAAAAAAAAAAAAAAA==") 2 2, default';
  if (target.style.cursor.indexOf("url") === 0) {
    Log.Info("Data URI scheme cursor supported");
    _supportsCursorURIs = true;
  } else {
    Log.Warn("Data URI scheme cursor not supported");
  }
} catch (exc) {
  Log.Error("Data URI scheme cursor test exception: " + exc);
}
var supportsCursorURIs = exports.supportsCursorURIs = _supportsCursorURIs;
var _hasScrollbarGutter = true;
try {
  // Create invisible container
  var container = document.createElement('div');
  container.style.visibility = 'hidden';
  container.style.overflow = 'scroll'; // forcing scrollbars
  document.body.appendChild(container);

  // Create a div and place it in the container
  var child = document.createElement('div');
  container.appendChild(child);

  // Calculate the difference between the container's full width
  // and the child's width - the difference is the scrollbars
  var scrollbarWidth = container.offsetWidth - child.offsetWidth;

  // Clean up
  container.parentNode.removeChild(container);
  _hasScrollbarGutter = scrollbarWidth != 0;
} catch (exc) {
  Log.Error("Scrollbar test exception: " + exc);
}
var hasScrollbarGutter = exports.hasScrollbarGutter = _hasScrollbarGutter;

/*
 * The functions for detection of platforms and browsers below are exported
 * but the use of these should be minimized as much as possible.
 *
 * It's better to use feature detection than platform detection.
 */

/* OS */

function isMac() {
  return !!/mac/i.exec(navigator.platform);
}
function isWindows() {
  return !!/win/i.exec(navigator.platform);
}
function isIOS() {
  return !!/ipad/i.exec(navigator.platform) || !!/iphone/i.exec(navigator.platform) || !!/ipod/i.exec(navigator.platform);
}
function isAndroid() {
  /* Android sets navigator.platform to Linux :/ */
  return !!navigator.userAgent.match('Android ');
}
function isChromeOS() {
  /* ChromeOS sets navigator.platform to Linux :/ */
  return !!navigator.userAgent.match(' CrOS ');
}

/* Browser */

function isSafari() {
  return !!navigator.userAgent.match('Safari/...') && !navigator.userAgent.match('Chrome/...') && !navigator.userAgent.match('Chromium/...') && !navigator.userAgent.match('Epiphany/...');
}
function isFirefox() {
  return !!navigator.userAgent.match('Firefox/...') && !navigator.userAgent.match('Seamonkey/...');
}
function isChrome() {
  return !!navigator.userAgent.match('Chrome/...') && !navigator.userAgent.match('Chromium/...') && !navigator.userAgent.match('Edg/...') && !navigator.userAgent.match('OPR/...');
}
function isChromium() {
  return !!navigator.userAgent.match('Chromium/...');
}
function isOpera() {
  return !!navigator.userAgent.match('OPR/...');
}
function isEdge() {
  return !!navigator.userAgent.match('Edg/...');
}

/* Engine */

function isGecko() {
  return !!navigator.userAgent.match('Gecko/...');
}
function isWebKit() {
  return !!navigator.userAgent.match('AppleWebKit/...') && !navigator.userAgent.match('Chrome/...');
}
function isBlink() {
  return !!navigator.userAgent.match('Chrome/...');
}