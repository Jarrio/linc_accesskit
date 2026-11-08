// AccessKit's Android adapter, for a tree of nodes sent from Haxe. Android asks for the tree and sends actions on its UI
// thread, so nothing here touches Haxe objects there: nodes are copied into plain C++ under a lock, and an action waits
// in a queue until the main thread reads it.
#include "linc_accesskit.h"

#include <jni.h>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include <accesskit.h>

extern "C" void *SDL_GetAndroidJNIEnv(void);

namespace linc {
namespace accesskit {

namespace {


struct Node {
	uint64_t id;
	uint64_t parent;
	int role;
	std::string label;
	std::string value;
	int toggled;
	bool disabled;
	bool clickable;
	double x0, y0, x1, y1;
	double scrollY, scrollMax;
	bool numeric;
	double number, numberMin, numberMax;
};

static std::mutex ak_lock;
static std::vector<Node> ak_nodes;
static std::vector<Node> ak_building;
static double ak_width = 0;
static double ak_height = 0;
static std::vector<int> ak_actions;
static std::vector<double> ak_numbers;
static std::atomic<bool> ak_active(false);
static accesskit_android_adapter *ak_adapter = nullptr;
static jclass ak_activity = nullptr;
static jmethodID ak_request = nullptr;

static void ak_wake() {
	SDL_Event event;
	SDL_zero(event);
	event.type = SDL_EVENT_USER;
	SDL_PushEvent(&event);
}

// The numbers are simple.AccessRole.
static accesskit_role ak_role(int role) {
	switch (role) {
		case 1: return ACCESSKIT_ROLE_LABEL;
		case 2: return ACCESSKIT_ROLE_BUTTON;
		case 3: return ACCESSKIT_ROLE_SWITCH;
		case 4: return ACCESSKIT_ROLE_CHECK_BOX;
		case 5: return ACCESSKIT_ROLE_RADIO_BUTTON;
		case 6: return ACCESSKIT_ROLE_TEXT_INPUT;
		case 7: return ACCESSKIT_ROLE_LIST_ITEM;
		case 8: return ACCESSKIT_ROLE_IMAGE;
		case 9: return ACCESSKIT_ROLE_SCROLL_VIEW;
		case 10: return ACCESSKIT_ROLE_SLIDER;
		default: return ACCESSKIT_ROLE_GENERIC_CONTAINER;
	}
}

// The whole tree. The first time Android asks there is nothing yet, so this wakes the main thread to build it.
static accesskit_tree_update *ak_tree(void *) {
	if (!ak_active.exchange(true)) {
		ak_wake();
	}
	std::lock_guard<std::mutex> guard(ak_lock);
	std::map<uint64_t, std::vector<uint64_t>> children;
	for (auto &item : ak_nodes) {
		children[item.parent].push_back(item.id);
	}
	accesskit_node *root = accesskit_node_new(ACCESSKIT_ROLE_WINDOW);
	accesskit_rect window = {0, 0, ak_width, ak_height};
	accesskit_node_set_bounds(root, window);
	for (uint64_t id : children[0]) {
		accesskit_node_push_child(root, id);
	}
	accesskit_tree_update *update = accesskit_tree_update_with_capacity_and_focus(ak_nodes.size() + 1, 0);
	accesskit_tree_update_set_tree_info(update, accesskit_tree_info_new(0));
	accesskit_tree_update_push_node(update, 0, root);
	for (auto &item : ak_nodes) {
		accesskit_node *node = accesskit_node_new(ak_role(item.role));
		accesskit_rect rect = {item.x0, item.y0, item.x1, item.y1};
		accesskit_node_set_bounds(node, rect);
		// A label says its text as its value; everything else has a name, and maybe a value as well.
		if (item.role == 1) {
			accesskit_node_set_value(node, item.label.c_str());
		} else {
			if (!item.label.empty()) {
				accesskit_node_set_label(node, item.label.c_str());
			}
			if (!item.value.empty()) {
				accesskit_node_set_value(node, item.value.c_str());
			}
		}
		if (item.toggled >= 0) {
			accesskit_node_set_toggled(node, item.toggled ? ACCESSKIT_TOGGLED_TRUE : ACCESSKIT_TOGGLED_FALSE);
		}
		if (item.disabled) {
			accesskit_node_set_disabled(node);
		}
		if (item.clickable || item.numeric) {
			accesskit_node_add_action(node, ACCESSKIT_ACTION_FOCUS);
		}
		if (item.clickable) {
			accesskit_node_add_action(node, ACCESSKIT_ACTION_CLICK);
		}
		if (item.scrollMax > 0) {
			accesskit_node_set_scroll_y(node, item.scrollY);
			accesskit_node_set_scroll_y_min(node, 0);
			accesskit_node_set_scroll_y_max(node, item.scrollMax);
			accesskit_node_set_clips_children(node);
			if (item.scrollY < item.scrollMax) {
				accesskit_node_add_action(node, ACCESSKIT_ACTION_SCROLL_DOWN);
			}
			if (item.scrollY > 0) {
				accesskit_node_add_action(node, ACCESSKIT_ACTION_SCROLL_UP);
			}
		}
		if (item.numeric) {
			accesskit_node_set_numeric_value(node, item.number);
			accesskit_node_set_min_numeric_value(node, item.numberMin);
			accesskit_node_set_max_numeric_value(node, item.numberMax);
			accesskit_node_add_action(node, ACCESSKIT_ACTION_INCREMENT);
			accesskit_node_add_action(node, ACCESSKIT_ACTION_DECREMENT);
			accesskit_node_add_action(node, ACCESSKIT_ACTION_SET_VALUE);
		}
		for (uint64_t id : children[item.id]) {
			accesskit_node_push_child(node, id);
		}
		accesskit_tree_update_push_node(update, item.id, node);
	}
	return update;
}

// Android's AccessibilityAction.ACTION_SHOW_ON_SCREEN (android.R.id.accessibilityActionShowOnScreen), which AccessKit's
// adapter doesn't handle: ak_action sends it through as an input focus request, the one action AccessKit passes on for
// any node, with ak_showing set, so ak_do gets it with the app's own node id.
static const jint AK_SHOW_ON_SCREEN = 0x01020036;
static const jint AK_ANDROID_FOCUS = 1;
static bool ak_showing = false;

// An action waits as its node id times 8 plus 0 for a click, 1 for scrolling down or a step up, 2 for scrolling up or a
// step down, 3 for a new value, which waits as well, and 4 for showing the node whole on screen.
static void ak_do(accesskit_action_request *request, void *) {
	int code = -1;
	switch (request->action) {
		case ACCESSKIT_ACTION_FOCUS:
			if (ak_showing) {
				code = 4;
			}
			break;
		case ACCESSKIT_ACTION_CLICK: code = 0; break;
		case ACCESSKIT_ACTION_SCROLL_DOWN: case ACCESSKIT_ACTION_INCREMENT: code = 1; break;
		case ACCESSKIT_ACTION_SCROLL_UP: case ACCESSKIT_ACTION_DECREMENT: code = 2; break;
		case ACCESSKIT_ACTION_SET_VALUE:
			if (request->data.has_value && request->data.value.tag == ACCESSKIT_ACTION_DATA_NUMERIC_VALUE) {
				code = 3;
			}
			break;
		default: break;
	}
	if (code >= 0) {
		{
			std::lock_guard<std::mutex> guard(ak_lock);
			ak_actions.push_back((int)request->target_node * 8 + code);
			if (code == 3) {
				ak_numbers.push_back(request->data.value.numeric_value);
			}
		}
		ak_wake();
	}
	accesskit_action_request_free(request);
}

static jobject JNICALL ak_node(JNIEnv *env, jclass, jobject host, jint id) {
	return accesskit_android_adapter_create_accessibility_node_info(ak_adapter, ak_tree, nullptr, env, host, id);
}

static jobject JNICALL ak_focus(JNIEnv *env, jclass, jobject host, jint type) {
	return accesskit_android_adapter_find_focus(ak_adapter, ak_tree, nullptr, env, host, type);
}

static jboolean JNICALL ak_action(JNIEnv *env, jclass, jobject host, jint id, jint action, jobject arguments) {
	bool show = action == AK_SHOW_ON_SCREEN;
	accesskit_android_platform_action *platform = accesskit_android_platform_action_from_java(env, show ? AK_ANDROID_FOCUS : action,
		show ? nullptr : arguments);
	if (!platform) {
		return JNI_FALSE;
	}
	// AccessKit calls ak_do before it returns, on this thread.
	ak_showing = show;
	accesskit_android_queued_events *events = accesskit_android_adapter_perform_action(ak_adapter, ak_do, nullptr, id, platform);
	ak_showing = false;
	accesskit_android_platform_action_free(platform);
	if (!events) {
		return JNI_FALSE;
	}
	accesskit_android_queued_events_raise(events, env, host);
	return JNI_TRUE;
}

static jboolean JNICALL ak_hover(JNIEnv *env, jclass, jobject host, jint action, jfloat x, jfloat y) {
	accesskit_android_queued_events *events = accesskit_android_adapter_on_hover_event(ak_adapter, ak_tree, nullptr, action, x, y);
	if (!events) {
		return JNI_FALSE;
	}
	accesskit_android_queued_events_raise(events, env, host);
	return JNI_TRUE;
}

// Android's accessibility turned on or off (MainActivity's listener). While it's off nothing reads the tree, so the
// app stops building it; turned back on, the app wakes to send what changed meanwhile, as the adapter keeps the last
// tree it was given and would hand that out.
static void JNICALL ak_state(JNIEnv *, jclass, jboolean enabled) {
	ak_active.store(enabled == JNI_TRUE);
	if (enabled == JNI_TRUE) {
		ak_wake();
	}
}

static void JNICALL ak_update(JNIEnv *env, jclass, jobject host) {
	accesskit_android_queued_events *events = accesskit_android_adapter_update_if_active(ak_adapter, ak_tree, nullptr);
	if (events) {
		accesskit_android_queued_events_raise(events, env, host);
	}
}

}

bool active() {
	return ak_active.load();
}

void start(const char *activityClass) {
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (!env) {
		return;
	}
	jclass activity = env->FindClass(activityClass);
	if (!activity) {
		env->ExceptionClear();
		return;
	}
	ak_adapter = accesskit_android_adapter_new();
	JNINativeMethod methods[] = {
		{(char *)"nativeAccessibilityNode", (char *)"(Landroid/view/View;I)Landroid/view/accessibility/AccessibilityNodeInfo;", (void *)ak_node},
		{(char *)"nativeAccessibilityFocus", (char *)"(Landroid/view/View;I)Landroid/view/accessibility/AccessibilityNodeInfo;", (void *)ak_focus},
		{(char *)"nativeAccessibilityAction", (char *)"(Landroid/view/View;IILandroid/os/Bundle;)Z", (void *)ak_action},
		{(char *)"nativeAccessibilityHover", (char *)"(Landroid/view/View;IFF)Z", (void *)ak_hover},
		{(char *)"nativeAccessibilityUpdate", (char *)"(Landroid/view/View;)V", (void *)ak_update},
		{(char *)"nativeAccessibilityState", (char *)"(Z)V", (void *)ak_state},
	};
	if (env->RegisterNatives(activity, methods, 6) != 0) {
		env->ExceptionClear();
		return;
	}
	ak_activity = (jclass)env->NewGlobalRef(activity);
	ak_request = env->GetStaticMethodID(activity, "requestAccessibilityUpdate", "()V");
	jfieldID ready = env->GetStaticFieldID(activity, "accessibilityReady", "Z");
	if (ready) {
		env->SetStaticBooleanField(activity, ready, JNI_TRUE);
	}
	env->ExceptionClear();
	env->DeleteLocalRef(activity);
}

void add(int id, int parent, int role, const char *label, const char *value, int toggled, bool disabled, bool clickable, double x0, double y0, double x1, double y1, double scrollY, double scrollMax, bool numeric, double number, double numberMin, double numberMax) {
	ak_building.push_back({(uint64_t)id, (uint64_t)parent, role, std::string(label), std::string(value), toggled, disabled, clickable, x0, y0, x1, y1, scrollY, scrollMax, numeric, number, numberMin, numberMax});
}

void commit(double width, double height) {
	{
		std::lock_guard<std::mutex> guard(ak_lock);
		ak_nodes.swap(ak_building);
		ak_width = width;
		ak_height = height;
	}
	ak_building.clear();
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (env && ak_activity && ak_request) {
		env->CallStaticVoidMethod(ak_activity, ak_request);
	}
}

int nextAction() {
	std::lock_guard<std::mutex> guard(ak_lock);
	if (ak_actions.empty()) {
		return -1;
	}
	int action = ak_actions.front();
	ak_actions.erase(ak_actions.begin());
	return action;
}

double nextNumber() {
	std::lock_guard<std::mutex> guard(ak_lock);
	if (ak_numbers.empty()) {
		return 0;
	}
	double number = ak_numbers.front();
	ak_numbers.erase(ak_numbers.begin());
	return number;
}

}
}
