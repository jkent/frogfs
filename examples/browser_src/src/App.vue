<script setup>
import ModalDialog from './components/ModalDialog.vue';
import ModalIframe from './components/ModalIframe.vue';
import FolderTree from './components/FolderTree.vue';
import { reactive, ref } from 'vue';

const files = '';
const api = '/browser/api';
const folderTree = ref(null);
const selectedFile = reactive({});

const showModal = ref('none');
let modalMessageText = '';
let filePath = '';
let fileBody = '';
let fileLength = 0;

const showPreview = ref(false);
let previewTitle = '';
let previewUrl = '';

function doRefresh() {
  folderTree.value.refresh();
}

function showNewFile() {
  filePath = '/';
  fileBody = '';
  showModal.value = 'newFile';
}

function doNewFile() {
  let path = encodeURIComponent(filePath);

  fetch(api + '?action=new&path=' + path, {
    method: 'POST',
    body: fileBody,
  }) 
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(file => {
    folderTree.value.delFile(file);
    folderTree.value.addFile(file);
    folderTree.value.selectFile(file);
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error creating file!';
    showModal.value = 'message';
  });
}

function showEditFile() {
  if (!selectedFile.value) {
    modalMessageText = 'Select a file edit first.';
    showModal.value = 'message';
    return;
  }

  fetch(files + selectedFile.value.path)
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.text()
  })
  .then(text => {
    fileBody = text;
    showModal.value = 'editFile';
  })
  .catch((e) => {
    modalMessageText = 'Error getting raw file contents!'
    showModal.value = 'message';
  });
}

function doEditFile() {
  let path = encodeURIComponent(selectedFile.value.path);

  fetch(api + '?action=edit&path=' + path, {
    method: 'POST',
    body: fileBody,
  }) 
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(file => {
    folderTree.value.delFile(file);
    folderTree.value.addFile(file);
    folderTree.value.selectFile(file);
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error editing file!';
    showModal.value = 'message';
  });
}

function showTruncateFile() {
  if (!selectedFile.value) {
    modalMessageText = 'Select a file to truncate first.';
    showModal.value = 'message';
    return;
  }

  fileLength = 0;
  showModal.value = 'truncateFile';
}

function doTruncateFile() {
  let path = encodeURIComponent(selectedFile.value.path);
  let length = encodeURIComponent(fileLength);

  fetch(api + '?action=truncate&path=' + path + '&length=' + length)
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(file => {
    folderTree.value.delFile(selectedFile.value);
    folderTree.value.addFile(file);
    folderTree.value.selectFile(file);
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error truncating file!';
    showModal.value = 'message';
  });
}

function showMoveFile() {
  if (!selectedFile.value) {
    modalMessageText = 'Select a file to move first.';
    showModal.value = 'message';
    return;
  }

  if (selectedFile.value.frogfs) {
    modalMessageText = 'File cannot be moved, its on FrogFS.';
    showModal.value = 'message';
    return;
  }

  filePath = selectedFile.value.path;
  showModal.value = 'moveFile';
}

function doMoveFile() {
  let src_path = encodeURIComponent(selectedFile.value.path);
  let dst_path = encodeURIComponent(filePath);

  fetch(api + '?action=move&src=' + src_path + '&dst=' + dst_path)
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(files => {
	folderTree.value.delFile(selectedFile.value);
    for (let file of files) {
      folderTree.value.delFile(file);
      folderTree.value.addFile(file);
      folderTree.value.selectFile(file);
    }
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error moving file!';
    showModal.value = 'message';
  });
}

function showCopyFile() {
  if (!selectedFile.value) {
    modalMessageText = 'Select a file to copy first.';
    showModal.value = 'message';
    return;
  }

  filePath = selectedFile.value.path;
  showModal.value = 'copyFile';
}

function doCopyFile() {
  let src_path = encodeURIComponent(selectedFile.value.path);
  let dst_path = encodeURIComponent(filePath);

  fetch(api + '?action=copy&src=' + src_path + '&dst=' + dst_path)
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(file => {
    if (file.path == selectedFile.value.path) {
      folderTree.value.delFile(selectedFile.value);
    }
    folderTree.value.addFile(file);
    folderTree.value.selectFile(file);
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error copying file!';
    showModal.value = 'message';
  });
}

function showDeleteFile() {
  if (!selectedFile.value) {
    modalMessageText = 'Select a file to delete first.';
    showModal.value = 'message';
    return;
  }

  if (selectedFile.value.frogfs) {
    modalMessageText = 'File cannot be deleted, its on FrogFS.';
    showModal.value = 'message';
    return;
  }

  showModal.value = 'deleteFile';
}

