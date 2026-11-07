#pragma once

#ifndef HXCPP_H
#include <hxcpp.h>
#endif

namespace linc {
	namespace accesskit {
		/**
			Starts AccessKit's Android adapter. `activityClass`, as JNI writes it, must declare the native methods this
			registers (nativeAccessibilityNode, nativeAccessibilityFocus, nativeAccessibilityAction,
			nativeAccessibilityHover, nativeAccessibilityUpdate), a static boolean `accessibilityReady`, which this sets,
			and a static `requestAccessibilityUpdate()`, called after each `commit`.
		**/
		void start(const char *activityClass);

		/** Whether the system has asked for the tree. **/
		bool active();

		/** Adds a node to the tree being built; `toggled` is -1 for neither. **/
		void add(int id, int parent, int role, const char *label, const char *value, int toggled, bool disabled, bool clickable,
			double x0, double y0, double x1, double y1, double scrollY, double scrollMax, bool numeric, double number,
			double numberMin, double numberMax);

		/** Makes the tree built with `add` the current one, in a window of this size in pixels. **/
		void commit(double width, double height);

		/**
			An action that came in since the last call, as its node's id times 8 plus 0 for a click, 1 for scrolling down
			or a step up, 2 for scrolling up or a step down, 3 for a new value (`nextNumber`), 4 for showing the node whole
			on screen; or -1.
		**/
		int nextAction();

		/** The value that came with an action ending in 3. **/
		double nextNumber();
	}
}
