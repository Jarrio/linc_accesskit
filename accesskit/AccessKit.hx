package accesskit;

/** Brings linc/linc_accesskit.xml into a build: a class that imports this can include and call accesskit.h. **/
@:keep
@:build(linc.Linc.xml("accesskit"))
class AccessKit {}
