const process = require('node:process');

try {
	require('uglifycss/package')
} catch {
	const { spawnSync } = require('node:child_process');
	spawnSync('npm', ['--prefix=' + process.env.NODE_PREFIX, 'install', 'uglifycss'], {'stdio': ['ignore', 'ignore', 'inherit']});
	spawnSync('node', process.argv.slice(1), {'stdio': 'inherit'});
	process.exit(0);
}

const uglifycss = require('uglifycss');

var input = '';
process.stdin.setEncoding('utf-8');
process.stdin.on('data', (data) => {
	input = input.concat(data.toString());
});
process.stdin.on('close', () => {
	let output = uglifycss.processString(input);
	process.stdout.write(output);
	process.exit(0);
});