function doDeleteFile() {
  let path = encodeURIComponent(selectedFile.value.path);

  fetch(api + '?action=delete&path=' + path)
  .then(response => {
    if (!response.ok) {
      throw new Error('response not ok');
    }
    return response.json()
  })
  .then(file => {
    folderTree.value.delFile(selectedFile.value);
    if (file.path) {
      folderTree.value.addFile(file);
      folderTree.value.selectFile(file);
    }
    showModal.value = 'none';
  })
  .catch((e) => {
    modalMessageText = 'Error deleting file!';
    showModal.value = 'message';
  });
}

function openPreview(node) {
  previewUrl = files + node.path;
  previewTitle = 'Preview of ' + node.path;
  showPreview.value = true;
}

function acceptTab(evt) {
  if (evt.keyCode === 9) {
    let target = evt.target;
    let v = target.value;
    let s = target.selectionStart
    let e = target.selectionEnd;
    if (evt.shiftKey) {
      if (v.substring(s - 1, s) === '\t') {
        target.value = v.substr(0, s - 1) + v.substr(e);
        target.selectionStart = target.selectionEnd = s - 1;
      }
    } else {
      target.value = v.substr(0, s) + '\t' + v.substr(e);
      target.selectionStart = target.selectionEnd = s + 1;
    }
    evt.preventDefault();
  }
}
</script>

<template>
  <div class="file-action-buttons">
    <button @click="doRefresh()">Refresh</button>
    <button @click="showNewFile()">New</button>
    <button @click="showEditFile()">Edit</button>
    <button @click="showMoveFile()">Move</button>
    <button @click="showCopyFile()">Copy</button>
    <button @click="showTruncateFile()">Truncate</button>
    <button @click="showDeleteFile()">Delete</button>
  </div>

  <Teleport to="body">
    <div class="dialogs">
      <ModalDialog
        type="ok"
        :show="showModal == 'message'"
        @close="showModal = 'none'"
        @apply="showModal = 'none'"
      >
        <template #header>
          <h3>Alert!</h3>
        </template>
        <template #body>
          {{ modalMessageText }}
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'newFile'"
        :enter="false"
        @close="showModal = 'none'"
        @apply="doNewFile()"
      >
        <template #header>
          <h3>New file</h3>
        </template>
        <template #body>
          File path:<br>
          <input type="text" v-model="filePath"><br>
          <textarea v-model="fileBody" @keydown="acceptTab"></textarea>
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'editFile'"
        :enter="false"
        @close="showModal = 'none'"
        @apply="doEditFile()"
      >
        <template #header>
          <h3>Edit File</h3>
        </template>
        <template #body>
          Edit '{{ selectedFile.value.path }}':<br>
          <textarea v-model="fileBody" @keydown="acceptTab"></textarea>
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'moveFile'"
        @close="showModal = 'none'"
        @apply="doMoveFile()"
      >
        <template #header>
          <h3>Move File</h3>
        </template>
        <template #body>
          Move '{{ selectedFile.value.path }}' to:<br>
          <input type="text" v-model="filePath"><br>
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'copyFile'"
        @close="showModal = 'none'"
        @apply="doCopyFile()"
      >
        <template #header>
          <h3>Copy File</h3>
        </template>
        <template #body>
          Copy '{{ selectedFile.value.path }}' to:<br>
          <input type="text" v-model="filePath"><br>
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'truncateFile'"
        @close="showModal = 'none'"
        @apply="doTruncateFile()"
      >
        <template #header>
          <h3>Truncate File</h3>
        </template>
        <template #body>
          Truncate '{{ selectedFile.value.path }}' to bytes:<br>
          <input type="text" v-model="fileLength"><br>
        </template>
      </ModalDialog>

      <ModalDialog
        :show="showModal == 'deleteFile'"
        @close="showModal = 'none'"
        @apply="doDeleteFile()"
      >
        <template #header>
          <h3>Confirm Delete</h3>
        </template>
        <template #body>
          Delete '{{ selectedFile.value.path }}'?
        </template>
      </ModalDialog>
    </div>
  </Teleport>

  <Teleport to="body">
    <ModalIframe
      :show="showPreview"
      :url="previewUrl"
      :title="previewTitle"
      @close="showPreview = false"
    >
      <template #header><h3>{{ previewTitle }}</h3></template>
    </ModalIframe>
  </Teleport>

  <FolderTree
    ref="folderTree"
    :api="api"
    :selected="selectedFile"
    @dblClick="(node) => openPreview(node)"
  />
</template>

<style>
h1 {
  margin: 0.25em;
}
.file-action-buttons {
  user-select: none;
  padding: 0.75em 0 0.75em 0;
  border-top: 2px dashed black;
  border-bottom: 2px dashed black;
}
.file-action-buttons button {
  padding: 0.25em 0.5em;
  margin: 0 0.5em;
}
.dialogs {
  user-select: none;
}
</style>
