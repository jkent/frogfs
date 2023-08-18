const process = require('node:process');

try {
	require('uglify-js/package')
} catch {
	const { spawnSync } = require('node:child_process');
	spawnSync('npm', ['--prefix=' + process.env.NODE_PREFIX, 'install', 'uglify-js'], {'stdio': ['ignore', 'ignore', 'inherit']});
	spawnSync('node', process.argv.slice(1), {'stdio': 'inherit'});
	process.exit(0);
}

const uglifyjs = require('uglify-js');

var input = '';
process.stdin.setEncoding('utf-8');
process.stdin.on('data', (data) => {
	input = input.concat(data.toString());
});
process.stdin.on('close', () => {
	let result = uglifyjs.minify(input, {toplevel: true});
    if (result.code === undefined) {
        process.exit(1)
    }
	process.stdout.write(result.code);
	process.exit(0);
});
