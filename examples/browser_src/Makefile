.PHONY: dev update dist clean

files := $(shell find -maxdepth 1 -name \*.html -o -name \*.js -o -name \*.json)
files += ${files} $(shell find public src -type f)

dev: node_modules
	@npm run dev -- --host

node_modules:
	@npm install

update:
	@npm update

dist: dist/index.html

dist/index.html: node_modules ${files}
	@npm run build

clean:
	@rm -rf venv dist node_modules
