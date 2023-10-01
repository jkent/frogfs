<script setup>
import icon_folder from './icons/folder.svg';
import icon_folder_expanded from './icons/folder-expanded.svg';
import icon_file from './icons/file.svg';
import icon_file_image from './icons/file-image.svg'

const props = defineProps({
  api: {
    type: String,
    required: true,
  },
  root: {
    type: Object,
    required: true,
  },
  parent: {
    type: Object,
    required: false,
  },
  selected: {
    type: Object,
    required: true,
  },
});
const emit = defineEmits([
  'click',
  'dblClick',
]);

function nodeClasses(node) {
  let cls = '';
  if (node.root || node.nodes) {
    cls = 'folder';
  } else {
    cls = 'file';
  }
  if (node.expanded) {
    cls += ' expanded';
  }
  if (node.name.endsWith('.ico') || node.name.endsWith('.gif') ||
      node.name.endsWith('.jpeg') || node.name.endsWith('.jpg') ||
      node.name.endsWith('.png') || node.name.endsWith('.webp')) {
    cls += ' image';
  }
  if (props.selected.value == node) {
    cls += ' selected';
  }
  return cls;
}

function strcmp(a, b) {
  return (a == b) ? 0 : ((a < b) ? -1 : 1);
}

function nodecmp(a, b) {
  if (a.nodes && !b.nodes) {
    return -1;
  } else if (!a.nodes && b.nodes) {
    return 1;
  }
  return strcmp(a.name, b.name);
}

Array.prototype.pushSorted = function (el, unique=false, fn) {
  let [found, index] = (function (arr) {
    var first = 0;
    var last = arr.length - 1;
    while (first <= last) {
      var mid = (last + first) >> 1;
      var cmp = fn(el, arr[mid]);
      if (cmp == 0) {
        return [true, mid];
      } else if (cmp < 0) {
        last = mid - 1;
      } else {
        first = mid + 1;
      }
    }
    return [false, first];
  })(this);

  if (!unique || !found) {
    this.splice(index, 0, el);
  }
  return this[index];
};

function getSize(node) {
  if (node.nodes) {
    return undefined;
  } else {
    if (node.size > 1024 * 1024 * 1024) {
      return (node.size / 1024 / 1024 / 1024).toFixed(1) + ' GiB';
    } else if (node.size > 1024 * 1024) {
      return (node.size / 1024 / 1024).toFixed(1) + ' MiB';
    } else if (node.size > 1024) {
      return (node.size / 1024).toFixed(1) + ' KiB';
    } else {
      return node.size + ' B';
    }
  }
}

function addFolder(folder) {
  let node = props.root;
  let depth = 0;

  if (folder.path) {
    let names = folder.path.split('/');
    names.splice(0, 1);

    let currentPath = '';
        
    for (let name of names) {
      currentPath += '/' + name;
      let obj = {
        name: name,
        path: currentPath,
        nodes: [],
      }
      node = node.nodes.pushSorted(obj, true, nodecmp);
      depth += 1;
    }
  }
  return node;
}

function addFile(file) {
  let folderPath = file.path.substr(0, file.path.lastIndexOf('/'));
  let folder = addFolder({
    path: folderPath,
  });

  file.name = file.path.substr(file.path.lastIndexOf('/') + 1);
  return folder.nodes.pushSorted(file, true, nodecmp);
}

function delFile(file, parent, depth) {
  if (!parent) {
    parent = props.root;
    depth = 0;
  }

  let file_parts = file.path.split('/');
  file_parts.splice(0, 1);
  if (depth == file_parts.length) {
    return false;
  }
  let file_part = file_parts[depth];

  for (const [i, node] of parent.nodes.entries()) {
    if (file_part != node.name) {
      continue;
    }
    if (node.nodes && delFile(file, node, depth + 1)) {
      file = node;
    }
    if (file.path == node.path) {
      parent.nodes.splice(i, 1);
    }
    if (parent.nodes.length == 0) {
      return true;
    }
  }
  return false;
}

function selectFile(file, parent, depth) {
  if (!parent) {
    props.selected.value = file;
    if (file == null) {
      return;
    }
    parent = props.root;
    depth = 0;
  }
  
  let file_parts = file.path.split('/');
  if (depth == file_parts.length) {
    return false;
  }
  let file_part = file_parts[depth];

  for (const node of parent.nodes) {
    if (file_part != node.name) {
      continue;
    }
    if (node.nodes) {
      node.expanded = true;
      selectFile(file, node, depth + 1);
    }
  }
}

function nodeClick(e, node) {
  if (!node.nodes) {
    props.selected.value = props.selected.value == node ? null : node;
    emit('click', node);
  }
}

function nodeImgClick(e, node) {
  if (node.nodes) {
    node.expanded = !node.expanded;
    let path = props.selected.value ? props.selected.value.path : '';
    if (path.startsWith(node.path)) {
      props.selected.value = null;
    }
  } else {
    nodeClick(e, node);
  }
}

function nodeDblClick(e, node) {
  let path = props.selected.value ? props.selected.value.path : '';
  if (path.startsWith(node.path)) {
      props.selected.value = null;
  }
  if (node.nodes) {
    node.expanded = !node.expanded;
  } else {
    emit('dblClick', node);
  }
}

function refresh() {
  props.root.nodes = [];
  fetch(props.api + '?action=load', {
    method: 'GET',
    headers: {
      'Accept': 'application/json',
    },
  })
  .then(response => response.json())
  .then(files => {
    for (let file of files) {
      addFile(file);
    }
  });
}

defineExpose({
  refresh,
  addFile,
  delFile,
  selectFile,
});

if (props.root == props.parent) {
  refresh();
}
(new Image()).src = icon_folder;
(new Image()).src = icon_folder_expanded;
(new Image()).src = icon_file;
(new Image()).src = icon_file_image;
</script>

<template>
  <ul>
    <li
      v-for="node in parent.nodes"
      :key="node.path"
      :class="nodeClasses(node)"
    >
      <span class="line"></span>
      <img @click="nodeImgClick($event, node)"/>
      <span class="name"
        :title="getSize(node)"
        @click="nodeClick($event, node)"
        @dblclick="nodeDblClick($event, node)"
      >{{ node.name }}{{ node.frogfs ? ' 🐸' : '' }}</span>
      <FolderTreeBranch
        v-if="node.expanded"
        :api="api"
        :root="root"
        :parent="node"
        :selected="selected"
        @click="(node) => emit('click', node)"
        @dblClick="(node) => emit('dblClick', node)"
      />
    </li>
  </ul>
</template>

<style type="scoped">
ul {
  list-style-type: none;
  color: black;
  user-select: none;
}
img {
  padding-right: 0.25em;
  margin-left: -1em;
}
li.folder > img {
  content: url(icons/folder.svg);
}
li.folder.expanded > img {
  content: url(icons/folder-expanded.svg);
}
li.file > img {
  content: url(icons/file.svg);
}
li.file.image > img {
  content: url(icons/file-image.svg);
}
li.selected > .name {
  background-color: blue;
  color: white;
}
ul {
  padding: 0;
  margin: 0;
}
li {
  position: relative;
  margin-left: 1em;
}
.line {
  position: absolute;
  display: inline-block;
  top: calc(-0.5em + -2px);
  left: calc(-2em + 5px);
  width: 2px;
  height: calc(100% + 6px);
  background-color: black;
}
.name {
  display: inline-block;
  width: calc(100% - 1em);
  padding-inline: 0.5em;
  padding-block: 0.25em;
}
</style>
