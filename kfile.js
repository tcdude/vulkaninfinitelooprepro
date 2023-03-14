const fs = require("fs");
const child_process = require("child_process");

let project = new Project('vulkaninfinitelooprepro');

project.addFile('Sources/**');
project.addIncludeDir('Sources');
project.addDefine('KINC_NO_WAYLAND');
let krafix = await project.addProject('krafix');
krafix.useAsLibrary();
await project.addProject('Kinc');

project.setDebugDir('Deployment');

project.flatten();
resolve(project);
