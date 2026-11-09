package accesskit;

import cpp.ConstCharStar;

/**
	Direct calls into linc/linc_accesskit.cpp. Host code only: cppia scripts can't call externs, so they go through
	`AccessKit`.
**/
@:keep
@:include("linc_accesskit.h")
@:build(linc.Linc.touch())
@:build(linc.Linc.xml("accesskit"))
extern class Native {
	@:native("linc::accesskit::start")
	static function start(activityClass:ConstCharStar):Void;

	@:native("linc::accesskit::active")
	static function active():Bool;

	@:native("linc::accesskit::add")
	static function add(id:Int, parent:Int, role:Int, label:ConstCharStar, value:ConstCharStar, toggled:Int, disabled:Bool, clickable:Bool, x0:Float,
		y0:Float, x1:Float, y1:Float, scrollY:Float, scrollMax:Float, numeric:Bool, number:Float, numberMin:Float, numberMax:Float, level:Int):Void;

	@:native("linc::accesskit::commit")
	static function commit(width:Float, height:Float):Void;

	@:native("linc::accesskit::nextAction")
	static function nextAction():Int;

	@:native("linc::accesskit::nextNumber")
	static function nextNumber():Float;
}
