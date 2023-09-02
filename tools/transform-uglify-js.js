const { argv, env, exit, stdin, stdout, stderr } = require('process');

try {
	require('uglify-js/package');
} catch {
	const { spawnSync } = require('child_process');
	let result = spawnSync('npm', ['--prefix=' + env.NODE_PREFIX, 'install', 'uglify-js'], {'stdio': ['ignore', 'ignore', 'inherit']});
	if (result.status != 0) {
		stderr.write("npm failed to run, is it installed?\n");
		exit(result.status);
	}
	result = spawnSync('node', argv.slice(1), {'stdio': 'inherit'});
	exit(result.status);
}

const uglifyjs = require('uglify-js');

var input = '';
stdin.setEncoding('utf-8');
stdin.on('data', (data) => {
	input = input.concat(data.toString());
});
stdin.on('close', () => {
	let result = uglifyjs.minify(input, {toplevel: true});
	if (result.code === undefined) {
		exit(1)
	}
	stdout.write(result.code);
	exit(0);
});
