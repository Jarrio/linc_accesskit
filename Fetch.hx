import haxe.io.Bytes;
import haxe.io.Path;
import haxe.zip.Uncompress;
import sys.FileSystem;
import sys.io.File;
import sys.io.Process;

/**
	Puts AccessKit 0.23.2 in vendor/, where linc/linc_accesskit.xml links it from. Run it from the linc_accesskit folder:

	  haxe --run Fetch android              AccessKit for the targets named: android
	  haxe --run Fetch --force android      again, even if vendor/ already has this version

	- android: accesskit.h and the static library for each CPU type, from AccessKit's prebuilt release. No Rust needed.

	The download is checked against its SHA-256 and kept in a per-user cache, so other projects don't download it again:
	LINC_CACHE if set, otherwise ~/.cache/linc, ~/Library/Caches/linc or %LOCALAPPDATA%\linc. Needs curl, which Windows
	10 and later, macOS and Linux all have.
**/
class Fetch {
	static inline var VERSION = "0.23.2";
	static inline var RELEASES = "https://github.com/AccessKit/accesskit-c/releases/download";
	static final RELEASE = {file: 'accesskit-c-$VERSION.zip', sha256: "c7fa84f8f34fca909f662375737a54f98b17cf9eae51fffd94ab3d9f662e3681"};
	static final TARGETS = ["android"];

	static function main() {
		if (!FileSystem.exists("haxelib.json") || !FileSystem.exists("linc/linc_accesskit.xml")) {
			fail("Run this from the linc_accesskit folder.");
		}
		var force = false;
		var targets = [];
		for (arg in Sys.args()) {
			switch arg {
				case "--force":
					force = true;
				case "--help" | "-h":
					Sys.println("Usage: haxe --run Fetch [--force] [" + TARGETS.join(" | ") + " ...]");
					return;
				case target if (TARGETS.contains(target)):
					targets.push(target);
				case other:
					fail('Unknown target "$other": use ${TARGETS.join(", ")}.');
			}
		}
		if (targets.length == 0) {
			fail("Name a target: " + TARGETS.join(", "));
		}
		for (target in targets) {
			var vendor = Path.join([Sys.getCwd(), "vendor", target]);
			if (!force && installedVersion(vendor) == VERSION) {
				Sys.println('AccessKit $VERSION for $target is already in $vendor');
				continue;
			}
			unpackAndroid(vendor);
			File.saveContent(Path.join([vendor, ".version"]), VERSION + "\n");
			Sys.println('AccessKit $VERSION for $target is in $vendor');
		}
	}

	static function installedVersion(vendor:String):Null<String> {
		var file = Path.join([vendor, ".version"]);
		return FileSystem.exists(file) ? StringTools.trim(File.getContent(file)) : null;
	}

	static function unpackAndroid(vendor:String) {
		removeTree(vendor);
		var root = 'accesskit-c-$VERSION/';
		var library = ~/^lib\/android\/([^\/]+)\/static\/libaccesskit\.a$/;
		var count = extract(readZip(download(RELEASE)), vendor, name -> {
			if (!StringTools.startsWith(name, root)) {
				return null;
			}
			var path = name.substr(root.length);
			if (path == "include/accesskit.h") {
				return path;
			}
			return library.match(path) ? 'lib/${library.matched(1)}/libaccesskit.a' : null;
		});
		if (count < 2) {
			fail('The header or libraries are missing from ${RELEASE.file}.');
		}
	}

	static function extract(entries:Array<ZipEntry>, dir:String, place:String->Null<String>):Int {
		var count = 0;
		for (entry in entries) {
			if (StringTools.endsWith(entry.name, "/")) {
				continue;
			}
			var path = place(entry.name);
			if (path == null) {
				continue;
			}
			var file = Path.join([dir, path]);
			FileSystem.createDirectory(Path.directory(file));
			File.saveBytes(file, entry.data());
			count++;
		}
		return count;
	}

	static function readZip(file:String):Array<ZipEntry> {
		Sys.println('  unpacking ${Path.withoutDirectory(file)}');
		return Zip.read(File.getBytes(file));
	}


	static function download(release:{file:String, sha256:String}):String {
		var file = Path.join([cacheDir(), release.file]);
		if (!FileSystem.exists(file)) {
			var url = '$RELEASES/$VERSION/${release.file}';
			Sys.println('  downloading $url');
			var partial = file + ".part";
			if (Sys.command("curl", ["-fL", "--progress-bar", "-o", partial, url]) != 0) {
				fail('Couldn\'t download $url');
			}
			FileSystem.rename(partial, file);
		}
		var sha256 = sha256(file);
		if (sha256 != release.sha256) {
			FileSystem.deleteFile(file);
			fail('${release.file} has SHA-256 $sha256, expected ${release.sha256}. Deleted it; run this again to download it again.');
		}
		return file;
	}

