const process = require('node:process');

try {
	require('@babel/core/package');
	require('@babel/preset-env/package');
} catch {
	const { spawnSync } = require('node:child_process');
	spawnSync('npm', ['--prefix=' + process.env.NODE_PREFIX, 'install', '@babel/core', '@babel/preset-env'], {'stdio': ['ignore', 'ignore', 'inherit']});
	spawnSync('node', process.argv.slice(1), {'stdio': 'inherit'});
	process.exit(0);
}

const babel = require('@babel/core');

var input = '';
process.stdin.setEncoding('utf-8');
process.stdin.on('data', (data) => {
	input = input.concat(data.toString());
});
process.stdin.on('close', () => {
	let result = babel.transformSync(input, {presets: ['@babel/preset-env']});
	process.stdout.write(result.code);
	process.exit(0);
});
