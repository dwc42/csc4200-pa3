import path from "path";
import fs, { rmSync, write, writeSync } from "fs";

/**
 * 
 * @param {string} path 
 * @returns 
 */
function isFolder(path) {
	// console.log(path);
	try {
		return fs.lstatSync(path).isDirectory();
	} catch {
	}
}
/**
 * @param {string} src
 * @param {string} dest
 * @param {(src: string, dest: string, dir: string, staticSRCPath: string, relitiveFromSRCPath: string) => boolean} excludeCallback
 */
function copyRecursiveSync(src, dest, excludeCallback, deleteDest = false) {
	const pathArray = src.split(path.sep);
	const srcfolderName = pathArray[pathArray.length - 1];
	const srcFolderNameWithDest = path.join(dest, srcfolderName);
	if (!isFolder(dest)) fs.mkdirSync(dest);
	if (!isFolder(srcFolderNameWithDest)) fs.mkdirSync(srcFolderNameWithDest);
	if (deleteDest) {
		if (isFolder(srcFolderNameWithDest)) fs.rmSync(srcFolderNameWithDest, { recursive: true, force: true });
		fs.mkdirSync(srcFolderNameWithDest);
	}
	function copyRecursiveSyncInternal(srcInternal, destInternal) {
		fs.readdirSync(srcInternal).forEach(dir => {
			const staticSRCPath = path.join(srcInternal, dir);
			const staticDestPath = path.join(destInternal, dir);
			const relitiveFromSRCPath = staticSRCPath.replace(src, '');
			if (excludeCallback?.(src, dest, dir, staticSRCPath, relitiveFromSRCPath)) return;
			if (isFolder(staticSRCPath)) {
				try {
					fs.mkdirSync(staticDestPath);
				} catch { }
				copyRecursiveSyncInternal(staticSRCPath, staticDestPath);
			} else {
				const fileContents = fs.readFileSync(staticSRCPath);
				fs.writeFileSync(staticDestPath, fileContents);
			}
			// console.log(staticSRCPath);
		});
	};
	copyRecursiveSyncInternal(src, srcFolderNameWithDest);
};

const code_paths = [path.resolve("./src"), path.resolve("./include")];
const code_dest = path.resolve("../infrastructure-setup-dwc42/assignment-3-detect-a-person/");
code_paths.forEach(code_path => {
	console.log(code_dest, code_path);
	copyRecursiveSync(code_path, code_dest, undefined, true);
	try {
		rmSync(path.join(code_dest, "src", "node"), { "recursive": true });
	} catch {

	}
});
fs.writeFileSync(path.join(code_dest, "Makefile"), fs.readFileSync(path.resolve("./Makefile")));
