package accesskit;

/**
	The AccessKit calls simple needs, on Android. Every function is a real static function, so a `-D scriptable` host
	compiles it and cppia scripts can call it.
**/
class AccessKit {
	/** The roles `add` takes. **/
	public static inline var ROLE_GROUP = 0;

	public static inline var ROLE_LABEL = 1;
	public static inline var ROLE_BUTTON = 2;
	public static inline var ROLE_SWITCH = 3;
	public static inline var ROLE_CHECK_BOX = 4;
	public static inline var ROLE_RADIO_BUTTON = 5;
	public static inline var ROLE_TEXT_INPUT = 6;
	public static inline var ROLE_LIST_ITEM = 7;
	public static inline var ROLE_IMAGE = 8;
	public static inline var ROLE_SCROLL_VIEW = 9;
	public static inline var ROLE_SLIDER = 10;

	/** What `nextAction` carries, in its low three bits; its node's id is in the rest. **/
	public static inline var ACTION_CLICK = 0;

	public static inline var ACTION_STEP_UP = 1;
	public static inline var ACTION_STEP_DOWN = 2;
	public static inline var ACTION_SET_NUMBER = 3;

	/** Show the node whole on screen, scrolling what it's in: Android's "show on screen" action. **/
	public static inline var ACTION_SHOW = 4;

	/**
		Starts the adapter. `activityClass`, as JNI writes it ("org/simple/app/MainActivity"), must declare the native
		methods this registers, `accessibilityReady` and `requestAccessibilityUpdate` (see linc/linc_accesskit.h).
	**/
	public static function start(activityClass:String) {
		Native.start(activityClass);
	}

	/** Whether the system has asked for the tree. **/
	public static function active():Bool {
		return Native.active();
	}

	/** Adds a node to the tree being built, in pixels; `parent` 0 is the window, `toggled` -1 for neither. **/
	public static function add(id:Int, parent:Int, role:Int, label:String, value:String, toggled:Int, disabled:Bool, clickable:Bool, x0:Float, y0:Float,
			x1:Float, y1:Float, scrollY:Float, scrollMax:Float, numeric:Bool, number:Float, numberMin:Float, numberMax:Float) {
		Native.add(id, parent, role, label, value, toggled, disabled, clickable, x0, y0, x1, y1, scrollY, scrollMax, numeric, number, numberMin, numberMax);
	}

	/** Makes the tree built with `add` the current one, in a window of this size in pixels. **/
	public static function commit(width:Float, height:Float) {
		Native.commit(width, height);
	}

	/** An action that came in since the last call, as its node's id times 8 plus an `ACTION_*`; or -1. **/
	public static function nextAction():Int {
		return Native.nextAction();
	}

	/** The value that came with `ACTION_SET_NUMBER`. **/
	public static function nextNumber():Float {
		return Native.nextNumber();
	}
}