	/** Hashing a large file is slow in Haxe's interpreter, so this asks the system's own tool. **/
	static function sha256(file:String):String {
		var result = switch Sys.systemName() {
			case "Windows":
				// certutil prints a heading line, then the hash, with spaces between the bytes before Windows 10.
				var lines = output("certutil", ["-hashfile", file, "SHA256"]);
				lines == null ? null : StringTools.replace(lines.split("\n")[1], " ", "");
			case "Mac":
				output("shasum", ["-a", "256", file]);
			default:
				output("sha256sum", [file]);
		}
		if (result == null) {
			fail('Couldn\'t compute the SHA-256 of $file');
		}
		return StringTools.trim(result).split(" ")[0].toLowerCase();
	}

	static function cacheDir():String {
		var root = Sys.getEnv("LINC_CACHE");
		if (root == null) {
			var home = Sys.getEnv("HOME");
			root = switch Sys.systemName() {
				case "Windows": Path.join([Sys.getEnv("LOCALAPPDATA"), "linc"]);
				case "Mac": Path.join([home, "Library", "Caches", "linc"]);
				default: Path.join([Sys.getEnv("XDG_CACHE_HOME") ?? Path.join([home, ".cache"]), "linc"]);
			}
		}
		var dir = Path.join([root, "linc_sdl3", VERSION]);
		FileSystem.createDirectory(dir);
		return dir;
	}

	/** What the command printed, or null if it couldn't start or failed. **/
	static function output(command:String, args:Array<String>):Null<String> {
		try {
			var process = new Process(command, args);
			var text = process.stdout.readAll().toString();
			var code = process.exitCode();
			process.close();
			return code == 0 ? text : null;
		} catch (e) {
			return null;
		}
	}


	static function removeTree(path:String) {
		if (!FileSystem.exists(path)) {
			return;
		}
		if (FileSystem.isDirectory(path)) {
			for (name in FileSystem.readDirectory(path)) {
				removeTree(Path.join([path, name]));
			}
			FileSystem.deleteDirectory(path);
		} else {
			FileSystem.deleteFile(path);
		}
	}

	static function fail(message:String):Dynamic {
		Sys.println("Error: " + message);
		Sys.exit(1);
		return null;
	}
}

typedef ZipEntry = {name:String, data:() -> Bytes};

/**
	Reads a zip through its central directory, which lists every entry's sizes. haxe.zip.Reader walks the entries
	instead, and inflates the ones whose sizes come after their data in pure Haxe to find where they end, which is slow
	in Haxe's interpreter. This inflates each entry once, with zlib.
**/
private class Zip {
	public static function read(zip:Bytes):Array<ZipEntry> {
		var end = zip.length - 22;
		while (end >= 0 && zip.getInt32(end) != 0x06054b50) {
			end--;
		}
		if (end < 0) {
			throw "Not a zip file";
		}
		var count = zip.getUInt16(end + 10);
		var pos = zip.getInt32(end + 16);
		var entries = [];
		for (_ in 0...count) {
			if (zip.getInt32(pos) != 0x02014b50) {
				throw "Damaged zip directory";
			}
			var method = zip.getUInt16(pos + 10);
			var compressedSize = zip.getInt32(pos + 20);
			var size = zip.getInt32(pos + 24);
			var nameLength = zip.getUInt16(pos + 28);
			var name = zip.getString(pos + 46, nameLength);
			var header = zip.getInt32(pos + 42);
			var start = header + 30 + zip.getUInt16(header + 26) + zip.getUInt16(header + 28);
			entries.push({name: name, data: () -> method == 0 ? zip.sub(start, size) : inflate(zip.sub(start, compressedSize), size)});
			pos += 46 + nameLength + zip.getUInt16(pos + 30) + zip.getUInt16(pos + 32);
		}
		return entries;
	}

	static function inflate(data:Bytes, size:Int):Bytes {
		var out = Bytes.alloc(size);
		var inflater = new Uncompress(-15);
		var result = inflater.execute(data, 0, out, 0);
		inflater.close();
		if (!result.done || result.write != size) {
			throw "Damaged zip entry";
		}
		return out;
	}
}
