<script setup>
import FolderTreeBranch from './FolderTreeBranch.vue';
import { ref } from 'vue';

const props = defineProps({
  'api': {
    type: String,
    required: true,
  },
  'selected': {
    type: Object,
    required: true,
  }
});
const emit = defineEmits([
  'click',
  'dblClick',
]);

const folderTreeBranch = ref(null);
const root = ref({
  root: true,
  path: '',
  nodes: [],
});    

function refresh() {
  return folderTreeBranch.value.refresh();
}

function addFile(file) {
  return folderTreeBranch.value.addFile(file);
}

function delFile(file) {
  return folderTreeBranch.value.delFile(file);
}

function selectFile(file) {
  return folderTreeBranch.value.selectFile(file);
}

defineExpose({
  refresh,
  addFile,
  delFile,
  selectFile,
});
</script>

<template>
  <div class="folder-tree">
    <FolderTreeBranch
      ref="folderTreeBranch"
      :api="api"
      :root="root"
      :parent="root"
      :selected="selected"
      @click="(node) => emit('click', node)"
      @dblClick="(node) => emit('dblClick', node)"
    />
  </div>
</template>

<style>
.folder-tree {
  line-height: 100%;
  font-size: 1em;
  padding: 0.5em;
}
.folder-tree > ul > li > .line {
  display: none;
}
</style>
